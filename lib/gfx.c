#include "internal.h"

const unsigned char lc_vga_rgb[16][3]={
 {0,0,0},{170,0,0},{0,170,0},{170,85,0},{0,0,170},{170,0,170},{0,170,170},{170,170,170},
 {85,85,85},{255,85,85},{85,255,85},{255,255,85},{85,85,255},{255,85,255},{85,255,255},{255,255,255}
};
lc_ink lc_ink_make(int a,int b,int level) { lc_ink k; k.a=(unsigned char)a; k.b=(unsigned char)b; k.level=(unsigned char)level; return k; }
lc_ink lc_ink_solid(int c) { return lc_ink_make(c,0,0); }
int lc_dim_of(int c) { return c==8 ? 0 : (c==7 ? 8 : (c>8 ? c-8 : c)); }
int lc_bright_of(int c) { return c==0 ? 8 : (c<8 ? c+8 : c); }
int lc_contrast_on(int c) { const unsigned char *p=lc_vga_rgb[c&15]; return (p[0]*3+p[1]*6+p[2])/10>110 ? 0 : 15; }
lc_ink lc_side_ink(int c) { if (c==15) return lc_ink_solid(7); if (c>=9) return lc_ink_solid(c-8); return lc_ink_make(c,0,2); }
lc_ink lc_top_ink(int c) { if (c==15) return lc_ink_make(15,7,1); if (c>=9) return lc_ink_make(c,15,2); if (c==0 || c==8) return lc_ink_make(7,c,2); return lc_ink_solid(c+8); }
int lc_clip_segment(lc_pt *a,lc_pt *b,double x0,double y0,double x1,double y1) {
    double dx,dy,p[4],q[4],t0,t1,t; lc_pt start; int i;
    if (!a || !b) return 0;
    dx=b->x-a->x; dy=b->y-a->y;
    if (!lc_finite(dx)||!lc_finite(dy)||!lc_finite(a->x)||!lc_finite(a->y)) return 0;
    p[0]=-dx; p[1]=dx; p[2]=-dy; p[3]=dy;
    q[0]=a->x-x0; q[1]=x1-a->x; q[2]=a->y-y0; q[3]=y1-a->y; t0=0; t1=1;
    for (i=0;i<4;i++) {
        if (p[i]==0) { if (q[i]<0) return 0; continue; }
        t=q[i]/p[i];
        if (p[i]<0) { if (t>t1) return 0; t0=LC_MAX(t0,t); }
        else { if (t<t0) return 0; t1=LC_MIN(t1,t); }
    }
    start.x=a->x+t0*dx; start.y=a->y+t0*dy;
    b->x=a->x+t1*dx; b->y=a->y+t1*dy; *a=start; return 1;
}
int lc_surface_width(const lc_surface *s) { return s ? s->w : 0; }
int lc_surface_height(const lc_surface *s) { return s ? s->h : 0; }
int lc_surface_sx(const lc_surface *s) { return s ? s->sx : 1; }
int lc_surface_sy(const lc_surface *s) { return s ? s->sy : 1; }
int lc_surface_fine(const lc_surface *s) { return s && s->sx>=4; }
double lc_surface_aspect(const lc_surface *s) { return s ? 2.0*s->sx/s->sy : 1; }
static int inside(const lc_surface *s,int x,int y) { return s && x>=0 && y>=0 && x<s->w && y<s->h; }
int lc_surface_touched(const lc_surface *s,int x,int y) { return inside(s,x,y) && s->pixels[(size_t)y*s->w+x].level!=LC_EMPTY; }
lc_ink lc_surface_at(const lc_surface *s,int x,int y) { return inside(s,x,y) ? s->pixels[(size_t)y*s->w+x] : lc_ink_make(0,0,LC_EMPTY); }
int lc_surface_resolve(const lc_surface *s,int x,int y) {
    static const unsigned char rank[2][2]={{0,2},{3,1}}; lc_ink k=lc_surface_at(s,x,y);
    return k.level==0 || k.level==LC_EMPTY ? k.a : (rank[y&1][x&1]<k.level ? k.b : k.a);
}
void lc_surface_set(lc_surface *s,int x,int y,lc_ink k) { if (inside(s,x,y)) s->pixels[(size_t)y*s->w+x]=k; }
void lc_surface_erase(lc_surface *s,int x0,int y0,int x1,int y1) {
    int x,y;
    if (!s) return;
    x0=LC_MAX(0,x0); y0=LC_MAX(0,y0); x1=LC_MIN(s->w,x1); y1=LC_MIN(s->h,y1);
    for (y=y0;y<y1;y++) for (x=x0;x<x1;x++) s->pixels[(size_t)y*s->w+x]=lc_ink_make(0,0,LC_EMPTY);
}
void lc_surface_rect(lc_surface *s,double x0,double y0,double x1,double y1,lc_ink k) {
    int ax,ay,bx,by,x,y; double t;
    if (!s) return;
    if (x1<x0) { t=x0; x0=x1; x1=t; } if (y1<y0) { t=y0; y0=y1; y1=t; }
    ax=lc_clamp_int(lc_round(x0),0,s->w); ay=lc_clamp_int(lc_round(y0),0,s->h);
    bx=lc_clamp_int(lc_round(x1),0,s->w); by=lc_clamp_int(lc_round(y1),0,s->h);
    for (y=ay;y<by;y++) for (x=ax;x<bx;x++) s->pixels[(size_t)y*s->w+x]=k;
}
void lc_surface_hline(lc_surface *s,int x0,int x1,int y,lc_ink k) {
    int t,x; if (!s || y<0 || y>=s->h) return;
    if (x1<x0) { t=x0; x0=x1; x1=t; } x0=LC_MAX(x0,0); x1=LC_MIN(x1,s->w-1);
    for (x=x0;x<=x1;x++) s->pixels[(size_t)y*s->w+x]=k;
}
void lc_surface_vline(lc_surface *s,int x,int y0,int y1,lc_ink k) {
    int t,y; if (!s || x<0 || x>=s->w) return;
    if (y1<y0) { t=y0; y0=y1; y1=t; } y0=LC_MAX(y0,0); y1=LC_MIN(y1,s->h-1);
    for (y=y0;y<=y1;y++) s->pixels[(size_t)y*s->w+x]=k;
}
void lc_surface_dotted_h(lc_surface *s,int x0,int x1,int y,lc_ink k,int gap) {
    int x; if (!s) return; if (gap<1) gap=1;
    x1=LC_MIN(x1,s->w-1); for (x=LC_MAX(x0,0);x<=x1;x++) if (x%gap==0) lc_surface_set(s,x,y,k);
}
void lc_surface_dotted_v(lc_surface *s,int x,int y0,int y1,lc_ink k,int gap) {
    int y; if (!s) return; if (gap<1) gap=1;
    y1=LC_MIN(y1,s->h-1); for (y=LC_MAX(y0,0);y<=y1;y++) if (y%gap==0) lc_surface_set(s,x,y,k);
}
void lc_surface_line(lc_surface *s,double x0,double y0,double x1,double y1,lc_ink k,int width) {
    lc_pt a,b; double m; long ax,ay,bx,by,dx,dy,stepx,stepy,err,e2,budget,i,j,lo,hi;
    if (!s) return;
    if (width<1) width=1;
    if (width>1024) width=1024;
    m=width+2.0; a.x=x0; a.y=y0; b.x=x1; b.y=y1;
    if (!lc_clip_segment(&a,&b,-m,-m,s->w+m,s->h+m)) return;
    ax=(long)lc_round(a.x); ay=(long)lc_round(a.y); bx=(long)lc_round(b.x); by=(long)lc_round(b.y);
    dx=labs(bx-ax); dy=-labs(by-ay); stepx=ax<bx ? 1 : -1; stepy=ay<by ? 1 : -1;
    err=dx+dy; lo=-(width-1)/2; hi=width/2; budget=dx-dy+4;
    while (budget-->0) {
        for (j=lo;j<=hi;j++) for (i=lo;i<=hi;i++) if (ax+i>=0 && ax+i<s->w && ay+j>=0 && ay+j<s->h) lc_surface_set(s,(int)(ax+i),(int)(ay+j),k);
        if (ax==bx && ay==by) break;
        e2=2*err; if (e2>=dy) { err+=dy; ax+=stepx; } if (e2<=dx) { err+=dx; ay+=stepy; }
    }
}
static int compare_double(const void *a,const void *b) { double x=*(const double *)a,y=*(const double *)b; return x<y ? -1 : (x>y ? 1 : 0); }
void lc_surface_poly(lc_surface *s,const lc_pt *p,size_t count,lc_ink k) {
    double miny,maxy,sy,*xs; int y0,y1,y,xa,xb; size_t i,j,n,bytes;
    if (!s || !p || count<3) return;
    for (i=0;i<count;i++) if (!lc_finite(p[i].x) || !lc_finite(p[i].y)) return;
    miny=maxy=p[0].y; for (i=1;i<count;i++) { miny=LC_MIN(miny,p[i].y); maxy=LC_MAX(maxy,p[i].y); }
    if (!(maxy>=0) || !(miny<s->h)) return;
    if (!lc_size_mul(count,sizeof *xs,&bytes)) { lc_scene_fail(s->scene,LC_EOVERFLOW); return; }
    xs=(double *)lc_alloc(s->ctx,bytes); if (!xs) { lc_scene_fail(s->scene,LC_ENOMEM); return; }
    y0=lc_clamp_int(floor(miny),0,s->h-1); y1=lc_clamp_int(ceil(maxy),0,s->h-1);
    for (y=y0;y<=y1;y++) {
        sy=y+0.5; n=0;
        for (i=0,j=count-1;i<count;j=i++) if ((p[i].y<=sy && p[j].y>sy)||(p[j].y<=sy && p[i].y>sy)) xs[n++]=p[i].x+(sy-p[i].y)/(p[j].y-p[i].y)*(p[j].x-p[i].x);
        qsort(xs,n,sizeof *xs,compare_double);
        for (i=0;i+1<n;i+=2) { xa=lc_clamp_int(lc_round(xs[i]),0,s->w); xb=lc_clamp_int(lc_round(xs[i+1]),0,s->w); if (xa<xb) lc_surface_hline(s,xa,xb-1,y,k); }
    }
    lc_free(s->ctx,xs);
}
void lc_surface_disc(lc_surface *s,double cx,double cy,double r,lc_ink k) {
    double ry,dx,dy; int x0,x1,y0,y1,x,y;
    if (!s || !(r>0) || !lc_finite(r) || !lc_finite(cx) || !lc_finite(cy)) return;
    ry=r/lc_surface_aspect(s);
    x0=lc_clamp_int(floor(cx-r),0,s->w-1); x1=lc_clamp_int(ceil(cx+r),0,s->w-1);
    y0=lc_clamp_int(floor(cy-ry),0,s->h-1); y1=lc_clamp_int(ceil(cy+ry),0,s->h-1);
    for (y=y0;y<=y1;y++) for (x=x0;x<=x1;x++) { dx=(x+0.5-cx)/r; dy=(y+0.5-cy)/ry; if (dx*dx+dy*dy<=1) lc_surface_set(s,x,y,k); }
}
void lc_surface_ring(lc_surface *s,double cx,double cy,double r,lc_ink k) {
    double ry,in,dx,dy,d,aspect; int x0,x1,y0,y1,x,y;
    if (!s || !(r>0) || !lc_finite(r) || !lc_finite(cx) || !lc_finite(cy)) return;
    aspect=lc_surface_aspect(s); ry=r/aspect; in=LC_MAX(0,r-LC_MAX(1,r*0.35));
    x0=lc_clamp_int(floor(cx-r),0,s->w-1); x1=lc_clamp_int(ceil(cx+r),0,s->w-1);
    y0=lc_clamp_int(floor(cy-ry),0,s->h-1); y1=lc_clamp_int(ceil(cy+ry),0,s->h-1);
    for (y=y0;y<=y1;y++) for (x=x0;x<=x1;x++) { dx=x+0.5-cx; dy=(y+0.5-cy)*aspect; d=sqrt(dx*dx+dy*dy); if (d<=r && d>=in) lc_surface_set(s,x,y,k); }
}
void lc_surface_marker(lc_surface *s,double cx,double cy,int shape,double r,lc_ink k) {
    double ry; lc_pt p[4]; size_t n;
    if (!s || !lc_finite(cx) || !lc_finite(cy) || !lc_finite(r)) return;
    if (r<1) { if (cx>=0 && cx<=s->w && cy>=0 && cy<=s->h) lc_surface_set(s,(int)lc_round(cx-0.5),(int)lc_round(cy-0.5),k); return; }
    ry=r/lc_surface_aspect(s); n=3;
    switch (shape%7) {
    case 0: lc_surface_disc(s,cx,cy,r,k); return;
    case 1: lc_surface_rect(s,cx-r*0.85,cy-ry*0.85,cx+r*0.85,cy+ry*0.85,k); return;
    case 2: p[0].x=cx; p[0].y=cy-ry*1.2; p[1].x=cx+r*1.2; p[1].y=cy; p[2].x=cx; p[2].y=cy+ry*1.2; p[3].x=cx-r*1.2; p[3].y=cy; n=4; break;
    case 3: p[0].x=cx; p[0].y=cy-ry*1.1; p[1].x=cx+r*1.15; p[1].y=cy+ry*0.9; p[2].x=cx-r*1.15; p[2].y=cy+ry*0.9; break;
    case 4: lc_surface_line(s,cx-r,cy-ry,cx+r,cy+ry,k,lc_clamp_int(r*0.6,1,1024)); lc_surface_line(s,cx-r,cy+ry,cx+r,cy-ry,k,lc_clamp_int(r*0.6,1,1024)); return;
    case 5: lc_surface_ring(s,cx,cy,r*1.1,k); return;
    default: p[0].x=cx-r*1.15; p[0].y=cy-ry*0.9; p[1].x=cx+r*1.15; p[1].y=cy-ry*0.9; p[2].x=cx; p[2].y=cy+ry*1.1; break;
    }
    lc_surface_poly(s,p,n,k);
}
typedef struct font_glyph { lc_codepoint cp; unsigned char rows[16]; } font_glyph;
static const font_glyph font[]={
#include "font.inc"
};
static const unsigned char tofu[16]={0,0,126,66,66,66,66,66,66,66,66,126,0,0,0,0};
static const font_glyph *find_glyph(lc_codepoint cp) {
    size_t lo=0,hi=sizeof font/sizeof font[0],mid;
    while (lo<hi) { mid=lo+(hi-lo)/2; if (font[mid].cp<cp) lo=mid+1; else hi=mid; }
    return lo<sizeof font/sizeof font[0] && font[lo].cp==cp ? &font[lo] : NULL;
}
int lc_glyph_known(lc_codepoint cp) { return find_glyph(cp)!=NULL; }
const unsigned char *lc_glyph_rows(lc_codepoint cp) { const font_glyph *g=find_glyph(cp); return g ? g->rows : tofu; }
/* x + w clipped to limit, without overflowing int (w > 0). Integer, because
   this runs per glyph pixel and a double is a software routine without an FPU. */
