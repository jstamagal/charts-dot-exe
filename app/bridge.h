#ifndef CHARTS_APP_BRIDGE_H
#define CHARTS_APP_BRIDGE_H
#include "model.h"
#include "charts.h"
/* Views below use context scratch storage. Reset it after destroying the scene. */
lc_status app_dataset_view(lc_context *ctx,const app_dataset *data,lc_dataset *out);
lc_status app_block_options(lc_context *ctx,const app_deck *deck,const app_block *block,const app_spec *cli,lc_chart_options *out);
void app_apply_theme(lc_scene *scene,const app_deck *deck,const char *override_name);
lc_status app_render_deck(lc_context *ctx,lc_scene *scene,app_deck *deck,int index,const lc_slide_view *view,const app_spec *cli,const char *theme);
int app_file_sink(void *user,const unsigned char *bytes,size_t count);
#endif
