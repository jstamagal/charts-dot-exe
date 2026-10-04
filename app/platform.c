/* Only this application adapter owns terminals, OS calls, and VGA hardware. */
#if !defined(__DOS__) && !defined(MSDOS) && !defined(__MSDOS__)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#else
#define APP_DOS 1
#endif
#include "platform.h"
#include "bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include "os.h"
#include <fcntl.h>
#ifdef APP_DOS
#include <conio.h>
#include <bios.h>
#include <dos.h>
#include <i86.h>
#include <io.h>
#include <direct.h>
#else
#include <unistd.h>
#include <dirent.h>
#include <termios.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#ifdef __linux__
#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/vt.h>
#endif
#endif

#define MAXIMUM(a,b) ((a)>(b)?(a):(b))
#define MINIMUM(a,b) ((a)<(b)?(a):(b))
static const char *env(const char *s) { const char *v;v=getenv(s);return v ? v : ""; }
static void say(const char *s)
{
#ifndef APP_DOS
    size_t n;
    n=strlen(s);
    while (n) {
        ssize_t w;
        w=write(1,s,n);if (w<0 && errno==EINTR) continue;if (w<=0) break;
        s+=w;n-=(size_t)w;
    }
#else
    fputs(s,stdout);fflush(stdout);
#endif
}
static int stdout_sink(void *user,const unsigned char *p,size_t n)
{
    (void)user;
#ifndef APP_DOS
    while (n) {
        ssize_t w;
        w=write(1,p,n);if (w<0 && errno==EINTR) continue;if (w<=0) return -1;
        p+=w;n-=(size_t)w;
    }
    return 0;
#else
    return app_file_sink(stdout,p,n);
#endif
}
int app_is_tty(int fd) { return isatty(fd)!=0; }
void app_term_size(int *cols,int *rows)
{
    *cols=80;*rows=24;
#ifndef APP_DOS
    {
        int i;
        struct winsize w;
        for (i=0;i<3;i++) {
            memset(&w,0,sizeof w);
            if (ioctl(i==0 ? 1 : (i==1 ? 0 : 2),TIOCGWINSZ,&w)==0 && w.ws_col && w.ws_row) {
                *cols=w.ws_col;*rows=w.ws_row;return;
            }
        }
    }
#else
    *rows=25;
#endif
    if (atoi(env("COLUMNS"))>0) *cols=atoi(env("COLUMNS"));
    if (atoi(env("LINES"))>0) *rows=atoi(env("LINES"));
}
int app_raw_enter(app_raw *raw,int fd)
{
    raw->saved=NULL;raw->fd=fd;
#ifndef APP_DOS
    {
        struct termios *saved,now;
        if (!isatty(fd)) return 0;
        saved=(struct termios *)malloc(sizeof *saved);if (!saved) return 0;
        if (tcgetattr(fd,saved)!=0) { free(saved);return 0; }
        now=*saved;now.c_lflag&=~(ICANON|ECHO|ISIG);now.c_iflag&=~(IXON|ICRNL|BRKINT|INPCK|ISTRIP);
        now.c_oflag&=~OPOST;now.c_cc[VMIN]=1;now.c_cc[VTIME]=0;
        if (tcsetattr(fd,TCSANOW,&now)!=0) { free(saved);return 0; }
        raw->saved=saved;return 1;
    }
#else
    return isatty(fd)!=0;
#endif
}
void app_raw_leave(app_raw *raw)
{
#ifndef APP_DOS
    if (raw->saved) tcsetattr(raw->fd,TCSANOW,(struct termios *)raw->saved);
#endif
    free(raw->saved);raw->saved=NULL;
}
#ifdef APP_DOS
/* DOS console reads process Ctrl-C before the presenter can cancel an edit.
 * BIOS input preserves it, and returns ASCII and scan code together. */
