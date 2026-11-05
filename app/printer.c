/* Native Linux Supvan USB and TCP transport. C89, libc only; no subprocesses.
 * The printer speaks HID reports, not SVG or a system print spooler. */
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "printer.h"
#include "bridge.h"
#include "supvan.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __linux__
static int unsupported(app_error *err)
{ app_error_set(err,1,"native Supvan USB/network printing requires Linux");return 0; }
int app_printer_image(lc_context *ctx,const lc_image *im,const char *target,const char *preview,app_error *err)
{ (void)ctx;(void)im;(void)target;(void)preview;return unsupported(err); }
int app_printer_status(const char *target,app_error *err)
{ (void)target;return unsupported(err); }
int app_printer_serve(const char *address,app_error *err)
{ (void)address;return unsupported(err); }
#else
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <linux/hidraw.h>

#define NET_MAGIC "CHPR0001"
#define NET_REQUEST 80
#define NET_RESPONSE 16
#define MAX_PIXELS (16UL*1024UL*1024UL)
#define OP_STATUS 1
#define OP_PRINT 2
#define OP_PREVIEW 3

typedef struct device {
    int fd,lock;
    unsigned width,height,type,gap;
    unsigned char flags[4];
    unsigned long remaining;
    char path[64];
} device;
static volatile sig_atomic_t stopping=0;
static void stop_server(int signal_number){(void)signal_number;stopping=1;}
static int failure(app_error *err,const char *message)
{app_error_set(err,1,message);return 0;}
static double now_seconds(void)
{struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (double)t.tv_sec+(double)t.tv_nsec/1000000000.0;}
static void pause_ms(long ms)
{struct timespec t;t.tv_sec=ms/1000;t.tv_nsec=(ms%1000)*1000000L;while(nanosleep(&t,&t)<0&&errno==EINTR&&!stopping){} }
static int wait_fd(int fd,short events,int timeout)
{
    struct pollfd p;int n;
    p.fd=fd;p.events=events;p.revents=0;
    do{n=poll(&p,1,timeout);}while(n<0&&errno==EINTR&&!stopping);
    if(n<=0)return n;
    if(p.revents&events)return 1;
    return -1;
}
static unsigned be16(const unsigned char *p){return ((unsigned)p[0]<<8)|p[1];}
static void put16(unsigned char *p,unsigned n){p[0]=(unsigned char)(n>>8);p[1]=(unsigned char)n;}
static unsigned long be32(const unsigned char *p)
{return ((unsigned long)p[0]<<24)|((unsigned long)p[1]<<16)|((unsigned long)p[2]<<8)|p[3];}
static void put32(unsigned char *p,unsigned long n)
{p[0]=(unsigned char)(n>>24);p[1]=(unsigned char)(n>>16);p[2]=(unsigned char)(n>>8);p[3]=(unsigned char)n;}
static int transfer_timed(int fd,unsigned char *data,size_t length,int sending,double budget,app_error *err)
{
    double deadline=now_seconds()+budget;ssize_t n;int wait;
    while(length){
        if(stopping||now_seconds()>=deadline)return failure(err,"network transfer timed out; do not blindly retry a print");
        wait=wait_fd(fd,sending?POLLOUT:POLLIN,1000);
        if(wait<0)return failure(err,"printer network connection closed");
        if(!wait)continue;
        n=sending?send(fd,data,length,MSG_NOSIGNAL):recv(fd,data,length,0);
        if(n<0&&(errno==EAGAIN||errno==EINTR))continue;
        if(n<=0)return failure(err,"printer network transfer failed; a submitted job may have printed");
        data+=n;length-=(size_t)n;
    }
    return 1;
}
static int transfer(int fd,unsigned char *data,size_t length,int sending,app_error *err)
{return transfer_timed(fd,data,length,sending,120,err);}
static int hid_read(int fd,unsigned char reply[64],int timeout)
{
    int ready;ssize_t n;
    ready=wait_fd(fd,POLLIN,timeout);if(ready<=0)return ready;
    do{n=read(fd,reply,64);}while(n<0&&errno==EINTR);
    return n>0?(int)n:-1;
}
static int hid_write(int fd,const unsigned char *data,size_t size,app_error *err)
{
    unsigned char report[65];ssize_t n;
    if(size>64)return failure(err,"internal HID report overflow");
    memset(report,0,sizeof report);memcpy(report+1,data,size);
    if(wait_fd(fd,POLLOUT,2000)<=0)return failure(err,"USB write timeout");
    do{n=write(fd,report,sizeof report);}while(n<0&&errno==EINTR);
    if(n!=(ssize_t)sizeof report)return failure(err,"USB report write failed");
    return 1;
}
static int command(device *d,unsigned op,unsigned first,int second,unsigned char reply[64],app_error *err)
{
    unsigned char packet[10],stale[64];int n,i;
    /* hidraw keeps replies queued. Drain stale reports before each command. */
    for(i=0;i<128;i++){n=hid_read(d->fd,stale,0);if(n<=0)break;}
    memset(packet,0,sizeof packet);packet[0]=0xc0;packet[1]=0x40;
    put16(packet+2,first);packet[4]=(unsigned char)op;packet[6]=8;
    if(second>=0)put16(packet+8,(unsigned)second);
    if(!hid_write(d->fd,packet,second<0?8:10,err))return 0;
    n=hid_read(d->fd,reply,2000);
    if(n<=0){failure(err,"Supvan did not reply over USB");return 0;}
    return n;
}
static int status_read(device *d,app_error *err)
{
    unsigned char reply[64];int n;
    n=command(d,0x11,0,-1,reply,err);if(!n)return 0;
    if(n<7)return failure(err,"short Supvan status response");
    memcpy(d->flags,reply+1,4);return 1;
}
static int material_read(device *d,app_error *err)
{
    unsigned char reply[64];int n;
    n=command(d,0x30,0,-1,reply,err);if(!n)return 0;
    if(n<26)return failure(err,"short Supvan material response");
    d->type=reply[18];d->width=reply[19];d->height=reply[20];d->gap=reply[21];
    d->remaining=sv_le32(reply+22);return 1;
}
static int device_fault(device *d,app_error *err)
{
    if(d->flags[0]&0x7e||d->flags[1]&8||d->flags[2]&8||d->flags[3]&1)
        return failure(err,"printer fault: check roll, lid, battery and head temperature");
    return 1;
}
static void device_close(device *d)
{if(d->fd>=0)close(d->fd);if(d->lock>=0)close(d->lock);d->fd=d->lock=-1;}
static int lock_device(device *d,app_error *err)
{
    const char *home,*cache;char path[PATH_MAX];double deadline;
    cache=getenv("XDG_CACHE_HOME");home=getenv("HOME");
    if(cache&&*cache){if(strlen(cache)>sizeof path-64)return failure(err,"cache path too long");strcpy(path,cache);}
    else{if(!home||strlen(home)>sizeof path-80)return failure(err,"invalid HOME for printer lock");sprintf(path,"%s/.cache",home);}
    if(mkdir(path,0700)&&errno!=EEXIST)return failure(err,"cannot create printer cache directory");
    strcat(path,"/supvan-print");
    if(mkdir(path,0700)&&errno!=EEXIST)return failure(err,"cannot create printer lock directory");
    strcat(path,"/queue.lock");d->lock=open(path,O_CREAT|O_RDWR,0600);
    if(d->lock<0)return failure(err,"cannot open printer job lock");
    deadline=now_seconds()+60;
    while(flock(d->lock,LOCK_EX|LOCK_NB)){
        if(errno!=EWOULDBLOCK&&errno!=EAGAIN)return failure(err,"cannot lock printer");
        if(stopping||now_seconds()>deadline)return failure(err,"printer queue busy for 60 seconds; no job sent");
        pause_ms(100);
    }
    return 1;
}
static int matching_hid(const char *path)
{
    struct hidraw_devinfo info;int fd,match;
    fd=open(path,O_RDONLY|O_NONBLOCK);if(fd<0)return 0;
    memset(&info,0,sizeof info);
    match=ioctl(fd,HIDIOCGRAWINFO,&info)>=0&&info.bustype==3&&info.vendor==0x1820&&info.product==0x2076;
    close(fd);return match;
}
static int device_open(device *d,const char *target,app_error *err)
{
    DIR *dir;struct dirent *entry;char path[64];unsigned char reply[64];int count=0;
    memset(d,0,sizeof *d);d->fd=d->lock=-1;
    if(!lock_device(d,err))goto bad;
    if(target&&strncmp(target,"/dev/hidraw",11)==0){
        if(strlen(target)>=sizeof d->path||!matching_hid(target)){failure(err,"target is not a SUPVAN 1820:2076 USB HID device");goto bad;}
        strcpy(d->path,target);count=1;
    }else{
        dir=opendir("/dev");if(!dir){failure(err,"cannot inspect /dev for Supvan");goto bad;}
        while((entry=readdir(dir))!=NULL){
            if(strncmp(entry->d_name,"hidraw",6)||strlen(entry->d_name)>40)continue;
            sprintf(path,"/dev/%s",entry->d_name);
            if(matching_hid(path)){strcpy(d->path,path);count++;}
        }
        closedir(dir);
    }
    if(count!=1){failure(err,count?"multiple Supvan devices; select /dev/hidrawN":"Supvan USB device offline or inaccessible; check power and HID permissions");goto bad;}
    d->fd=open(d->path,O_RDWR|O_NONBLOCK);
    if(d->fd<0){failure(err,"cannot open Supvan HID for writing");goto bad;}
    if(!command(d,0x12,0,-1,reply,err)||!status_read(d,err)||!material_read(d,err))goto bad;
    return 1;
bad:device_close(d);return 0;
}
static int status_json(device *d,unsigned char *text,size_t capacity)
{
    if(capacity<400)return 0;
    return sprintf((char *)text,"{\"device\":\"%s\",\"label_mm\":[%u,%u],\"gap_mm\":%u,\"remaining\":%lu,\"printing\":%s,\"busy\":%s,\"registers\":[%u,%u,%u,%u]}\n",
        d->path,d->width,d->height,d->gap,d->remaining,d->flags[2]&64?"true":"false",d->flags[1]&4?"true":"false",
        d->flags[0],d->flags[1],d->flags[2],d->flags[3]);
}
static int fit_label(const lc_image *input,device *d,lc_image *label,app_error *err)
{
    static const unsigned char bayer[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
    int media,boxw,boxh,rot,rw,rh,w,h,left,top,x,y,rx,ry,sx,sy,index,lum;
    double normal,turned,scale;
    const unsigned char *rgb;
    if(d->type!=1||d->width<5||d->height<5||d->height*8>SV_MAX_ROWS)
        return failure(err,"native printing requires sized die-cut stock, 5..255 mm long");
    if(input->width<1||input->height<1||input->stride<(size_t)input->width||
       (unsigned long)input->width*(unsigned long)input->height>MAX_PIXELS)
        return failure(err,"invalid or oversized print image");
    media=(int)d->width*8;if(media>SV_HEAD)media=SV_HEAD;
    boxw=media-32;boxh=(int)d->height*8-32;
    normal=(double)boxw/input->width;if(normal>(double)boxh/input->height)normal=(double)boxh/input->height;
    turned=(double)boxw/input->height;if(turned>(double)boxh/input->width)turned=(double)boxh/input->width;
    rot=turned>normal;scale=rot?turned:normal;
    rw=rot?input->height:input->width;rh=rot?input->width:input->height;
    w=(int)(rw*scale);h=(int)(rh*scale);if(w<1)w=1;if(h<1)h=1;
    label->width=SV_HEAD;label->height=(int)d->height*8;label->stride=SV_HEAD;
    label->pixels=(unsigned char *)malloc(label->stride*(size_t)label->height);
    if(!label->pixels)return failure(err,"out of memory fitting label");
    memset(label->pixels,15,label->stride*(size_t)label->height);
    left=(SV_HEAD-w)/2;top=(label->height-h)/2;
    for(y=0;y<h;y++)for(x=0;x<w;x++){
        rx=(int)((long)x*rw/w);ry=(int)((long)y*rh/h);
        sx=rot?input->width-1-ry:rx;sy=rot?rx:ry;
        index=input->pixels[(size_t)sy*input->stride+(size_t)sx];
        if(index>15){free(label->pixels);label->pixels=NULL;return failure(err,"invalid VGA palette index in print image");}
        rgb=lc_vga_rgb[index];lum=(299*(int)rgb[0]+587*(int)rgb[1]+114*(int)rgb[2])/1000;
        label->pixels[(size_t)(top+y)*label->stride+(size_t)(left+x)]=
            (unsigned char)(lum<(int)bayer[(top+y)&3][(left+x)&3]*16+8?0:15);
    }
    return 1;
}
static int wait_status(device *d,int kind,double seconds,app_error *err)
{
    double deadline=now_seconds()+seconds;int ready;
    do{
        if(!status_read(d,err)||!device_fault(d,err))return 0;
        ready=kind==0?!(d->flags[1]&4)&&!(d->flags[2]&64):
              kind==1?(d->flags[2]&64)!=0:!(d->flags[0]&1);
        if(ready)return 1;
        pause_ms(kind==2?20:100);
    }while(!stopping&&now_seconds()<deadline);
    return failure(err,"timed out waiting for printer state; no automatic retry");
}
static int print_label(device *d,const lc_image *label,app_error *err)
{
    unsigned char *rows=NULL,*buffers=NULL,*blocks=NULL,whole[4096],reply[64];
    size_t lengths[64],whole_len,n,offset;
    int count,i,y,x,first,ok=0,started=0;unsigned take,speed;
    unsigned long before=d->remaining;
    double deadline,stable=0,now;
    count=(label->height+SV_ROWS_PER_BUFFER-1)/SV_ROWS_PER_BUFFER;
    if(count<1||count>64)return failure(err,"invalid printer buffer count");
    rows=(unsigned char *)calloc((size_t)label->height,SV_BPL);
    buffers=(unsigned char *)malloc((size_t)count*SV_BUFFER);
    blocks=(unsigned char *)malloc((size_t)count*SV_BUFFER);
    if(!rows||!buffers||!blocks){failure(err,"out of memory packing label");goto done;}
    for(y=0;y<label->height;y++)for(x=0;x<SV_HEAD;x++)
        if(label->pixels[(size_t)y*label->stride+(size_t)x]==0)rows[(size_t)y*SV_BPL+(size_t)x/8]|=(unsigned char)(1U<<(x&7));
    for(i=0;i<count;i++){
        first=i*SV_ROWS_PER_BUFFER;take=(unsigned)(label->height-first);
        if(take>SV_ROWS_PER_BUFFER)take=SV_ROWS_PER_BUFFER;
        sv_buffer(buffers+(size_t)i*SV_BUFFER,rows+(size_t)first*SV_BPL,take,i==0,i==count-1);
    }
    whole_len=sv_lzma(buffers,(size_t)count*SV_BUFFER,whole,sizeof whole);
    if(whole_len){memcpy(blocks,whole,whole_len);lengths[0]=whole_len;count=1;speed=60;}
    else{
        speed=10; /* 32-row blocks: 4 mm at 10 mm/s covers the HID/timer overhead. */
        for(i=0;i<count;i++){
            lengths[i]=sv_lzma(buffers+(size_t)i*SV_BUFFER,SV_BUFFER,blocks+(size_t)i*SV_BUFFER,SV_BUFFER);
            if(!lengths[i]){failure(err,"compressed print block exceeds device capacity; no job sent");goto done;}
        }
    }
    if(!wait_status(d,0,10,err))goto done;
    fprintf(stderr,"charts: native USB %s, %ux%u mm, %d LZMA block%s, remaining %lu\n",d->path,d->width,d->height,count,count==1?"":"s",before);
    if(!command(d,0x13,1,-1,reply,err))goto done;
    started=1;if(!wait_status(d,1,10,err))goto done;
    for(i=0;i<count;i++){
        if(!wait_status(d,2,20,err)||!command(d,0x5c,(unsigned)lengths[i],-1,reply,err))goto done;
        for(offset=0;offset<lengths[i];offset+=n){
            n=lengths[i]-offset;if(n>64)n=64;
            if(!hid_write(d->fd,blocks+(size_t)i*SV_BUFFER+offset,n,err))goto done;
        }
        if(hid_read(d->fd,reply,2000)<=0){failure(err,"missing USB bulk acknowledgement");goto done;}
        pause_ms(150);
        if(!command(d,0x10,(unsigned)lengths[i],(int)speed,reply,err))goto done;
        pause_ms(100);
    }
    deadline=now_seconds()+60;
    do{
        if(!status_read(d,err)||!device_fault(d,err)||!material_read(d,err))goto done;
        now=now_seconds();
        if(!(d->flags[1]&4)&&!(d->flags[2]&64)&&(!before||d->remaining<before)){
            if(!stable)stable=now;
            if(now-stable>=1){ok=1;break;}
        }else stable=0;
        pause_ms(250);
    }while(!stopping&&now_seconds()<deadline);
    if(!ok)failure(err,"final printer state/count did not settle; check paper before retrying");
    else fprintf(stderr,"charts: label complete, printer idle, remaining %lu\n",d->remaining);
done:
    if(!ok&&started){app_error ignored;memset(&ignored,0,sizeof ignored);command(d,0x14,0,-1,reply,&ignored);}
    free(rows);free(buffers);free(blocks);return ok;
}
static int local_operation(int op,const lc_image *input,const char *target,lc_image *label,
                            unsigned char text[512],size_t *text_length,app_error *err)
{
    device d;int ok=0;
    if(!device_open(&d,target,err))return 0;
    if(op==OP_STATUS){*text_length=(size_t)status_json(&d,text,512);ok=1;}
    else if(fit_label(input,&d,label,err)){
        if(op==OP_PREVIEW)ok=1;
        else if(device_fault(&d,err)&&print_label(&d,label,err)){
            *text_length=(size_t)status_json(&d,text,512);ok=1;
        }
    }
    device_close(&d);return ok;
}
static int token_read(unsigned char token[64],app_error *err)
{
    char path[PATH_MAX];const char *configured,*home;FILE *f;size_t n;unsigned i;int c;
    struct stat st;
    configured=getenv("CHARTS_PRINTER_TOKEN_FILE");home=getenv("HOME");
    if(configured&&*configured){if(strlen(configured)>=sizeof path)return failure(err,"token path too long");strcpy(path,configured);}
    else{if(!home||strlen(home)>sizeof path-32)return failure(err,"invalid HOME for printer token");sprintf(path,"%s/.config/charts/printer.token",home);}
    if(stat(path,&st)||!S_ISREG(st.st_mode)||(st.st_mode&077)||st.st_uid!=getuid())
        return failure(err,"network printing needs an owned mode-0600 ~/.config/charts/printer.token (64 hex characters)");
    f=fopen(path,"rb");if(!f)return failure(err,"cannot read printer token");
    n=fread(token,1,64,f);c=fgetc(f);
    if(c=='\n')c=fgetc(f);
    fclose(f);
    if(n!=64||c!=EOF)return failure(err,"printer token must be exactly 64 hex characters");
    for(i=0;i<64;i++)if(!isxdigit(token[i]))return failure(err,"printer token is not hexadecimal");
    return 1;
}
static int parse_address(const char *address,char host[256],char port[6],app_error *err)
{
    const char *colon;size_t n;long value;char *end;
    if(!strncmp(address,"supvan://",9))address+=9;
    colon=strchr(address,':');n=colon?(size_t)(colon-address):strlen(address);
    if(!n||n>=256)return failure(err,"printer address wants HOST[:PORT]");
    memcpy(host,address,n);host[n]=0;
    if(!colon)strcpy(port,"9280");
    else{
        value=strtol(colon+1,&end,10);
        if(!colon[1]||*end||value<1||value>65535)return failure(err,"invalid printer TCP port");
        sprintf(port,"%ld",value);
    }
    return 1;
}
static int tcp_connect(const char *address,app_error *err)
{
    struct addrinfo hints,*list,*p;char host[256],port[6];int fd=-1,error; socklen_t length;
    if(!parse_address(address,host,port,err))return -1;
    memset(&hints,0,sizeof hints);hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;
    if(getaddrinfo(host,port,&hints,&list)){failure(err,"cannot resolve printer host");return -1;}
    for(p=list;p;p=p->ai_next){
        fd=socket(p->ai_family,p->ai_socktype,p->ai_protocol);if(fd<0)continue;
        if(fcntl(fd,F_SETFL,O_NONBLOCK)<0){close(fd);fd=-1;continue;}
        if(connect(fd,p->ai_addr,p->ai_addrlen)==0)break;
        if(errno==EINPROGRESS&&wait_fd(fd,POLLOUT,10000)>0){
            error=0;length=sizeof error;
            if(!getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&length)&&!error)break;
        }
        close(fd);fd=-1;
    }
    freeaddrinfo(list);if(fd<0)failure(err,"cannot connect to native charts printer listener");return fd;
}
static int response_send(int fd,int success,const unsigned char *body,size_t size,app_error *err)
{
    unsigned char header[NET_RESPONSE];
    memcpy(header,NET_MAGIC,8);put32(header+8,success?0:1);put32(header+12,(unsigned long)size);
    return transfer(fd,header,sizeof header,1,err)&&transfer(fd,(unsigned char *)body,size,1,err);
}
static int remote_operation(int op,const lc_image *input,const char *target,lc_image *label,
                             unsigned char text[512],size_t *text_length,app_error *err)
{
    unsigned char request[NET_REQUEST],response[NET_RESPONSE],*body=NULL;
    unsigned long size;int fd=-1,y,ok=0;
    memset(request,0,sizeof request);memcpy(request,NET_MAGIC,8);
    if(!token_read(request+8,err))return 0;
    request[72]=(unsigned char)op;
    if(input){
        if(input->width<1||input->width>8192||input->height<1||input->height>8192||
           (unsigned long)input->width*input->height>MAX_PIXELS)return failure(err,"network image exceeds 16 million pixels");
        put16(request+76,(unsigned)input->width);put16(request+78,(unsigned)input->height);
    }
    fd=tcp_connect(target,err);if(fd<0)return 0;
    if(!transfer(fd,request,sizeof request,1,err))goto done;
    if(input)for(y=0;y<input->height;y++)
        if(!transfer(fd,input->pixels+(size_t)y*input->stride,(size_t)input->width,1,err))goto done;
    if(!transfer(fd,response,sizeof response,0,err))goto done;
    if(memcmp(response,NET_MAGIC,8)){failure(err,"invalid native printer response");goto done;}
    size=be32(response+12);
    if(size>(unsigned long)SV_HEAD*SV_MAX_ROWS+4){failure(err,"oversized native printer response");goto done;}
    body=(unsigned char *)malloc((size_t)size+1);if(!body){failure(err,"out of memory reading printer reply");goto done;}
    if(!transfer(fd,body,(size_t)size,0,err))goto done;
    body[size]=0;
    if(be32(response+8)){failure(err,(const char *)body);goto done;}
    if(op==OP_PREVIEW){
        if(size<4||be16(body)!=SV_HEAD||be16(body+2)>SV_MAX_ROWS||!be16(body+2)||size!=4UL+SV_HEAD*(unsigned long)be16(body+2)){
            failure(err,"invalid printer preview dimensions");goto done;
        }
        label->width=SV_HEAD;label->height=(int)be16(body+2);label->stride=SV_HEAD;
        label->pixels=(unsigned char *)malloc((size_t)size-4);
        if(!label->pixels){failure(err,"out of memory reading preview");goto done;}
        memcpy(label->pixels,body+4,(size_t)size-4);
        for(size=0;size<label->stride*(size_t)label->height;size++)if(label->pixels[size]>15){failure(err,"invalid preview palette");goto done;}
    }else{
        if(size>=512){failure(err,"oversized printer status");goto done;}
        memcpy(text,body,(size_t)size);*text_length=(size_t)size;
    }
    ok=1;
done:if(fd>=0)close(fd);free(body);return ok;
}
static int operation(int op,const lc_image *input,const char *target,lc_image *label,
                      unsigned char text[512],size_t *size,app_error *err)
{
    const char *configured;
    if(!target||!strcmp(target,"supvan")){
        configured=getenv("CHARTS_PRINTER");target=configured&&*configured?configured:"supvan";
    }
    if(!strncmp(target,"supvan://",9))return remote_operation(op,input,target,label,text,size,err);
    if(strcmp(target,"supvan")&&strncmp(target,"/dev/hidraw",11))return failure(err,"printer target wants supvan, /dev/hidrawN or supvan://HOST[:PORT]");
    return local_operation(op,input,target,label,text,size,err);
}
int app_printer_status(const char *target,app_error *err)
{
    lc_image label;unsigned char text[512];size_t size=0;int ok;
    memset(&label,0,sizeof label);ok=operation(OP_STATUS,NULL,target,&label,text,&size,err);
    if(ok&&fwrite(text,1,size,stdout)!=size)ok=failure(err,"cannot write printer status");
    free(label.pixels);return ok;
}
int app_printer_image(lc_context *ctx,const lc_image *image,const char *target,const char *preview,app_error *err)
{
    lc_image label;unsigned char text[512];size_t size=0;int ok;FILE *f;lc_sink sink;lc_status result;
    memset(&label,0,sizeof label);
    ok=operation(preview?OP_PREVIEW:OP_PRINT,image,target,&label,text,&size,err);
    if(ok&&preview){
        f=!strcmp(preview,"-")?stdout:fopen(preview,"wb");
        if(!f)ok=failure(err,"cannot open preview output");
        else{
            sink.user=f;sink.write=app_file_sink;result=lc_write_png(ctx,&label,&sink);
            if(f!=stdout){if(fclose(f)&&result==LC_OK)result=LC_EIO;}else if(fflush(f)&&result==LC_OK)result=LC_EIO;
            if(result!=LC_OK)ok=failure(err,"cannot write printer preview PNG");
        }
    }else if(ok&&size)fwrite(text,1,size,stderr);
    free(label.pixels);return ok;
}
static void handle_client(int fd,const unsigned char token[64])
{
    unsigned char request[NET_REQUEST],text[512],*body=NULL;
    lc_image input,label;app_error err,send_error;size_t size=0;unsigned difference=0,i,w,h;int op,ok=0;
    memset(&err,0,sizeof err);memset(&send_error,0,sizeof send_error);
    memset(&input,0,sizeof input);memset(&label,0,sizeof label);
    /* Short unauthenticated header deadline; token checked before allocating data. */
    if(wait_fd(fd,POLLIN,5000)<=0)goto done;
    if(!transfer_timed(fd,request,sizeof request,0,5,&err))goto done;
    for(i=0;i<64;i++)difference|=request[i+8]^token[i];
    if(memcmp(request,NET_MAGIC,8)||difference){failure(&err,"printer authentication failed");goto reply;}
    op=request[72];w=be16(request+76);h=be16(request+78);
    if(request[73]||request[74]||request[75]||op<OP_STATUS||op>OP_PREVIEW){failure(&err,"invalid printer request");goto reply;}
    if(op==OP_STATUS){if(w||h){failure(&err,"status request must not contain pixels");goto reply;}}
    else{
        if(!w||!h||w>8192||h>8192||(unsigned long)w*h>MAX_PIXELS){failure(&err,"invalid network image dimensions");goto reply;}
        input.width=(int)w;input.height=(int)h;input.stride=w;
        input.pixels=(unsigned char *)malloc((size_t)w*h);
        if(!input.pixels){failure(&err,"out of memory reading network image");goto reply;}
        if(!transfer(fd,input.pixels,(size_t)w*h,0,&err))goto done;
        for(size=0;size<(size_t)w*h;size++)if(input.pixels[size]>15){failure(&err,"invalid network image palette");goto reply;}
    }
    size=0;ok=local_operation(op,op==OP_STATUS?NULL:&input,"supvan",&label,text,&size,&err);
    if(ok&&op==OP_PREVIEW){
        size=4+label.stride*(size_t)label.height;body=(unsigned char *)malloc(size);
        if(!body){ok=failure(&err,"out of memory sending preview");goto reply;}
        put16(body,SV_HEAD);put16(body+2,(unsigned)label.height);memcpy(body+4,label.pixels,size-4);
    }
reply:
    if(ok)response_send(fd,1,body?body:text,size,&send_error);
    else response_send(fd,0,(const unsigned char *)err.message,strlen(err.message),&send_error);
done:
    if(err.code)fprintf(stderr,"charts printer: %s\n",err.message);
    free(body);free(input.pixels);free(label.pixels);
}
int app_printer_serve(const char *address,app_error *err)
{
    struct sockaddr_in addr,peer;struct sigaction action,old_term,old_int;
    char host[256],port[6];unsigned char token[64];unsigned long ip;
    int fd,client,one=1; socklen_t length;
    if(!token_read(token,err)||!parse_address(address,host,port,err))return 0;
    memset(&addr,0,sizeof addr);addr.sin_family=AF_INET;addr.sin_port=htons((unsigned short)atoi(port));
    if(inet_pton(AF_INET,host,&addr.sin_addr)!=1)return failure(err,"--printer-listen requires an explicit private IPv4 address");
    ip=ntohl(addr.sin_addr.s_addr);
    if(!((ip>>24)==10||(ip>>24)==127||(ip>>20)==0xac1||(ip>>16)==0xc0a8))
        return failure(err,"printer listener must bind a private LAN or loopback address, not a public/wildcard interface");
    fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return failure(err,"cannot create printer listener socket");
    setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
    if(bind(fd,(struct sockaddr *)&addr,sizeof addr)||listen(fd,8)){close(fd);return failure(err,"cannot bind/listen on printer address");}
    memset(&action,0,sizeof action);action.sa_handler=stop_server;sigemptyset(&action.sa_mask);
    sigaction(SIGTERM,&action,&old_term);sigaction(SIGINT,&action,&old_int);stopping=0;
    fprintf(stderr,"charts: native printer listener %s:%s (token-authenticated, unencrypted trusted LAN)\n",host,port);
    while(!stopping){
        if(wait_fd(fd,POLLIN,500)<=0)continue;
        length=sizeof peer;client=accept(fd,(struct sockaddr *)&peer,&length);if(client<0)continue;
        fcntl(client,F_SETFL,O_NONBLOCK);handle_client(client,token);close(client);
    }
    close(fd);sigaction(SIGTERM,&old_term,NULL);sigaction(SIGINT,&old_int,NULL);stopping=0;return 1;
}
#endif
