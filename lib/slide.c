/* Presentation layout over the same scene used by standalone charts. */
#include "render.h"
#define MIN LC_MIN
#define MAX LC_MAX
#define TXT(s) ((s)?(s):"")
#define EMPTY(s) (!(s)||!(s)[0])
#define BOTTOM(r) ((r).y+(r).h-1)

void lc_block_init(lc_block *b)
{
    memset(b,0,sizeof *b);b->weight=1;b->at[2]=b->at[3]=12;
    b->size=1;b->align=b->color=-1;lc_chart_options_init(&b->chart);
}
void lc_slide_view_init(lc_slide_view *v)
{ memset(v,0,sizeof *v);v->cur_series=v->cur_index=-1;v->chrome=1; }
static int shadow_w(lc_scene *sc){return sc->skin.slide_bg==LC_BG_NONE?0:2;}
static int shadow_h(lc_scene *sc){return sc->skin.slide_bg==LC_BG_NONE?0:1;}
static void place(lc_scene *,lc_block *,size_t,lc_rect,const char *,int);
static void place_one(lc_scene *sc,lc_block *b,lc_rect r,int depth)
{
    b->rect=r;
    if(b->kind==3 || b->kind==4)place(sc,b->children,b->child_count,r,b->kind==3?"rows":"cols",depth+1);
}
static void split(lc_scene *sc,lc_block **flow,size_t count,lc_rect body,int across,int depth)
{
    size_t i;int gap,room,pos,end;double total,used,big,scale,at;lc_rect r;
    if(!count)return;
    gap=across?1+shadow_w(sc):shadow_h(sc);
    total=0;big=0;scale=1;
    for(i=0;i<count;++i){total+=flow[i]->weight;if(flow[i]->weight>big)big=flow[i]->weight;}
    /* weights whose sum overflows are compared relative to the largest */
    if(!lc_finite(total) && lc_finite(big) && big>0){scale=big;total=0;for(i=0;i<count;++i)total+=flow[i]->weight/scale;}
    if(!(total>0) || !lc_finite(total)){lc_scene_fail(sc,LC_EINVAL);return;}
    room=(across?body.w:body.h)-gap*((int)count-1);used=0;pos=across?body.x:body.y;
    for(i=0;i<count;++i) {
        used+=flow[i]->weight/scale;at=room*used/total;
        if(!lc_finite(at))at=room*(used/total);
        end=(across?body.x:body.y)+lc_clamp_int(lc_round(at),INT_MIN/2,INT_MAX/2)+gap*(int)i;
        r=across?lc_rect_make(pos,body.y,end-pos,body.h):lc_rect_make(body.x,pos,body.w,end-pos);
        place_one(sc,flow[i],r,depth);pos=end+gap;
    }
}
static void place(lc_scene *sc,lc_block *blocks,size_t count,lc_rect body,const char *layout,int depth)
{
    lc_block **flow,**band_ptrs,*bands,*b;size_t i,n,from,number;int x0,y0,x1,y1,cols,rows,j;const char *how;
    if(depth>32 || count>(size_t)INT_MAX || (count && !blocks)){lc_scene_fail(sc,LC_EINVAL);return;}
    flow=(lc_block **)lc_render_array(sc,count,sizeof *flow);if(!flow)return;n=0;
    for(i=0;i<count;++i) {
        b=blocks+i;
        if(!lc_finite(b->weight) || b->weight<=0){lc_scene_fail(sc,LC_EINVAL);return;}
        if(!b->has_at){flow[n++]=b;continue;}
        for(j=0;j<4;++j)if(!lc_finite(b->at[j]) || fabs(b->at[j])>10000){lc_scene_fail(sc,LC_EINVAL);return;}
        x0=body.x+(int)lc_round(b->at[0]/12*body.w);y0=body.y+(int)lc_round(b->at[1]/12*body.h);
        x1=body.x+(int)lc_round((b->at[0]+b->at[2])/12*body.w);y1=body.y+(int)lc_round((b->at[1]+b->at[3])/12*body.h);
        if(b->at[0]+b->at[2]<11.99)x1-=1+shadow_w(sc);
        if(b->at[1]+b->at[3]<11.99)y1-=shadow_h(sc);
        place_one(sc,b,lc_rect_make(x0,y0,x1-x0,y1-y0),depth);
    }
    if(!n)return;
    how=EMPTY(layout)?"auto":layout;
    if(strcmp(how,"auto")==0)how=n<=3?"cols":"grid";
    if(strcmp(how,"cols")==0 || strcmp(how,"rows")==0){split(sc,flow,n,body,strcmp(how,"cols")==0,depth);return;}
    cols=(int)ceil(sqrt((double)n));rows=((int)n+cols-1)/cols;
    bands=(lc_block *)lc_render_array(sc,(size_t)rows,sizeof *bands);band_ptrs=(lc_block **)lc_render_array(sc,(size_t)rows,sizeof *band_ptrs);
    if(!bands || !band_ptrs)return;
    for(j=0;j<rows;++j){lc_block_init(bands+j);band_ptrs[j]=bands+j;}split(sc,band_ptrs,(size_t)rows,body,0,depth);
    for(j=0;j<rows;++j){from=(size_t)j*cols;number=MIN((size_t)cols,n-from);split(sc,flow+from,number,bands[j].rect,1,depth);}
}