static int dos_key(void)
{
    if (!_bios_keybrd(_KEYBRD_READY)) return -1;
    return (int)_bios_keybrd(_KEYBRD_READ);
}
#endif
static int read_byte(int ms)
{
#ifndef APP_DOS
    fd_set fds;
    struct timeval tv;
    unsigned char c;
    int n;
    FD_ZERO(&fds);FD_SET(0,&fds);tv.tv_sec=ms/1000;tv.tv_usec=(ms%1000)*1000;
    n=select(1,&fds,NULL,NULL,&tv);if (n<=0) return -1;
    return read(0,&c,1)==1 ? c : -1;
#else
    clock_t start,now,ticks;
    int c;
    start=clock();ticks=(clock_t)((double)ms*CLOCKS_PER_SEC/1000.0);
    if (start==(clock_t)-1) return dos_key();
    do {
        c=dos_key();if (c>=0) return c;
        now=clock();
    } while (now!=(clock_t)-1 && now>=start && now-start<ticks);
    return -1;
#endif
}
void app_read_key(int timeout_ms,char key[32])
{
    static const struct { const char *seq,*name; } keys[]={
      {"[A","up"},{"OA","up"},{"[B","down"},{"OB","down"},{"[C","right"},{"OC","right"},
      {"[D","left"},{"OD","left"},{"[5~","pgup"},{"[6~","pgdn"},{"[H","home"},{"[1~","home"},
      {"OH","home"},{"[7~","home"},{"[F","end"},{"[4~","end"},{"OF","end"},{"[8~","end"},
      {"[2~","insert"},{"[3~","delete"},{"[Z","shift-tab"},
      {"OP","f1"},{"[11~","f1"},{"[[A","f1"},{"OQ","f2"},{"[12~","f2"},{"[[B","f2"},
      {"OR","f3"},{"[13~","f3"},{"[[C","f3"},{"OS","f4"},{"[14~","f4"},{"[[D","f4"},
      {"[15~","f5"},{"[[E","f5"},{"[17~","f6"},{"[18~","f7"},{"[19~","f8"},{"[20~","f9"},
      {"[21~","f10"},{"[1;5C","ctrl-right"},{"[1;5D","ctrl-left"},{"[1;2C","shift-right"},{"[1;2D","shift-left"}
    };
    int c,b,i,more;
    size_t k;
    char seq[16];
    key[0]=0;c=read_byte(timeout_ms);if (c<0) return;
#ifdef APP_DOS
    b=(c>>8)&255;c&=255;
    if (c==0 || (c==224 && b)) {
        static const struct { int code;const char *name; } doskeys[]={
            {72,"up"},{80,"down"},{75,"left"},{77,"right"},{73,"pgup"},{81,"pgdn"},
            {71,"home"},{79,"end"},{82,"insert"},{83,"delete"},{15,"shift-tab"},
            {59,"f1"},{60,"f2"},{61,"f3"},{62,"f4"},{63,"f5"},{64,"f6"},{65,"f7"},{66,"f8"},{67,"f9"},{68,"f10"}
        };
        strcpy(key,"unknown");
        for (k=0;k<sizeof doskeys/sizeof doskeys[0];k++) if (doskeys[k].code==b) { strcpy(key,doskeys[k].name);break; }
        return;
    }
    /* BIOS character codes are OEM bytes, not UTF-8. Until a codepage adapter is
     * selected, reject non-ASCII input without consuming subsequent keys. */
    if (c>=128) { strcpy(key,"unknown");return; }
    if (c==27) { strcpy(key,"esc");return; }
#endif
    if (c==9) { strcpy(key,"tab");return; }
    if (c==13 || c==10) { strcpy(key,"enter");return; }
    if (c==127 || c==8) { strcpy(key,"backspace");return; }
    if (c<27) { strcpy(key,"ctrl-");key[5]=(char)('a'+c-1);key[6]=0;return; }
    if (c>=128) {
        key[0]=(char)c;i=1;more=c>=240 ? 3 : (c>=224 ? 2 : (c>=192 ? 1 : 0));
        while (more--) { b=read_byte(25);if (b<0) break;key[i++]=(char)b; }key[i]=0;return;
    }
    if (c!=27) { key[0]=(char)c;key[1]=0;return; }
    seq[0]=0;
    for (i=0;i<12;i++) {
        b=read_byte(25);if (b<0) break;seq[i]=(char)b;seq[i+1]=0;
        if (!strcmp(seq,"[") || !strcmp(seq,"O") || !strcmp(seq,"[[")) continue;
        if ((b>='A' && b<='Z') || (b>='a' && b<='z') || b=='~') break;
    }
    if (!*seq) { strcpy(key,"esc");return; }
    for (k=0;k<sizeof keys/sizeof keys[0];k++) if (!strcmp(seq,keys[k].seq)) { strcpy(key,keys[k].name);return; }
    if (!seq[1] && seq[0]>=32) { strcpy(key,"alt-");key[4]=seq[0];key[5]=0;return; }
    strcpy(key,"unknown");
}
void app_cursor_show(int show)
{
#ifdef APP_DOS
    union REGS r;
    memset(&r,0,sizeof r);r.w.ax=0x0100;r.w.cx=show ? 0x0607 : 0x2000;int386(0x10,&r,&r);
#else
    say(show ? "\033[?25h" : "\033[?25l");
#endif
}
void app_screen_clear(void) { say("\033[2J\033[H"); }
void app_cursor_home(void) { say("\033[H"); }

static void (*cleanup_hook)(void);
static void (*resume_hook)(void);
#ifndef APP_DOS
static struct termios guard_saved;
static int guard_have,guard_installed;
static volatile sig_atomic_t guard_resumed;
static void cleanup(void) { void (*fn)(void);fn=cleanup_hook;cleanup_hook=NULL;if (fn) fn(); }
static void fatal(int sig)
{
    cleanup();say("\033[0m\033[?25h");
    if (guard_have) tcsetattr(0,TCSANOW,&guard_saved);
    signal(sig,SIG_DFL);raise(sig);
}
static void stopped(int sig);
static void arm_stop(void)
{
    struct sigaction sa;
    memset(&sa,0,sizeof sa);sa.sa_handler=stopped;sigemptyset(&sa.sa_mask);sigaction(SIGTSTP,&sa,NULL);
}
static void stopped(int sig)
{
    int saved_errno,had;
    struct termios now;
    sigset_t set;
    (void)sig;saved_errno=errno;
    if (cleanup_hook) cleanup_hook();
    had=guard_have && tcgetattr(0,&now)==0;
    if (guard_have) tcsetattr(0,TCSANOW,&guard_saved);
    say("\033[0m\033[2J\033[H\033[?25h");signal(SIGTSTP,SIG_DFL);
    sigemptyset(&set);sigaddset(&set,SIGTSTP);sigprocmask(SIG_UNBLOCK,&set,NULL);raise(SIGTSTP);
    arm_stop();if (had) tcsetattr(0,TCSANOW,&now);
    say("\033[?25l");if (resume_hook) resume_hook();guard_resumed=1;errno=saved_errno;
}
void app_guard_install(void)
{
    static const int signals[]={SIGINT,SIGTERM,SIGHUP,SIGQUIT,SIGSEGV,SIGABRT,SIGBUS,SIGFPE,SIGILL,SIGPIPE};
    struct sigaction sa;
    size_t i;
    if (isatty(0) && tcgetattr(0,&guard_saved)==0) guard_have=1;
    if (guard_installed) return;
    memset(&sa,0,sizeof sa);sa.sa_handler=fatal;sigemptyset(&sa.sa_mask);
    for (i=0;i<sizeof signals/sizeof signals[0];i++) sigaction(signals[i],&sa,NULL);
    arm_stop();atexit(cleanup);guard_installed=1;
}
void app_guard_restore(void) { cleanup();if (guard_have) tcsetattr(0,TCSANOW,&guard_saved); }
int app_guard_continued(void) { int n;n=guard_resumed;guard_resumed=0;return n; }
#else
static void cleanup(void) { if (cleanup_hook) cleanup_hook();cleanup_hook=NULL; }
void app_guard_install(void) { atexit(cleanup); }
void app_guard_restore(void) { cleanup(); }
int app_guard_continued(void) { return 0; }
#endif

