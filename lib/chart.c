/* Chart layout and rasterization. No file, terminal, or platform ownership. */
#include "render.h"

#define PI 3.14159265358979323846
#define MIN LC_MIN
#define MAX LC_MAX
#define TXT(s) ((s) ? (s) : "")
#define EMPTY(s) (!(s) || !(s)[0])
#define RIGHT(r) ((r).x+(r).w-1)
#define BOTTOM(r) ((r).y+(r).h-1)
#define INK(c) lc_ink_solid(c)
#if DBL_MAX_10_EXP >= 300
#define CHART_SENTINEL 1e300
#else
#define CHART_SENTINEL DBL_MAX
#endif

typedef struct box { double x,y,w,h; } box;
typedef struct anchor { int s,i; lc_pt p; double dir; int directional; } anchor;
typedef struct ticks { double v[66]; int n; } ticks;
typedef struct legend_entry { const char *name,*value; int color,level; } legend_entry;
typedef struct plot {
    lc_scene *sc;
    lc_surface *sf;
    lc_rect cells,clip;
    int ox,oy,pw,ph,fine,sx,sy,across,numeric_x;
    double lo,hi,xlo,xhi;
    anchor *anchors; size_t anchor_count,anchor_cap;
    double *slot_cx,*slot_cy; size_t slot_nx,slot_ny;
    box *taken; size_t taken_count,taken_cap;
} plot;
typedef struct render {
    lc_scene *sc;
    lc_dataset ds;
    lc_series *series;
    lc_chart_options o;
    double **err_lo,**err_hi;
    unsigned char **err_valid;
    size_t n,err_series_count;
} render;

static const char *const types[]={"bar","stacked","hbar","dumbbell","line","area","pie","pie3d","donut","scatter","hist","table"};
static const char *const pal_names[]={"dos","ega","cga","ice","fire","green","amber","mono"};
static const int pal_sizes[]={13,14,4,7,7,6,6,6};
static const unsigned char pals[8][14]={
    {11,14,10,9,13,12,15,3,6,2,1,5,7},
    {9,10,11,12,13,14,6,2,4,5,1,3,7,15},
    {14,13,15,11},{14,15,12,6,7,4,11},{11,9,3,1,15,13,5},
    {10,2,15,14,6,7},{11,3,15,7,9,1},{15,7,15,7,15,7}
};

const char *lc_chart_type(const char *name)
{
    size_t i;
    static const char *const aliases[][2]={
        {"column","bar"},{"grouped","bar"},{"bars","bar"},{"bar3d","bar"},
        {"xy","scatter"},{"points","scatter"},{"histogram","hist"},{"lines","line"},
        {"doughnut","donut"},{"ring","donut"},{"stack","stacked"},{"stackedbar","stacked"},
        {"barh","hbar"},{"horizontal","hbar"},{"dumbbells","dumbbell"},{"before_after","dumbbell"},
        {"beforeafter","dumbbell"},{"change","dumbbell"},{"dots","dumbbell"}
    };
    if (EMPTY(name)) return "bar";
    for (i=0;i<sizeof types/sizeof types[0];++i) if (lc_name_eq(name,types[i])) return types[i];
    for (i=0;i<sizeof aliases/sizeof aliases[0];++i) if (lc_name_eq(name,aliases[i][0])) return aliases[i][1];
    return name;
}

int lc_chart_palette(lc_scene *sc,const char *name,size_t index,size_t count)
{
    int pal,light,c;
    size_t i,k,used,n;
    const char *custom,*p,*next;
    pal=0;
    custom=NULL;
    for (i=0;i<8;++i) if (lc_name_eq(name,pal_names[i])) pal=(int)i;
    n=(size_t)pal_sizes[pal];
    if (name && strncmp(name,"custom:",7)==0) {
        custom=name+7;
        n=1;
        for (p=custom;*p;++p) if (*p==',') ++n;
    }
    light=lc_contrast_on(sc->skin.panel_bg)==0 && sc->skin.slide_bg!=LC_BG_NONE;
    used=0;
    p=custom;
    for (k=0;k<count+64;++k) {
        if (custom) {
            c=(int)(strtol(p,NULL,10)&15);
            next=strchr(p,',');
            p=next ? next+1 : custom;
        } else c=pals[pal][k%n];
        if (light) c=c==15 ? 0 : (c==7 ? 8 : lc_dim_of(c));
        if (sc->skin.slide_bg!=LC_BG_NONE && c==sc->skin.panel_bg && n>1) continue;
        if (used++==index) return c;
    }
    return 7;
}

static int hidden(const lc_series *s) { return s->name && s->name[0]=='\001'; }
static int valid(const lc_series *s,size_t i)
{ return i<s->count && s->values && (!s->valid || s->valid[i]) && lc_finite(s->values[i]); }
static double cell_at(const lc_dataset *d,size_t s,size_t i)
{ return valid(d->series+s,i) ? d->series[s].values[i] : 0; }
static size_t rows(const lc_dataset *d)
{ size_t i,n; n=0; for(i=0;i<d->series_count;++i) n=MAX(n,d->series[i].count); return n; }
static double sum(const lc_series *s)
{ size_t i; double t; t=0; for(i=0;i<s->count;++i) if(valid(s,i)) t+=s->values[i]; return t; }
static void bounds(const lc_dataset *d,double *lo,double *hi)
{
    size_t i,j; int any; double v;
    *lo=0; *hi=1; any=0;
    for(i=0;i<d->series_count;++i) for(j=0;j<d->series[i].count;++j) if(valid(d->series+i,j)) {
        v=d->series[i].values[j];
        if(!any) { *lo=*hi=v; any=1; } else { *lo=MIN(*lo,v); *hi=MAX(*hi,v); }
    }
}
static int pinned(const lc_chart_options *o,const char *name,size_t position)
{
    size_t i,pos; int by_pos;
    pos=0; by_pos=-1;
    for(i=0;i<o->color_count;++i) {
        if(EMPTY(o->colors[i].name)) { if(pos++==position) by_pos=o->colors[i].color; }
        else if(lc_name_eq(o->colors[i].name,name)) return o->colors[i].color;
    }
    return by_pos;
}
static int shaded(const lc_chart_options *o) { return !o->color || lc_name_eq(o->palette,"mono"); }
static int panel(lc_scene *sc) { return sc->skin.slide_bg==LC_BG_NONE ? 0 : sc->skin.panel_bg; }
static lc_ink fill_ink(render *rr,int color,size_t series)
{
    if(shaded(&rr->o)) return lc_ink_make(rr->o.color?color:15,rr->sc->skin.panel_bg==LC_BG_NONE?0:rr->sc->skin.panel_bg,(int)(series%4));
    return INK(color);
}
static const char *format_point(lc_scene *sc,const lc_series *s,double v,int prec)
{ if(prec<0 && s->decimals>=0 && s->decimals<=3) prec=s->decimals; return lc_fmt_val(sc->ctx,v,prec); }
static lc_codepoint glyph(lc_scene *sc,lc_codepoint u,int a) { return sc->mode.ascii ? (lc_codepoint)a : u; }
static void text(lc_scene *sc,double x,double y,const char *s,int fg)
{ lc_scene_text(sc,x,y,s,fg,-1,1); }
static int hit(box a,box b) { return a.x<b.x+b.w && b.x<a.x+a.w && a.y<b.y+b.h && b.y<a.y+a.h; }
/* Preserve ordinary arithmetic exactly, but halve extreme operands when a
 * subtraction would overflow. Missing/degenerate ranges map to the origin. */
static double range_fraction(double value,double lo,double hi)
{
    double span,delta,f;
    if (value!=value) return 0;
    if (lo < -DBL_MAX) lo=-DBL_MAX;
    if (hi > DBL_MAX) hi=DBL_MAX;
    if (!(hi>lo)) return 0;
    span=hi-lo;
    delta=value-lo;
    if (lc_finite(span) && lc_finite(delta)) f=delta/span;
    else f=(value/2-lo/2)/(hi/2-lo/2);
    return lc_finite(f) ? f : (value>lo ? 1 : 0);
}
static double px_x(const plot *p,double v)
{ return p->ox+MAX(0.0,MIN(1.0,range_fraction(v,p->lo,p->hi)))*(p->pw-1); }
static double px_y(const plot *p,double v)
{ double f; f=range_fraction(v,p->lo,p->hi); if(!(f>0))f=0; if(f>1)f=1; return p->oy+p->ph-f*p->ph; }
static double cellx(const plot *p,double x) { return p->cells.x+x/p->sx; }
static double celly(const plot *p,double y) { return p->cells.y+y/p->sy; }
static void plot_init(plot *p,lc_scene *sc,lc_rect clip)
{
    memset(p,0,sizeof *p); p->sc=sc; p->clip=clip;
    p->fine=sc->mode.pixel; p->sx=p->fine?8:1; p->sy=p->fine?16:(sc->mode.ascii?1:2);
    p->pw=p->ph=1; p->hi=p->xhi=1;
}
static int grow(lc_scene *sc,void **v,size_t *cap,size_t count,size_t size)
{
    void *newp; size_t n;
    if(count<*cap) return 1;
    n=*cap ? *cap*2 : 16;
    if(n<*cap || n<=count) { lc_scene_fail(sc,LC_EOVERFLOW); return 0; }
    newp=lc_render_array(sc,n,size); if(!newp) return 0;
    if(*v) memcpy(newp,*v,count*size);
    *v=newp; *cap=n; return 1;
}
static void remember(plot *p,int s,int i,double x,double y,int directional,double dir)
{
    anchor *a;
    if(!grow(p->sc,(void **)&p->anchors,&p->anchor_cap,p->anchor_count,sizeof *a)) return;
    a=p->anchors+p->anchor_count++; a->s=s; a->i=i; a->p.x=x; a->p.y=y; a->dir=dir; a->directional=directional;
}
static const anchor *find_anchor(const plot *p,int s,int i)
{ size_t k; for(k=0;k<p->anchor_count;++k) if(p->anchors[k].i==i && (s<0 || p->anchors[k].s==s)) return p->anchors+k; return NULL; }
static void plot_put(plot *p,double x,double y,const char *s,int fg,int align,int bg)
{
    double len; box *b;
    len=(double)lc_text_width(s);
    x=align<0?x:(align==0?x-len/2:x-len);
    if(!p->fine) { x=floor(x+0.5); y=floor(y+0.5); }
    x=MAX(p->clip.x,MIN(x,RIGHT(p->clip)+1-len)); y=MAX(p->clip.y,MIN(y,BOTTOM(p->clip)));
    lc_scene_text(p->sc,x,y,s,fg,bg,1);
    if(!grow(p->sc,(void **)&p->taken,&p->taken_cap,p->taken_count,sizeof *b)) return;
    b=p->taken+p->taken_count++; b->x=x;b->y=y;b->w=len;b->h=1;
}
static void plot_label(plot *p,double x,double y,const char *s,int fg,int bg)
{ plot_put(p,cellx(p,x),celly(p,y)-0.5,s,fg,0,bg); }

static ticks nice_ticks(double lo,double hi,int want)
{
    ticks t; double tmp,span,step,start,v; int tries;
    t.n=0; if(!lc_finite(lo)||!lc_finite(hi)) return t;
    if(hi<lo){tmp=lo;lo=hi;hi=tmp;} want=MAX(2,MIN(16,want)); span=hi-lo;
    if(!(span>0)){t.v[t.n++]=lo;return t;}
    step=lc_nice_step(span/(want-1)); if(!(step>0))step=span;
    for(tries=0;tries<8;++tries) {
        t.n=0; start=ceil(lo/step-1e-9)*step;
        for(v=start;v<=hi+step*1e-6 && t.n<65;v+=step) t.v[t.n++]=fabs(v)<step*1e-9?0:v;
        if(t.n>=2)break;
         step/=2;
    }
    if(!t.n)t.v[t.n++]=lo;
    return t;
}
static ticks nice_range(double *lo,double *hi,int want,int pin_lo,int pin_hi,int whole)
{
    double lo0,hi0,best_waste,best_lo,best_hi,l,h,step,waste;
    ticks best,t; int w;
    if(!(*hi>*lo))*hi=*lo+1;
    if(whole)want=MAX(2,MIN(want,lc_clamp_int(ceil(*hi-*lo),0,INT_MAX-1)+1));
    lo0=*lo;hi0=*hi;best_waste=1e18;best_lo=*lo;best_hi=*hi;best.n=0;
    for(w=want;w<=want+(whole?0:4);++w) {
        l=lo0;h=hi0;t=nice_ticks(l,h,w);
        if(t.n>=2) {
            step=t.v[1]-t.v[0];
            if(!pin_lo)l=floor(l/step+1e-9)*step;
            if(!pin_hi)h=ceil(h/step-1e-9)*step;
            t=nice_ticks(l,h,w);
        }
        waste=((h-hi0)+(lo0-l))/(hi0-lo0);
        if(waste<best_waste-1e-9){best_waste=waste;best=t;best_lo=l;best_hi=h;}
        if(waste<=0.2)break;
    }
    *lo=best_lo;*hi=best_hi;
    if(pin_hi && best.n>=2 && best.n<66) {
        step=best.v[1]-best.v[0]; if(*hi-best.v[best.n-1]>=step*0.35)best.v[best.n++]=*hi;
    }
    return best;
}

