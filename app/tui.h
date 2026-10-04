#ifndef CHARTS_APP_TUI_H
#define CHARTS_APP_TUI_H
#include "model.h"
typedef struct app_present_opts {
    const char *gfx,*theme;
    int scale,slide,color,verbose;
    const app_load_opts *load;
    const app_spec *cli;
} app_present_opts;
int app_run_presenter(app_deck *deck,const app_present_opts *options);
#endif
