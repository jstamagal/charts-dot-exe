/* A standalone C89 libcharts client. No JSON, terminal or platform code.
 * cc -ansi -Iinclude examples/embed.c libcharts.a -lm -o embed
 * ./embed example.png
 */
#include "charts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int write_bytes(void *user,const unsigned char *bytes,size_t count)
{ return fwrite(bytes,1,count,(FILE *)user)==count ? 0 : -1; }
int main(int argc,char **argv)
{
    static const double values[]={12,18,15,27,33};
    static const char *labels[]={"Mon","Tue","Wed","Thu","Fri"};
    lc_context *ctx;
    lc_scene *scene;
    lc_mode mode;
    lc_dataset data;
    lc_series series;
    lc_chart_options chart;
    lc_rect rect;
    lc_image image;
    lc_sink sink;
    lc_status status;
    FILE *file;
    int result;
    if (argc!=2) { fputs("usage: embed OUTPUT.png\n",stderr);return 2; }
    status=lc_context_create(NULL,&ctx);if (status!=LC_OK) return 1;
    mode.pixel=1;mode.ascii=0;mode.color=1;
    status=lc_scene_create(ctx,80,30,mode,&scene);
    if (status!=LC_OK) { lc_context_destroy(ctx);return 1; }
    memset(&series,0,sizeof series);series.name="units";series.values=values;
    series.count=sizeof values/sizeof values[0];series.color=-1;series.decimals=-1;
    memset(&data,0,sizeof data);data.labels=labels;data.label_count=series.count;
    data.series=&series;data.series_count=1;
    lc_chart_options_init(&chart);chart.title="A week in libcharts";chart.values=1;
    rect.x=1;rect.y=1;rect.w=78;rect.h=28;
    status=lc_render_chart(scene,rect,&data,&chart);
    /* The caller can keep this byte-per-pixel target between frames or provide
     * its own packed framebuffer. Real VGA's planar conversion lives in app/. */
    image.width=640;image.height=480;image.stride=640;
    image.pixels=(unsigned char *)malloc(image.stride*(size_t)image.height);
    if (!image.pixels) status=LC_ENOMEM;
    if (status==LC_OK) status=lc_scene_draw_image(scene,&image);
    file=NULL;
    if (status==LC_OK) { file=fopen(argv[1],"wb");if (!file) status=LC_EIO; }
    if (status==LC_OK) { sink.user=file;sink.write=write_bytes;status=lc_write_png(ctx,&image,&sink); }
    if (file && fclose(file)) status=LC_EIO;
    result=status==LC_OK ? 0 : 1;
    if (result) fprintf(stderr,"libcharts: %s\n",lc_status_string(status));
    free(image.pixels);lc_scene_destroy(scene);lc_context_destroy(ctx);
    return result;
}