static plot axes(render *rr,lc_rect r,lc_rect clip,double lo,double hi,ticks ts,
                 const char *const *cats,size_t nc,int numeric,double xlo,double xhi)
{
    plot p;
    lc_scene *sc; lc_surface *sf; lc_canvas *cv;
    lc_chart_options *o; lc_skin *skin;
    int tw,yl,left,has_x,below,top,gx,gy,gw,gh,i,last_row,len,yi,row;
    int n,maxlen,every,room,cut;
    double y,ty,last_ty,laby,slot,cxp,x,f,xp;
    const char *lab,*s; ticks xt;
    sc=rr->sc;cv=&sc->canvas;skin=&sc->skin;o=&rr->o;
    plot_init(&p,sc,clip);p.lo=lo;p.hi=hi;
    tw=1;
    for(i=0;i<ts.n;++i)tw=MAX(tw,(int)lc_text_width(lc_fmt_axis(sc->ctx,ts.v[i])));
    yl=EMPTY(o->ylabel)||r.w<24?0:2;left=yl+tw+1;
    if(left+6>r.w)left=MAX(1,r.w-6);
    has_x=numeric||(cats && nc);below=1+(has_x?1:0)+(EMPTY(o->xlabel)?0:1);top=r.h>=10?1:0;
    gx=r.x+left;gy=r.y+top;gw=MAX(1,RIGHT(r)-gx+1-(r.w>30?1:0));gh=MAX(1,r.h-top-below);
    p.cells=lc_rect_make(gx-1,gy,gw+1,gh+1);p.sf=lc_scene_surface(sc,p.cells);
    p.ox=p.sx;p.pw=gw*p.sx;p.ph=gh*p.sy;sf=p.sf;
    if(!sf)return p;
    if(yl>0) {
        s=lc_text_truncate(sc->ctx,o->ylabel,(size_t)gh);
        if(lc_text_width(o->ylabel)>(size_t)gh) {
            lab=lc_render_join(sc,"ylabel \"",o->ylabel,"\" is cut short: it runs down the axis one letter a row, and there are ");
            lc_scene_fit(sc,lc_render_join(sc,lab,lc_render_uint(sc,(size_t)gh)," rows"));
        }
        lc_canvas_vtext(cv,r.x,gy+(gh-(int)lc_text_width(s))/2,s,skin->ylabel);
    }
    if(p.fine) {
        lc_surface_vline(sf,p.ox-1,0,p.ph,INK(skin->axis));
        lc_surface_hline(sf,p.ox-1,p.ox+p.pw-1,p.ph,INK(skin->axis));
    } else {
        lc_canvas_vline(cv,gx-1,gy,gh,glyph(sc,0x2502,'|'),skin->axis);
        lc_canvas_put(cv,gx-1,gy+gh,glyph(sc,0x2514,'+'),skin->axis,-1);
        lc_canvas_hline(cv,gx,gy+gh,gw,glyph(sc,0x2500,'-'),skin->axis);
    }
    last_row=-99;last_ty=1e9;
    for(i=0;i<ts.n;++i) {
        y=px_y(&p,ts.v[i]);lab=lc_fmt_axis(sc->ctx,ts.v[i]);len=(int)lc_text_width(lab);
        if(p.fine) {
            yi=(int)lc_round(y);lc_surface_hline(sf,p.ox-5,p.ox-2,yi,INK(skin->axis));
            if(o->grid && yi<p.ph-1)lc_surface_dotted_h(sf,p.ox,p.ox+p.pw-1,yi,INK(skin->grid),4);
            ty=MAX(clip.y,celly(&p,y)-0.5);if(last_ty-ty<0.95)continue;last_ty=ty;
            text(sc,gx-1.5-len,ty,lab,skin->tick);
        } else {
            row=(int)floor(celly(&p,y));row=MAX(gy,MIN(row,gy+gh));if(row==last_row)continue;last_row=row;
            lc_canvas_text_r(cv,r.x+yl,row,left-yl-1,lab,skin->tick);
            if(row<gy+gh) {
                lc_canvas_put(cv,gx-1,row,glyph(sc,0x2524,'+'),skin->axis,-1);
                if(o->grid)lc_canvas_hline(cv,gx,row,gw,glyph(sc,0xb7,'.'),skin->grid);
            }
        }
    }
    laby=p.fine?gy+gh+0.45:gy+gh+1;
    if(cats && nc && !numeric) {
        n=(int)nc;slot=(double)gw/n;maxlen=1;
        p.slot_cx=(double *)lc_render_array(sc,nc,sizeof(double));if(!p.slot_cx)return p;p.slot_nx=nc;
        for(i=0;i<n;++i)maxlen=MAX(maxlen,(int)lc_text_width(cats[i]));
        every=1;
        if(slot<maxlen+1)every=MAX(1,(int)ceil((MIN(maxlen,10)+1)/slot));
        if(every>1) {
            lab=lc_render_join(sc,"only every ",lc_render_uint(sc,(size_t)every),every==2?"nd":(every==3?"rd":"th"));
            lab=lc_render_join(sc,lab," category label is shown: ",lc_render_uint(sc,nc));
            lab=lc_render_join(sc,lab," labels of up to ",lc_render_uint(sc,(size_t)maxlen));
            lab=lc_render_join(sc,lab," characters do not fit across ",lc_render_uint(sc,(size_t)gw));
            lc_scene_fit(sc,lc_render_join(sc,lab," columns (shorten them, or use hbar)",""));
        }
        cut=0;
        for(i=0;i<n;++i) {
            cxp=p.ox+(i+0.5)*p.pw/n;p.slot_cx[i]=cxp;
            if(p.fine)lc_surface_vline(sf,(int)cxp,p.ph+1,p.ph+3,INK(skin->axis));
            else if(slot>=2)lc_canvas_put(cv,(int)cellx(&p,cxp),gy+gh,glyph(sc,0x252c,'+'),skin->axis,-1);
            if(i%every)continue;
            room=MAX(1,(int)(slot*every)-(every>1 || slot>=3?1:0));
            lab=lc_text_truncate(sc->ctx,cats[i],(size_t)room);len=(int)lc_text_width(lab);
            if(!cut && every==1 && lc_text_width(cats[i])>(size_t)room) {
                cut=1;s=lc_render_join(sc,"category label \"",cats[i],"\" is cut to ");
                s=lc_render_join(sc,s,lc_render_uint(sc,(size_t)room)," characters (shorten the labels, or use hbar)");lc_scene_fit(sc,s);
            }
            x=cellx(&p,cxp)-len/2.0;if(!p.fine)x=floor(x+0.5);x=MAX(r.x,MIN(x,RIGHT(r)+1-len));text(sc,x,laby,lab,skin->label);
        }
    } else if(numeric) {
        p.numeric_x=1;p.xlo=xlo;p.xhi=xhi;xt=nice_ticks(xlo,xhi,MAX(2,MIN(8,gw/12+2)));
        for(i=0;i<xt.n;++i) {
            f=xhi>xlo?(xt.v[i]-xlo)/(xhi-xlo):0;if(f< -1e-9 || f>1+1e-9)continue;
            xp=p.ox+f*(p.pw-1);lab=lc_fmt_axis(sc->ctx,xt.v[i]);len=(int)lc_text_width(lab);
            if(p.fine) {
                lc_surface_vline(sf,(int)xp,p.ph+1,p.ph+3,INK(skin->axis));
                if(o->grid && f>0.001)lc_surface_dotted_v(sf,(int)xp,0,p.ph-1,INK(skin->grid),4);
            } else lc_canvas_put(cv,(int)cellx(&p,xp),gy+gh,glyph(sc,0x252c,'+'),skin->axis,-1);
            x=cellx(&p,xp)-len/2.0;if(!p.fine)x=floor(x+0.5);x=MAX(r.x,MIN(x,RIGHT(r)+1-len));text(sc,x,laby,lab,skin->label);
        }
    }
    if(!EMPTY(o->xlabel))lc_canvas_text_c(cv,gx,BOTTOM(r),gw,o->xlabel,skin->xlabel);
    return p;
}

static int legend_width(const legend_entry *entries,size_t count)
{
    size_t i,w,n;w=0;
    for(i=0;i<count;++i) {
        n=lc_text_width(entries[i].name)+3;
        if(!EMPTY(entries[i].value))n+=lc_text_width(entries[i].value)+2;
        w=MAX(w,n);
    }
    return (int)w;
}
static int legend_colwidth(const legend_entry *entries,size_t count)
{
    size_t i;int w,n;w=0;
    for(i=0;i<count;++i) {
        n=(int)(lc_text_width(entries[i].name)+(EMPTY(entries[i].value)?0:lc_text_width(entries[i].value)+1))+5;
        w=MAX(w,n);
    }
    return w;
}
static int legend_rows(const legend_entry *entries,size_t count,int width)
{
    int ncols;ncols=MAX(1,MIN((int)count,width/MAX(1,legend_colwidth(entries,count))));
    return ((int)count+ncols-1)/ncols;
}
static void swatch(lc_scene *sc,int x,int y,const legend_entry *e)
{
    static const lc_codepoint u[4]={0x2588,0x2593,0x2592,0x2591};
    static const char a[4]={'#','%',':','.'};
    lc_codepoint g;g=glyph(sc,u[e->level&3],a[e->level&3]);
    lc_canvas_put(&sc->canvas,x,y,g,e->color,-1);lc_canvas_put(&sc->canvas,x+1,y,g,e->color,-1);
}
static void legend_draw(lc_scene *sc,lc_rect r,const legend_entry *e,size_t count,int vertical)
{
    size_t i;int y,room,colw,ncols,nrows,x0,row,col,x;const char *s;
    if(!count || r.w<4 || r.h<1)return;
    if(vertical) {
        y=r.y;
        for(i=0;i<count;++i,++y) {
            if(y>BOTTOM(r))return;
            swatch(sc,r.x,y,e+i);
            room=r.w-3;
            if(!EMPTY(e[i].value))room-=(int)lc_text_width(e[i].value)+2;
            lc_canvas_text(&sc->canvas,r.x+3,y,lc_text_truncate(sc->ctx,e[i].name,(size_t)MAX(1,room)),sc->skin.legend,-1);
            if(!EMPTY(e[i].value) && r.w>(int)lc_text_width(e[i].value)+5)lc_canvas_text_r(&sc->canvas,r.x,y,r.w,e[i].value,sc->skin.value);
        }
        return;
    }
    colw=MIN(legend_colwidth(e,count),r.w);ncols=MAX(1,MIN((int)count,r.w/MAX(1,colw)));
    nrows=((int)count+ncols-1)/ncols;ncols=((int)count+nrows-1)/nrows;
    x0=r.x+MAX(0,(r.w-ncols*colw+2)/2);
    for(i=0;i<count;++i) {
        row=(int)i/ncols;col=(int)i%ncols;if(row>=r.h)return;x=x0+col*colw;y=r.y+row;
        s=EMPTY(e[i].value)?e[i].name:lc_render_join(sc,e[i].name," ",e[i].value);
        swatch(sc,x,y,e+i);lc_canvas_text(&sc->canvas,x+3,y,lc_text_truncate(sc->ctx,s,(size_t)MAX(1,colw-4)),sc->skin.legend,-1);
    }
}