struct app_display {
    int kind,scale,want_scale,color,ascii,bright_bg,raw,closed;
    int cols,rows,cw,ch,last_w,last_h,last_bg;
    unsigned frame;
    int fd,fake,took_vt,xres,yres,bytes,stride,xoff,yoff;
    unsigned long lut[16];
    unsigned char *line;
    size_t line_size;
};
/* kind: cells=0, fb=1, kitty=2, sixel=3, VGA=4 */
#if defined(__linux__) && !defined(APP_DOS)
static int vt_fd=-1,vt_graphics;
static volatile sig_atomic_t vt_active=1,vt_redraw;
static void vt_release(int s) { (void)s;vt_active=0;if (vt_fd>=0) ioctl(vt_fd,VT_RELDISP,1); }
static void vt_acquire(int s) { (void)s;if (vt_fd>=0) ioctl(vt_fd,VT_RELDISP,VT_ACKACQ);vt_active=1;vt_redraw=1; }
static void vt_take(void)
{
    struct vt_mode vm;
    if (vt_fd<0) return;
    memset(&vm,0,sizeof vm);vm.mode=VT_PROCESS;vm.relsig=SIGUSR1;vm.acqsig=SIGUSR2;
    ioctl(vt_fd,VT_SETMODE,&vm);if (ioctl(vt_fd,KDSETMODE,KD_GRAPHICS)==0) vt_graphics=1;vt_active=1;vt_redraw=1;
}
static void vt_restore(void)
{
    struct vt_mode vm;
    if (vt_fd<0) return;
    memset(&vm,0,sizeof vm);vm.mode=VT_AUTO;ioctl(vt_fd,VT_SETMODE,&vm);
    if (vt_graphics) ioctl(vt_fd,KDSETMODE,KD_TEXT);
    vt_graphics=0;
}
static unsigned long channel(unsigned char v,struct fb_bitfield f)
{
    unsigned bits;
    bits=f.length>8 ? 8 : f.length;
    if (!bits || f.offset>=32) return 0;
    return ((unsigned long)v>>(8-bits))<<f.offset;
}
static int fb_open(app_display *d,app_error *why)
{
    const char *dev,*geom;
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    int w,h,bpp,i,mode;
    dev=env("CHARTS_FB");if (!*dev) dev=env("FRAMEBUFFER");if (!*dev) dev="/dev/fb0";
    d->fd=open(dev,O_RDWR);if (d->fd<0) { app_error_path(why,0,dev,strerror(errno));return 0; }
    memset(&var,0,sizeof var);memset(&fix,0,sizeof fix);geom=env("CHARTS_FB_GEOM");
    if (*geom) {
        w=h=0;bpp=32;
        if (sscanf(geom,"%dx%dx%d",&w,&h,&bpp)<2 || w<64 || h<64 || w>32768 || h>32768 || (bpp!=16 && bpp!=24 && bpp!=32)) { app_error_set(why,0,"bad CHARTS_FB_GEOM");return 0; }
        var.xres=var.xres_virtual=(unsigned)w;var.yres=var.yres_virtual=(unsigned)h;var.bits_per_pixel=(unsigned)bpp;
        if (bpp==16) { var.red.offset=11;var.red.length=5;var.green.offset=5;var.green.length=6;var.blue.length=5; }
        else { var.red.offset=16;var.green.offset=8;var.red.length=var.green.length=var.blue.length=8; }
        fix.line_length=(unsigned)(w*(bpp/8));d->fake=1;
    } else if (ioctl(d->fd,FBIOGET_VSCREENINFO,&var)!=0 || ioctl(d->fd,FBIOGET_FSCREENINFO,&fix)!=0) { app_error_path(why,0,dev,"not a framebuffer");return 0; }
    if ((var.bits_per_pixel!=16 && var.bits_per_pixel!=24 && var.bits_per_pixel!=32) || var.xres>32768 || var.yres>32768 || fix.line_length>INT_MAX) {
        app_error_path(why,0,dev,"unsupported framebuffer geometry");return 0;
    }
    d->xres=(int)var.xres;d->yres=(int)var.yres;d->bytes=(int)var.bits_per_pixel/8;
    d->stride=MAXIMUM((int)fix.line_length,d->xres*d->bytes);
    if (!d->xres || !d->yres || !d->stride ||
        var.xoffset>(unsigned)(d->stride/d->bytes) ||
        var.xres>(unsigned)(d->stride/d->bytes)-var.xoffset ||
        var.xoffset>var.xres_virtual || var.xres>var.xres_virtual-var.xoffset ||
        var.yoffset>var.yres_virtual || var.yres>var.yres_virtual-var.yoffset ||
        var.yoffset>(unsigned long)LONG_MAX/(unsigned)d->stride ||
        var.yres>(unsigned long)LONG_MAX/(unsigned)d->stride-var.yoffset) {
        app_error_path(why,0,dev,"framebuffer viewport exceeds its storage");return 0;
    }
    d->xoff=(int)var.xoffset;d->yoff=(int)var.yoffset;
    for (i=0;i<16;i++) {
        d->lut[i]=channel(lc_vga_rgb[i][0],var.red)|channel(lc_vga_rgb[i][1],var.green)|channel(lc_vga_rgb[i][2],var.blue);
        if (var.transp.length && var.transp.length<32 && var.transp.offset<32) d->lut[i]|=((1UL<<var.transp.length)-1)<<var.transp.offset;
    }
    d->scale=d->want_scale>0 ? d->want_scale : MAXIMUM(1,(d->xres+480)/960);
    while (d->scale>1 && (d->xres/(8*d->scale)<60 || d->yres/(16*d->scale)<16)) d->scale--;
    if (!d->fake && ioctl(0,KDGETMODE,&mode)==0) {
        struct sigaction sa;
        vt_fd=0;memset(&sa,0,sizeof sa);sigemptyset(&sa.sa_mask);sa.sa_flags=SA_RESTART;sa.sa_handler=vt_release;
        sigaction(SIGUSR1,&sa,NULL);sa.sa_handler=vt_acquire;sigaction(SIGUSR2,&sa,NULL);
        vt_take();vt_redraw=0;cleanup_hook=vt_restore;resume_hook=vt_take;d->took_vt=1;
    }
    return 1;
}
#endif
#ifdef APP_DOS
static int vga_old_mode=3;
static void vga_mode(int mode) { union REGS r;memset(&r,0,sizeof r);r.w.ax=(unsigned short)mode;int386(0x10,&r,&r); }
static void vga_restore(void) { vga_mode(vga_old_mode); }
static void vga_open(void)
{
    union REGS r;
    int i;
    memset(&r,0,sizeof r);r.w.ax=0x0f00;int386(0x10,&r,&r);vga_old_mode=r.h.al;
    vga_mode(0x12);
    for (i=0;i<16;i++) { inp(0x3da);outp(0x3c0,i);outp(0x3c0,i); }
    inp(0x3da);outp(0x3c0,0x20);outp(0x3c8,0);
    for (i=0;i<16;i++) { outp(0x3c9,lc_vga_rgb[i][0]>>2);outp(0x3c9,lc_vga_rgb[i][1]>>2);outp(0x3c9,lc_vga_rgb[i][2]>>2); }
    cleanup_hook=vga_restore;
}
#endif
static void term_pixels(app_display *d)
{
    d->cw=d->ch=0;app_term_size(&d->cols,&d->rows);
#ifndef APP_DOS
    {
        struct winsize w;
        memset(&w,0,sizeof w);
        if (ioctl(1,TIOCGWINSZ,&w)!=0 || !w.ws_col) ioctl(0,TIOCGWINSZ,&w);
        if (w.ws_col && w.ws_row && w.ws_xpixel && w.ws_ypixel) {
            d->cols=w.ws_col;d->rows=w.ws_row;d->cw=w.ws_xpixel/w.ws_col;d->ch=w.ws_ypixel/w.ws_row;
        }
    }
#endif
}
static void query(char reply[514])
{
    app_raw raw;
    size_t n;
    int c;
    reply[0]=0;
    if (!app_is_tty(0) || !app_is_tty(1) || !app_raw_enter(&raw,0)) return;
    say("\033_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA\033\\\033[c");n=0;
    while (n<512 && (c=read_byte(250))>=0) { reply[n++]=(char)c;reply[n]=0;if (c=='c' && strstr(reply,"\033[?")) break; }
    app_raw_leave(&raw);
}
int app_gfx_valid(const char *n)
{ return !strcmp(n,"auto") || !strcmp(n,"fb") || !strcmp(n,"kitty") || !strcmp(n,"sixel") || !strcmp(n,"cells") || !strcmp(n,"ascii") || !strcmp(n,"vga"); }
app_display *app_display_open(const char *want,int scale,int color,app_error *why)
{
    app_display *d;
    const char *term,*override;
    char reply[514],*p;
    d=(app_display *)calloc(1,sizeof *d);if (!d) { app_error_set(why,1,"out of memory");return NULL; }
    d->fd=-1;d->scale=1;d->want_scale=scale;d->color=color;d->last_bg=-1;
    term=env("TERM");d->bright_bg=strcmp(term,"linux")!=0;d->raw=*env("CHARTS_KITTY_RAW")!=0;
    if (!want) want="auto";
    override=env("CHARTS_GFX");if (!strcmp(want,"auto") && *override && app_gfx_valid(override)) want=override;
    app_error_set(why,0,"cells: asked for");
    if (!strcmp(want,"ascii")) { d->ascii=1;app_error_set(why,0,"ascii: asked for");return d; }
    if (!strcmp(want,"cells")) return d;
    if (!color) { app_error_set(why,0,"cells: colour is off, and pixels are nothing but colour");return d; }
#ifdef APP_DOS
    if (!strcmp(want,"auto") || !strcmp(want,"vga")) { vga_open();d->kind=4;d->xres=640;d->yres=480;app_error_set(why,0,"vga: 640x480 planar 16-colour framebuffer");return d; }
#else
    if (!strcmp(want,"vga")) { app_error_set(why,0,"cells: VGA is available in the DOS build");return d; }
#endif
    if (!strcmp(want,"fb") || (!strcmp(want,"auto") && (!strcmp(term,"linux") || *env("CHARTS_FB")))) {
#if defined(__linux__) && !defined(APP_DOS)
        if (fb_open(d,why)) { d->kind=1;app_error_set(why,0,"fb: the console framebuffer");return d; }
        if (d->fd>=0) close(d->fd);
        d->fd=-1;
#else
        app_error_set(why,0,"cells: framebuffer backend requires Linux");
#endif
        return d;
    }
    if (!strcmp(want,"kitty")) { d->kind=2;app_error_set(why,0,"kitty: asked for");return d; }
    if (!strcmp(want,"sixel")) { d->kind=3;app_error_set(why,0,"sixel: asked for");return d; }
    if (*env("TMUX") || !strncmp(term,"screen",6) || !strncmp(term,"tmux",4)) { app_error_set(why,0,"cells: inside tmux/screen, which does not pass graphics through");return d; }
    term_pixels(d);if (!d->cw || !d->ch) { app_error_set(why,0,"cells: the terminal does not report its pixel size");return d; }
    query(reply);
    if (strstr(reply,"_Gi=31;OK")) { d->kind=2;app_error_set(why,0,"kitty: the terminal answered the graphics query");return d; }
    p=strstr(reply,"\033[?");
    if (p) {
        char attrs[520];
        strcpy(attrs,";");strcat(attrs,p+3);for (p=attrs;*p;p++) if (*p=='c') *p=';';
        if (strstr(attrs,";4;")) { d->kind=3;app_error_set(why,0,"sixel: the terminal lists sixel in its device attributes");return d; }
    }
    app_error_set(why,0,"cells: the terminal offers neither kitty graphics nor sixel");return d;
}
const char *app_display_name(const app_display *d)
{ return d->kind==1 ? "fb" : (d->kind==2 ? "kitty" : (d->kind==3 ? "sixel" : (d->kind==4 ? "vga" : (d->ascii ? "ascii" : "cells")))); }
lc_mode app_display_mode(const app_display *d)
{ lc_mode m;m.pixel=d->kind!=0;m.ascii=d->ascii;m.color=d->color;return m; }
void app_display_grid(app_display *d,int *cols,int *rows)
{
    int pw,ph;
    if (!d->kind) { app_term_size(cols,rows);return; }
    if (d->kind==1 || d->kind==4) { *cols=d->xres/(8*d->scale);*rows=d->yres/(16*d->scale);return; }
    term_pixels(d);if (!d->cw || !d->ch) { *cols=100;*rows=30;d->scale=1;return; }
    pw=d->cols*d->cw;ph=(d->rows-(d->kind==3))*d->ch;
    d->scale=d->want_scale>0 ? d->want_scale : MAXIMUM(1,(pw+480)/960);
    while (d->scale>1 && (pw/(8*d->scale)<60 || ph/(16*d->scale)<16)) d->scale--;
    *cols=MAXIMUM(20,pw/(8*d->scale));*rows=MAXIMUM(6,ph/(16*d->scale));
}

