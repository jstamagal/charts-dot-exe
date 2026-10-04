#include "libcharts.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef struct heap {size_t calls,fail,live;} heap;
static void *alloc(void *user,size_t n){heap*h=(heap*)user;void*p;if(h->calls++==h->fail)return NULL;p=malloc(n);if(p)h->live++;return p;}
static void release(void *user,void*p){heap*h=(heap*)user;if(p){assert(h->live);h->live--;free(p);}}
int main(void){
static const char *types[]={"bar","stacked","hbar","dumbbell","line","area","pie","pie3d","donut","scatter","hist","table"};
heap h;lc_allocator al;lc_context*ctx;lc_scene*sc;lc_mode m;lc_dataset d;lc_series s[3];lc_error_bars e;lc_annotation note;lc_chart_options o;lc_rect r;lc_image image;double a[]={10,20,15,25},b[]={5,12,14,20},sd[]={1,2,1,3};const char*labels[]={"one","two","three","four"};size_t fail;int type;
al.user=&h;al.alloc=alloc;al.free=release;memset(&d,0,sizeof d);memset(s,0,sizeof s);d.series=s;d.series_count=3;d.labels=labels;d.label_count=4;
s[0].name="alpha";s[0].values=a;s[0].count=4;s[0].decimals=-1;s[1]=s[0];s[1].name="beta";s[1].values=b;s[2]=s[0];s[2].name="sd";s[2].values=sd;
e.series="alpha";e.lo="sd";e.hi="";e.plus_minus=1;memset(&note,0,sizeof note);note.kind=0;note.label="two";note.series="alpha";note.text="annotation";note.color=-1;note.index=-1;note.series_i=-1;
m.pixel=1;m.ascii=0;m.color=1;r.x=1;r.y=1;r.w=75;r.h=26;
for(type=0;type<12;type++)for(fail=0;fail<100;fail++){
h.calls=0;h.fail=fail;h.live=0;
if(lc_context_create(&al,&ctx)==LC_OK){if(lc_scene_create(ctx,80,30,m,&sc)==LC_OK){lc_chart_options_init(&o);o.type=types[type];o.values=1;o.errors=&e;o.error_count=1;o.notes=&note;o.note_count=1;lc_render_chart(sc,r,&d,&o);if(lc_scene_to_image(sc,&image)==LC_OK)lc_free(ctx,image.pixels);lc_scene_destroy(sc);}lc_context_destroy(ctx);}assert(h.live==0);
}
puts("1200 chart allocation-failure positions passed");return 0;}