static int has_whisker(const render *rr,size_t s,size_t i)
{ return rr->err_valid && s<rr->err_series_count && rr->err_valid[s] && i<rr->n && rr->err_valid[s][i]; }
static void whisker_bounds(const render *rr,double *lo,double *hi)
{
    size_t s,i;
    for(s=0;s<rr->err_series_count;++s)for(i=0;i<rr->n;++i)if(has_whisker(rr,s,i)) {
        *lo=MIN(*lo,rr->err_lo[s][i]);*hi=MAX(*hi,rr->err_hi[s][i]);
    }
}
static void whisker_segment(plot *p,double u0,double v0,double u1,double v1,lc_ink ink,int w,int horizontal)
{
    if(horizontal)lc_surface_line(p->sf,v0,u0,v1,u1,ink,w);
    else lc_surface_line(p->sf,u0,v0,u1,v1,ink,w);
}
static void whisker(plot *p,double a0,double a1,double at,int horizontal)
{
    double cap,c;int pass,w;lc_ink ink;
    cap=p->fine?5:1;
    for(pass=p->fine?0:1;pass<2;++pass) {
        c=cap+(pass?0:1);w=pass?1:3;ink=INK(pass?p->sc->skin.value:panel(p->sc));
        whisker_segment(p,at,a0,at,a1,ink,w,horizontal);
        whisker_segment(p,at-c,a0,at+c,a0,ink,w,horizontal);
        whisker_segment(p,at-c,a1,at+c,a1,ink,w,horizontal);
    }
}
static void quad(lc_surface *sf,double x0,double y0,double x1,double y1,double x2,double y2,double x3,double y3,lc_ink ink)
{
    lc_pt v[4];v[0].x=x0;v[0].y=y0;v[1].x=x1;v[1].y=y1;v[2].x=x2;v[2].y=y2;v[3].x=x3;v[3].y=y3;lc_surface_poly(sf,v,4,ink);
}
static void bar3d(plot *p,double x0,double x1,double ya,double yb,lc_ink front,int color,double dx,double dy,int shade,int top_face)
{
    lc_surface *sf;double top,bot;int solid,ix0,ix1,iy0,iy1;lc_ink edge;
    sf=p->sf;top=MIN(ya,yb);bot=MAX(ya,yb);if(x1-x0<1)x1=x0+1;
    if(!p->fine) {
        lc_surface_rect(sf,x0,top,x1,bot,front);
        if(shade && x1-x0>=3)lc_surface_rect(sf,x1-1,top,x1,bot,lc_side_ink(color));
        return;
    }
    solid=dx>0 && shade;
    if(solid) {
        quad(sf,x1,top,x1+dx,top-dy,x1+dx,bot-dy,x1,bot,lc_side_ink(color));
        if(top_face)quad(sf,x0,top,x1,top,x1+dx,top-dy,x0+dx,top-dy,lc_top_ink(color));
    }
    lc_surface_rect(sf,x0,top,x1,bot,front);
    if(x1-x0>=5) {
        edge=INK(panel(p->sc));ix0=(int)lc_round(x0);ix1=(int)lc_round(x1)-1;iy0=(int)lc_round(top);iy1=(int)lc_round(bot)-1;
        lc_surface_vline(sf,ix0,iy0,iy1,edge);lc_surface_vline(sf,ix1,iy0,iy1,edge);lc_surface_hline(sf,ix0,ix1,iy0,edge);
        if(solid) {
            lc_surface_line(sf,x1+dx,top-dy,x1+dx,bot-dy,edge,1);
            if(top_face) {
                lc_surface_line(sf,x0,top,x0+dx,top-dy,edge,1);
                lc_surface_line(sf,x0+dx,top-dy,x1+dx,top-dy,edge,1);
                lc_surface_line(sf,x1-1,top,x1+dx-1,top-dy,edge,1);
            }
        }
    }
}
static lc_pt break_at(plot *p,int along,int across,int horizontal)
{
    lc_pt q;q.x=horizontal?p->ox+across:p->ox+along;q.y=horizontal?p->ph-1-along:p->ph-across;return q;
}
static void clear_at(plot *p,lc_pt q)
{ int x,y;x=(int)q.x;y=(int)q.y;lc_surface_erase(p->sf,x,y,x+1,y+1); }
static void axis_break(plot *p,int horizontal)
{
    int len,t,cx,cy,off,w,k,s;lc_pt c,a,b;
    len=horizontal?p->ph:p->pw;
    if(!p->fine) {
        for(t=0;t<len;++t)clear_at(p,break_at(p,t,2,horizontal));
        c=break_at(p,0,2,horizontal);cx=horizontal?(int)cellx(p,c.x):p->cells.x;cy=horizontal?BOTTOM(p->cells):(int)celly(p,c.y);
        lc_canvas_put(&p->sc->canvas,cx,cy,glyph(p->sc,0x2248,'~'),p->sc->skin.axis,-1);return;
    }
    off=MAX(8,MIN(14,len/10));
    for(t=0;t<len;++t) { w=abs(t%8-4)-2;for(k=-2;k<=1;++k)clear_at(p,break_at(p,t,off+w+k,horizontal)); }
    for(k=-3;k<=3;++k)clear_at(p,break_at(p,-1,off+k,horizontal));
    for(s=-2;s<=2;s+=4) {
        a=break_at(p,-6,off+s-2,horizontal);b=break_at(p,4,off+s+2,horizontal);
        lc_surface_line(p->sf,a.x,a.y,b.x,b.y,INK(p->sc->skin.axis),1);
    }
}

static plot draw_bars(render *rr,lc_rect r,lc_rect clip,int stacked,int tight)
{
    const lc_dataset *ds;lc_chart_options *o;lc_scene *sc;plot p;ticks ts;
    size_t n,ns,i,s,last_pos,wn;const char *const *cats;const char *lab;
    double lo,hi,pos,neg,v,a,b,base,slot,group,bar_w,dx,dy,shift,gx0,x0,x1,yv,top,bottom,wa,wb,ly;
    int want,col,pin;lc_pt *whiskers;
    ds=&rr->ds;o=&rr->o;sc=rr->sc;n=rows(ds);ns=ds->series_count;cats=ds->label_count==n?ds->labels:NULL;
    lo=hi=0;
    if(stacked) {
        for(i=0;i<n;++i) {
            pos=neg=0;
            for(s=0;s<ns;++s)if(valid(ds->series+s,i)) {v=cell_at(ds,s,i);if(v>0)pos+=v;else neg+=v;}
            hi=MAX(hi,pos);lo=MIN(lo,neg);
        }
    } else {bounds(ds,&lo,&hi);whisker_bounds(rr,&lo,&hi);}
    if(lo>0)lo=0;
    if(hi<0)hi=0;
    if(o->has_lo)lo=o->lo;
    if(o->has_hi)hi=o->hi;else hi+=(hi-lo)*(o->values?0.10:0.04);
    want=MAX(3,MIN(9,r.h/3));ts=nice_range(&lo,&hi,want,o->has_lo,o->has_hi,tight);
    p=axes(rr,r,clip,lo,hi,ts,cats,n,0,0,1);if(!p.sf || !n || !ns)return p;
    base=px_y(&p,MAX(lo,MIN(hi,0.0)));slot=(double)p.pw/n;
    group=tight?slot:MAX(1.0,floor(slot*(ns>1?0.82:0.68)));
    if(!p.fine && !tight)group=MAX(1.0,MIN(slot,floor(slot)-(slot>=3?1:0)));
    bar_w=stacked?group:MAX(1.0,floor(group/ns));if(!tight)group=stacked?group:bar_w*ns;
    dx=p.fine && o->depth>0 && !tight?MIN(o->depth*3.0,MAX(2.0,bar_w*0.5)):0;dy=dx*0.7;shift=-dx/2;
    whiskers=NULL;wn=0;
    if(rr->err_valid) {size_t count;if(!lc_size_mul(n,ns,&count) || !lc_size_mul(count,2,&count)){lc_scene_fail(sc,LC_EOVERFLOW);return p;}whiskers=(lc_pt *)lc_render_array(sc,count,sizeof *whiskers);if(!whiskers)return p;}
    for(i=0;i<n;++i) {
        gx0=p.ox+i*slot+(slot-group)/2+shift;if(!tight)gx0=floor(gx0+0.5);
        if(stacked) {
            pos=neg=0;last_pos=ns;
            for(s=0;s<ns;++s)if(valid(ds->series+s,i) && cell_at(ds,s,i)>0)last_pos=s;
            for(s=0;s<ns;++s) {
                if(!valid(ds->series+s,i))continue;
                v=cell_at(ds,s,i);
                if(v==0)continue;
                a=v>0?pos:neg;b=a+v;if(v>0)pos=b;else neg=b;
                bar3d(&p,gx0,gx0+bar_w,px_y(&p,a),px_y(&p,b),fill_ink(rr,ds->series[s].color,s),ds->series[s].color,dx,dy,!shaded(o),s==last_pos);
                remember(&p,(int)s,(int)i,gx0+bar_w/2,px_y(&p,b),0,0);
            }
            if(o->values && pos>0)plot_label(&p,gx0+bar_w/2+dx/2,px_y(&p,pos)-dy-p.sy*0.6,lc_fmt_val(sc->ctx,pos+neg,o->prec),sc->skin.value,-1);
        } else for(s=0;s<ns;++s) {
            if(!valid(ds->series+s,i))continue;
            v=cell_at(ds,s,i);
            x0=gx0+s*bar_w;
            x1=x0+bar_w;
            if(p.fine && !tight && ns>1 && bar_w>=6)x1-=1;
            yv=px_y(&p,v);if(fabs(yv-base)<1 && v!=0)yv=base+(v>0?-1:1);
            col=ds->series[s].color;if(ns==1 && i<ds->label_count){pin=pinned(o,ds->labels[i],(size_t)-1);if(pin>=0)col=pin;}
            bar3d(&p,x0,x1,base,yv,fill_ink(rr,col,s),col,dx,dy,!shaded(o),1);
            remember(&p,(int)s,(int)i,(x0+x1)/2,yv,0,0);top=bottom=yv;
            if(has_whisker(rr,s,i)) {
                wa=px_y(&p,rr->err_lo[s][i]);wb=px_y(&p,rr->err_hi[s][i]);
                whiskers[wn].x=(x0+x1)/2;whiskers[wn++].y=wa;whiskers[wn].x=0;whiskers[wn++].y=wb;
                top=MIN(top,MIN(wa,wb)-(p.fine?2:0));bottom=MAX(bottom,MAX(wa,wb));
            }
            if(o->values) {
                lab=format_point(sc,ds->series+s,v,o->prec);
                if((int)lc_text_width(lab)*p.sx<=bar_w+(ns==1?slot-group:0)+(p.fine?2:0)) {
                    ly=v>=0?MIN(yv-dy,top)-p.sy*0.6:bottom+p.sy*0.6;
                    plot_label(&p,(x0+x1)/2+dx/2,ly,lab,sc->skin.value,-1);
                }
            }
        }
    }
    for(i=0;i<wn;i+=2)whisker(&p,whiskers[i].y,whiskers[i+1].y,whiskers[i].x,0);
    if(lo>0)axis_break(&p,0);
    return p;
}

