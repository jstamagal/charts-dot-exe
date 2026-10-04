/* Shapes and flow diagrams; the original pixel/cell geometry in ISO C89. */
#include "render.h"
#define DIAGRAM_PI 3.14159265358979323846

typedef struct diagram_lines { lc_lines lines; int w; } diagram_lines;
typedef struct placed { lc_rect r; diagram_lines text; int layer; } placed;
static lc_pt point(double x,double y) { lc_pt p; p.x=x; p.y=y; return p; }
static lc_pt shifted(lc_pt p,double x,double y) { return point(p.x+x,p.y+y); }
static double length(double x,double y) {
    double a=fabs(x),b=fabs(y),t;
    if (a<b) { t=a; a=b; b=t; }
    if (!a) return 0;
    t=b/a; return a*sqrt(1+t*t);
}
static void stroke(lc_surface *sf,lc_pt a,lc_pt b,lc_ink k,int w,int dash,int fine) {
    double len,on,off,t,u;
    if (!dash) { lc_surface_line(sf,a.x,a.y,b.x,b.y,k,w); return; }
    if (!lc_clip_segment(&a,&b,-8,-8,sf->w+8,sf->h+8)) return;
    len=length(b.x-a.x,b.y-a.y); on=fine ? 7 : 2; off=fine ? 5 : 1;
    if (len<1e-9) return;
    for (t=0;t<len;t+=on+off) {
        u=LC_MIN(len,t+on);
        lc_surface_line(sf,a.x+(b.x-a.x)*t/len,a.y+(b.y-a.y)*t/len,a.x+(b.x-a.x)*u/len,a.y+(b.y-a.y)*u/len,k,w);
    }
}
static void arrowhead(lc_surface *sf,lc_pt tip,lc_pt from,lc_ink k,double size) {
    double dx,dy,len,half; lc_pt base,p[3];
    dx=tip.x-from.x; dy=tip.y-from.y; len=length(dx,dy); if (len<1e-9) return;
    dx/=len; dy/=len; base=point(tip.x-dx*size,tip.y-dy*size); half=size*0.55;
    p[0]=tip; p[1]=point(base.x-dy*half,base.y+dx*half); p[2]=point(base.x+dy*half,base.y-dx*half);
    lc_surface_poly(sf,p,3,k);
}
static lc_pt back_off(lc_pt to,lc_pt from,double by) {
    double dx=to.x-from.x,dy=to.y-from.y,len=length(dx,dy);
    if (len<=by) return from;
    return point(to.x-dx/len*by,to.y-dy/len*by);
}
static void arrow(lc_surface *sf,const lc_pt *p,size_t n,lc_ink k,int w,int dash,int head,int tail,int fine) {
    double hs; lc_pt tip,before,tail_tip,after,a,b; size_t i;
    if (n<2) return;
    hs=fine ? 7+2.5*w : 2; tip=p[n-1]; before=p[n-2]; tail_tip=p[0]; after=p[1];
    for (i=0;i+1<n;i++) {
        a=p[i]; b=p[i+1];
        if (!i && tail) a=back_off(tail_tip,after,hs*0.8);
        if (i+2==n && head) b=back_off(tip,before,hs*0.8);
        stroke(sf,a,b,k,w,dash,fine);
    }
    if (head) arrowhead(sf,tip,before,k,hs);
    if (tail) arrowhead(sf,tail_tip,after,k,hs);
}
static void extrude(lc_surface *sf,const lc_pt *o,size_t n,double dx,double dy,int c) {
    lc_pt mid,p[4]; size_t i,j; double nx,ny,cx,cy;
    mid=point(0,0); if (!n) return;
    for (i=0;i<n;i++) { mid.x+=o[i].x/n; mid.y+=o[i].y/n; }
    for (i=0;i<n;i++) {
        j=(i+1)%n; nx=o[j].y-o[i].y; ny=o[i].x-o[j].x;
        cx=(o[i].x+o[j].x)/2-mid.x; cy=(o[i].y+o[j].y)/2-mid.y;
        if (nx*cx+ny*cy<0) { nx=-nx; ny=-ny; }
        if (nx*dx-ny*dy<=0) continue;
        p[0]=o[i]; p[1]=o[j]; p[2]=shifted(o[j],dx,-dy); p[3]=shifted(o[i],dx,-dy);
        lc_surface_poly(sf,p,4,ny<-fabs(nx)*0.5 ? lc_top_ink(c) : lc_side_ink(c));
    }
}
static diagram_lines lines_of(lc_scene *sc,const char *text,int width) {
    diagram_lines l; size_t i,n;
    l.lines=lc_render_wrap(sc,text,(size_t)LC_MAX(1,width)); l.w=0;
    for (i=0;i<l.lines.n;i++) { n=lc_text_width(l.lines.v[i]); l.w=LC_MAX(l.w,(int)LC_MIN(n,(size_t)INT_MAX)); }
    return l;
}
static void put_lines(lc_scene *sc,double x,double y,diagram_lines l,int fg,int bg,int scale,int align) {
    int k=sc->mode.pixel ? scale : 1; size_t i; double len,tx,ty;
    for (i=0;i<l.lines.n;i++) {
        len=(double)lc_text_width(l.lines.v[i])*k;
        tx=align<0 ? x : (align==0 ? x-len/2 : x-len); ty=y+(double)i*k;
        if (!sc->mode.pixel) { tx=floor(tx+0.5); ty=floor(ty+0.5); }
        lc_scene_text(sc,tx,ty,l.lines.v[i],fg,bg,k);
    }
}
static lc_pt to_cells(lc_rect r,lc_surface *sf,lc_pt p) { return point(r.x+p.x/sf->sx,r.y+p.y/sf->sy); }
void lc_draw_shapes(lc_scene *sc,lc_rect r,const lc_shape *shapes,size_t count,int bg) {
    lc_surface *sf; int fine,w,scale,fg,plain,fillc,borderc,room,k; double ux,uy,sh,rx,ry,tall;
    size_t si,i,j,n; const lc_shape *s; lc_pt *o,*shadow,a,b,c,lo,hi,c0,c1; lc_ink ink; diagram_lines lines;
    if (!sc || !shapes || !count || r.w<2 || r.h<2) return;
    sf=lc_scene_surface(sc,r); if (!sf) return;
    fine=sc->mode.pixel; ux=sf->w/12.0; uy=sf->h/12.0; sh=fine ? 8 : 1;
    for (si=0;si<count;si++) {
        s=&shapes[si]; w=fine ? s->width : 1; scale=LC_MAX(1,s->size);
        if (!s->points || !s->point_count) continue;
        if (s->kind==4) {
            a=to_cells(r,sf,point(s->points[0].x*ux,s->points[0].y*uy));
            fg=s->text_color>=0 ? s->text_color : (s->color>=0 ? s->color : lc_contrast_on(bg));
            put_lines(sc,a.x,a.y,lines_of(sc,s->text,1000),fg,-1,scale,s->align); continue;
        }
        if (s->point_count<2) continue;
        n=s->kind==2 || s->kind==3 ? s->point_count : (s->kind==0 ? 4 : (fine ? 72 : 32));
        o=(lc_pt *)lc_render_array(sc,n,sizeof *o); if (!o) return;
        if (s->kind==2 || s->kind==3) for (i=0;i<n;i++) o[i]=point(s->points[i].x*ux,s->points[i].y*uy);
        else {
            a=point(s->points[0].x*ux,s->points[0].y*uy); b=point(s->points[1].x*ux,s->points[1].y*uy);
            if (s->kind==0) { o[0]=a; o[1]=point(b.x,a.y); o[2]=b; o[3]=point(a.x,b.y); }
            else { c=point((a.x+b.x)/2,(a.y+b.y)/2); rx=fabs(b.x-a.x)/2; ry=fabs(b.y-a.y)/2; for (i=0;i<n;i++) o[i]=point(c.x+rx*cos(2*DIAGRAM_PI*i/n),c.y+ry*sin(2*DIAGRAM_PI*i/n)); }
        }
        shadow=NULL;
        if (s->shadow) { shadow=(lc_pt *)lc_render_array(sc,n,sizeof *shadow); if (!shadow) return; for (i=0;i<n;i++) shadow[i]=shifted(o[i],s->kind==3 ? sh/2 : sh,s->kind==3 ? sh/2 : sh); }
        if (s->kind==3) {
            ink=lc_ink_solid(s->color>=0 ? s->color : lc_contrast_on(bg));
            if (shadow) arrow(sf,shadow,n,lc_ink_solid(sc->skin.shadow),w,s->dash,s->head,s->tail,fine);
            arrow(sf,o,n,ink,w,s->dash,s->head,s->tail,fine);
            if (s->text && *s->text) {
                j=(n-1)/2; c=to_cells(r,sf,point((o[j].x+o[j+1].x)/2,(o[j].y+o[j+1].y)/2));
                lines=lines_of(sc,s->text,24); fg=s->text_color>=0 ? s->text_color : ink.a;
                put_lines(sc,c.x,c.y-(double)lines.lines.n*(fine ? scale : 1),lines,fg,bg,scale,0);
            }
            continue;
        }
        plain=s->color<0; fillc=plain ? sc->skin.panel_bg : s->color;
        borderc=s->border>=0 ? s->border : (plain || !s->fill ? (plain ? sc->skin.frame : fillc) : -1);
        if (shadow) lc_surface_poly(sf,shadow,n,lc_ink_solid(sc->skin.shadow));
        if (s->depth>0 && fine) extrude(sf,o,n,s->depth*3.0,s->depth*2.1,fillc);
        if (s->fill) lc_surface_poly(sf,o,n,s->dither ? lc_ink_make(fillc,bg,s->dither) : lc_ink_solid(fillc));
        if (borderc>=0) for (i=0;i<n;i++) stroke(sf,o[i],o[(i+1)%n],lc_ink_solid(borderc),w,s->dash,fine);
        if (!s->text || !*s->text) continue;
        lo=hi=o[0]; for (i=1;i<n;i++) { lo.x=LC_MIN(lo.x,o[i].x); lo.y=LC_MIN(lo.y,o[i].y); hi.x=LC_MAX(hi.x,o[i].x); hi.y=LC_MAX(hi.y,o[i].y); }
        c0=to_cells(r,sf,lo); c1=to_cells(r,sf,hi); k=fine ? scale : 1; room=lc_clamp_int((c1.x-c0.x)/k,INT_MIN+2,INT_MAX)-2;
        lines=lines_of(sc,s->text,LC_MAX(1,room)); tall=(double)lines.lines.n*k;
        if (lines.w>room || tall>c1.y-c0.y) lc_scene_fit(sc,lc_render_join(sc,"the text \"",lc_text_truncate(sc->ctx,s->text,30),"\" does not fit its shape: make the shape bigger or the text shorter"));
        fg=s->text_color>=0 ? s->text_color : lc_contrast_on(s->fill && !s->dither ? fillc : bg);
        put_lines(sc,(c0.x+c1.x)/2,(c0.y+c1.y)/2-tall/2,lines,fg,-1,scale,0);
    }
}