static int clip_end(int x,int w,int limit) { return x>=0 && w>=limit-x ? limit : LC_MIN(x+w,limit); }
void lc_image_rect(lc_image *im,int x,int y,int w,int h,int color) {
    int x0,y0,x1,y1,j;
    if (!im || !im->pixels || w<=0 || h<=0) return;
    x0=LC_MAX(x,0); y0=LC_MAX(y,0); x1=clip_end(x,w,im->width); y1=clip_end(y,h,im->height);
    if (x0>=x1 || y0>=y1) return;
    for (j=y0;j<y1;j++) memset(im->pixels+(size_t)j*im->stride+x0,color&15,(size_t)(x1-x0));
}
void lc_image_glyph(lc_image *im,int x,int y,lc_codepoint cp,int fg,int bg,int scale) {
    const unsigned char *rows; int r,c;
    if (scale<1) scale=1;
    if (scale>INT_MAX/16) return;
    if (bg>=0) lc_image_rect(im,x,y,8*scale,16*scale,bg);
    if (cp==' ') return;
    rows=lc_glyph_rows(cp);
    if (scale==1 && im && im->pixels && x>=0 && y>=0 && x<=im->width-8 && y<=im->height-16) {
        unsigned char *row; int ink=fg&15;
        for (r=0;r<16;r++) { row=im->pixels+(size_t)(y+r)*im->stride+x; for (c=0;c<8;c++) if (rows[r]&(128>>c)) row[c]=(unsigned char)ink; }
        return;
    }
    /* a pixel whose position overflows int is skipped */
    for (r=0;r<16;r++) for (c=0;c<8;c++) if (rows[r]&(128>>c)) {
        if ((x>0 && x>INT_MAX-c*scale) || (y>0 && y>INT_MAX-r*scale)) continue;
        lc_image_rect(im,x+c*scale,y+r*scale,scale,scale,fg);
    }
}
lc_status lc_image_scale(lc_context *ctx,const lc_image *im,int k,lc_image *out) {
    size_t size; int x,y,i,j; unsigned char *dst; const unsigned char *src;
    if (!ctx || !im || !out || !im->pixels || im->width<1 || im->height<1 || im->stride<(size_t)im->width || k<1) return LC_EINVAL;
    memset(out,0,sizeof *out);
    if (im->width>INT_MAX/k || im->height>INT_MAX/k) return LC_EOVERFLOW;
    out->width=im->width*k; out->height=im->height*k; out->stride=(size_t)out->width;
    if (!lc_size_mul(out->stride,(size_t)out->height,&size)) return LC_EOVERFLOW;
    out->pixels=(unsigned char *)lc_alloc(ctx,size); if (!out->pixels) return LC_ENOMEM;
    for (y=0;y<im->height;y++) {
        dst=out->pixels+(size_t)y*k*out->stride; src=im->pixels+(size_t)y*im->stride;
        for (x=0;x<im->width;x++) for (i=0;i<k;i++) dst[(size_t)x*k+i]=src[x];
        for (j=1;j<k;j++) memcpy(dst+(size_t)j*out->stride,dst,out->stride);
    }
    return LC_OK;
}