static plot sideways_axes(render *rr,lc_rect r,lc_rect clip,double *lo,double *hi,int *gut)
{
    plot p;lc_scene *sc;lc_canvas *cv;lc_surface *sf;lc_chart_options *o;const lc_dataset *ds;
    size_t i,labw,n;int gx,gw,gy,gh,k;double x,len,tx;const char *lab,*s;ticks ts;
    sc=rr->sc;cv=&sc->canvas;o=&rr->o;ds=&rr->ds;plot_init(&p,sc,clip);p.across=1;
    n=rows(ds);labw=0;
    for(i=0;i<n;++i){lab=i<ds->label_count?ds->labels[i]:lc_render_uint(sc,i+1);labw=MAX(labw,lc_text_width(lab));}
    *gut=MIN((int)labw+1,MAX(4,r.w/3));
    if((int)labw+1>*gut) {
        s=lc_render_join(sc,"category labels are cut to ",lc_render_uint(sc,(size_t)(*gut-1))," characters (the longest is ");
        lc_scene_fit(sc,lc_render_join(sc,s,lc_render_uint(sc,labw),")"));
    }
    gx=r.x+*gut+1;gw=MAX(1,RIGHT(r)-gx+1-2);gy=r.y;gh=MAX(1,r.h-2-(EMPTY(o->xlabel)?0:1));
    ts=nice_range(lo,hi,MAX(3,MIN(8,gw/10)),o->has_lo,o->has_hi,0);p.lo=*lo;p.hi=*hi;
    p.cells=lc_rect_make(gx-1,gy,gw+1,gh+1);p.sf=lc_scene_surface(sc,p.cells);p.ox=p.sx;p.pw=gw*p.sx;p.ph=gh*p.sy;
    sf=p.sf;if(!sf)return p;
    p.slot_cy=(double *)lc_render_array(sc,n,sizeof(double));if(!p.slot_cy)return p;p.slot_ny=n;
    if(p.fine)lc_surface_hline(sf,p.ox-1,p.ox+p.pw-1,p.ph,INK(sc->skin.axis));
    else lc_canvas_hline(cv,gx,gy+gh,gw,glyph(sc,0x2500,'-'),sc->skin.axis);
    for(k=0;k<ts.n;++k) {
        x=px_x(&p,ts.v[k]);lab=lc_fmt_axis(sc->ctx,ts.v[k]);len=(double)lc_text_width(lab);
        if(p.fine) {
            lc_surface_vline(sf,(int)x,p.ph+1,p.ph+3,INK(sc->skin.axis));
            if(o->grid)lc_surface_dotted_v(sf,(int)x,0,p.ph-1,INK(sc->skin.grid),4);
        } else {
            lc_canvas_put(cv,(int)cellx(&p,x),gy+gh,glyph(sc,0x252c,'+'),sc->skin.axis,-1);
            if(o->grid)lc_canvas_vline(cv,(int)cellx(&p,x),gy,gh,glyph(sc,0xb7,'.'),sc->skin.grid);
        }
        tx=cellx(&p,x)-len/2;if(!p.fine)tx=floor(tx+0.5);tx=MAX(r.x,MIN(tx,RIGHT(r)+1-len));
        text(sc,tx,p.fine?gy+gh+0.45:gy+gh+1,lab,sc->skin.tick);
    }
    if(!EMPTY(o->xlabel))lc_canvas_text_c(cv,gx,BOTTOM(r),gw,o->xlabel,sc->skin.xlabel);
    return p;
}

static plot draw_hbars(render *rr,lc_rect r,lc_rect clip)
{
    plot p;lc_scene *sc;lc_surface *sf;const lc_dataset *ds;lc_chart_options *o;
    size_t n,ns,i,s;int gut,gx,gy,gh,col,pin;double lo,hi,slot,group,bar_h,zero,dx,dy,gy0,ly,v,y0,y1,xv,xa,xb,reach,wa,wb,tx;
    const char *lab,*vl;lc_ink edge;
    sc=rr->sc;ds=&rr->ds;o=&rr->o;n=rows(ds);ns=ds->series_count;plot_init(&p,sc,clip);if(!n || !ns)return p;
    bounds(ds,&lo,&hi);whisker_bounds(rr,&lo,&hi);if(lo>0)lo=0;if(hi<0)hi=0;if(o->has_lo)lo=o->lo;
    if(o->has_hi)hi=o->hi;else hi+=(hi-lo)*(o->values?0.12:0.03);
    gut=0;p=sideways_axes(rr,r,clip,&lo,&hi,&gut);sf=p.sf;if(!sf || !p.slot_cy)return p;
    gx=p.cells.x+1;gy=p.cells.y;gh=p.cells.h-1;slot=(double)p.ph/n;group=MAX(1.0,floor(slot*(ns>1?0.8:0.66)));
    if(!p.fine)group=MAX(1.0,floor(slot)-(slot>=3*p.sy?p.sy:0));
    bar_h=group/ns;zero=px_x(&p,MAX(lo,MIN(hi,0.0)));
    dx=p.fine && o->depth>0 && !shaded(o)?MIN(o->depth*3.0,MAX(2.0,bar_h*0.5)):0;dy=dx*0.7;
    if(p.fine)lc_surface_vline(sf,(int)zero-(lo>=0?1:0),0,p.ph,INK(sc->skin.axis));
    else lc_canvas_vline(&sc->canvas,gx-1,gy,gh,glyph(sc,0x2502,'|'),sc->skin.axis);
    for(i=0;i<n;++i) {
        gy0=i*slot+(slot-group)/2+dy/2;p.slot_cy[i]=gy0+group/2;
        lab=i<ds->label_count?ds->labels[i]:lc_render_uint(sc,i+1);lab=lc_text_truncate(sc->ctx,lab,(size_t)(gut-1));
        ly=celly(&p,gy0+group/2)-0.5;if(!p.fine)ly=floor(ly+0.5);
        text(sc,r.x+gut-1-(int)lc_text_width(lab),MIN(ly,gy+gh-1),lab,sc->skin.label);
        for(s=0;s<ns;++s) {
            if(!valid(ds->series+s,i))continue;
            v=cell_at(ds,s,i);
            y0=gy0+s*bar_h;
            y1=y0+bar_h;
            if(p.fine && ns>1 && bar_h>=6)y1-=1;
            xv=px_x(&p,v);xa=MIN(zero,xv);xb=MAX(zero,xv)+1;col=ds->series[s].color;
            if(ns==1 && i<ds->label_count){pin=pinned(o,ds->labels[i],(size_t)-1);if(pin>=0)col=pin;}
            if(dx>0) {
                quad(sf,xa,y0,xb,y0,xb+dx,y0-dy,xa+dx,y0-dy,lc_top_ink(col));
                quad(sf,xb,y0,xb+dx,y0-dy,xb+dx,y1-dy,xb,y1,lc_side_ink(col));
            }
            lc_surface_rect(sf,xa,y0,xb,y1,fill_ink(rr,col,s));
            if(p.fine && bar_h>=5) {
                edge=INK(panel(sc));lc_surface_hline(sf,(int)xa,(int)xb-1,(int)lc_round(y0),edge);
                lc_surface_hline(sf,(int)xa,(int)xb-1,(int)lc_round(y1)-1,edge);
                lc_surface_vline(sf,(int)xb-1,(int)lc_round(y0),(int)lc_round(y1)-1,edge);
                if(dx>0) {
                    lc_surface_line(sf,xa,y0,xa+dx,y0-dy,edge,1);lc_surface_line(sf,xa+dx,y0-dy,xb+dx,y0-dy,edge,1);
                    lc_surface_line(sf,xb+dx,y0-dy,xb+dx,y1-dy,edge,1);lc_surface_line(sf,xb-1,y0,xb+dx-1,y0-dy,edge,1);
                }
            } else if(!p.fine && !shaded(o) && bar_h>=2 && xb-xa>=3)lc_surface_rect(sf,xa,y1-1,xb,y1,lc_side_ink(col));
            remember(&p,(int)s,(int)i,xv+dx,(y0+y1)/2-dy/2,1,0);reach=xb+dx;
            if(has_whisker(rr,s,i)) {
                wa=px_x(&p,rr->err_lo[s][i]);wb=px_x(&p,rr->err_hi[s][i]);whisker(&p,wa,wb,(y0+y1)/2,1);
                reach=MAX(reach,MAX(wa,wb)+(p.fine?6:1));
            }
            if(o->values) {
                vl=format_point(sc,ds->series+s,v,o->prec);tx=cellx(&p,reach)+0.6;if(!p.fine)tx=ceil(tx);
                if(tx+lc_text_width(vl)<=RIGHT(clip)+1)plot_put(&p,tx,celly(&p,(y0+y1)/2-dy/2)-0.5,vl,sc->skin.value,-1,-1);
            }
        }
    }
    if(lo>0)axis_break(&p,1);
    return p;
}

static plot draw_dumbbell(render *rr,lc_rect r,lc_rect clip)
{
    plot p;lc_scene *sc;lc_surface *sf;const lc_dataset *ds;lc_chart_options *o;
    size_t n,ns,i,s;int gut,gy,gh,col,any,negative,fg;double lo,hi,pad,slot,rad,cy,ly,first,last,a,b,v,x,x0,x1,dir,head,tip;
    const char *lab,*vl;lc_ink bar;lc_pt tri[3];
    sc=rr->sc;ds=&rr->ds;o=&rr->o;n=rows(ds);ns=ds->series_count;plot_init(&p,sc,clip);if(!n || !ns)return p;
    bounds(ds,&lo,&hi);negative=lo<0;pad=MAX((hi-lo)*0.06,fabs(hi)*1e-3+1e-9);lo-=pad;hi+=pad;
    if(lo<0 && !negative)lo=0;
    if(o->has_lo)lo=o->lo;
    if(o->has_hi)hi=o->hi;
    gut=0;p=sideways_axes(rr,r,clip,&lo,&hi,&gut);sf=p.sf;if(!sf || !p.slot_cy)return p;
    gy=p.cells.y;gh=p.cells.h-1;slot=(double)p.ph/n;rad=p.fine?MAX(3.0,MIN(7.0,slot*0.22)):1;bar=INK(p.fine?7:8);
    for(i=0;i<n;++i) {
        cy=floor(i*slot+slot/2);p.slot_cy[i]=cy;
        lab=lc_text_truncate(sc->ctx,i<ds->label_count?ds->labels[i]:lc_render_uint(sc,i+1),(size_t)(gut-1));
        ly=celly(&p,cy)-0.5;if(!p.fine)ly=floor(ly+0.5);text(sc,r.x+gut-1-(int)lc_text_width(lab),MIN(ly,gy+gh-1),lab,sc->skin.label);
        first=last=0;any=0;a=CHART_SENTINEL;b=-CHART_SENTINEL;
        for(s=0;s<ns;++s)if(valid(ds->series+s,i)) {
            v=cell_at(ds,s,i);if(!any){first=v;any=1;}last=v;a=MIN(a,px_x(&p,v));b=MAX(b,px_x(&p,v));
        }
        if(!any)continue;
        if(b>a)lc_surface_line(sf,a,cy,b,cy,bar,p.fine?3:1);
        x0=px_x(&p,first);x1=px_x(&p,last);dir=x1>x0?1:-1;head=p.fine?rad+7:2;
        if(p.fine && fabs(x1-x0)>rad*2+head) {
            tip=x1-dir*(rad+1);tri[0].x=tip;tri[0].y=cy;tri[1].x=tri[2].x=tip-dir*8;tri[1].y=cy-5;tri[2].y=cy+5;
            lc_surface_poly(sf,tri,3,bar);
        }
        for(s=0;s<ns;++s) {
            if(!valid(ds->series+s,i))continue;
            v=cell_at(ds,s,i);
            x=px_x(&p,v);
            col=ds->series[s].color;
            if(p.fine) {lc_surface_marker(sf,x,cy,(int)s,rad+1.5,INK(panel(sc)));lc_surface_marker(sf,x,cy,(int)s,rad,INK(col));}
            else lc_surface_rect(sf,x-1,cy,x+1,cy+1,INK(col));
            remember(&p,(int)s,(int)i,x,cy,1,0);
            if(!o->values || (b-a<1 && v!=last))continue;
            vl=format_point(sc,ds->series+s,v,o->prec);
            fg=ns==1?sc->skin.value:col;
            if(x<=a+0.5 && ns>1 && b>a)plot_put(&p,cellx(&p,x-rad)-0.6,celly(&p,cy)-0.5,vl,fg,1,-1);
            else if(x>=b-0.5)plot_put(&p,cellx(&p,x+rad)+0.6,celly(&p,cy)-0.5,vl,fg,-1,-1);
            else plot_put(&p,cellx(&p,x),celly(&p,cy-rad)-1.2,vl,fg,0,-1);
        }
    }
    return p;
}