typedef struct stream_buffer { unsigned char data[4096];size_t count; } stream_buffer;
static int buffered_write(void *user,const unsigned char *bytes,size_t n)
{
    stream_buffer *b;
    size_t take;
    b=(stream_buffer *)user;
    while (n) {
        take=sizeof b->data-b->count;if (take>n) take=n;
        memcpy(b->data+b->count,bytes,take);b->count+=take;bytes+=take;n-=take;
        if (b->count==sizeof b->data) {
            if (stdout_sink(NULL,b->data,b->count-2)) return -1;
            memmove(b->data,b->data+b->count-2,2);b->count=2;
        }
    }
    return 0;
}
static lc_status fb_show(app_display *d,const lc_image *im,int border)
{
#if defined(__linux__) && !defined(APP_DOS)
    int x,y,sx,sy,ox,oy,in_y,k,c,j,little;
    unsigned short endian_probe;
    unsigned long value;
    off_t base;
    size_t done,row_bytes;
    if (!vt_active) return LC_OK;
    row_bytes=(size_t)d->xres*d->bytes;
    endian_probe=1;little=*(const unsigned char *)&endian_probe;
    if (row_bytes>d->line_size) {
        unsigned char *p;
        p=(unsigned char *)realloc(d->line,row_bytes);if (!p) return LC_ENOMEM;
        d->line=p;d->line_size=row_bytes;
    }
    memset(d->line,0,row_bytes);k=d->scale;ox=MAXIMUM(0,(d->xres-im->width*k)/2);oy=MAXIMUM(0,(d->yres-im->height*k)/2);
    base=(off_t)d->yoff*d->stride+(off_t)d->xoff*d->bytes;
    for (y=0;y<d->yres;y++) {
        sy=(y-oy)/k;in_y=y>=oy && sy<im->height;
        if (!(in_y && y>oy && (y-oy)%k)) {
            for (x=0;x<d->xres;x++) {
                sx=(x-ox)/k;c=in_y && x>=ox && sx<im->width ? im->pixels[(size_t)sy*im->stride+sx] : border;
                value=d->lut[c&15];
                /* Native fbdev bitfields, including native byte order. */
                if (d->bytes==2) { unsigned short v;v=(unsigned short)value;memcpy(d->line+(size_t)x*2,&v,2); }
                else if (d->bytes==4) {
                    unsigned int v;
                    v=(unsigned int)value;
                    if (sizeof v==4) memcpy(d->line+(size_t)x*4,&v,4);
                    else for (j=0;j<4;j++) d->line[(size_t)x*4+j]=(unsigned char)(value>>(8*j));
                } else for (j=0;j<3;j++) d->line[(size_t)x*3+j]=(unsigned char)(value>>(8*(little ? j : 2-j)));
            }
        }
        if (lseek(d->fd,base+(off_t)y*d->stride,SEEK_SET)==(off_t)-1) return LC_EIO;
        done=0;
        while (done<row_bytes) {
            ssize_t n;
            n=write(d->fd,d->line+done,row_bytes-done);
            if (n<0 && errno==EINTR) continue;
            if (n<=0) return LC_EIO;
            done+=(size_t)n;
        }
    }
    return LC_OK;
#else
    (void)d;(void)im;(void)border;return LC_EINVAL;
#endif
}
/* The four VGA bit planes of a row of 4-bit colours, eight pixels a byte,
   leftmost in the high bit: one pass, with each pixel's four bits spread to
   one per byte of a long. Testing every pixel against every plane took
   eleven seconds a frame on a 386SX-25. */
