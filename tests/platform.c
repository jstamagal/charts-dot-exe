/* Linux adapter regression: panned framebuffer rows and hostile fake geometry. */
#define _POSIX_C_SOURCE 200809L
#include "../app/platform.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
int ioctl(int fd,unsigned long request,...)
{
    va_list args;
    void *p;
    (void)fd;
    va_start(args,request);p=va_arg(args,void *);va_end(args);
    if (request==FBIOGET_VSCREENINFO) {
        struct fb_var_screeninfo *v;
        v=(struct fb_var_screeninfo *)p;memset(v,0,sizeof *v);
        v->xres=v->yres=64;v->xres_virtual=v->yres_virtual=80;
        v->xoffset=v->yoffset=8;v->bits_per_pixel=32;
        v->red.offset=16;v->green.offset=8;v->red.length=v->green.length=v->blue.length=8;return 0;
    }
    if (request==FBIOGET_FSCREENINFO) {
        struct fb_fix_screeninfo *f;
        f=(struct fb_fix_screeninfo *)p;memset(f,0,sizeof *f);f->line_length=80*4;f->smem_len=80*80*4;return 0;
    }
    errno=ENOTTY;return -1;
}
int app_file_sink(void *user,const unsigned char *p,size_t n)
{ return fwrite(p,1,n,(FILE *)user)==n ? 0 : -1; }
int main(void)
{
    char path[]="/tmp/libcharts-fb-XXXXXX";
    int fd,x,y,k;
    unsigned char bytes[80*80*4],expected[4];
    unsigned int pixel;
    FILE *f;
    app_error error;
    app_display *display;
    lc_context *ctx;
    lc_scene *scene;
    lc_mode mode;
    fd=mkstemp(path);assert(fd>=0);close(fd);
    memset(bytes,0xa5,sizeof bytes);f=fopen(path,"wb");assert(f);assert(fwrite(bytes,1,sizeof bytes,f)==sizeof bytes);assert(!fclose(f));
    assert(!setenv("CHARTS_FB",path,1));unsetenv("CHARTS_FB_GEOM");
    display=app_display_open("fb",1,1,&error);assert(display);assert(!strcmp(app_display_name(display),"fb"));
    assert(lc_context_create(NULL,&ctx)==LC_OK);mode=app_display_mode(display);
    assert(lc_scene_create(ctx,8,4,mode,&scene)==LC_OK);
    assert(app_display_show(display,scene)==LC_OK);app_display_close(display);
    f=fopen(path,"rb");assert(f);assert(fread(bytes,1,sizeof bytes,f)==sizeof bytes);fclose(f);
    pixel=170;memcpy(expected,&pixel,4);
    for(y=0;y<80;y++)for(x=0;x<80;x++)for(k=0;k<4;k++)
        assert(bytes[(y*80+x)*4+k]==(x>=8 && x<72 && y>=8 && y<72 ? expected[k] : 0xa5));
    assert(!setenv("CHARTS_FB_GEOM","640x480x2147483647",1));
    display=app_display_open("fb",1,1,&error);assert(display);assert(!strcmp(app_display_name(display),"cells"));app_display_close(display);
    lc_scene_destroy(scene);lc_context_destroy(ctx);unlink(path);
    /* VGA planes against the obvious bit-at-a-time packing */
    for(k=0;k<200;k++) {
        unsigned char line[640],planes[4][80],want[4][80];int plane;
        for(x=0;x<640;x++)line[x]=(unsigned char)(k==0 ? x&15 : k==1 ? (x&1)*15 : rand()&15);
        memset(want,0,sizeof want);
        for(plane=0;plane<4;plane++)for(x=0;x<640;x++)if(line[x]&(1<<plane))want[plane][x>>3]|=(unsigned char)(0x80>>(x&7));
        app_vga_planes(line,640,planes);assert(!memcmp(planes,want,sizeof want));
    }
    puts("platform: panned framebuffer padding, invalid bpp and VGA planes passed");return 0;
}