static plot draw_lines(render *rr,lc_rect r,lc_rect clip,int area)
{
    plot p;lc_scene *sc;const lc_dataset *ds;lc_chart_options *o;const lc_series *se;
    const char *const *cats;size_t n,ns,s,i,k,j,np,*order,tmp,*idx;lc_pt *pts;
    double lo,hi,pad,base,mr,v,nx,ny;int pass,width,c;ticks ts;
    sc=rr->sc;ds=&rr->ds;o=&rr->o;n=rows(ds);ns=ds->series_count;cats=ds->label_count==n?ds->labels:NULL;
    bounds(ds,&lo,&hi);whisker_bounds(rr,&lo,&hi);
    if(area && o->zero_base){if(lo>0)lo=0;if(hi<0)hi=0;}
    pad=(hi-lo)*0.05;if(!(area && lo==0) && lo!=0)lo-=(lo>0 && lo-pad<0)?lo:pad;
    hi+=pad*(o->values?2:1);if(o->has_lo)lo=o->lo;if(o->has_hi)hi=o->hi;
    ts=nice_range(&lo,&hi,MAX(3,MIN(9,r.h/3)),o->has_lo,o->has_hi,0);
    p=axes(rr,r,clip,lo,hi,ts,cats,n,0,0,1);if(!p.sf || !n || !ns)return p;
    base=px_y(&p,MAX(lo,MIN(hi,0.0)));width=p.fine?(p.ph>160?3:2):1;mr=p.fine?(n>40?2.5:4):0;
    order=(size_t *)lc_render_array(sc,ns,sizeof *order);pts=(lc_pt *)lc_render_array(sc,n+2,sizeof *pts);idx=(size_t *)lc_render_array(sc,n,sizeof *idx);
    if(!order || !pts || !idx)return p;
    for(s=0;s<ns;++s)order[s]=s;
    if(area)for(i=1;i<ns;++i) {
        tmp=order[i];nx=sum(ds->series+tmp)/MAX(1.0,(double)ds->series[tmp].count);j=i;
        while(j>0) {ny=sum(ds->series+order[j-1])/MAX(1.0,(double)ds->series[order[j-1]].count);if(!(nx>ny))break;order[j]=order[j-1];--j;}
        order[j]=tmp;
    }
    for(pass=area?0:1;pass<2;++pass)for(j=0;j<ns;++j) {
        s=order[j];se=ds->series+s;c=se->color;np=0;
        for(i=0;i<n;++i)if(valid(se,i)) {
            v=cell_at(ds,s,i);pts[np].x=p.ox+(i+0.5)*p.pw/n;pts[np].y=px_y(&p,v);idx[np++]=i;
        }
        if(!np)continue;
        if(pass==0) {
            pts[np].x=pts[np-1].x;pts[np].y=base;pts[np+1].x=pts[0].x;pts[np+1].y=base;
            lc_surface_poly(p.sf,pts,np+2,shaded(o)?fill_ink(rr,c,s):lc_ink_make(c,panel(sc),2));continue;
        }
        for(k=0;k<np;++k){i=idx[k];if(has_whisker(rr,s,i))whisker(&p,px_y(&p,rr->err_lo[s][i]),px_y(&p,rr->err_hi[s][i]),pts[k].x,0);}
        for(k=0;k+1<np;++k)lc_surface_line(p.sf,pts[k].x,pts[k].y,pts[k+1].x,pts[k+1].y,INK(c),width);
        for(k=0;k<np;++k) {
            if(p.fine){lc_surface_marker(p.sf,pts[k].x,pts[k].y,(int)s,mr+1,INK(panel(sc)));lc_surface_marker(p.sf,pts[k].x,pts[k].y,(int)s,mr,INK(c));}
            remember(&p,(int)s,(int)idx[k],pts[k].x,pts[k].y,0,0);
            if(o->values && (ns==1 || n<=8))plot_label(&p,pts[k].x,pts[k].y-p.sy*0.75-mr,format_point(sc,se,cell_at(ds,s,idx[k]),o->prec),ns==1?sc->skin.value:c,-1);
        }
    }
    return p;
}

static plot draw_scatter(render *rr,lc_rect r,lc_rect clip)
{
    plot p;lc_scene *sc;const lc_dataset *ds;lc_chart_options *o;const lc_series *xs;
    size_t n,start,s,i;double xlo,xhi,ylo,yhi,x,y,padx,pady,mr,px,py;int c;ticks ts;
    sc=rr->sc;ds=&rr->ds;o=&rr->o;n=rows(ds);xs=NULL;start=0;
    if(ds->series_count>=2 && (hidden(ds->series)||o->xy)){xs=ds->series;start=1;}
    xlo=ylo=CHART_SENTINEL;xhi=yhi=-CHART_SENTINEL;
    for(s=start;s<ds->series_count;++s)for(i=0;i<n;++i) {
        if(!valid(ds->series+s,i) || (xs && !valid(xs,i)))continue;
        y=cell_at(ds,s,i);x=xs?xs->values[i]:(double)(i+1);
        xlo=MIN(xlo,x);xhi=MAX(xhi,x);ylo=MIN(ylo,y);yhi=MAX(yhi,y);
    }
    if(xhi<xlo){xlo=ylo=0;xhi=yhi=1;}
    whisker_bounds(rr,&ylo,&yhi);padx=(xhi-xlo)*0.04;pady=(yhi-ylo)*0.06;
    /* data spanning most of the double range: no padding rather than infinity */
    if(!lc_finite(xlo-padx) || !lc_finite(xhi+padx))padx=0;
    if(!lc_finite(ylo-pady) || !lc_finite(yhi+pady))pady=0;
    xlo-=padx;xhi+=padx;ylo-=pady;yhi+=pady;
    if(o->has_lo)ylo=o->lo;
    if(o->has_hi)yhi=o->hi;
    if(!(xhi>xlo))xhi=xlo+1;
    ts=nice_range(&ylo,&yhi,MAX(3,MIN(9,r.h/3)),o->has_lo,o->has_hi,0);
    p=axes(rr,r,clip,ylo,yhi,ts,NULL,0,1,xlo,xhi);if(!p.sf)return p;mr=p.fine?(n>60?2:3.5):0;
    for(s=start;s<ds->series_count;++s) {
        c=ds->series[s].color;
        for(i=0;i<n;++i) {
            if(!valid(ds->series+s,i) || (xs && !valid(xs,i)))continue;
            y=cell_at(ds,s,i);x=xs?xs->values[i]:(double)(i+1);px=(x-xlo)/(xhi-xlo);
            if(!lc_finite(px))px=(x/2-xlo/2)/(xhi/2-xlo/2);
            px=p.ox+px*(p.pw-1);py=px_y(&p,y);
            if(has_whisker(rr,s,i))whisker(&p,px_y(&p,rr->err_lo[s][i]),px_y(&p,rr->err_hi[s][i]),px,0);
            if(p.fine){lc_surface_marker(p.sf,px,py,(int)(s-start),mr+1,INK(panel(sc)));lc_surface_marker(p.sf,px,py,(int)(s-start),mr,INK(c));}
            else lc_surface_set(p.sf,lc_clamp_int(px,INT_MIN,INT_MAX),lc_clamp_int(MIN(py,p.ph-1),INT_MIN,INT_MAX),INK(c));
            remember(&p,(int)s,(int)i,px,py,0,0);
        }
    }
    return p;
}

typedef struct slice { const char *name;double val,a0,a1,cx,cy;int color,row; } slice;
typedef struct pie_geometry { slice *s;size_t n;int explode;double radius,ry,cx,cy,hole; } pie_geometry;
static int in_ring(const pie_geometry *g,int x,int y,double ox,double oy,double *fraction)
{
    double dx,dy,rr;
    dx=(x+0.5-ox)/g->radius;dy=(y+0.5-oy)/g->ry;rr=dx*dx+dy*dy;
    if(rr>1 || rr<g->hole*g->hole)return 0;
    *fraction=(atan2(dy,dx)+PI/2)/(2*PI);if(*fraction<0)*fraction+=1;return 1;
}
static int pie_probe(const pie_geometry *g,int x,int y)
{
    double f;const slice *e;size_t k;
    f=0;
    if(g->explode>=0){e=g->s+g->explode;if(in_ring(g,x,y,e->cx,e->cy,&f) && f>=e->a0 && f<e->a1)return g->explode;}
    if(!in_ring(g,x,y,g->cx,g->cy,&f))return -1;
    for(k=0;k<g->n;++k)if(f>=g->s[k].a0 && f<g->s[k].a1)return (int)k==g->explode?-1:(int)k;
    return -1;
}
/* Crossings of row y with the ring edges around one centre, and with the
   slice boundary rays inside them, in pixel x: what that centre's in_ring
   and slice tests depend on can change only at these. A row clear of the
   ring has none, and a ray outside it changes nothing. The ray at a = 0
   points straight up, where the angle wraps. */
static size_t row_crossings(const pie_geometry *g,const double *ray_cos,const double *ray_sin,
                            double ox,double oy,int y,double *out)
{
    double dy,e,c;size_t k,m;
    m=0;dy=(y+0.5-oy)/g->ry;
    if(!(dy*dy<=1))return 0;
    e=g->radius*sqrt(1-dy*dy);out[m++]=ox-e;out[m++]=ox+e;
    if(g->hole>0 && dy*dy<g->hole*g->hole){c=g->radius*sqrt(g->hole*g->hole-dy*dy);out[m++]=ox-c;out[m++]=ox+c;}
    for(k=0;k<=g->n;++k)if(ray_sin[k]*dy>0){c=ox+g->radius*dy*ray_cos[k]/ray_sin[k];if(fabs(c-ox)<=e+1)out[m++]=c;}
    return m;
}
/* pie_probe for every pixel of row y. A probe per pixel is an atan2 and a
   dozen double operations; without an FPU each is a software routine, and a
   pie took minutes on a 386SX. The answer is constant between crossings, so
   pixels within a pixel of one get the exact probe and every run between
   them takes the probe of its first pixel: the same pixels, a few probes. */