void app_vga_planes(const unsigned char *line,int width,unsigned char planes[4][80])
{
    static const unsigned long spread[16]={
        0x00000000UL,0x00000001UL,0x00000100UL,0x00000101UL,0x00010000UL,0x00010001UL,0x00010100UL,0x00010101UL,
        0x01000000UL,0x01000001UL,0x01000100UL,0x01000101UL,0x01010000UL,0x01010001UL,0x01010100UL,0x01010101UL};
    unsigned long acc;
    int b,i;
    for (b=0;b<width/8 && b<80;b++) {
        acc=0;
        for (i=0;i<8;i++) acc=(acc<<1)|spread[line[b*8+i]&15];
        planes[0][b]=(unsigned char)(acc&255);planes[1][b]=(unsigned char)((acc>>8)&255);
        planes[2][b]=(unsigned char)((acc>>16)&255);planes[3][b]=(unsigned char)((acc>>24)&255);
    }
}
static lc_status vga_show(app_display *d,const lc_image *im,int border)
{
#ifdef APP_DOS
    unsigned char line[640],planes[4][80];
    unsigned char *video;
    int plane,y,sy,ox,oy;
    video=(unsigned char *)0xa0000UL;ox=MAXIMUM(0,(640-im->width)/2);oy=MAXIMUM(0,(480-im->height)/2);
    outpw(0x3ce,0x0005);outpw(0x3ce,0xff08);outpw(0x3ce,0x0001);outpw(0x3ce,0x0003);
    for (y=0;y<480;y++) {
        memset(line,border,sizeof line);sy=y-oy;
        if (sy>=0 && sy<im->height) memcpy(line+ox,im->pixels+(size_t)sy*im->stride,(size_t)MINIMUM(im->width,640-ox));
        app_vga_planes(line,640,planes);
        for (plane=0;plane<4;plane++) { outpw(0x3c4,(1<<(plane+8))|2);memcpy(video+(size_t)y*80,planes[plane],80); }
    }
    outpw(0x3c4,0x0f02);(void)d;return LC_OK;
#else
    (void)d;(void)im;(void)border;return LC_EINVAL;
#endif
}
lc_status app_display_show(app_display *d,lc_scene *scene)
{
    lc_context *ctx;
    lc_status status;
    lc_sink sink;
    stream_buffer buffer;
    lc_cell *cells;
    lc_image im,scaled;
    int bg,col,row,id;
    char command[128];
    ctx=lc_scene_context(scene);sink.user=&buffer;sink.write=buffered_write;buffer.count=0;
    if (!d->kind) {
        status=lc_scene_to_cells(scene,&cells);if (status!=LC_OK) return status;
        say("\033[?2026h\033[H");
        status=lc_write_cells(cells,lc_scene_cols(scene),lc_scene_rows(scene),(size_t)lc_scene_cols(scene),d->color,1,d->bright_bg,&sink);
        lc_free(ctx,cells);
        if (buffer.count>=2 && buffer.data[buffer.count-2]=='\r' && buffer.data[buffer.count-1]=='\n') buffer.count-=2;
        if (stdout_sink(NULL,buffer.data,buffer.count)) status=LC_EIO;
        say("\033[?2026l");return status;
    }
    status=lc_scene_to_image(scene,&im);if (status!=LC_OK) return status;
    bg=lc_scene_skin(scene)->slide_bg;bg=bg==LC_BG_NONE ? 0 : bg;
    if (d->kind==1 || d->kind==4) {
        status=d->kind==1 ? fb_show(d,&im,bg) : vga_show(d,&im,bg);lc_free(ctx,im.pixels);return status;
    }
    if (d->scale>1) {
        status=lc_image_scale(ctx,&im,d->scale,&scaled);lc_free(ctx,im.pixels);if (status!=LC_OK) return status;im=scaled;
    }
    if (d->kind==3) say("\033[?2026h");
    if (im.width!=d->last_w || im.height!=d->last_h || bg!=d->last_bg) {
        sprintf(command,"\033[48;2;%d;%d;%dm\033[2J\033[0m",lc_vga_rgb[bg&15][0],lc_vga_rgb[bg&15][1],lc_vga_rgb[bg&15][2]);say(command);
        d->last_w=im.width;d->last_h=im.height;d->last_bg=bg;
    }
    col=MAXIMUM(0,(d->cols*d->cw-im.width)/2/MAXIMUM(1,d->cw));
    row=MAXIMUM(0,((d->rows-(d->kind==3))*d->ch-im.height)/2/MAXIMUM(1,d->ch));
    sprintf(command,"\033[%d;%dH",row+1,col+1);say(command);
    if (d->kind==2) { id=1+(d->frame++&1);status=lc_write_kitty_ex(ctx,&im,&sink,id,d->raw); }
    else status=lc_write_sixel(ctx,&im,&sink);
    if (stdout_sink(NULL,buffer.data,buffer.count)) status=LC_EIO;
    if (d->kind==2) { sprintf(command,"\033_Ga=d,d=I,i=%u,q=2\033\\",1+(d->frame&1));say(command); }
    else say("\033[?2026l");
    lc_free(ctx,im.pixels);return status;
}
int app_display_needs_redraw(app_display *d)
{
#if defined(__linux__) && !defined(APP_DOS)
    if (d->kind==1 && vt_redraw) { vt_redraw=0;return 1; }
#else
    (void)d;
#endif
    return 0;
}
void app_display_invalidate(app_display *d) { d->last_w=d->last_h=0;d->last_bg=-1; }
void app_display_close(app_display *d)
{
    if (!d) return;
    if (d->fd>=0) close(d->fd);
#if defined(__linux__) && !defined(APP_DOS)
    if (d->took_vt) { cleanup_hook=NULL;resume_hook=NULL;vt_restore();vt_fd=-1; }
#endif
#ifdef APP_DOS
    if (d->kind==4) { vga_restore();cleanup_hook=NULL; }
#endif
    if (d->kind==2) say("\033_Ga=d,d=A,q=2\033\\");
    if (d->kind==2 || d->kind==3) say("\033[0m\033[2J\033[H");
    free(d->line);free(d);
}
int app_path_is_dir(const char *path)
{
    app_stat_info st;
    return app_stat(path,&st)==0 && APP_ISDIR(st.st_mode);
}
int app_mkdir(const char *path,app_error *err)
{
    int rc;
    if (app_path_is_dir(path)) return 1;
#ifdef APP_DOS
    rc=mkdir(path);
#else
    rc=mkdir(path,0777);
#endif
    if (!rc) return 1;
    app_error_path(err,1,path,strerror(errno));return 0;
}
static int path_compare(const void *a,const void *b)
{ return strcmp(*(const char *const *)a,*(const char *const *)b); }
static int add_path(char ***paths,size_t *count,size_t *cap,const char *dir,const char *name,app_error *err)
{
    char *p;
    size_t a,b;
    if (!strcmp(name,".") || !strcmp(name,"..")) return 1;
    a=strlen(dir);b=strlen(name);if (a>(size_t)-1-b-2) { app_error_set(err,1,"path too large");return 0; }
    p=(char *)malloc(a+b+2);if (!p) { app_error_set(err,1,"out of memory");return 0; }
    memcpy(p,dir,a);if (a && dir[a-1]!='/' && dir[a-1]!='\\') p[a++]='/';memcpy(p+a,name,b+1);
    if (!app_reserve((void **)paths,cap,*count+1,sizeof **paths,err)) { free(p);return 0; }
    (*paths)[(*count)++]=p;return 1;
}
int app_paths_in_dir(const char *path,char ***paths,size_t *count,app_error *err)
{
    size_t cap;
    int ok;
    *paths=NULL;*count=0;cap=0;ok=1;
#ifndef APP_DOS
    {
        DIR *d;
        struct dirent *e;
        d=opendir(path);if (!d) { app_error_path(err,1,path,strerror(errno));return 0; }
        while ((e=readdir(d))!=NULL) if (!add_path(paths,count,&cap,path,e->d_name,err)) { ok=0;break; }
        closedir(d);
    }
#else
    {
        struct find_t found;
        char *pattern;
        unsigned result;
        pattern=(char *)malloc(strlen(path)+5);if (!pattern) { app_error_set(err,1,"out of memory");return 0; }
        strcpy(pattern,path);strcat(pattern,"/*.*");result=_dos_findfirst(pattern,_A_NORMAL|_A_RDONLY|_A_HIDDEN|_A_SYSTEM|_A_SUBDIR,&found);
        free(pattern);
        while (!result) {
            if (!add_path(paths,count,&cap,path,found.name,err)) { ok=0;break; }
            result=_dos_findnext(&found);
        }
    }
#endif
    if (!ok) { app_paths_free(*paths,*count);*paths=NULL;*count=0;return 0; }
    if (*count>1) qsort(*paths,*count,sizeof **paths,path_compare);
    return 1;
}
void app_paths_free(char **paths,size_t count)
{ size_t i;for (i=0;i<count;i++) free(paths[i]);free(paths); }
app_stamp app_file_stamp(const char *path)
{
    app_stamp stamp;
    app_stat_info st;
    memset(&stamp,0,sizeof stamp);
    if (!path || !*path || !strcmp(path,"-") || app_stat(path,&st)) return stamp;
    stamp.exists=1;stamp.seconds=st.st_mtime;stamp.size=(unsigned long)st.st_size;
#if defined(CHARTS_STAT_NSEC)
    stamp.nanoseconds=CHARTS_STAT_NSEC(st);
#elif defined(__linux__) && defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 12))
    stamp.nanoseconds=st.st_mtim.tv_nsec;
#elif defined(__FreeBSD__) && __FreeBSD__ >= 12
    stamp.nanoseconds=st.st_mtim.tv_nsec;
#endif
    return stamp;
}
#ifndef APP_DOS
static void shell_quote(FILE *f,const char *s)
{
    fputc('\'',f);for (;*s;s++) { if (*s=='\'') fputs("'\\''",f);else fputc(*s,f); }fputc('\'',f);
}
#endif
int app_launch_file(const char *output,const char *deck_path,const char *executable,char **created_path,app_error *err)
{
    FILE *f;
    char *path,*deck,*self;
    size_t n;
    char cwd[4096];
    if (created_path) *created_path=NULL;
    path=app_strdup(output,err);deck=self=NULL;if (!path) return 0;
#ifndef APP_DOS
    if (!strchr(output,'/')) {
        const char *home;
        char *dir;
        home=env("HOME");if (!*home) home=".";
        n=strlen(home)+strlen(output)+16;dir=(char *)malloc(n);if (!dir) goto memory;
        sprintf(dir,"%s/.local",home);mkdir(dir,0755);strcat(dir,"/bin");mkdir(dir,0755);
        strcat(dir,"/");strcat(dir,output);free(path);path=dir;
    }
#endif
    if (deck_path[0]!='/'
#ifdef APP_DOS
        && deck_path[0]!='\\' && !(deck_path[0] && deck_path[1]==':')
#endif
        && getcwd(cwd,sizeof cwd)) {
        n=strlen(cwd)+strlen(deck_path)+2;deck=(char *)malloc(n);if (!deck) goto memory;sprintf(deck,"%s/%s",cwd,deck_path);
    } else deck=app_strdup(deck_path,err);
    self=app_strdup(executable,err);if (!deck || !self) goto memory;
#ifdef __linux__
    {
        ssize_t got;
        got=readlink("/proc/self/exe",cwd,sizeof cwd-1);
        if (got>0) { cwd[got]=0;free(self);self=app_strdup(cwd,err);if (!self) goto memory; }
    }
#endif
    f=fopen(path,"wb");if (!f) { app_error_path(err,1,path,strerror(errno));goto fail; }
#ifdef APP_DOS
    fprintf(f,"@echo off\r\n\"%s\" \"%s\" %%1 %%2 %%3 %%4 %%5 %%6 %%7 %%8 %%9\r\n",self,deck);
#else
    fputs("#!/bin/sh\n# Presents the deck. Left/right to page, ? for keys, q to quit.\nDECK=",f);shell_quote(f,deck);
    fputs("\nCHARTS=",f);shell_quote(f,self);
    fputs("\n[ -x \"$CHARTS\" ] || CHARTS=$(command -v charts) || { echo 'charts is not installed' >&2; exit 127; }\n",f);
    fputs("[ -r \"$DECK\" ] || { echo \"deck not found: $DECK\" >&2; exit 1; }\nif [ -t 0 ] && [ -t 1 ]; then\n  exec \"$CHARTS\" \"$DECK\" \"$@\"\nfi\n",f);
    fputs("if [ -n \"$WAYLAND_DISPLAY$DISPLAY\" ]; then\n  for T in kitty ghostty foot wezterm alacritty xterm; do\n    command -v $T >/dev/null 2>&1 || continue\n    case $T in\n",f);
    fputs("      wezterm) exec wezterm start -- \"$CHARTS\" \"$DECK\" \"$@\" ;;\n      *) exec $T -e \"$CHARTS\" \"$DECK\" \"$@\" ;;\n    esac\n  done\nfi\n",f);
    fputs("echo 'run this from a terminal or a console' >&2\nexit 1\n",f);
#endif
    if (ferror(f)) { fclose(f);app_error_path(err,1,path,"write failed");goto fail; }
    if (fclose(f)) { app_error_path(err,1,path,strerror(errno));goto fail; }
#ifndef APP_DOS
    if (chmod(path,0755)) { app_error_path(err,1,path,strerror(errno));goto fail; }
#endif
    if (created_path) *created_path=path;else free(path);
    free(deck);free(self);return 1;
memory:
    app_error_set(err,1,"out of memory");
fail:
    free(path);free(deck);free(self);return 0;
}

