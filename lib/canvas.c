#include "internal.h"

static int inside(const lc_canvas *c,int x,int y) { return c && x>=0 && y>=0 && x<c->w && y<c->h; }
void lc_canvas_clear(lc_canvas *c,int fg,int bg) {
    size_t i,n; if (!c) return; n=(size_t)c->w*c->h;
    for (i=0;i<n;i++) { c->cells[i].ch=' '; c->cells[i].fg=(unsigned char)fg; c->cells[i].bg=(unsigned char)bg; }
}
void lc_canvas_put(lc_canvas *c,int x,int y,lc_codepoint cp,int fg,int bg) {
    lc_cell *cl; if (!inside(c,x,y) || !cp) return;
    cp=lc_cell_of(cp); if (!cp) cp=' ';
    cl=&c->cells[(size_t)y*c->w+x]; cl->ch=cp; cl->fg=(unsigned char)(fg&15);
    if (bg>=0) cl->bg=(unsigned char)bg;
}
void lc_canvas_fill_bg(lc_canvas *c,int x,int y,int w,int h,int bg) {
    int i,j,x0,y0,x1,y1;
    if (!c || w<=0 || h<=0) return;
    x0=LC_MAX(x,0); y0=LC_MAX(y,0); x1=lc_clamp_int((double)x+w,0,c->w); y1=lc_clamp_int((double)y+h,0,c->h);
    for (j=y0;j<y1;j++) for (i=x0;i<x1;i++) lc_canvas_put(c,i,j,' ',7,bg);
}
void lc_canvas_text(lc_canvas *c,int x,int y,const char *text,int fg,int bg) {
    lc_codepoint cp; if (!c || !text || y<0 || y>=c->h) return;
    while (*text && x<c->w) { cp=lc_utf8_next(&text); if (!lc_cell_of(cp)) continue; if (x>=0) lc_canvas_put(c,x,y,cp,fg,bg); x++; }
}
static void align_text(lc_canvas *c,int x,int y,int w,const char *text,int fg,int right) {
    char *t; size_t len; int pad;
    if (!c || w<=0) return;
    t=lc_text_truncate(c->ctx,text,(size_t)w); if (!t) return;
    len=lc_text_width(t); pad=w-(int)len; if (!right) pad/=2;
    if ((double)x+LC_MAX(0,pad)<=INT_MAX) lc_canvas_text(c,x+LC_MAX(0,pad),y,t,fg,-1);
}
void lc_canvas_text_c(lc_canvas *c,int x,int y,int w,const char *text,int fg) { align_text(c,x,y,w,text,fg,0); }
void lc_canvas_text_r(lc_canvas *c,int x,int y,int w,const char *text,int fg) { align_text(c,x,y,w,text,fg,1); }
void lc_canvas_fill(lc_canvas *c,int x,int y,int w,int h,lc_codepoint cp,int fg) {
    int i,j,x0,y0,x1,y1;
    if (!c || w<=0 || h<=0) return;
    x0=LC_MAX(x,0); y0=LC_MAX(y,0); x1=lc_clamp_int((double)x+w,0,c->w); y1=lc_clamp_int((double)y+h,0,c->h);
    for (j=y0;j<y1;j++) for (i=x0;i<x1;i++) lc_canvas_put(c,i,j,cp,fg,-1);
}
void lc_canvas_hline(lc_canvas *c,int x,int y,int len,lc_codepoint cp,int fg) { lc_canvas_fill(c,x,y,len,1,cp,fg); }
void lc_canvas_vline(lc_canvas *c,int x,int y,int len,lc_codepoint cp,int fg) { lc_canvas_fill(c,x,y,1,len,cp,fg); }
void lc_canvas_box(lc_canvas *c,int x,int y,int w,int h,int style,int fg) {
    lc_codepoint hh,v,tl,tr,bl,br; int right,bottom;
    if (!c || style==LC_BOX_NONE || w<2 || h<2) return;
    if ((double)x+w-1>INT_MAX || (double)y+h-1>INT_MAX) return;
    right=x+w-1; bottom=y+h-1;
    switch (style) {
    case LC_BOX_DOUBLE: hh=0x2550; v=0x2551; tl=0x2554; tr=0x2557; bl=0x255a; br=0x255d; break;
    case LC_BOX_HEAVY: hh=0x2501; v=0x2503; tl=0x250f; tr=0x2513; bl=0x2517; br=0x251b; break;
    case LC_BOX_ASCII: hh='-'; v='|'; tl=tr=bl=br='+'; break;
    default: hh=0x2500; v=0x2502; tl=0x250c; tr=0x2510; bl=0x2514; br=0x2518; break;
    }
    lc_canvas_hline(c,x+1,y,w-2,hh,fg); lc_canvas_hline(c,x+1,bottom,w-2,hh,fg);
    lc_canvas_vline(c,x,y+1,h-2,v,fg); lc_canvas_vline(c,right,y+1,h-2,v,fg);
    lc_canvas_put(c,x,y,tl,fg,-1); lc_canvas_put(c,right,y,tr,fg,-1); lc_canvas_put(c,x,bottom,bl,fg,-1); lc_canvas_put(c,right,bottom,br,fg,-1);
}
static void dark(lc_canvas *c,int x,int y) {
    lc_cell *cl; if (!inside(c,x,y)) return; cl=&c->cells[(size_t)y*c->w+x]; cl->fg=8; cl->bg=0;
}
void lc_canvas_shadow(lc_canvas *c,int x,int y,int w,int h) {
    int i,j,right,bottom;
    if (!c || w<=0 || h<=0 || (double)x+w+1>INT_MAX || (double)y+h>INT_MAX) return;
    right=x+w; bottom=y+h;
    for (j=LC_MAX(y+1,0);j<=LC_MIN(bottom,c->h-1);j++) { dark(c,right,j); dark(c,right+1,j); }
    for (i=LC_MAX(x+2,0);i<LC_MIN(right,c->w);i++) dark(c,i,bottom);
}
void lc_canvas_vtext(lc_canvas *c,int x,int y,const char *text,int fg) {
    lc_codepoint cp; if (!c || !text) return;
    while (*text && y<c->h) { cp=lc_utf8_next(&text); if (lc_cell_of(cp)) lc_canvas_put(c,x,y++,cp,fg,-1); }
}