static void pie_row(const pie_geometry *g,const double *ray_cos,const double *ray_sin,
                    double *cross,unsigned char *near,int w,int y,int *own)
{
    size_t m,k;int x,v,f,exact;double c;
    m=row_crossings(g,ray_cos,ray_sin,g->cx,g->cy,y,cross);
    if(g->explode>=0)m+=row_crossings(g,ray_cos,ray_sin,g->s[g->explode].cx,g->s[g->explode].cy,y,cross+m);
    exact=fabs(y+0.5-g->cy)<1.5 || (g->explode>=0 && fabs(y+0.5-g->s[g->explode].cy)<1.5);
    memset(near,exact,(size_t)w);
    for(k=0;k<m && !exact;++k) {
        c=cross[k];
        if(!lc_finite(c)){memset(near,1,(size_t)w);break;}
        if(c<-2 || c>w+2)continue;
        /* the two pixels whose centres lie within a pixel of it */
        f=(int)floor(c+0.5);for(x=f-1;x<=f;++x)if(x>=0 && x<w)near[x]=1;
    }
    v=-1;
    for(x=0;x<w;++x){if(near[x] || !x || near[x-1])v=pie_probe(g,x,y);own[x]=v;}
}
static int owner(const int *own,int w,int h,int x,int y)
{ return x<0 || y<0 || x>=w || y>=h ? -1 : own[(size_t)y*w+x]; }
static const char *percent(lc_scene *sc,double frac,int prec)
{ return lc_render_join(sc,lc_fmt_val(sc->ctx,frac*100,prec),"%",""); }
static plot draw_pie(render *rr,lc_rect r,lc_rect clip,int solid,double hole)
{
    plot p;lc_scene *sc;lc_surface *sf;const lc_dataset *ds;lc_chart_options *o;const lc_series *se;
    slice *sl;legend_entry *leg;pie_geometry g;lc_rect area;lc_ink edge;
    size_t cap,n,i,k,pixels;double total,v,acc,best,asp,tilt,dp,margin,room_x,room_y,radius,ry,cx,cy,ang,lr,frac,px,py;
    int by_row,pin,explode,want,legw,ly,nrows,w,h,x,y,idp,dz,s,ws,rgt,dn,rim,right,c,*own,*wall;
    double *ray_cos,*ray_sin,*cross;unsigned char *near;
    const char *label;
    sc=rr->sc;ds=&rr->ds;o=&rr->o;plot_init(&p,sc,clip);
    by_row=ds->series_count==1 || (ds->series_count==2 && hidden(ds->series));
    cap=by_row?ds->series[ds->series_count-1].count:ds->series_count;
    sl=(slice *)lc_render_array(sc,cap,sizeof *sl);if(!sl)return p;n=0;total=0;
    if(by_row) {
        se=ds->series+ds->series_count-1;
        for(i=0;i<se->count;++i)if(valid(se,i) && se->values[i]>0) {
            sl[n].name=i<ds->label_count?ds->labels[i]:lc_render_uint(sc,i+1);sl[n].val=se->values[i];sl[n].row=(int)i;total+=sl[n++].val;
        }
        for(i=0;i<n;++i){pin=pinned(o,sl[i].name,i);sl[i].color=pin>=0?pin:lc_chart_palette(sc,o->palette,i,n);}
    } else for(k=0;k<ds->series_count;++k) {
        v=0;se=ds->series+k;
        for(i=0;i<se->count;++i)if(valid(se,i) && se->values[i]>0)v+=se->values[i];
        if(v<=0 || hidden(se))continue;
        sl[n].name=se->name;sl[n].val=v;sl[n].color=se->color;sl[n].row=(int)k;++n;total+=v;
    }
    if(!n || total<=0){lc_canvas_text_c(&sc->canvas,r.x,r.y+r.h/2,r.w,"(no positive values for a pie)",sc->skin.subtitle);return p;}
    acc=0;for(i=0;i<n;++i){sl[i].a0=acc;acc+=sl[i].val/total;sl[i].a1=acc;}sl[n-1].a1=1;
    explode=-1;want=o->cur_index>=0?-3:o->explode;
    if(want==-1){best=-1;for(i=0;i<n;++i)if(sl[i].val>best){best=sl[i].val;explode=(int)i;}}
    else if(want==-3){for(i=0;i<n;++i)if(sl[i].row==o->cur_index)explode=(int)i;}
    else if(want>=0){for(i=0;i<n;++i)if(sl[i].row==want)explode=(int)i;}
    leg=(legend_entry *)lc_render_array(sc,n,sizeof *leg);if(!leg)return p;
    for(i=0;i<n;++i) {
        leg[i].name=sl[i].name;leg[i].color=sl[i].color;leg[i].level=shaded(o)?(int)(i%4):0;
        leg[i].value=lc_render_join(sc,lc_fmt_val(sc->ctx,sl[i].val,o->prec),"  ",percent(sc,sl[i].val/total,1));
    }
    area=r;
    if(o->legend) {
        legw=legend_width(leg,n)+1;
        if(r.w-legw>=22 && (int)n<=r.h) {
            ly=r.y+MAX(0,(r.h-(int)n)/2);legend_draw(sc,lc_rect_make(RIGHT(r)-legw+1,ly,legw,r.h-(ly-r.y)),leg,n,1);area.w=r.w-legw-2;
        } else {
            for(i=0;i<n;++i)leg[i].value=percent(sc,sl[i].val/total,0);
            nrows=MIN(MAX(1,r.h/3),legend_rows(leg,n,r.w));legend_draw(sc,lc_rect_make(r.x,BOTTOM(r)-nrows+1,r.w,nrows),leg,n,0);area.h=r.h-nrows-1;
        }
    }
    if(area.w<8 || area.h<4)return p;
    p.cells=p.clip=area;p.sf=lc_scene_surface(sc,area);sf=p.sf;if(!sf)return p;
    p.pw=sf->w;p.ph=sf->h;asp=lc_surface_aspect(sf);tilt=solid?0.56:1;dp=0;
    if(solid && o->depth>0)dp=p.fine?o->depth*9.0:MAX(1.0,o->depth*(p.sy==2?1.0:0.5));
    margin=p.fine?6:1;room_x=sf->w/2.0-margin;room_y=(sf->h-dp)/2.0-margin/asp;
    radius=MIN(room_x,room_y*asp/tilt);if(explode>=0)radius/=1.12;if(radius<2)return p;
    ry=radius*tilt/asp;cx=sf->w/2.0;cy=(sf->h-dp)/2.0;
    for(i=0;i<n;++i){sl[i].cx=cx;sl[i].cy=cy;if((int)i==explode){ang=(sl[i].a0+sl[i].a1)*PI-PI/2;sl[i].cx+=cos(ang)*radius*0.13;sl[i].cy+=sin(ang)*ry*0.13;}}
    g.s=sl;g.n=n;g.explode=explode;g.radius=radius;g.ry=ry;g.cx=cx;g.cy=cy;g.hole=hole;
    w=sf->w;h=sf->h;if(!lc_size_mul((size_t)w,(size_t)h,&pixels)){lc_scene_fail(sc,LC_EOVERFLOW);return p;}
    own=(int *)lc_render_array(sc,pixels,sizeof *own);wall=(int *)lc_render_array(sc,pixels,sizeof *wall);if(!own || !wall)return p;
    ray_cos=(double *)lc_render_array(sc,n+1,sizeof *ray_cos);ray_sin=(double *)lc_render_array(sc,n+1,sizeof *ray_sin);
    cross=(double *)lc_render_array(sc,2*(n+5),sizeof *cross);near=(unsigned char *)lc_render_array(sc,(size_t)w,1);
    if(!ray_cos || !ray_sin || !cross || !near)return p;
    for(i=0;i<=n;++i){ang=(i<n?sl[i].a0:1)*2*PI-PI/2;ray_cos[i]=cos(ang);ray_sin[i]=sin(ang);}
    for(y=0;y<h;++y){pie_row(&g,ray_cos,ray_sin,cross,near,w,y,own+(size_t)y*w);for(x=0;x<w;++x)wall[(size_t)y*w+x]=-1;}
    idp=(int)lc_round(dp);
    if(idp>0)for(y=0;y<h;++y)for(x=0;x<w;++x) {
        if(owner(own,w,h,x,y)>=0)continue;
        for(dz=1;dz<=idp;++dz){s=owner(own,w,h,x,y-dz);if(s>=0){wall[(size_t)y*w+x]=s;break;}}
    }
    edge=INK(panel(sc));
    for(y=0;y<h;++y)for(x=0;x<w;++x) {
        s=own[(size_t)y*w+x];ws=wall[(size_t)y*w+x];
        if(s>=0) {
            rgt=owner(own,w,h,x+1,y);dn=owner(own,w,h,x,y+1);rim=dn<0 && y+1<h && wall[(size_t)(y+1)*w+x]>=0;
            if(p.fine && ((rgt>=0 && rgt!=s)||(dn>=0 && dn!=s)||rim))lc_surface_set(sf,x,y,edge);
            else lc_surface_set(sf,x,y,fill_ink(rr,sl[s].color,(size_t)s));
        } else if(ws>=0) {
            right=x+1<w?wall[(size_t)y*w+x+1]:-1;
            if(p.fine && right>=0 && right!=ws)lc_surface_set(sf,x,y,edge);
            else if(shaded(o))lc_surface_set(sf,x,y,lc_ink_make(15,0,3));
            else lc_surface_set(sf,x,y,lc_side_ink(sl[ws].color));
        }
    }
    lr=hole>0?(1+hole)/2:0.64;
    for(i=0;i<n;++i) {
        frac=sl[i].val/total;ang=(sl[i].a0+sl[i].a1)*PI-PI/2;px=sl[i].cx+cos(ang)*radius*lr;py=sl[i].cy+sin(ang)*ry*lr;
        remember(&p,by_row?-1:sl[i].row,by_row?sl[i].row:-1,sl[i].cx+cos(ang)*radius*0.9,sl[i].cy+sin(ang)*ry*0.9,1,ang);
        if(frac<(p.fine?0.04:0.07) || radius<(p.fine?40:8))continue;
        label=percent(sc,frac,0);c=sl[i].color;
        if(shaded(o))plot_label(&p,px,py,label,15,0);else plot_label(&p,px,py,label,lc_contrast_on(c),p.fine?-1:c);
    }
    return p;
}

typedef struct table_column { const char *head;int order,w,dec;const lc_series *s;const lc_text_column *t; } table_column;
static const char *table_cell(render *rr,const table_column *c,size_t i)
{
    if(c->t)return i<c->t->count?c->t->values[i]:"";
    return valid(c->s,i)?lc_fmt_val(rr->sc->ctx,c->s->values[i],rr->o.prec>=0?rr->o.prec:c->dec):"-";
}
static const char *row_label(render *rr,size_t i)
{ return i<rr->ds.label_count?rr->ds.labels[i]:lc_render_uint(rr->sc,i+1); }
static int table_width(table_column *cols,size_t shown,int w0)
{ size_t i;for(i=0;i<shown;++i)w0+=cols[i].w+2;return w0; }
static void table_put(render *rr,const table_column *c,int x,int y,const char *s,int fg)
{
    const char *t;t=lc_text_truncate(rr->sc->ctx,s,(size_t)c->w);
    if(c->t)lc_canvas_text(&rr->sc->canvas,x+2,y,t,fg,-1);else lc_canvas_text_r(&rr->sc->canvas,x+2,y,c->w,t,fg);
}
static void draw_table(render *rr,lc_rect r)
{
    lc_scene *sc;const lc_dataset *ds;table_column *cols,tmp;size_t nc,n,i,j,k,shown,show;
    int w0,d,x0,x,y,room,width;double v,m;const char *s;
    sc=rr->sc;ds=&rr->ds;n=rows(ds);nc=ds->series_count+ds->text_count;
    cols=(table_column *)lc_render_array(sc,nc,sizeof *cols);if(!cols || !n || !nc)return;
    for(i=0;i<ds->series_count;++i) {
        cols[i].s=ds->series+i;cols[i].head=hidden(cols[i].s)?TXT(ds->x_name):TXT(cols[i].s->name);
        cols[i].order=cols[i].s->col>0?cols[i].s->col:INT_MAX;cols[i].dec=cols[i].s->decimals;
    }
    for(i=0;i<ds->text_count;++i) {
        j=ds->series_count+i;cols[j].t=ds->text+i;cols[j].head=TXT(cols[j].t->name);cols[j].order=cols[j].t->col>0?cols[j].t->col:INT_MAX;cols[j].dec=-1;
    }
    for(i=1;i<nc;++i){tmp=cols[i];j=i;while(j>0 && cols[j-1].order>tmp.order){cols[j]=cols[j-1];--j;}cols[j]=tmp;}
    for(i=0;i<nc;++i) {
        if(!cols[i].s || cols[i].s->decimals>=0 || rr->o.prec>=0)continue;
        d=0;
        for(j=0;j<cols[i].s->count;++j)if(valid(cols[i].s,j)) {
            v=cols[i].s->values[j];m=pow(10.0,d);while(d<3 && fabs(v*m-lc_round(v*m))>1e-6){++d;m=pow(10.0,d);}
        }
        cols[i].dec=d;
    }
    w0=(int)lc_text_width(ds->label_name);for(i=0;i<n;++i)w0=MAX(w0,(int)lc_text_width(row_label(rr,i)));w0=MIN(w0,20);
    for(i=0;i<nc;++i){cols[i].w=MAX(3,(int)lc_text_width(cols[i].head));for(j=0;j<n;++j)cols[i].w=MAX(cols[i].w,(int)lc_text_width(table_cell(rr,cols+i,j)));cols[i].w=MIN(cols[i].w,cols[i].t?24:16);}
    shown=nc;while(shown>1 && table_width(cols,shown,w0)>r.w)--shown;
    width=table_width(cols,shown,w0);x0=r.x+MAX(0,(r.w-width)/2);y=r.y;x=x0+w0;
    lc_canvas_text(&sc->canvas,x0,y,lc_text_truncate(sc->ctx,ds->label_name,(size_t)w0),sc->skin.table_head,-1);
    for(i=0;i<shown;++i){table_put(rr,cols+i,x,y,cols[i].head,sc->skin.table_head);x+=cols[i].w+2;}
    ++y;lc_canvas_hline(&sc->canvas,x0,y,MIN(width,r.w),glyph(sc,0x2500,'-'),sc->skin.table_rule);++y;
    room=BOTTOM(r)-y+1;show=n;if((int)n>room)show=(size_t)MAX(0,room-1);
    for(i=0;i<show;++i,++y) {
        lc_canvas_text(&sc->canvas,x0,y,lc_text_truncate(sc->ctx,row_label(rr,i),(size_t)w0),sc->skin.label,-1);x=x0+w0;
        for(k=0;k<shown;++k){table_put(rr,cols+k,x,y,table_cell(rr,cols+k,i),sc->skin.table_row);x+=cols[k].w+2;}
    }
    if(show<n) {
        lc_canvas_text(&sc->canvas,x0,y,lc_render_join(sc,"... ",lc_render_uint(sc,n-show)," more rows"),sc->skin.subtitle,-1);
        s=lc_render_join(sc,"the table shows ",lc_render_uint(sc,show)," of ");s=lc_render_join(sc,s,lc_render_uint(sc,n)," rows; give it more height or split it");lc_scene_fit(sc,s);
    }
    if(shown<nc) {
        lc_canvas_text_r(&sc->canvas,r.x,r.y,r.w,lc_render_join(sc,"+",lc_render_uint(sc,nc-shown)," cols"),sc->skin.subtitle);
        s=lc_render_join(sc,"the table shows ",lc_render_uint(sc,shown)," of ");s=lc_render_join(sc,s,lc_render_uint(sc,nc)," columns; give it more width or use series_col");lc_scene_fit(sc,s);
    }
}

