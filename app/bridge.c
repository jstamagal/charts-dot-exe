#include "bridge.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

static const char *str(const char *s) { return s ? s : ""; }
static void *array(lc_context *ctx,size_t n,size_t size)
{
    void *p;
    if (!n) return NULL;
    if (size && n > (size_t)-1 / size) return NULL;
    p=lc_scratch(ctx,n*size);
    if (p) memset(p,0,n*size);
    return p;
}
static const char *js(const app_json *v)
{ return v && v->type==APP_JSON_STRING ? str(v->string) : ""; }
static const char *field(const app_json *v,const char *key)
{ return js(app_json_get(v,key)); }
static int integer(const app_json *v,const char *key,int fallback)
{
    const app_json *p;
    p=app_json_get(v,key);
    return p && p->type==APP_JSON_NUMBER && p->number>=INT_MIN && p->number<=INT_MAX ? (int)p->number : fallback;
}
static int boolean(const app_json *v,const char *key,int fallback)
{
    const app_json *p;
    p=app_json_get(v,key);
    return p ? (p->type==APP_JSON_BOOL ? p->boolean : 1) : fallback;
}
static int color(const app_json *v,const char *key,int fallback)
{
    int c;
    c=fallback;
    app_parse_color(app_json_get(v,key),&c);
    return c;
}

