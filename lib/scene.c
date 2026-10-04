#include "internal.h"

lc_status lc_scene_create(lc_context *ctx,int cols,int rows,lc_mode mode,lc_scene **out) {
    lc_scene *sc; size_t count,bytes;
    if (!out) return LC_EINVAL;
    *out=NULL;
    if (!ctx || cols<1 || rows<1) return LC_EINVAL;
    /* Leave headroom for clipping, coordinates and Bresenham's doubled error. */
    if (cols>INT_MAX/64 || rows>INT_MAX/128 || !lc_size_mul((size_t)cols,(size_t)rows,&count) || !lc_size_mul(count,sizeof(lc_cell),&bytes)) return LC_EOVERFLOW;
    sc=(lc_scene *)lc_alloc(ctx,sizeof *sc); if (!sc) return LC_ENOMEM;
    memset(sc,0,sizeof *sc); sc->ctx=ctx; sc->canvas.ctx=ctx; sc->canvas.w=cols; sc->canvas.h=rows;
    sc->mode=mode; sc->where=""; lc_skin_init(&sc->skin,"dos");
    sc->canvas.cells=(lc_cell *)lc_alloc(ctx,bytes);
    if (!sc->canvas.cells) { lc_free(ctx,sc); return LC_ENOMEM; }
    lc_canvas_clear(&sc->canvas,sc->skin.text,sc->skin.slide_bg); *out=sc; return LC_OK;
}
void lc_scene_reset(lc_scene *sc) {
    lc_layer *l,*ln; lc_text_item *t,*tn;
    if (!sc) return;
    for (l=sc->layers;l;l=ln) { ln=l->next; lc_free(sc->ctx,l->surface.pixels); lc_free(sc->ctx,l); }
    for (t=sc->texts;t;t=tn) { tn=t->next; lc_free(sc->ctx,t->text); lc_free(sc->ctx,t); }
    sc->layers=sc->last_layer=NULL; sc->texts=sc->last_text=NULL; sc->status=LC_OK; sc->where="";
    lc_canvas_clear(&sc->canvas,sc->skin.text,sc->skin.slide_bg);
}
void lc_scene_destroy(lc_scene *sc) { if (sc) { lc_scene_reset(sc); lc_free(sc->ctx,sc->canvas.cells); lc_free(sc->ctx,sc); } }
lc_status lc_scene_status(const lc_scene *sc) { return sc ? (sc->status!=LC_OK ? sc->status : lc_context_status(sc->ctx)) : LC_EINVAL; }
int lc_scene_cols(const lc_scene *sc) { return sc ? sc->canvas.w : 0; }
int lc_scene_rows(const lc_scene *sc) { return sc ? sc->canvas.h : 0; }
lc_mode lc_scene_mode(const lc_scene *sc) { lc_mode m; memset(&m,0,sizeof m); return sc ? sc->mode : m; }
lc_skin *lc_scene_skin(lc_scene *sc) { return sc ? &sc->skin : NULL; }
lc_context *lc_scene_context(lc_scene *sc) { return sc ? sc->ctx : NULL; }
lc_canvas *lc_scene_canvas(lc_scene *sc) { return sc ? &sc->canvas : NULL; }
void lc_scene_set_diagnostic(lc_scene *sc,lc_diagnostic_fn fn,void *user) { if (sc) { sc->diagnostic=fn; sc->diagnostic_user=user; } }
void lc_scene_where(lc_scene *sc,const char *path) { if (sc) sc->where=path ? path : ""; }
void lc_scene_fit(lc_scene *sc,const char *message) { if (sc && sc->diagnostic && message) sc->diagnostic(sc->diagnostic_user,sc->where,message); }
lc_surface *lc_scene_surface(lc_scene *sc,lc_rect rect) {
    lc_layer *l; lc_surface *s; int sx,sy,w,h; size_t count,bytes,i;
    if (!sc || sc->status!=LC_OK) return NULL;
    sx=sc->mode.pixel ? 8 : 1; sy=sc->mode.pixel ? 16 : (sc->mode.ascii ? 1 : 2);
    w=LC_MAX(1,rect.w); h=LC_MAX(1,rect.h);
    if (w>INT_MAX/8/sx || h>INT_MAX/8/sy || !lc_size_mul((size_t)w*sx,(size_t)h*sy,&count) || !lc_size_mul(count,sizeof(lc_ink),&bytes)) { lc_scene_fail(sc,LC_EOVERFLOW); return NULL; }
    l=(lc_layer *)lc_alloc(sc->ctx,sizeof *l); if (!l) { lc_scene_fail(sc,LC_ENOMEM); return NULL; }
    memset(l,0,sizeof *l); l->rect=rect; s=&l->surface; s->ctx=sc->ctx; s->scene=sc;
    s->w=w*sx; s->h=h*sy; s->sx=sx; s->sy=sy; s->pixels=(lc_ink *)lc_alloc(sc->ctx,bytes);
    if (!s->pixels) { lc_free(sc->ctx,l); lc_scene_fail(sc,LC_ENOMEM); return NULL; }
    for (i=0;i<count;i++) s->pixels[i]=lc_ink_make(0,0,LC_EMPTY);
    if (sc->last_layer) sc->last_layer->next=l; else sc->layers=l;
    sc->last_layer=l; return s;
}
void lc_scene_text(lc_scene *sc,double x,double y,const char *text,int fg,int bg,int scale) {
    lc_text_item *t; const char *p; lc_codepoint cp; size_t n,bytes,used; char encoded[5];
    if (!sc || !text || !*text || !lc_finite(x) || !lc_finite(y) || sc->status!=LC_OK) return;
    n=strlen(text); if (!lc_size_mul(n,3,&bytes) || bytes==(size_t)-1) { lc_scene_fail(sc,LC_EOVERFLOW); return; }
    t=(lc_text_item *)lc_alloc(sc->ctx,sizeof *t); if (!t) { lc_scene_fail(sc,LC_ENOMEM); return; }
    t->text=(char *)lc_alloc(sc->ctx,bytes+1); if (!t->text) { lc_free(sc->ctx,t); lc_scene_fail(sc,LC_ENOMEM); return; }
    p=text; used=0; while (*p) { cp=lc_cell_of(lc_utf8_next(&p)); if (!cp) continue; n=lc_utf8_encode(cp,encoded); memcpy(t->text+used,encoded,n); used+=n; } t->text[used]=0;
    t->x=x; t->y=y; t->fg=fg; t->bg=bg; t->scale=LC_MAX(1,scale); t->next=NULL;
    if (sc->last_text) sc->last_text->next=t; else sc->texts=t; sc->last_text=t;
}
void lc_scene_big(lc_scene *sc,int x,int y,int w,const char *text,int fg,int scale,int align) {
    int k,len,off,row; char *t;
    if (!sc || w<1 || scale>INT_MAX/16) return;
    scale=LC_MAX(1,scale); k=sc->mode.pixel ? scale : 1;
    t=lc_text_truncate(sc->ctx,text,(size_t)LC_MAX(1,w/k)); if (!t) { lc_scene_fail(sc,LC_ENOMEM); return; }
    len=(int)lc_text_width(t)*k; off=align<0 ? 0 : (align==0 ? (w-len)/2 : w-len); off=LC_MAX(0,off);
    row=sc->mode.pixel ? y : lc_clamp_int((double)y+(scale-1)/2,INT_MIN,INT_MAX); lc_scene_text(sc,(double)x+off,row,t,fg,-1,k);
}
void lc_scene_cover(lc_scene *sc,lc_rect r) {
    lc_layer *l; lc_text_item **link,*t; double w,h;
    if (!sc) return;
    for (l=sc->layers;l;l=l->next) lc_surface_erase(&l->surface,
        lc_clamp_int(((double)r.x-l->rect.x)*l->surface.sx,INT_MIN,INT_MAX),
        lc_clamp_int(((double)r.y-l->rect.y)*l->surface.sy,INT_MIN,INT_MAX),
        lc_clamp_int(((double)r.x+r.w-l->rect.x)*l->surface.sx,INT_MIN,INT_MAX),
        lc_clamp_int(((double)r.y+r.h-l->rect.y)*l->surface.sy,INT_MIN,INT_MAX));
    link=&sc->texts; sc->last_text=NULL;
    while ((t=*link)!=NULL) {
        w=(double)lc_text_width(t->text)*t->scale; h=t->scale;
        if (t->x<(double)r.x+r.w && r.x<t->x+w && t->y<(double)r.y+r.h && r.y<t->y+h) { *link=t->next; lc_free(sc->ctx,t->text); lc_free(sc->ctx,t); }
        else { sc->last_text=t; link=&t->next; }
    }
}
void lc_scene_panel(lc_scene *sc,lc_rect r,int bg) { if (sc) lc_canvas_fill_bg(&sc->canvas,r.x,r.y,r.w,r.h,bg); }
lc_status lc_scene_to_cells(lc_scene *sc,lc_cell **out) {
    static const lc_codepoint shade_u[4]={0x2588,0x2593,0x2592,0x2591},shade_a[4]={'#','%','=','.'};
    lc_canvas cv; lc_layer *l; lc_surface *s; lc_text_item *t; lc_ink kt,kb; int cx,cy,x,y,top,bottom; size_t bytes;
    if (!sc || !out) return LC_EINVAL;
    *out=NULL; if (lc_scene_status(sc)!=LC_OK) return lc_scene_status(sc);
    cv=sc->canvas; bytes=(size_t)cv.w*cv.h*sizeof(lc_cell); cv.cells=(lc_cell *)lc_alloc(sc->ctx,bytes); if (!cv.cells) return LC_ENOMEM;
    memcpy(cv.cells,sc->canvas.cells,bytes);
    for (l=sc->layers;l;l=l->next) {
        s=&l->surface; if (lc_surface_fine(s)) continue;
        for (cy=0;cy<l->rect.h;cy++) for (cx=0;cx<l->rect.w;cx++) {
            double xx=(double)l->rect.x+cx,yy=(double)l->rect.y+cy;
            if (xx<0 || yy<0 || xx>=cv.w || yy>=cv.h) continue;
            x=(int)xx; y=(int)yy;
            if (s->sy==1) { if (!lc_surface_touched(s,cx,cy)) continue; kt=lc_surface_at(s,cx,cy); lc_canvas_put(&cv,x,y,(sc->mode.ascii ? shade_a : shade_u)[kt.level&3],kt.a,-1); continue; }
            top=lc_surface_touched(s,cx,cy*2); bottom=lc_surface_touched(s,cx,cy*2+1); if (!top && !bottom) continue;
            kt=top ? lc_surface_at(s,cx,cy*2) : lc_ink_solid(7); kb=bottom ? lc_surface_at(s,cx,cy*2+1) : lc_ink_solid(7);
            if (top && bottom && ((kt.a==kb.a && kt.b==kb.b && kt.level==kb.level)||!sc->mode.color)) lc_canvas_put(&cv,x,y,shade_u[kt.level&3],kt.a,kt.level && sc->mode.color ? kt.b : -1);
            else if (top && bottom) { if (kb.a>=8 && kt.a<8) lc_canvas_put(&cv,x,y,0x2584,kb.a,kt.a); else lc_canvas_put(&cv,x,y,0x2580,kt.a,kb.a); }
            else if (top) lc_canvas_put(&cv,x,y,0x2580,kt.a,-1); else lc_canvas_put(&cv,x,y,0x2584,kb.a,-1);
        }
    }
    for (t=sc->texts;t;t=t->next) if (t->x>=INT_MIN && t->x<=INT_MAX && t->y>=INT_MIN && t->y<=INT_MAX) lc_canvas_text(&cv,lc_clamp_int(lc_round(t->x),INT_MIN,INT_MAX),lc_clamp_int(lc_round(t->y),INT_MIN,INT_MAX),t->text,t->fg,t->bg);
    *out=cv.cells; return LC_OK;
}
lc_status lc_scene_draw_image(lc_scene *sc,lc_image *im) {
    int base,x,y,bg,kx,ky,px,py; lc_layer *l; lc_surface *s; lc_text_item *t; const lc_cell *c; const char *text; lc_codepoint cp; size_t bytes; double ox,oy,tx,ty;
    if (!sc || !im || !im->pixels || im->width!=sc->canvas.w*8 || im->height!=sc->canvas.h*16 || im->stride<(size_t)im->width) return LC_EINVAL;
    if (lc_scene_status(sc)!=LC_OK) return lc_scene_status(sc);
    if (!lc_size_mul(im->stride,(size_t)im->height,&bytes)) return LC_EOVERFLOW;
    base=sc->skin.slide_bg==LC_BG_NONE ? 0 : sc->skin.slide_bg;
    for (y=0;y<im->height;y++) memset(im->pixels+(size_t)y*im->stride,base,(size_t)im->width);
    for (y=0;y<sc->canvas.h;y++) for (x=0;x<sc->canvas.w;x++) {
        c=&sc->canvas.cells[(size_t)y*sc->canvas.w+x]; bg=c->bg==LC_BG_NONE ? base : c->bg;
        if (bg!=base) lc_image_rect(im,x*8,y*16,8,16,bg);
        if (c->ch!=' ') lc_image_glyph(im,x*8,y*16,c->ch,c->fg,-1,1);
    }
    for (l=sc->layers;l;l=l->next) {
        s=&l->surface; ox=(double)l->rect.x*8; oy=(double)l->rect.y*16; kx=8/s->sx; ky=16/s->sy;
        if (kx==1 && ky==1) {
            static const unsigned char rank[2][2]={{0,2},{3,1}};
            int x0,x1,y0,y1,dx,dy;
            const lc_ink *ink;
            unsigned char *dst;
            /* Clip once; the VGA/pixel hot loop is integer-only and writes
             * directly into the caller's row. No per-pixel geometry calls. */
            if (ox>=im->width || oy>=im->height || ox+s->w<=0 || oy+s->h<=0) continue;
            dx=(int)ox; dy=(int)oy;
            x0=LC_MAX(0,-dx); y0=LC_MAX(0,-dy);
            x1=LC_MIN(s->w,im->width-dx); y1=LC_MIN(s->h,im->height-dy);
            for (y=y0;y<y1;y++) {
                ink=s->pixels+(size_t)y*s->w+x0;
                dst=im->pixels+(size_t)(dy+y)*im->stride+(size_t)(dx+x0);
                for (x=x0;x<x1;x++,ink++,dst++) if (ink->level!=LC_EMPTY)
                    *dst=ink->level && rank[y&1][x&1]<ink->level ? ink->b : ink->a;
            }
            continue;
        }
        for (y=0;y<s->h;y++) for (x=0;x<s->w;x++) if (lc_surface_touched(s,x,y)) {
            tx=ox+(double)x*kx; ty=oy+(double)y*ky;
            if (tx>=im->width || ty>=im->height || tx+kx<=0 || ty+ky<=0) continue;
            px=(int)tx; py=(int)ty; lc_image_rect(im,px,py,kx,ky,lc_surface_resolve(s,x,y));
        }
    }
    for (t=sc->texts;t;t=t->next) {
        tx=lc_round(t->x*8); ty=lc_round(t->y*16); text=t->text;
        while (*text) {
            cp=lc_utf8_next(&text);
            if (tx>=INT_MIN && tx<=INT_MAX && ty>=INT_MIN && ty<=INT_MAX) lc_image_glyph(im,(int)tx,(int)ty,cp,t->fg,t->bg,t->scale);
            tx+=8.0*t->scale;
        }
    }
    return LC_OK;
}

/* Convenience allocation; embedded callers can keep their own pitched target. */
lc_status lc_scene_to_image(lc_scene *sc,lc_image *im) {
    size_t bytes; lc_status status;
    if (!sc || !im) return LC_EINVAL;
    memset(im,0,sizeof *im);
    if (lc_scene_status(sc)!=LC_OK) return lc_scene_status(sc);
    im->width=sc->canvas.w*8; im->height=sc->canvas.h*16; im->stride=(size_t)im->width;
    if (!lc_size_mul(im->stride,(size_t)im->height,&bytes)) return LC_EOVERFLOW;
    im->pixels=(unsigned char *)lc_alloc(sc->ctx,bytes);
    if (!im->pixels) return LC_ENOMEM;
    status=lc_scene_draw_image(sc,im);
    if (status!=LC_OK) { lc_free(sc->ctx,im->pixels); memset(im,0,sizeof *im); }
    return status;
}