static plot draw_hist(render *rr,lc_rect r,lc_rect clip)
{
    const lc_series *s;lc_series hs;render hr;plot p;lc_dataset h;
    size_t i,cnt;double lo,hi,v,half,step,start,need,f,edge,*values;const char **labels;int want,bins,b;
    s=rr->ds.series+(hidden(rr->ds.series) && rr->ds.series_count>1?1:0);lo=CHART_SENTINEL;hi=-CHART_SENTINEL;cnt=0;
    for(i=0;i<s->count;++i)if(valid(s,i)){v=s->values[i];lo=MIN(lo,v);hi=MAX(hi,v);++cnt;}
    plot_init(&p,rr->sc,clip);if(!cnt)return p;if(!(hi>lo))hi=lo+1;
    want=MAX(1,MIN(60,rr->o.bins));half=hi/2-lo/2;step=lc_nice_step(half/want);
    if(!(step>0) || !lc_finite(step))step=half/want;
    start=floor(lo/2/step)*step;
    if(!lc_finite(start))start=lo/2;
    need=ceil((hi/2-start)/step-1e-9);bins=lc_finite(need)?(int)MAX(1.0,MIN(60.0,need)):want;
    if(bins<60 && start+bins*step<=hi/2)++bins;
    values=(double *)lc_render_array(rr->sc,(size_t)bins,sizeof *values);labels=(const char **)lc_render_array(rr->sc,(size_t)bins,sizeof *labels);
    if(!values || !labels)return p;
    for(i=0;i<s->count;++i)if(valid(s,i)) {
        f=(s->values[i]/2-start)/step;if(!lc_finite(f))f=0;b=MAX(0,MIN(bins-1,(int)MAX(0.0,MIN(bins,floor(f)))));values[b]+=1;
    }
    for(b=0;b<bins;++b){edge=(start+b*step)*2;labels[b]=lc_fmt_axis(rr->sc->ctx,lc_finite(edge)?edge:start+b*step);}
    memset(&h,0,sizeof h);memset(&hs,0,sizeof hs);hs.name="count";hs.color=s->color;hs.values=values;hs.count=(size_t)bins;hs.decimals=-1;
    h.labels=labels;h.label_count=(size_t)bins;h.series=&hs;h.series_count=1;
    hr=*rr;hr.ds=h;hr.o.has_lo=hr.o.has_hi=0;hr.o.cur_index=hr.o.cur_series=-1;
    if(EMPTY(hr.o.xlabel))hr.o.xlabel=s->name;
    if(EMPTY(hr.o.ylabel))hr.o.ylabel="count";
    return draw_bars(&hr,r,clip,0,1);
}

static int note_row(const lc_dataset *ds,const lc_annotation *a)
{
    size_t i;
    if(!EMPTY(a->label))for(i=0;i<ds->label_count;++i)if(lc_name_eq(ds->labels[i],a->label))return (int)i;
    return a->index>=0 && (size_t)a->index<rows(ds)?a->index:-1;
}
static int note_series(const lc_dataset *ds,const lc_annotation *a)
{
    size_t i;if(!EMPTY(a->series))for(i=0;i<ds->series_count;++i)if(lc_name_eq(ds->series[i].name,a->series))return (int)i;return a->series_i;
}
static void note_box(lc_scene *sc,double x,double y,lc_lines lines,int fg,int bg)
{
    size_t i,w,n,len;char *s;w=0;
    for(i=0;i<lines.n;++i)w=MAX(w,lc_text_width(lines.v[i]));
    for(i=0;i<lines.n;++i) {
        len=strlen(TXT(lines.v[i]));n=w-lc_text_width(lines.v[i]);
        if(len>(size_t)-1-n-3){lc_scene_fail(sc,LC_EOVERFLOW);return;}
        s=(char *)lc_render_array(sc,len+n+3,1);if(!s)return;
        s[0]=' ';memcpy(s+1,TXT(lines.v[i]),len);memset(s+1+len,' ',n+1);s[len+n+2]=0;
        lc_scene_text(sc,x,y+i,s,fg,bg,1);
    }
}
static double box_penalty(box b,const box *placed,size_t n,double weight)
{ size_t i;double v;v=0;for(i=0;i<n;++i)if(hit(b,placed[i]))v+=weight;return v; }
static void draw_notes(render *rr,plot *p,int pie)
{
    lc_scene *sc;lc_surface *sf;const lc_dataset *ds;const lc_annotation *a;const anchor *an;
    box *placed,b,t,pick,best;size_t placed_n,i,j,tw;lc_lines lines;
    int fg,bg,at,row,up,c,x,y,k,ser,sideways;double score,best_score,xv,ax,ay,ux,uy,bx,by,lx,ly;
    char *end;
    static const double offsets[12][2]={{1,-1},{-1,-1},{0,-1},{1,-2.2},{-1,-2.2},{0,-2.2},{1.6,-1.6},{-1.6,-1.6},{1,1},{-1,1},{0,1},{1,-3.4}};
    if(!p->sf)return;
    sc=rr->sc;
    sf=p->sf;
    ds=&rr->ds;
    placed=(box *)lc_render_array(sc,rr->o.note_count,sizeof *placed);if(!placed)return;placed_n=0;
    for(i=0;i<rr->o.note_count;++i) {
        a=rr->o.notes+i;fg=sc->skin.note_fg;bg=sc->skin.note_bg;if(a->color>=0){bg=a->color;fg=lc_contrast_on(bg);}
        lines=lc_render_wrap(sc,a->text,24);tw=0;for(j=0;j<lines.n;++j)tw=MAX(tw,lc_text_width(lines.v[j]));
        memset(&b,0,sizeof b);b.w=(double)tw+2;b.h=(double)lines.n;
        if(p->across && (a->kind==1 || a->kind==2)) {
            at=-1;
            if(a->kind==1){if(a->value<p->lo || a->value>p->hi)continue;at=(int)lc_round(px_x(p,a->value));}
            else {row=note_row(ds,a);if(row<0 || (size_t)row>=p->slot_ny)continue;at=(int)p->slot_cy[row];}
            up=a->kind==1;c=a->color>=0?a->color:sc->skin.note_bg;
            for(k=0;k<(up?p->ph:p->pw);++k)if(!p->fine || (k/6)%2==0) {
                if(up){lc_surface_set(sf,at,k,INK(c));if(p->fine)lc_surface_set(sf,at+1,k,INK(c));}
                else{lc_surface_set(sf,p->ox+k,at,INK(c));if(p->fine)lc_surface_set(sf,p->ox+k,at-1,INK(c));}
            }
            if(!EMPTY(a->text)) {
                if(up){b.x=cellx(p,at)+0.5;if(b.x+b.w>RIGHT(p->clip)+1)b.x=cellx(p,at)-b.w-0.5;b.y=p->cells.y;}
                else{b.x=cellx(p,p->ox+p->pw)-b.w;b.y=celly(p,at)-b.h-(p->fine?0.15:0);if(b.y<p->clip.y)b.y=celly(p,at)+0.2;}
                if(!p->fine){b.x=floor(b.x+0.5);b.y=floor(b.y+0.5);}note_box(sc,b.x,b.y,lines,fg,bg);placed[placed_n++]=b;
            }
            continue;
        }
        if(a->kind==1 && !pie) {
            if(a->value<p->lo || a->value>p->hi)continue;
            y=(int)lc_round(px_y(p,a->value));
            c=a->color>=0?a->color:sc->skin.note_bg;
            if(!p->fine)y=MIN(y,p->ph-1);
            for(x=p->ox;x<p->ox+p->pw;++x)if(p->fine?(x/6)%2==0:(x%2==0 && !lc_surface_touched(sf,x,y))) {
                lc_surface_set(sf,x,y,INK(c));if(p->fine)lc_surface_set(sf,x,y-1,INK(c));
            }
            if(!EMPTY(a->text)) {
                best_score=1e18;pick=b;
                for(k=0;k<4;++k) {
                    t=b;t.x=(k&1)?cellx(p,p->ox)+0.5:cellx(p,p->ox+p->pw)-b.w;
                    t.y=(k&2)?celly(p,y)+0.2:celly(p,y)-b.h-(p->fine?0.15:0);
                    if(!p->fine){t.x=floor(t.x);t.y=floor(t.y+0.5);}score=k;
                    if(t.y<p->clip.y || t.y+t.h>celly(p,p->ph)+0.01)score+=1000;
                    score+=box_penalty(t,p->taken,p->taken_count,50)+box_penalty(t,placed,placed_n,200);
                    if(score<best_score){best_score=score;pick=t;}
                }
                note_box(sc,pick.x,pick.y,lines,fg,bg);placed[placed_n++]=pick;
            }
            continue;
        }
        if(a->kind==2 && !pie) {
            x=-1;xv=0;
            if(p->numeric_x) {
                xv=strtod(TXT(a->label),&end);while(isspace((unsigned char)*end))++end;
                if(end==TXT(a->label) || *end || !lc_finite(xv) || xv<p->xlo || xv>p->xhi || !(p->xhi>p->xlo))continue;
                x=lc_clamp_int(lc_round(p->ox+(xv-p->xlo)/(p->xhi-p->xlo)*(p->pw-1)),INT_MIN,INT_MAX);
            } else {row=note_row(ds,a);if(row<0 || (size_t)row>=p->slot_nx)continue;x=(int)p->slot_cx[row];}
            c=a->color>=0?a->color:sc->skin.note_bg;
            for(y=0;y<p->ph;++y)if(!p->fine || (y/6)%2==0){lc_surface_set(sf,x,y,INK(c));if(p->fine)lc_surface_set(sf,x+1,y,INK(c));}
            if(!EMPTY(a->text)) {
                b.x=cellx(p,x)+0.5;if(b.x+b.w>RIGHT(p->clip)+1)b.x=cellx(p,x)-b.w-0.5;b.y=p->cells.y;
                if(!p->fine)b.x=floor(b.x+0.5);
                note_box(sc,b.x,b.y,lines,fg,bg);
                placed[placed_n++]=b;
            }
            continue;
        }
        if(a->kind==3) {
            b.x=p->cells.x+a->fx*MAX(0.0,p->cells.w-b.w);b.y=p->cells.y+a->fy*MAX(0.0,p->cells.h-b.h);
            if(!p->fine){b.x=floor(b.x+0.5);b.y=floor(b.y+0.5);}note_box(sc,b.x,b.y,lines,fg,bg);placed[placed_n++]=b;continue;
        }
        if(a->kind!=0)continue;
        row=note_row(ds,a);
        ser=note_series(ds,a);
        an=NULL;
        if(pie) {
            an=find_anchor(p,-1,row);
            if(!an && ser>=0)for(j=0;j<p->anchor_count;++j)if(p->anchors[j].s==ser)an=p->anchors+j;
            if(!an && row>=0)for(j=0;j<p->anchor_count;++j)if(p->anchors[j].s==row)an=p->anchors+j;
        } else {if(row<0)continue;an=find_anchor(p,ser,row);if(!an)an=find_anchor(p,-1,row);}
        if(!an)continue;
        ax=cellx(p,an->p.x);
        ay=celly(p,an->p.y);
        best_score=1e18;
        best=b;
        sideways=!pie && an->directional;
        for(k=sideways?-2:0;k<12;++k) {
            if(k<0) {
                t=b;t.x=ax+(k==-2?2.5:10);t.y=ay-b.h/2;score=(k+2)*0.5;
                if(t.x+t.w>RIGHT(p->clip)+1)score+=500;
                score+=box_penalty(t,placed,placed_n,200)+box_penalty(t,p->taken,p->taken_count,40);
                if(score<best_score){best_score=score;best=t;}continue;
            }
            ux=offsets[k][0];uy=offsets[k][1];if(pie && an->directional && ux*cos(an->dir)+uy*sin(an->dir)<-0.2)continue;
            t=b;t.x=ux>0?ax+2.5*ux:(ux<0?ax+2.5*ux-b.w:ax-b.w/2);t.y=uy>0?ay+1.4*uy:ay+2.0*uy-b.h+0.4;score=k*0.5;
            if(t.x<p->clip.x){score+=(p->clip.x-t.x)*50;t.x=p->clip.x;}
            if(t.x+t.w>RIGHT(p->clip)+1){score+=(t.x+t.w-RIGHT(p->clip)-1)*50;t.x=RIGHT(p->clip)+1-t.w;}
            if(t.y<p->clip.y){score+=(p->clip.y-t.y)*50;t.y=p->clip.y;}
            if(t.y+t.h>BOTTOM(p->clip)+1){score+=(t.y+t.h-BOTTOM(p->clip)-1)*50;t.y=BOTTOM(p->clip)+1-t.h;}
            score+=box_penalty(t,placed,placed_n,200)+box_penalty(t,p->taken,p->taken_count,40);
            if(t.x<=ax && ax<=t.x+t.w && t.y<=ay && ay<=t.y+t.h)score+=500;
            if(score<best_score){best_score=score;best=t;}
        }
        if(!p->fine){best.x=floor(best.x+0.5);best.y=floor(best.y+0.5);}
        bx=MAX(best.x,MIN(ax,best.x+best.w));by=MAX(best.y,MIN(ay,best.y+best.h));lx=(bx-p->cells.x)*p->sx;ly=(by-p->cells.y)*p->sy;
        if(p->fine)lc_surface_line(sf,an->p.x+1,an->p.y+1,lx+1,ly+1,INK(panel(sc)),1);
        lc_surface_line(sf,an->p.x,an->p.y,lx,ly,INK(bg),1);
        if(p->fine){lc_surface_disc(sf,an->p.x,an->p.y,4,INK(panel(sc)));lc_surface_disc(sf,an->p.x,an->p.y,3,INK(bg));}
        note_box(sc,best.x,best.y,lines,fg,bg);placed[placed_n++]=best;
    }
}