/* Keep temporary files beside their target, including on plain 8.3 FAT. */
static int temporary_file(const char *target,char **name,app_error *err)
{
    static unsigned long serial;
    const char *slash;
    size_t dir_len;
    unsigned tries;
    int fd,flags;
    char *path;
    slash=strrchr(target,'/');
#ifdef APP_DOS
    {
        const char *back;
        back=strrchr(target,'\\');if (!slash || (back && back>slash)) slash=back;
    }
#endif
    dir_len=slash ? (size_t)(slash-target)+1 : 0;
#ifdef APP_DOS
    if (!dir_len && target[0] && target[1]==':') dir_len=2;
#endif
    if (dir_len>(size_t)-1-13) { app_error_set(err,1,"path too large");return -1; }
    path=(char *)malloc(dir_len+13);if (!path) { app_error_set(err,1,"out of memory");return -1; }
    memcpy(path,target,dir_len);flags=O_WRONLY|O_CREAT|O_EXCL;
#ifdef APP_DOS
    flags|=O_BINARY;
#endif
    for (tries=0;tries<1024;tries++) {
        serial=(serial+1)%1000000UL;sprintf(path+dir_len,"CH%06lu.TMP",serial);
#ifdef APP_DOS
        if (app_streq_ci(path,target)) continue;
#else
        if (!strcmp(path,target)) continue;
#endif
        fd=open(path,flags,0600);
        if (fd<0) { if (errno==EEXIST) continue;app_error_path(err,1,path,strerror(errno));free(path);return -1; }
        *name=path;return fd;
    }
    app_error_set(err,1,"cannot allocate a unique temporary filename");free(path);return -1;
}
int app_write_replace(const char *path,const char *data,size_t count,app_error *err)
{
    char *temporary;
    int failed,fd,written;
    size_t take;
#ifndef APP_DOS
    app_stat_info st;
#else
    char *backup;
    int reserve;
    app_stat_info st;
#endif
    temporary=NULL;fd=temporary_file(path,&temporary,err);if (fd<0) return 0;
    failed=0;
    while (count) {
        take=count>16384U ? 16384U : count;
        written=(int)write(fd,data,(unsigned)take);
        if (written<0 && errno==EINTR) continue;
        if (written<=0) { failed=1;break; }
        data+=written;count-=(size_t)written;
    }
    if (close(fd)) failed=1;
    if (failed) { app_error_path(err,1,path,"write failed");remove(temporary);free(temporary);return 0; }
#ifndef APP_DOS
    if (app_stat(path,&st)==0) chmod(temporary,st.st_mode&0777);
    if (rename(temporary,path)) { app_error_path(err,1,path,strerror(errno));remove(temporary);free(temporary);return 0; }
#else
    backup=NULL;
    if (app_stat(path,&st)==0) {
        reserve=temporary_file(path,&backup,err);
        if (reserve<0) { remove(temporary);free(temporary);return 0; }
        if (close(reserve) || remove(backup) || rename(path,backup)) {
            app_error_path(err,1,path,"cannot preserve original before replacement");remove(backup);remove(temporary);free(backup);free(temporary);return 0;
        }
    }
    if (rename(temporary,path)) {
        failed=backup && rename(backup,path);
        if (failed) app_error_path(err,1,"save failed; original retained at",backup);
        else app_error_path(err,1,path,"cannot replace file; original restored");
        remove(temporary);free(backup);free(temporary);return 0;
    }
    if (backup) { remove(backup);free(backup); }
#endif
    free(temporary);return 1;
}