lc_status app_dataset_view(lc_context *ctx,const app_dataset *data,lc_dataset *out)
{
    lc_series *series;
    lc_text_column *text;
    size_t i;
    memset(out,0,sizeof *out);
    out->title=str(data->title);out->source=str(data->source);
    out->label_name=str(data->label_name);out->x_name=str(data->x_name);
    out->labels=(const char *const *)data->labels;out->label_count=data->label_count;
    series=(lc_series *)array(ctx,data->series_count,sizeof *series);
    text=(lc_text_column *)array(ctx,data->text_count,sizeof *text);
    if ((data->series_count && !series) || (data->text_count && !text)) return LC_ENOMEM;
    for (i=0;i<data->series_count;i++) {
        series[i].name=str(data->series[i].name);series[i].values=data->series[i].values;
        series[i].valid=data->series[i].valid;series[i].count=data->series[i].count;
        series[i].color=data->series[i].color;series[i].col=data->series[i].column;
        series[i].decimals=data->series[i].decimals;
    }
    for (i=0;i<data->text_count;i++) {
        text[i].name=str(data->text[i].name);text[i].values=(const char *const *)data->text[i].values;
        text[i].count=data->text[i].count;text[i].col=data->text[i].column;
    }
    out->series=series;out->series_count=data->series_count;
    out->text=text;out->text_count=data->text_count;
    return LC_OK;
}
static lc_status apply_spec(lc_context *ctx,lc_chart_options *o,const app_spec *s)
{
    size_t i;
    lc_annotation *notes;
    lc_color *colors;
    lc_error_bars *errors;
    if (!s) return LC_OK;
#define FIELD(bit,member) if (s->flags & APP_SPEC_##bit) o->member=s->member
    FIELD(TYPE,type);FIELD(PALETTE,palette);FIELD(FRAME,frame);FIELD(TITLE,title);
    FIELD(SUBTITLE,subtitle);FIELD(XLABEL,xlabel);FIELD(YLABEL,ylabel);
    FIELD(LEGEND,legend);FIELD(VALUES,values);FIELD(GRID,grid);FIELD(SHADOW,shadow);
    FIELD(COLOR,color);FIELD(DEPTH,depth);FIELD(BINS,bins);FIELD(PREC,prec);
    FIELD(EXPLODE,explode);FIELD(XY,xy);
#undef FIELD
    if (s->flags & APP_SPEC_LO) { o->lo=s->lo;o->has_lo=1; }
    if (s->flags & APP_SPEC_HI) { o->hi=s->hi;o->has_hi=1; }
    if (s->flags & APP_SPEC_NOTES) {
        notes=(lc_annotation *)array(ctx,s->note_count,sizeof *notes);
        if (s->note_count && !notes) return LC_ENOMEM;
        for (i=0;i<s->note_count;i++) {
            const app_annotation *n;
            n=&s->notes[i];notes[i].kind=(int)n->kind;
            notes[i].text=str(n->text);notes[i].label=str(n->label);notes[i].series=str(n->series);
            notes[i].index=n->index;notes[i].series_i=n->series_index;notes[i].color=n->color;
            notes[i].value=n->value;notes[i].fx=n->fx;notes[i].fy=n->fy;
        }
        o->notes=notes;o->note_count=s->note_count;
    }
    if (s->flags & APP_SPEC_COLORS) {
        colors=(lc_color *)array(ctx,s->color_count,sizeof *colors);
        if (s->color_count && !colors) return LC_ENOMEM;
        for (i=0;i<s->color_count;i++) { colors[i].name=str(s->colors[i].name);colors[i].color=s->colors[i].color; }
        o->colors=colors;o->color_count=s->color_count;
    }
    if (s->flags & APP_SPEC_ERRORS) {
        errors=(lc_error_bars *)array(ctx,s->error_count,sizeof *errors);
        if (s->error_count && !errors) return LC_ENOMEM;
        for (i=0;i<s->error_count;i++) {
            errors[i].series=str(s->errors[i].series);errors[i].lo=str(s->errors[i].lo);
            errors[i].hi=str(s->errors[i].hi);errors[i].plus_minus=s->errors[i].plus_minus;
        }
        o->errors=errors;o->error_count=s->error_count;
    }
    return LC_OK;
}
lc_status app_block_options(lc_context *ctx,const app_deck *deck,const app_block *block,const app_spec *cli,lc_chart_options *out)
{
    lc_chart_options_init(out);
    if (deck->palette && *deck->palette) out->palette=deck->palette;
    if (apply_spec(ctx,out,&block->data.spec)!=LC_OK || apply_spec(ctx,out,&block->spec)!=LC_OK || apply_spec(ctx,out,cli)!=LC_OK) return LC_ENOMEM;
    if (!*str(out->title) && !(block->spec.flags&APP_SPEC_TITLE) && !(cli && (cli->flags&APP_SPEC_TITLE))) out->title=str(block->data.title);
    if (deck->implicit && !*str(deck->file)) out->source=str(block->data_ref);
    return LC_OK;
}
void app_apply_theme(lc_scene *scene,const app_deck *deck,const char *override_name)
{
    const app_json *theme;
    lc_skin *skin;
    size_t i;
    int c;
    skin=lc_scene_skin(scene);
    if (override_name && *override_name) { lc_skin_init(skin,override_name);return; }
    if (!deck->theme || !*deck->theme) return;
    theme=app_json_get(deck->root,"theme");
    if (!theme || theme->type!=APP_JSON_OBJECT) { lc_skin_init(skin,deck->theme);return; }
    lc_skin_init(skin,*field(theme,"base") ? field(theme,"base") : "dos");
    for (i=0;i<theme->count;i++) {
        const app_json_member *m;
        m=&theme->members[i];c=0;
        if (app_parse_color(m->value,&c)) lc_skin_set(skin,m->key,c);
        else if (!strcmp(m->key,"slide_bg") && (!strcmp(js(m->value),"none") || !strcmp(js(m->value),"transparent"))) lc_skin_set(skin,m->key,LC_BG_NONE);
    }
}
static const char *shape_text(lc_context *ctx,const app_json *v)
{
    size_t i,n,k;
    char *s;
    if (!v || v->type!=APP_JSON_ARRAY) return js(v);
    n=1;
    for (i=0;i<v->count;i++) {
        k=strlen(js(v->items[i]));
        if (k>(size_t)-1-n-1) return NULL;
        n+=k+1;
    }
    s=(char *)lc_scratch(ctx,n);if (!s) return NULL;k=0;
    for (i=0;i<v->count;i++) {
        if (i) s[k++]='\n';
        n=strlen(js(v->items[i]));memcpy(s+k,js(v->items[i]),n);k+=n;
    }
    s[k]=0;return s;
}
static double coord(const app_json *a,size_t i)
{
    double n;
    n=a && a->type==APP_JSON_ARRAY && i<a->count && a->items[i]->type==APP_JSON_NUMBER ? a->items[i]->number : 0;
    return n;
}
static lc_status shapes_view(lc_context *ctx,const app_json *a,lc_block *b)
{
    static const char *keys[]={"rect","ellipse","poly","line","arrow","label"};
    static const int kinds[]={0,1,2,3,3,4};
    size_t i,j,k,n;
    lc_shape *shapes,*s;
    lc_pt *p;
    const app_json *v,*points,*head,*fill;
    double x,y,w,h;
    if (!a || a->type!=APP_JSON_ARRAY) return LC_OK;
    shapes=(lc_shape *)array(ctx,a->count,sizeof *shapes);
    if (a->count && !shapes) return LC_ENOMEM;
    b->shapes=shapes;b->shape_count=a->count;
    for (i=0;i<a->count;i++) {
        v=a->items[i];s=&shapes[i];points=NULL;k=0;
        for (j=0;j<6;j++) if ((points=app_json_get(v,keys[j]))!=NULL) { k=j;break; }
        s->kind=kinds[k];n=k<2 ? 2 : (k==5 ? 1 : (points && points->type==APP_JSON_ARRAY ? points->count : 0));
        p=(lc_pt *)array(ctx,n,sizeof *p);if (n && !p) return LC_ENOMEM;
        s->points=p;s->point_count=n;
        if (k<2) {
            x=coord(points,0);y=coord(points,1);w=coord(points,2);h=coord(points,3);
            p[0].x=x;p[0].y=y;p[1].x=x+w;p[1].y=y+h;
        } else if (k==5) { p[0].x=coord(points,0);p[0].y=coord(points,1); }
        else for (j=0;j<n;j++) { p[j].x=coord(points->items[j],0);p[j].y=coord(points->items[j],1); }
        for (j=0;j<n;j++) {
            if (p[j].x < -12) p[j].x=-12;
            if (p[j].x>24) p[j].x=24;
            if (p[j].y < -12) p[j].y=-12;
            if (p[j].y>24) p[j].y=24;
        }
        s->text=shape_text(ctx,app_json_get(v,"text"));if (!s->text) return LC_ENOMEM;
        s->color=color(v,"color",color(v,"colour",-1));s->text_color=color(v,"text_color",-1);s->border=color(v,"border",-1);
        s->dither=integer(v,"dither",0);s->depth=integer(v,"depth",0);s->width=integer(v,"width",1);s->size=integer(v,"size",1);
        s->align=app_streq_ci(field(v,"align"),"right") ? 1 : (app_streq_ci(field(v,"align"),"center") || app_streq_ci(field(v,"align"),"centre") ? 0 : -1);
        s->shadow=boolean(v,"shadow",0);s->fill=boolean(v,"fill",1);s->dash=boolean(v,"dash",0);s->head=k==4;
        fill=app_json_get(v,"fill");if (app_streq_ci(js(fill),"none")) s->fill=0;
        head=app_json_get(v,"head");
        if (head) {
            s->head=head->type==APP_JSON_BOOL ? head->boolean : (app_streq_ci(js(head),"end") || app_streq_ci(js(head),"both"));
            s->tail=app_streq_ci(js(head),"start") || app_streq_ci(js(head),"both");
        }
    }
    return LC_OK;
}
static int node_index(const lc_flow *flow,const char *id)
{
    size_t i;
    for (i=0;i<flow->node_count;i++) if (!strcmp(flow->nodes[i].id,id)) return (int)i;
    return -1;
}
static lc_status flow_view(lc_context *ctx,const app_json *v,lc_flow *flow)
{
    const app_json *a,*es,*e,*labels;
    lc_flow_node *nodes;
    lc_flow_edge *edges;
    size_t i,j,n;
    const char *from,*to,*gt,*key;
    char *left;
    size_t len;
    a=app_json_get(v,"flow");if (!a || a->type!=APP_JSON_ARRAY) return LC_OK;
    nodes=(lc_flow_node *)array(ctx,a->count,sizeof *nodes);if (a->count && !nodes) return LC_ENOMEM;
    flow->nodes=nodes;flow->node_count=a->count;flow->down=app_streq_ci(field(v,"dir"),"down");
    for (i=0;i<a->count;i++) {
        e=a->items[i];nodes[i].color=-1;
        if (e->type==APP_JSON_OBJECT) {
            nodes[i].text=field(e,"text");nodes[i].id=app_json_get(e,"id") ? field(e,"id") : nodes[i].text;
            if (!*nodes[i].text) nodes[i].text=nodes[i].id;
            nodes[i].color=color(e,"color",color(e,"colour",-1));
        } else nodes[i].id=nodes[i].text=e->type==APP_JSON_NUMBER ? lc_fmt_val(ctx,e->number,-1) : js(e);
        if (!nodes[i].id || !nodes[i].text) return LC_ENOMEM;
    }
    es=app_json_get(v,"edges");n=es && es->type==APP_JSON_ARRAY ? es->count : (es || !a->count ? 0 : a->count-1);
    edges=(lc_flow_edge *)array(ctx,n,sizeof *edges);if (n && !edges) return LC_ENOMEM;
    flow->edges=edges;flow->edge_count=n;
    for (i=0;i<n;i++) {
        edges[i].color=-1;edges[i].text="";
        if (!es) { edges[i].from=(int)i;edges[i].to=(int)i+1;continue; }
        e=es->items[i];from=to="";
        if (e->type==APP_JSON_ARRAY && e->count>=2) {
            from=js(e->items[0]);to=js(e->items[1]);if (e->count>2) edges[i].text=js(e->items[2]);
        } else if (e->type==APP_JSON_OBJECT) {
            from=field(e,"from");to=field(e,"to");edges[i].text=field(e,"text");
            edges[i].color=color(e,"color",color(e,"colour",-1));edges[i].dash=boolean(e,"dash",0);
        }
        edges[i].from=node_index(flow,from);edges[i].to=node_index(flow,to);
    }
    labels=app_json_get(v,"labels");
    if (labels && labels->type==APP_JSON_OBJECT) for (i=0;i<labels->count;i++) {
        int f,t;
        key=labels->members[i].key;gt=strstr(key,"->");if (!gt) gt=strchr(key,'>');if (!gt) continue;
        while (isspace((unsigned char)*key)) key++;
        len=(size_t)(gt-key);while (len && isspace((unsigned char)key[len-1])) len--;
        left=(char *)lc_scratch(ctx,len+1);if (!left) return LC_ENOMEM;memcpy(left,key,len);left[len]=0;
        f=node_index(flow,left);key=gt+(*gt=='-' ? 2 : 1);while (isspace((unsigned char)*key)) key++;
        len=strlen(key);while (len && isspace((unsigned char)key[len-1])) len--;
        left=(char *)lc_scratch(ctx,len+1);if (!left) return LC_ENOMEM;memcpy(left,key,len);left[len]=0;t=node_index(flow,left);
        for (j=0;j<n;j++) if (edges[j].from==f && edges[j].to==t) edges[j].text=js(labels->members[i].value);
    }
    return LC_OK;
}
static lc_status block_view(lc_context *ctx,const app_deck *deck,app_block *a,lc_block *b,const app_spec *cli,int depth)
{
    size_t i;
    lc_block_init(b);if (depth>96) return LC_EINVAL;
    b->kind=(int)a->kind;b->tag=a;b->path=str(a->path);b->weight=a->weight;
    memcpy(b->at,a->at,sizeof b->at);b->has_at=a->has_at;
    b->error=str(a->error);b->data_ref=str(a->data_ref);b->title=str(a->title);
    b->lines=(const char *const *)a->lines;b->line_count=a->line_count;
    b->size=a->size;b->align=a->align;b->color=a->color;b->box=a->box;b->middle=a->middle;
    b->value=str(a->value);b->label=str(a->label);b->delta=str(a->delta);
    if (app_dataset_view(ctx,&a->data,&b->data)!=LC_OK || app_block_options(ctx,deck,a,cli,&b->chart)!=LC_OK) return LC_ENOMEM;
    if (shapes_view(ctx,a->shapes,b)!=LC_OK || flow_view(ctx,a->flow,&b->flow)!=LC_OK) return LC_ENOMEM;
    b->children=(lc_block *)array(ctx,a->child_count,sizeof *b->children);b->child_count=a->child_count;
    if (a->child_count && !b->children) return LC_ENOMEM;
    for (i=0;i<a->child_count;i++) {
        lc_status status;
        status=block_view(ctx,deck,&a->children[i],&b->children[i],cli,depth+1);if (status!=LC_OK) return status;
    }
    return LC_OK;
}
static void copy_layout(const lc_block *b)
{
    app_block *a;
    size_t i;
    a=(app_block *)b->tag;a->x=b->rect.x;a->y=b->rect.y;a->w=b->rect.w;a->h=b->rect.h;
    for (i=0;i<b->child_count;i++) copy_layout(&b->children[i]);
}
lc_status app_render_deck(lc_context *ctx,lc_scene *scene,app_deck *deck,int index,const lc_slide_view *view,const app_spec *cli,const char *theme)
{
    lc_slide slide;
    lc_slide_view local;
    app_slide *a;
    size_t i;
    lc_status status;
    app_apply_theme(scene,deck,theme);
    lc_canvas_clear(lc_scene_canvas(scene),lc_scene_skin(scene)->text,lc_scene_skin(scene)->slide_bg);
    if (!deck->slide_count) {
        lc_canvas_text_c(lc_scene_canvas(scene),0,lc_scene_rows(scene)/2,lc_scene_cols(scene),"this deck has no slides",lc_scene_skin(scene)->text);
        return lc_scene_status(scene);
    }
    if (index<0) index=0;
    if ((size_t)index>=deck->slide_count) index=(int)deck->slide_count-1;
    a=&deck->slides[index];memset(&slide,0,sizeof slide);
    slide.path=str(a->path);slide.title=str(a->title);slide.subtitle=str(a->subtitle);slide.notes=str(a->notes);slide.layout=str(a->layout);
    slide.blocks=(lc_block *)array(ctx,a->block_count,sizeof *slide.blocks);slide.block_count=a->block_count;
    if (a->block_count && !slide.blocks) return LC_ENOMEM;
    for (i=0;i<a->block_count;i++) { status=block_view(ctx,deck,&a->blocks[i],&slide.blocks[i],cli,0);if (status!=LC_OK) return status; }
    if (view) local=*view;else lc_slide_view_init(&local);
    local.index=(size_t)index;local.count=deck->slide_count;local.bare=deck->implicit && !*str(a->title);
    if (!local.footer || !*local.footer) {
        local.footer=*str(deck->footer) ? deck->footer : str(deck->title);
        if (!*local.footer && *str(deck->file)) {
            const char *slash,*back;
            slash=strrchr(deck->file,'/');back=strrchr(deck->file,'\\');
            if (!slash || (back && back>slash)) slash=back;
            local.footer=slash ? slash+1 : deck->file;
        }
    }
    status=lc_render_slide(scene,&slide,&local);
    for (i=0;i<a->block_count;i++) copy_layout(&slide.blocks[i]);
    return status;
}
int app_file_sink(void *user,const unsigned char *bytes,size_t count)
{ return fwrite(bytes,1,count,(FILE *)user)==count ? 0 : -1; }