typedef struct marked { lc_codepoint c;int strong; } marked;
typedef struct text_line {
    struct text_line *next;marked *g;size_t n;int fg,scale,indent;lc_codepoint bullet;
} text_line;
static marked *inline_marks(lc_scene *sc,const char *s,size_t *count)
{
    marked *g;size_t n;int strong;lc_codepoint cp;const char *p;
    s=TXT(s);g=(marked *)lc_render_array(sc,strlen(s)+1,sizeof *g);if(!g){*count=0;return NULL;}n=0;strong=0;p=s;
    while(*p) {
        if(p[0]=='*' && p[1]=='*'){strong=!strong;p+=2;continue;}
        cp=lc_utf8_next(&p);if(!lc_cell_of(cp))continue;g[n].c=cp;g[n++].strong=strong;
    }
    *count=n;return g;
}
static const char *trim(lc_scene *sc,const char *s)
{
    const char *e;char *out;size_t n;s=TXT(s);while(isspace((unsigned char)*s))++s;e=s+strlen(s);while(e>s && isspace((unsigned char)e[-1]))--e;
    n=(size_t)(e-s);out=(char *)lc_render_array(sc,n+1,1);if(out)memcpy(out,s,n);return out?out:"";
}
static text_line *append_line(lc_scene *sc,text_line **head,text_line **tail,const text_line *proto,marked *g)
{
    text_line *line;line=(text_line *)lc_render_array(sc,1,sizeof *line);if(!line)return NULL;
    *line=*proto;line->next=NULL;line->g=g;line->n=0;
    if(*tail)(*tail)->next=line;else *head=line;*tail=line;return line;
}
static const char *glyph_run(lc_scene *sc,const marked *g,size_t count)
{
    char *run,bytes[5];size_t i,n,used;
    if(count>((size_t)-1-1)/4){lc_scene_fail(sc,LC_EOVERFLOW);return "";}
    run=(char *)lc_render_array(sc,count*4+1,1);if(!run)return "";used=0;
    for(i=0;i<count;++i){n=lc_utf8_encode(g[i].c,bytes);memcpy(run+used,bytes,n);used+=n;}run[used]=0;return run;
}
static void draw_text_block(lc_scene *sc,const lc_block *b,lc_rect r,int frame)
{
    int base,px,want,total,y,len,x;size_t ri,n,width,i,e,k,used;
    const char *s,*t,*run;marked *g,*wrapped;char bytes[5];text_line proto,*head,*tail,*line;
    px=sc->mode.pixel;base=b->color>=0?b->color:sc->skin.text;
    if(b->box) {
        if(sc->skin.slide_bg!=LC_BG_NONE){lc_canvas_shadow(&sc->canvas,r.x,r.y,r.w,r.h);lc_scene_panel(sc,r,sc->skin.panel_bg);}
        lc_canvas_box(&sc->canvas,r.x,r.y,r.w,r.h,sc->mode.ascii?LC_BOX_ASCII:LC_BOX_SINGLE,frame);
        if(!EMPTY(b->title))lc_canvas_text_c(&sc->canvas,r.x+2,r.y,r.w-4,lc_render_join(sc," ",b->title," "),sc->skin.title);
        r=lc_rect_make(r.x+2,r.y+1,r.w-4,r.h-2);
        if(b->color<0 && sc->skin.slide_bg!=LC_BG_NONE)base=lc_contrast_on(sc->skin.panel_bg)==0?0:7;
    } else if(!EMPTY(b->title)) {
        lc_canvas_text(&sc->canvas,r.x,r.y,lc_text_truncate(sc->ctx,b->title,(size_t)MAX(0,r.w)),sc->skin.heading,-1);r.y+=2;r.h-=2;
    }
    if(r.w<4 || r.h<1)return;
    head=tail=NULL;
    total=0;
    for(ri=0;ri<b->line_count;++ri) {
        s=b->lines[ri];memset(&proto,0,sizeof proto);proto.fg=base;proto.scale=px?MAX(1,b->size):1;t=trim(sc,s);
        if(strncmp(t,"## ",3)==0){proto.fg=sc->skin.accent;s=t+3;}
        else if(strncmp(t,"# ",2)==0) {
            proto.fg=sc->skin.heading;s=t+2;want=px?MAX(2,b->size):1;g=inline_marks(sc,s,&n);if(!g)return;
            proto.scale=(int)n*want<=r.w?want:(px?MAX(1,b->size):1);
        } else if(strncmp(t,"- ",2)==0 || strncmp(t,"* ",2)==0){proto.bullet=sc->mode.ascii?'*':0x25a0;proto.indent=2;s=t+2;}
        else if(strncmp(t,"> ",2)==0){proto.fg=sc->skin.dim;proto.indent=2;s=t+2;}
        if(!*t){if(!append_line(sc,&head,&tail,&proto,NULL))return;total+=proto.scale;continue;}
        width=(size_t)MAX(1,r.w/proto.scale-proto.indent);g=inline_marks(sc,s,&n);if(!g)return;
        wrapped=(marked *)lc_render_array(sc,n+1,sizeof *wrapped);if(!wrapped)return;used=0;
        line=append_line(sc,&head,&tail,&proto,wrapped);if(!line)return;total+=proto.scale;i=0;
        while(i<n) {
            e=i;while(e<n && g[e].c!=' ')++e;
            if(line->n && line->n+1+e-i>width){proto.bullet=0;line=append_line(sc,&head,&tail,&proto,wrapped+used);if(!line)return;total+=proto.scale;}
            if(line->n){wrapped[used].c=' ';wrapped[used++].strong=0;++line->n;}
            for(k=i;k<e;++k) {
                if(line->n>=width){proto.bullet=0;line=append_line(sc,&head,&tail,&proto,wrapped+used);if(!line)return;total+=proto.scale;}
                wrapped[used++]=g[k];++line->n;
            }
            i=e;while(i<n && g[i].c==' ')++i;
        }
    }
    y=r.y+(b->middle?MAX(0,(r.h-total)/2):0);
    for(line=head;line;line=line->next) {
        if(y+line->scale>r.y+r.h) {
            lc_canvas_text_r(&sc->canvas,r.x,r.y+r.h-1,r.w,"...",sc->skin.dim);
            s=lc_render_join(sc,"the text does not fit its block: ",lc_render_uint(sc,(size_t)total)," rows of text, ");
            lc_scene_fit(sc,lc_render_join(sc,s,lc_render_uint(sc,(size_t)r.h)," rows of room"));break;
        }
        len=((int)line->n+line->indent)*line->scale;x=r.x+(b->align<0?0:(b->align==0?(r.w-len)/2:r.w-len));if(x<r.x)x=r.x;
        if(line->bullet){lc_utf8_encode(line->bullet,bytes);lc_scene_text(sc,x,y,bytes,sc->skin.bullet,-1,line->scale);}
        x+=line->indent*line->scale;i=0;
        while(i<line->n) {
            e=i;while(e<line->n && line->g[e].strong==line->g[i].strong)++e;run=glyph_run(sc,line->g+i,e-i);
            lc_scene_text(sc,x,y,run,line->g[i].strong?sc->skin.accent:line->fg,-1,line->scale);x+=(int)(e-i)*line->scale;i=e;
        }
        y+=line->scale;
    }
}