/* CSR adjacency and iterative DFS preserve edge order without recursion. */
typedef struct layers {
    int *layer,*nodes; size_t *offset; unsigned char *back; int count;
} layers;
static int layer_nodes(lc_scene *sc,const lc_flow *f,layers *result) {
    size_t n=f->node_count,m=f->edge_count,i,j,e,sp,order_count,start,end,at;
    size_t *offset,*cursor,*edges,*stack_edge; int *state,*order,*stack,*by,*layer,levels,v,t,keynode;
    unsigned char *back; double *pos,*key,sum; size_t cnt;
    memset(result,0,sizeof *result);
    offset=(size_t *)lc_render_array(sc,n+1,sizeof *offset); cursor=(size_t *)lc_render_array(sc,n,sizeof *cursor);
    edges=(size_t *)lc_render_array(sc,m,sizeof *edges); stack_edge=(size_t *)lc_render_array(sc,n,sizeof *stack_edge);
    state=(int *)lc_render_array(sc,n,sizeof *state); order=(int *)lc_render_array(sc,n,sizeof *order);
    stack=(int *)lc_render_array(sc,n,sizeof *stack); layer=(int *)lc_render_array(sc,n,sizeof *layer);
    by=(int *)lc_render_array(sc,n,sizeof *by); back=(unsigned char *)lc_render_array(sc,m,1);
    pos=(double *)lc_render_array(sc,n,sizeof *pos); key=(double *)lc_render_array(sc,n,sizeof *key);
    if (!offset||!cursor||!edges||!stack_edge||!state||!order||!stack||!layer||!by||!back||!pos||!key) return 0;
    for (e=0;e<m;e++) { if (f->edges[e].from<0 || f->edges[e].to<0 || (size_t)f->edges[e].from>=n || (size_t)f->edges[e].to>=n) { lc_scene_fail(sc,LC_EINVAL); return 0; } offset[(size_t)f->edges[e].from+1]++; }
    for (i=1;i<=n;i++) offset[i]+=offset[i-1];
    memcpy(cursor,offset,n*sizeof *cursor);
    for (e=0;e<m;e++) edges[cursor[f->edges[e].from]++]=e;
    order_count=0;
    for (i=0;i<n;i++) {
        if (state[i]) continue;
        sp=1; stack[0]=(int)i; stack_edge[0]=offset[i]; state[i]=1;
        while (sp) {
            v=stack[sp-1];
            if (stack_edge[sp-1]<offset[(size_t)v+1]) {
                e=edges[stack_edge[sp-1]++]; t=f->edges[e].to;
                if (state[t]==1) back[e]=1;
                else if (!state[t]) { state[t]=1; stack[sp]=t; stack_edge[sp]=offset[t]; sp++; }
            } else { state[v]=2; order[order_count++]=v; sp--; }
        }
    }
    for (i=order_count;i>0;i--) { v=order[i-1]; for (j=offset[v];j<offset[(size_t)v+1];j++) { e=edges[j]; if (!back[e]) { t=f->edges[e].to; layer[t]=LC_MAX(layer[t],layer[v]+1); } } }
    levels=1; for (i=0;i<n;i++) levels=LC_MAX(levels,layer[i]+1);
    memset(offset,0,(n+1)*sizeof *offset);
    for (i=0;i<n;i++) offset[(size_t)layer[i]+1]++;
    for (i=1;i<=(size_t)levels;i++) offset[i]+=offset[i-1];
    memcpy(cursor,offset,(size_t)levels*sizeof *cursor);
    for (i=0;i<n;i++) by[cursor[layer[i]]++]=(int)i;
    for (i=0;i<(size_t)levels;i++) {
        start=offset[i]; end=offset[i+1];
        if (i) {
            for (j=start;j<end;j++) { v=by[j]; sum=0; cnt=0; for (e=0;e<m;e++) if (f->edges[e].to==v && layer[f->edges[e].from]<(int)i) { sum+=pos[f->edges[e].from]; cnt++; } key[v]=cnt ? sum/cnt : 1e9; }
            for (j=start+1;j<end;j++) { keynode=by[j]; at=j; while (at>start && key[keynode]<key[by[at-1]]) { by[at]=by[at-1]; at--; } by[at]=keynode; }
        }
        for (j=start;j<end;j++) pos[by[j]]=(double)(j-start);
    }
    result->layer=layer; result->nodes=by; result->offset=offset; result->back=back; result->count=levels; return 1;
}
static lc_pt flow_point(lc_rect r,double sx,double sy,double x,double y) { return point((x-r.x)*sx,(y-r.y)*sy); }
static lc_pt rim(lc_rect r,double sx,double sy,const placed *p,lc_pt toward,int fine) {
    lc_pt mid; double dx,dy,hx,hy,t,den;
    mid=flow_point(r,sx,sy,p->r.x+p->r.w/2.0,p->r.y+p->r.h/2.0); dx=toward.x-mid.x; dy=toward.y-mid.y;
    hx=p->r.w*sx/2+(fine ? 3 : 1); hy=p->r.h*sy/2+(fine ? 3 : 1); den=LC_MAX(fabs(dx)/hx,fabs(dy)/hy);
    if (!(den>0)) return mid;
    t=1/den; return point(mid.x+dx*t,mid.y+dy*t);
}
void lc_draw_flow(lc_scene *sc,lc_rect r,const lc_flow *f,int bg) {
    int n,fine,down,shw,shh,L,along,cross,cap,v,l,used,want_gap,gap,total,at,span,k,cgap,c,d,col,fill,fg,style,w;
    int label_align,far_right,top,bottom,edge,over; size_t i,j,e,widest,longest,path_count; layers lay;
    placed *box,*b,*a; int *depth; const lc_flow_edge *fe; lc_surface *sf; lc_pt path[4],label,s,t,ca,cb;
    double sx,sy,gap_px,mid,lx,lo,hi,lane; diagram_lines text;
    if (!sc || !f || !f->nodes || !f->node_count || r.w<6 || r.h<3) return;
    if (f->node_count>(size_t)INT_MAX/16 || (f->edge_count && !f->edges)) { lc_scene_fail(sc,LC_EINVAL); return; }
    n=(int)f->node_count; fine=sc->mode.pixel; down=f->down; shw=sc->skin.slide_bg==LC_BG_NONE ? 0 : 2; shh=shw ? 1 : 0;
    if (!layer_nodes(sc,f,&lay)) return;
    L=lay.count; widest=1;
    for (l=0;l<L;l++) widest=LC_MAX(widest,lay.offset[l+1]-lay.offset[l]);
    along=down ? r.h : r.w; cross=down ? r.w : r.h;
    cap=down ? LC_MAX(8,(r.w-((int)widest-1)*4)/(int)widest)-shw : LC_MAX(8,(r.w-(L-1)*6)/L)-shw;
    box=(placed *)lc_render_array(sc,(size_t)n,sizeof *box); depth=(int *)lc_render_array(sc,(size_t)L,sizeof *depth); if (!box || !depth) return;
    for (v=0;v<n;v++) {
        b=&box[v]; b->text=lines_of(sc,f->nodes[v].text,cap-4); b->r.w=LC_MAX(6,b->text.w+4);
        b->r.h=(int)b->text.lines.n+2; b->layer=lay.layer[v]; depth[b->layer]=LC_MAX(depth[b->layer],down ? b->r.h+shh : b->r.w+shw);
    }
    used=0; for (l=0;l<L;l++) used+=depth[l];
    longest=0; for (e=0;e<f->edge_count;e++) longest=LC_MAX(longest,lc_text_width(f->edges[e].text));
    want_gap=down ? (longest ? 4 : 3) : LC_MAX(6,(int)LC_MIN(longest,(size_t)(INT_MAX/4))+4);
    gap=L>1 ? (along-used)/(L-1) : 0;
    if (L>1 && gap<(down ? 2 : 4)) lc_scene_fit(sc,lc_render_join(sc,lc_render_join(sc,"the flow does not fit: ",lc_render_uint(sc,(size_t)L)," steps need more room "),down ? "down" : "across",down ? " (use \"dir\": \"right\", shorter names, or a bigger block)" : " (use \"dir\": \"down\", shorter names, or a bigger block)"));
    gap=LC_MAX(down ? 2 : 3,LC_MIN(gap,want_gap*2)); total=used+gap*(L-1); at=(down ? r.y : r.x)+LC_MAX(0,(along-total)/2);
    for (l=0;l<L;l++) {
        span=0; for (i=lay.offset[l];i<lay.offset[l+1];i++) { b=&box[lay.nodes[i]]; span+=down ? b->r.w+shw : b->r.h+shh; }
        k=(int)(lay.offset[l+1]-lay.offset[l]); cgap=k>1 ? LC_MIN((cross-span)/(k-1),down ? 8 : 4) : 0;
        if (k>1 && cross-span<k-1) { cgap=0; lc_scene_fit(sc,lc_render_join(sc,"the flow does not fit: ",lc_render_uint(sc,(size_t)k)," boxes side by side at one step")); }
        c=(down ? r.x : r.y)+LC_MAX(0,(cross-span-cgap*(k-1))/2);
        for (i=lay.offset[l];i<lay.offset[l+1];i++) {
            b=&box[lay.nodes[i]]; d=depth[l]-(down ? shh : shw);
            if (down) { b->r.y=at+(d-b->r.h)/2; b->r.x=c; c+=b->r.w+shw+cgap; }
            else { b->r.x=at+(d-b->r.w)/2; b->r.y=c; c+=b->r.h+shh+cgap; }
        }
        at+=depth[l]+gap;
    }
    style=sc->mode.ascii ? LC_BOX_ASCII : LC_BOX_DOUBLE;
    for (v=0;v<n;v++) {
        b=&box[v]; col=f->nodes[v].color; fill=col>=0 ? col : sc->skin.panel_bg; fg=lc_contrast_on(fill);
        if (shw) lc_canvas_shadow(&sc->canvas,b->r.x,b->r.y,b->r.w,b->r.h);
        lc_scene_panel(sc,b->r,fill); lc_canvas_box(&sc->canvas,b->r.x,b->r.y,b->r.w,b->r.h,style,col>=0 ? fg : sc->skin.frame);
        for (i=0;i<b->text.lines.n;i++) lc_canvas_text_c(&sc->canvas,b->r.x+1,b->r.y+1+(int)i,b->r.w-2,b->text.lines.v[i],fg);
    }
    sf=lc_scene_surface(sc,r); if (!sf) return;
    sx=sf->sx; sy=sf->sy; w=fine ? 2 : 1;
    for (e=0;e<f->edge_count;e++) {
        fe=&f->edges[e]; a=&box[fe->from]; b=&box[fe->to]; col=fe->color>=0 ? fe->color : lc_contrast_on(bg);
        path_count=4; label_align=0; gap_px=down ? gap*sy : gap*sx;
        if (!lay.back[e] && b->layer>a->layer) {
            if (down) {
                s=flow_point(r,sx,sy,a->r.x+a->r.w/2.0,a->r.y+a->r.h); t=flow_point(r,sx,sy,b->r.x+b->r.w/2.0,b->r.y-(fine ? 0 : 1));
                s.y+=fine ? 2 : 0; mid=s.y+LC_MIN(gap_px/2,t.y-s.y);
                path[0]=s; path[1]=point(s.x,mid); path[2]=point(t.x,mid); path[3]=t;
                label=point(r.x+t.x/sx+1.5,r.y+(mid+t.y)/2/sy-0.5); label_align=-1;
            } else {
                s=flow_point(r,sx,sy,a->r.x+a->r.w,a->r.y+a->r.h/2.0); t=flow_point(r,sx,sy,b->r.x-(fine ? 0 : 1),b->r.y+b->r.h/2.0);
                s.x+=fine ? 2 : 0; mid=s.x+LC_MIN(gap_px/2,t.x-s.x);
                path[0]=s; path[1]=point(mid,s.y); path[2]=point(mid,t.y); path[3]=t;
                lx=fabs(s.y-t.y)<1 ? (s.x+t.x)/2 : (mid+t.x)/2; label=point(r.x+lx/sx,r.y+t.y/sy-1.5);
            }
            if (down && fabs(path[0].x-path[3].x)<sx*1.5) {
                lo=LC_MAX(a->r.x+1,b->r.x+1)*sx; hi=LC_MIN(a->r.x+a->r.w-1,b->r.x+b->r.w-1)*sx;
                if (hi>lo) path[0].x=path[3].x=LC_MAX(lo,LC_MIN(hi-1,(path[0].x+path[3].x)/2));
            } else if (!down && fabs(path[0].y-path[3].y)<sy*1.5) {
                lo=(LC_MAX(a->r.y+1,b->r.y+1)-r.y)*sy; hi=(LC_MIN(a->r.y+a->r.h-1,b->r.y+b->r.h-1)-r.y)*sy;
                if (hi>lo) path[0].y=path[3].y=floor(LC_MAX(lo,LC_MIN(hi-1,(path[0].y+path[3].y)/2))/sy)*sy+sy/2;
                label.y=r.y+path[3].y/sy-1.5;
            }
            if (fabs(path[0].x-path[3].x)<1 || fabs(path[0].y-path[3].y)<1) { path[1]=path[3]; path_count=2; }
        } else if (lay.back[e] || b->layer<a->layer) {
            far_right=0; top=INT_MAX; bottom=0;
            for (j=0;j<(size_t)n;j++) if (box[j].layer>=LC_MIN(a->layer,b->layer) && box[j].layer<=LC_MAX(a->layer,b->layer)) { far_right=LC_MAX(far_right,box[j].r.x+box[j].r.w); top=LC_MIN(top,box[j].r.y); bottom=LC_MAX(bottom,box[j].r.y+box[j].r.h); }
            if (down) {
                edge=far_right+shw+1; lane=LC_MIN((edge-r.x)*sx+(fine ? 4 : 0),(r.w-1)*sx-1);
                s=flow_point(r,sx,sy,a->r.x+a->r.w+shw,a->r.y+a->r.h/2.0); t=flow_point(r,sx,sy,b->r.x+b->r.w+(fine ? 0 : 1),b->r.y+b->r.h/2.0);
                path[0]=s; path[1]=point(lane,s.y); path[2]=point(lane,t.y); path[3]=t;
                label=point(r.x+lane/sx+1,r.y+(s.y+t.y)/2/sy-0.5); label_align=-1;
            } else {
                lane=(top-r.y-1)*sy+sy/2; if (top-r.y<2) lane=(bottom+shh-r.y)*sy+sy/2; over=lane<(a->r.y-r.y)*sy;
                s=flow_point(r,sx,sy,a->r.x+a->r.w/2.0,over ? a->r.y-(fine ? 0.1 : 1) : a->r.y+a->r.h+shh);
                t=flow_point(r,sx,sy,b->r.x+b->r.w/2.0,over ? b->r.y-(fine ? 0.1 : 1) : b->r.y+b->r.h+shh);
                path[0]=s; path[1]=point(s.x,lane); path[2]=point(t.x,lane); path[3]=t;
                label=point(r.x+(s.x+t.x)/2/sx,r.y+lane/sy-1.5);
            }
        } else {
            ca=flow_point(r,sx,sy,a->r.x+a->r.w/2.0,a->r.y+a->r.h/2.0); cb=flow_point(r,sx,sy,b->r.x+b->r.w/2.0,b->r.y+b->r.h/2.0);
            path[0]=rim(r,sx,sy,a,cb,fine); path[1]=rim(r,sx,sy,b,ca,fine); path_count=2;
            label=point(r.x+(path[0].x+path[1].x)/2/sx,r.y+(path[0].y+path[1].y)/2/sy-1.5);
        }
        arrow(sf,path,path_count,lc_ink_solid(col),w,fe->dash,1,0,fine);
        if (fe->text && *fe->text) {
            text=lines_of(sc,fe->text,down ? LC_MAX(6,r.w/3) : LC_MAX(6,gap+6));
            if (!down && text.w>gap+4) lc_scene_fit(sc,lc_render_join(sc,"the label \"",fe->text,"\" is wider than the gap it names; shorten it or use \"dir\": \"down\""));
            put_lines(sc,label.x,label.y-((double)text.lines.n-1),text,col,bg,1,label_align);
        }
    }
}