static int series_find(const lc_dataset *ds,const char *name)
{ size_t i;for(i=0;i<ds->series_count;++i)if(!hidden(ds->series+i) && lc_name_eq(ds->series[i].name,name))return (int)i;return -1; }
static int prepare(render *rr,lc_scene *sc,const lc_dataset *ds,const lc_chart_options *opts)
{
    size_t ns,n,i,j,k,kept,pos;unsigned char *used;const lc_error_bars *e;int a,b,t,cur,col,pin;double v,x,y;
    double **lo,**hi;unsigned char **ok;
    memset(rr,0,sizeof *rr);rr->sc=sc;rr->ds=*ds;rr->o=*opts;ns=ds->series_count;n=rows(ds);rr->n=n;
    rr->series=(lc_series *)lc_render_array(sc,ns,sizeof *rr->series);if(!rr->series)return 0;
    used=(unsigned char *)lc_render_array(sc,ns,1);if(!used)return 0;lo=hi=NULL;ok=NULL;
    if(opts->error_count) {
        lo=(double **)lc_render_array(sc,ns,sizeof *lo);hi=(double **)lc_render_array(sc,ns,sizeof *hi);ok=(unsigned char **)lc_render_array(sc,ns,sizeof *ok);
        if(!lo || !hi || !ok)return 0;
        for(k=0;k<opts->error_count;++k){e=opts->errors+k;a=series_find(ds,e->lo);b=e->plus_minus?a:series_find(ds,e->hi);if(a>=0 && b>=0)used[a]=used[b]=1;}
        for(k=0;k<opts->error_count;++k) {
            e=opts->errors+k;a=series_find(ds,e->lo);b=e->plus_minus?a:series_find(ds,e->hi);t=EMPTY(e->series)?-1:series_find(ds,e->series);
            if(a<0 || b<0)continue;
            if(EMPTY(e->series))for(i=0;i<ns && t<0;++i)if(!hidden(ds->series+i) && !used[i])t=(int)i;
            if(t<0 || used[t])continue;
            lo[t]=(double *)lc_render_array(sc,n,sizeof(double));hi[t]=(double *)lc_render_array(sc,n,sizeof(double));ok[t]=(unsigned char *)lc_render_array(sc,n,1);
            if(!lo[t] || !hi[t] || !ok[t])return 0;
            for(i=0;i<n;++i)if(valid(ds->series+a,i) && valid(ds->series+b,i) && (!e->plus_minus || valid(ds->series+t,i))) {
                v=cell_at(ds,(size_t)t,i);x=cell_at(ds,(size_t)a,i);y=cell_at(ds,(size_t)b,i);
                lo[t][i]=e->plus_minus?v-fabs(x):MIN(x,y);hi[t][i]=e->plus_minus?v+fabs(x):MAX(x,y);
                ok[t][i]=(unsigned char)(lc_finite(lo[t][i]) && lc_finite(hi[t][i]));
            }
        }
    }
    kept=0;cur=-1;
    for(i=0;i<ns;++i)if(!used[i]) {
        if((int)i==opts->cur_series)cur=(int)kept;
        rr->series[kept]=ds->series[i];if(ok){lo[kept]=lo[i];hi[kept]=hi[i];ok[kept]=ok[i];}++kept;
    }
    rr->ds.series=rr->series;rr->ds.series_count=kept;rr->err_lo=lo;rr->err_hi=hi;rr->err_valid=ok;rr->err_series_count=kept;
    if(opts->cur_series>=0)rr->o.cur_series=cur;
    pos=0;
    for(j=0;j<kept;++j)if(!hidden(rr->series+j)) {
        pin=pinned(opts,rr->series[j].name,pos);col=lc_chart_palette(sc,opts->palette,pos,kept);rr->series[j].color=pin>=0?pin:col;++pos;
    }
    return 1;
}
static void ring_cursor(plot *p,double x,double y)
{
    int yy;
    if(!p->fine){lc_surface_ring(p->sf,x,y,1.5,INK(15));return;}
    if(!lc_finite(x) || !lc_finite(y))return;
    for(yy=lc_clamp_int(y,INT_MIN,INT_MAX);yy<p->oy+p->ph;++yy)if((yy/3)%2==0)lc_surface_set(p->sf,lc_clamp_int(x,INT_MIN,INT_MAX),yy,INK(15));
    lc_surface_disc(p->sf,x,y,9,INK(panel(p->sc)));lc_surface_disc(p->sf,x,y,8,INK(15));
    lc_surface_disc(p->sf,x,y,5,INK(panel(p->sc)));lc_surface_disc(p->sf,x,y,3,INK(p->sc->skin.accent));
}

lc_status lc_render_chart(lc_scene *sc,lc_rect r,const lc_dataset *data,const lc_chart_options *opts)
{
    render rr;lc_chart_options defaults,*o;const lc_dataset *ds;lc_canvas *cv;const char *type,*title,*bottom,*slash;
    lc_rect inner,clip;int st,pie_like,nrows;size_t i,n,visible;legend_entry *leg;plot p;const anchor *a;
    if(!sc || !data)return LC_EINVAL;
    if(data->series_count>(size_t)INT_MAX || data->label_count>(size_t)INT_MAX || data->text_count>(size_t)INT_MAX ||
       (data->series_count && !data->series) || (data->label_count && !data->labels) || (data->text_count && !data->text))return LC_EINVAL;
    for(i=0;i<data->series_count;++i)if(data->series[i].count>(size_t)INT_MAX || (data->series[i].count && !data->series[i].values))return LC_EINVAL;
    if(!opts){lc_chart_options_init(&defaults);opts=&defaults;}
    if((opts->note_count && !opts->notes)||(opts->color_count && !opts->colors)||(opts->error_count && !opts->errors))return LC_EINVAL;
    if(r.w<8 || r.h<4)return lc_scene_status(sc);
    if(!prepare(&rr,sc,data,opts))return lc_scene_status(sc);
    o=&rr.o;
    ds=&rr.ds;
    cv=&sc->canvas;
    type=lc_chart_type(o->type);
    inner=r;
    if(!lc_name_eq(o->frame,"none")) {
        if(o->shadow && sc->skin.slide_bg!=LC_BG_NONE)lc_canvas_shadow(cv,r.x,r.y,r.w,r.h);
        if(sc->skin.slide_bg!=LC_BG_NONE)lc_scene_panel(sc,r,sc->skin.panel_bg);
        st=LC_BOX_DOUBLE;
        if(lc_name_eq(o->frame,"single"))st=LC_BOX_SINGLE;else if(lc_name_eq(o->frame,"heavy"))st=LC_BOX_HEAVY;else if(lc_name_eq(o->frame,"ascii"))st=LC_BOX_ASCII;
        if(sc->mode.ascii)st=LC_BOX_ASCII;
        lc_canvas_box(cv,r.x,r.y,r.w,r.h,st,o->frame_color>=0?o->frame_color:sc->skin.frame);inner=lc_rect_make(r.x+2,r.y+1,r.w-4,r.h-2);
    } else if(sc->skin.slide_bg!=LC_BG_NONE){lc_scene_panel(sc,r,sc->skin.panel_bg);inner=lc_rect_make(r.x+1,r.y,r.w-2,r.h);}
    if(inner.w<4 || inner.h<2)return lc_scene_status(sc);
    title=EMPTY(o->title)?data->title:o->title;
    if(!EMPTY(title)) {
        if(!lc_name_eq(o->frame,"none") && (int)lc_text_width(title)+8<r.w)lc_canvas_text_c(cv,r.x+2,r.y,r.w-4,lc_render_join(sc," ",title," "),sc->skin.title);
        else {lc_canvas_text_c(cv,inner.x,inner.y,inner.w,title,sc->skin.title);++inner.y;--inner.h;}
    }
    if(!EMPTY(o->subtitle) && inner.h>4){lc_canvas_text_c(cv,inner.x,inner.y,inner.w,o->subtitle,sc->skin.subtitle);++inner.y;--inner.h;}
    if(!EMPTY(o->source) && !lc_name_eq(o->frame,"none") && r.h>3) {
        bottom=o->source;if((int)lc_text_width(bottom)+6>r.w){slash=strrchr(bottom,'/');if(slash && slash[1])bottom=slash+1;}
        lc_canvas_text_c(cv,r.x+2,BOTTOM(r),r.w-4,lc_render_join(sc," ",bottom," "),sc->skin.grid==sc->skin.panel_bg?sc->skin.dim:8);
    }
    if(inner.w<4 || inner.h<2)return lc_scene_status(sc);
    clip=inner;
    if(!ds->series_count || !rows(ds)){lc_canvas_text_c(cv,inner.x,inner.y+inner.h/2,inner.w,"(no data)",sc->skin.subtitle);return lc_scene_status(sc);}
    if(strcmp(type,"table")==0){draw_table(&rr,inner);return lc_scene_status(sc);}
    pie_like=strcmp(type,"pie")==0 || strcmp(type,"pie3d")==0 || strcmp(type,"donut")==0;visible=0;
    for(i=0;i<ds->series_count;++i)if(!hidden(ds->series+i))++visible;
    if(!pie_like && strcmp(type,"hist")!=0 && o->legend && visible>1) {
        leg=(legend_entry *)lc_render_array(sc,visible,sizeof *leg);if(!leg)return lc_scene_status(sc);n=0;
        for(i=0;i<ds->series_count;++i)if(!hidden(ds->series+i)) {leg[n].name=ds->series[i].name;leg[n].color=ds->series[i].color;leg[n].value="";leg[n++].level=shaded(o)?(int)(i%4):0;}
        nrows=MIN(MAX(1,inner.h/4),legend_rows(leg,n,inner.w));
        if(inner.h-nrows>=6){legend_draw(sc,lc_rect_make(inner.x,BOTTOM(inner)-nrows+1,inner.w,nrows),leg,n,0);inner.h-=nrows;}
    }
    if(strcmp(type,"stacked")==0)p=draw_bars(&rr,inner,clip,1,0);
    else if(strcmp(type,"hbar")==0)p=draw_hbars(&rr,inner,clip);
    else if(strcmp(type,"dumbbell")==0)p=draw_dumbbell(&rr,inner,clip);
    else if(strcmp(type,"line")==0)p=draw_lines(&rr,inner,clip,0);
    else if(strcmp(type,"area")==0)p=draw_lines(&rr,inner,clip,1);
    else if(strcmp(type,"scatter")==0)p=draw_scatter(&rr,inner,clip);
    else if(strcmp(type,"hist")==0)p=draw_hist(&rr,inner,clip);
    else if(strcmp(type,"pie")==0)p=draw_pie(&rr,inner,clip,0,0);
    else if(strcmp(type,"pie3d")==0)p=draw_pie(&rr,inner,clip,1,0);
    else if(strcmp(type,"donut")==0)p=draw_pie(&rr,inner,clip,o->depth>0,0.5);
    else p=draw_bars(&rr,inner,clip,0,0);
    if(!pie_like && p.sf && o->cur_index>=0){a=find_anchor(&p,o->cur_series,o->cur_index);if(a)ring_cursor(&p,a->p.x,a->p.y);}
    draw_notes(&rr,&p,pie_like);return lc_scene_status(sc);
}
