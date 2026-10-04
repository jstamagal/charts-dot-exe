#include "libcharts.h"
#include <stdlib.h>
#include <stdio.h>
#include <float.h>
#include <string.h>
static const char *types[]={"bar","stacked","hbar","dumbbell","line","area","pie","pie3d","donut","scatter","hist","table"};
int main(void){lc_context *ctx;lc_scene *sc;lc_mode mode;lc_dataset ds;lc_series series[3];lc_chart_options o;lc_rect r;lc_image im;double v[3][8];const char *labels[]={"a","b","c","d","e","f","g","h"};int i,j,k,t,m;double values[]={-DBL_MAX,DBL_MAX,1e300,-1e300,1e-300,-1e-300,0,1};
lc_context_create(NULL,&ctx);memset(&ds,0,sizeof ds);memset(series,0,sizeof series);ds.series=series;ds.series_count=3;ds.labels=labels;ds.label_count=8;
for(i=0;i<3;i++){series[i].name="series";series[i].values=v[i];series[i].count=8;series[i].decimals=-1;}
for(k=0;k<16;k++)for(t=0;t<12;t++)for(m=0;m<3;m++){for(i=0;i<3;i++)for(j=0;j<8;j++)v[i][j]=k<8?values[k]:values[(i+j+k)%8];mode.pixel=m==0;mode.ascii=m==2;mode.color=1;lc_scene_create(ctx,80,30,mode,&sc);r.x=0;r.y=0;r.w=75;r.h=27;lc_chart_options_init(&o);o.type=types[t];o.values=1;lc_render_chart(sc,r,&ds,&o);if(lc_scene_to_image(sc,&im)==LC_OK)lc_free(ctx,im.pixels);lc_scene_destroy(sc);lc_scratch_reset(ctx);}
lc_context_destroy(ctx);puts("576 extreme-value chart cases passed");return 0;}
