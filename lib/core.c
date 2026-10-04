#include "internal.h"
#include <stddef.h>

static void *default_alloc(void *user,size_t bytes) { (void)user; return malloc(bytes); }
static void default_free(void *user,void *ptr) { (void)user; free(ptr); }
int lc_size_mul(size_t a,size_t b,size_t *out) {
    if (a && b > (size_t)-1 / a) return 0;
    *out=a*b; return 1;
}
lc_status lc_context_create(const lc_allocator *allocator,lc_context **out) {
    lc_allocator a; lc_context *ctx;
    if (!out) return LC_EINVAL;
    *out=NULL;
    if (allocator) { a=*allocator; if (!a.alloc || !a.free) return LC_EINVAL; }
    else { a.user=NULL; a.alloc=default_alloc; a.free=default_free; }
    ctx=(lc_context *)a.alloc(a.user,sizeof *ctx);
    if (!ctx) return LC_ENOMEM;
    ctx->allocator=a; ctx->scratch=NULL; ctx->status=LC_OK; *out=ctx; return LC_OK;
}
void *lc_alloc(lc_context *ctx,size_t bytes) {
    void *p;
    if (!ctx) return NULL;
    p=ctx->allocator.alloc(ctx->allocator.user,bytes ? bytes : 1);
    if (!p) ctx->status=LC_ENOMEM;
    return p;
}
lc_status lc_context_status(const lc_context *ctx) { return ctx ? ctx->status : LC_EINVAL; }
void lc_free(lc_context *ctx,void *ptr) { if (ctx && ptr) ctx->allocator.free(ctx->allocator.user,ptr); }
void lc_scratch_reset(lc_context *ctx) {
    lc_arena *p;
    if (!ctx) return;
    for (p=ctx->scratch;p;p=p->next) p->used=0;
    ctx->status=LC_OK;
}
void *lc_scratch(lc_context *ctx,size_t bytes) {
    lc_arena *p,*tail; size_t rounded,capacity,align;
    if (!ctx) return NULL;
    align=sizeof(lc_align); if (!bytes) bytes=1;
    if (bytes>(size_t)-1-(align-1)) { ctx->status=LC_EOVERFLOW; return NULL; }
    rounded=((bytes+align-1)/align)*align; tail=NULL;
    for (p=ctx->scratch;p;p=p->next) {
        if (rounded<=p->capacity-p->used) {
            void *result=(unsigned char *)(p+1)+p->used;
            p->used+=rounded; return result;
        }
        tail=p;
    }
    capacity=LC_MAX(rounded,(size_t)4096);
    if (capacity>(size_t)-1-sizeof *p) { ctx->status=LC_EOVERFLOW; return NULL; }
    p=(lc_arena *)lc_alloc(ctx,sizeof *p+capacity);
    if (!p) return NULL;
    p->next=NULL; p->used=rounded; p->capacity=capacity;
    if (tail) tail->next=p; else ctx->scratch=p;
    return (void *)(p+1);
}
void lc_context_destroy(lc_context *ctx) {
    lc_arena *p,*next; lc_allocator a;
    if (!ctx) return;
    for (p=ctx->scratch;p;p=next) { next=p->next; lc_free(ctx,p); }
    a=ctx->allocator; a.free(a.user,ctx);
}
char *lc_strdup(lc_context *ctx,const char *text) {
    size_t n; char *out;
    if (!text) text="";
    n=strlen(text); if (n==(size_t)-1) return NULL;
    out=(char *)lc_alloc(ctx,n+1); if (out) memcpy(out,text,n+1); return out;
}
const char *lc_status_string(lc_status status) {
    static const char *const names[]={"success","invalid argument","out of memory","size overflow","output failed"};
    return (unsigned)status<sizeof names/sizeof names[0] ? names[status] : "unknown error";
}
int lc_finite(double v) { return v==v && v<=DBL_MAX && v>=-DBL_MAX; }
double lc_round(double v) { return v<0 ? ceil(v-0.5) : floor(v+0.5); }
int lc_clamp_int(double v,int lo,int hi) { if (!(v>lo)) return lo; if (v>hi) return hi; return (int)v; }
void lc_scene_fail(lc_scene *sc,lc_status s) { if (sc && sc->status==LC_OK) sc->status=s; }

int lc_skin_init(lc_skin *s,const char *name) {
    static const lc_skin dos={4,0,15,11,7,7,15,8,7,14,14,7,15,0,11,15,7,14,11,0,7,1,0,11,0,14,11,8,7};
    if (!s) return 0;
    *s=dos;
    if (!name || lc_ieq(name,"dos") || lc_ieq(name,"blue")) return 1;
    if (lc_ieq(name,"black") || lc_ieq(name,"dark") || lc_ieq(name,"none")) {
        s->slide_bg=LC_BG_NONE; s->frame=8; s->title=15; s->subtitle=8;
        s->xlabel=8; s->ylabel=8; s->shadow=8; s->heading=15; s->text=7;
        s->dim=8; s->bar_fg=15; s->bar_bg=4; s->bar_key=11; s->table_head=15; return 1;
    }
    if (lc_ieq(name,"light") || lc_ieq(name,"paper")) {
        s->slide_bg=7; s->panel_bg=15; s->frame=0; s->title=4; s->subtitle=8;
        s->axis=0; s->tick=0; s->grid=7; s->label=0; s->xlabel=4; s->ylabel=4;
        s->legend=0; s->value=0; s->heading=4; s->text=0; s->dim=8;
        s->accent=1; s->bullet=1; s->bar_fg=15; s->bar_bg=4; s->bar_key=11;
        s->note_fg=15; s->note_bg=1; s->cursor_fg=15; s->cursor_bg=4;
        s->table_head=4; s->table_rule=8; s->table_row=0; return 1;
    }
    return 0;
}
int lc_skin_set(lc_skin *s,const char *key,int color) {
    typedef struct field { const char *name; size_t offset; } field;
#define FIELD(n) {#n,offsetof(lc_skin,n)}
    static const field fields[]={FIELD(slide_bg),FIELD(panel_bg),FIELD(frame),FIELD(title),FIELD(subtitle),FIELD(axis),FIELD(tick),FIELD(grid),FIELD(label),FIELD(xlabel),FIELD(ylabel),FIELD(legend),FIELD(value),FIELD(shadow),FIELD(heading),FIELD(text),FIELD(dim),FIELD(accent),FIELD(bullet),FIELD(bar_fg),FIELD(bar_bg),FIELD(bar_key),FIELD(note_fg),FIELD(note_bg),FIELD(cursor_fg),FIELD(cursor_bg),FIELD(table_head),FIELD(table_rule),FIELD(table_row)};
#undef FIELD
    size_t i;
    if (!s || !key || color<0 || (color>15 && color!=LC_BG_NONE)) return 0;
    for (i=0;i<sizeof fields/sizeof fields[0];i++) if (!strcmp(key,fields[i].name)) {
        *((unsigned char *)s+fields[i].offset)=(unsigned char)color; return 1;
    }
    return 0;
}
void lc_chart_options_init(lc_chart_options *o) {
    if (!o) return;
    memset(o,0,sizeof *o); o->type="bar"; o->title=""; o->subtitle="";
    o->xlabel=""; o->ylabel=""; o->palette="dos"; o->frame="double"; o->source="";
    o->legend=1; o->grid=1; o->color=1; o->shadow=1; o->zero_base=1;
    o->explode=-2; o->depth=2; o->bins=10; o->prec=-1;
    o->cur_series=-1; o->cur_index=-1; o->frame_color=-1;
}