static void draw_stat(lc_scene *sc,const lc_block *b,lc_rect r,int frame)
{
    lc_rect in;int len,extra,k,t,total,y,fg,light,down,up,c;const char *s,*arrow;
    if(sc->skin.slide_bg!=LC_BG_NONE){lc_canvas_shadow(&sc->canvas,r.x,r.y,r.w,r.h);lc_scene_panel(sc,r,sc->skin.panel_bg);}
    lc_canvas_box(&sc->canvas,r.x,r.y,r.w,r.h,sc->mode.ascii?LC_BOX_ASCII:LC_BOX_SINGLE,frame);
    if(!EMPTY(b->title))lc_canvas_text_c(&sc->canvas,r.x+2,r.y,r.w-4,lc_render_join(sc," ",b->title," "),sc->skin.title);
    in=lc_rect_make(r.x+2,r.y+1,r.w-4,r.h-2);if(in.w<2 || in.h<1)return;
    len=MAX(1,(int)lc_text_width(b->value));extra=(EMPTY(b->label)?0:1)+(EMPTY(b->delta)?0:1);k=1;
    if(sc->mode.pixel)for(t=4;t>=1;--t)if(len*t<=in.w && t+extra<=in.h){k=t;break;}
    total=k+extra;y=in.y+MAX(0,(in.h-total)/2);fg=b->color>=0?b->color:sc->skin.title;
    if(len>in.w) {
        s=lc_render_join(sc,"stat \"",b->value,"\" is cut short: ");lc_scene_fit(sc,lc_render_join(sc,s,lc_render_uint(sc,(size_t)in.w)," characters fit"));
    }
    if((int)lc_text_width(b->label)>in.w) {
        s=lc_render_join(sc,"stat label \"",b->label,"\" is cut short: ");lc_scene_fit(sc,lc_render_join(sc,s,lc_render_uint(sc,(size_t)in.w)," characters fit"));
    }
    lc_scene_big(sc,in.x,y,in.w,b->value,fg,k,0);y+=k;light=lc_contrast_on(sc->skin.panel_bg)==0 && sc->skin.slide_bg!=LC_BG_NONE;
    if(!EMPTY(b->label) && y<=BOTTOM(in))lc_canvas_text_c(&sc->canvas,in.x,y++,in.w,b->label,light?0:7);
    if(!EMPTY(b->delta) && y<=BOTTOM(in)) {
        s=trim(sc,b->delta);down=*s=='-' || strncmp(s,"\342\210\222",3)==0 || strncmp(s,"\342\226\274",3)==0;
        up=*s=='+' || strncmp(s,"\342\226\262",3)==0;c=down?(light?1:9):(up?(light?2:10):sc->skin.dim);
        arrow=sc->mode.ascii?"":(down?"\342\226\274 ":(up?"\342\226\262 ":""));
        lc_canvas_text_c(&sc->canvas,in.x,y,in.w,lc_render_join(sc,arrow,s,""),c);
    }
}
static void draw_blocks(lc_scene *sc,lc_block *blocks,size_t count,const lc_slide_view *v,int depth)
{
    size_t i,n;lc_block *b,msg;lc_rect r,in;int frame,under,focus;lc_chart_options o;const char *lines[5];
    if(depth>32 || (count && !blocks)){lc_scene_fail(sc,LC_EINVAL);return;}
    for(i=0;i<count;++i) {
        b=blocks+i;r=b->rect;lc_scene_where(sc,b->path);
        if(r.w<4 || r.h<2){lc_scene_fit(sc,"no room left for this block on the slide");continue;}
        focus=v->focus && b->tag==v->focus;frame=focus?sc->skin.accent:sc->skin.frame;
        if(b->kind==3 || b->kind==4){draw_blocks(sc,b->children,b->child_count,v,depth+1);continue;}
        if(b->kind==1 || b->kind==2) {
            if(focus && b->kind==1 && !b->box)lc_canvas_box(&sc->canvas,r.x-1,r.y-1,r.w+2,r.h+2,sc->mode.ascii?LC_BOX_ASCII:LC_BOX_SINGLE,sc->skin.accent);
            if(b->kind==1)draw_text_block(sc,b,r,frame);else draw_stat(sc,b,r,frame);continue;
        }
        if(b->kind==5 || b->kind==6) {
            under=sc->skin.slide_bg==LC_BG_NONE?0:sc->skin.slide_bg;in=r;
            if(b->box) {
                in.w-=shadow_w(sc);in.h-=shadow_h(sc);
                if(sc->skin.slide_bg!=LC_BG_NONE){lc_canvas_shadow(&sc->canvas,in.x,in.y,in.w,in.h);lc_scene_panel(sc,in,sc->skin.panel_bg);under=sc->skin.panel_bg;}
                lc_canvas_box(&sc->canvas,in.x,in.y,in.w,in.h,sc->mode.ascii?LC_BOX_ASCII:LC_BOX_DOUBLE,frame);
                if(!EMPTY(b->title))lc_canvas_text_c(&sc->canvas,in.x+2,in.y,in.w-4,lc_render_join(sc," ",b->title," "),sc->skin.title);
                in=lc_rect_make(in.x+2,in.y+1,in.w-4,in.h-2);
            } else {
                if(focus)lc_canvas_box(&sc->canvas,r.x-1,r.y-1,r.w+2,r.h+2,sc->mode.ascii?LC_BOX_ASCII:LC_BOX_SINGLE,sc->skin.accent);
                if(!EMPTY(b->title)){lc_canvas_text(&sc->canvas,r.x,r.y,lc_text_truncate(sc->ctx,b->title,(size_t)r.w),sc->skin.heading,-1);in.y+=2;in.h-=2;}
            }
            if(b->kind==5)lc_draw_shapes(sc,in,b->shapes,b->shape_count,under);else lc_draw_flow(sc,in,&b->flow,under);continue;
        }
        if(b->kind!=0){lc_scene_fail(sc,LC_EINVAL);continue;}
        r.w-=shadow_w(sc);r.h-=shadow_h(sc);o=b->chart;o.color=o.color && sc->mode.color;o.frame_color=frame;
        if(focus){o.cur_series=v->cur_series;o.cur_index=v->cur_index;}
        if(!EMPTY(b->error)) {
            if(sc->skin.slide_bg!=LC_BG_NONE)lc_scene_panel(sc,r,sc->skin.panel_bg);
            lc_canvas_box(&sc->canvas,r.x,r.y,r.w,r.h,sc->mode.ascii?LC_BOX_ASCII:LC_BOX_DOUBLE,9);
            lc_canvas_text_c(&sc->canvas,r.x+2,r.y,r.w-4," cannot draw this chart ",9);lc_block_init(&msg);n=0;
            if(!EMPTY(b->data_ref)){lines[n++]=lc_render_join(sc,"**",b->data_ref,"**");lines[n++]="";}
            lines[n++]=b->error;lines[n++]="";lines[n++]=lc_render_join(sc,"> ",b->path,"");
            msg.lines=lines;msg.line_count=n;msg.align=0;msg.middle=1;msg.color=15;
            draw_text_block(sc,&msg,lc_rect_make(r.x+3,r.y+1,r.w-6,r.h-2),sc->skin.frame);
        } else lc_scene_fail(sc,lc_render_chart(sc,r,&b->data,&o));
    }
}
void lc_draw_status(lc_scene *sc,const char *left,const char *hint,const char *position,int bad)
{
    int w,y,fg,bg,x,px,left_min,width;const char **items,*p,*end,*sp,*key;size_t count,n;char *item;
    if(!sc)return;
    left=TXT(left);
    hint=TXT(hint);
    position=TXT(position);
    w=sc->canvas.w;
    y=sc->canvas.h-1;
    fg=bad?15:sc->skin.bar_fg;bg=bad?1:sc->skin.bar_bg;
    for(x=0;x<w;++x)lc_canvas_put(&sc->canvas,x,y,' ',fg,bg);
    px=w-(int)lc_text_width(position)-1;lc_canvas_text(&sc->canvas,px,y,position,fg,bg);
    items=(const char **)lc_render_array(sc,strlen(hint)+1,sizeof *items);if(!items)return;count=0;width=0;p=hint;
    while(*p) {
        end=strstr(p,"  ");if(!end)end=p+strlen(p);n=(size_t)(end-p);
        if(n){item=(char *)lc_render_array(sc,n+1,1);if(!item)return;memcpy(item,p,n);items[count++]=item;width+=(int)lc_text_width(item)+2;}
        p=*end?end+2:end;
    }
    left_min=MIN((int)lc_text_width(left),bad || !*hint?200:40);
    while(count && width>px-2-left_min)width-=(int)lc_text_width(items[--count])+2;
    x=px-1-width;lc_canvas_text(&sc->canvas,1,y,lc_text_truncate(sc->ctx,left,(size_t)MAX(0,x-2)),fg,bg);
    for(n=0;n<count;++n) {
        p=items[n];sp=strchr(p,' ');
        if(sp){item=(char *)lc_render_array(sc,(size_t)(sp-p)+1,1);if(!item)return;memcpy(item,p,(size_t)(sp-p));key=item;}
        else{key=p;sp="";}
        lc_canvas_text(&sc->canvas,x,y,key,sc->skin.bar_key,bg);lc_canvas_text(&sc->canvas,x+(int)lc_text_width(key),y,sp,fg,bg);x+=(int)lc_text_width(p)+2;
    }
}
static lc_lines paragraphs(lc_scene *sc,const char *s)
{
    lc_lines lines;char *copy,*p;size_t n;s=TXT(s);n=strlen(s);
    lines.n=0;lines.v=(const char **)lc_render_array(sc,n+1,sizeof *lines.v);copy=(char *)lc_render_array(sc,n+1,1);
    if(!lines.v || !copy)return lines;
    memcpy(copy,s,n+1);
    lines.v[lines.n++]=copy;
    for(p=copy;*p;++p) {
        if(*p=='\n') {
            *p=0;
            lines.v[lines.n++]=p+1;
        }
    }
    return lines;
}
lc_status lc_render_slide(lc_scene *sc,lc_slide *s,const lc_slide_view *view)
{
    lc_slide_view defaults;const lc_slide_view *v;int w,h,px,bare,margin,top,bottom,k,y,rule,fits,nh;lc_rect body,r;
    lc_block sub;lc_lines lines;const char *msg,*pos;
    if(!sc)return LC_EINVAL;
    if(!view){lc_slide_view_init(&defaults);view=&defaults;}v=view;w=sc->canvas.w;h=sc->canvas.h;
    if(!s){lc_canvas_text_c(&sc->canvas,0,h/2,w,"this deck has no slides",sc->skin.text);return lc_scene_status(sc);}
    px=sc->mode.pixel;bare=v->bare && EMPTY(s->title);margin=bare?(sc->skin.slide_bg==LC_BG_NONE?0:1):2;
    top=bare?(sc->skin.slide_bg==LC_BG_NONE?0:1):1;bottom=h-(v->chrome?1:0);
    if(!s->block_count) {
        k=px?((int)lc_text_width(s->title)*3<=w-8?3:2):1;y=MAX(1,(bottom-k-4)/2);
        lc_scene_big(sc,2,y,w-4,s->title,sc->skin.heading,k,0);y+=k+1;rule=MIN(w-12,MAX(20,(int)lc_text_width(s->title)*k+8));
        lc_canvas_hline(&sc->canvas,(w-rule)/2,y,rule,sc->mode.ascii?'=':0x2550,sc->skin.accent);y+=2;
        lc_block_init(&sub);lines=paragraphs(sc,s->subtitle);sub.lines=lines.v;sub.line_count=lines.n;sub.align=0;
        if(!EMPTY(s->subtitle))draw_text_block(sc,&sub,lc_rect_make(6,y,w-12,MAX(1,bottom-y-1)),sc->skin.frame);
    } else {
        if(!EMPTY(s->title)) {
            k=px?2:1;lc_scene_where(sc,lc_render_join(sc,s->path,".title",""));fits=(w-2*margin-2)/k;
            if((int)lc_text_width(s->title)>fits) {
                msg=lc_render_join(sc,"the title is cut short: ",lc_render_uint(sc,lc_text_width(s->title))," characters, ");
                lc_scene_fit(sc,lc_render_join(sc,msg,lc_render_uint(sc,(size_t)MAX(0,fits))," fit"));
            }
            lc_scene_big(sc,margin+1,top,w-2*margin-2,s->title,sc->skin.heading,k,-1);top+=k;
            if(!EMPTY(s->subtitle)){lc_canvas_text(&sc->canvas,margin+1,top,lc_text_truncate(sc->ctx,s->subtitle,(size_t)MAX(0,w-2*margin-2)),sc->skin.dim,-1);++top;}++top;
        }
        body=lc_rect_make(margin,top,w-2*margin,bottom-top-(bare?0:1));
        if(sc->skin.slide_bg==LC_BG_NONE && bare)body=lc_rect_make(0,top,w,bottom-top);
        place(sc,s->blocks,s->block_count,body,s->layout,0);draw_blocks(sc,s->blocks,s->block_count,v,0);
    }
    if(v->notes && !EMPTY(s->notes)) {
        nh=MIN(h-4,MAX(6,h/3));r=lc_rect_make(4,bottom-nh-1,w-8,nh);lc_scene_cover(sc,lc_rect_make(r.x,r.y,r.w+2,r.h+1));
        lc_block_init(&sub);sub.box=1;sub.title="notes";lines=paragraphs(sc,s->notes);sub.lines=lines.v;sub.line_count=lines.n;draw_text_block(sc,&sub,r,sc->skin.frame);
    }
    if(v->chrome) {
        msg=EMPTY(v->message)?v->footer:v->message;pos=lc_render_join(sc,lc_render_uint(sc,v->index+1),"/",lc_render_uint(sc,v->count));lc_draw_status(sc,msg,v->hint,pos,v->message_bad);
    }
    return lc_scene_status(sc);
}
