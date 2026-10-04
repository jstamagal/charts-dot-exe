#ifndef CHARTS_APP_CLI_H
#define CHARTS_APP_CLI_H
#include <stdio.h>
#include "model.h"

typedef struct app_args {
  char **files; size_t file_count,file_capacity;
  char **types; size_t type_count,type_capacity;
  char *out,*png,*png_dir,*launcher,*example,*gfx,*theme;
  app_spec cli;
  app_load_opts load;
  int width,height,scale,slide;
  int show,print,check,json,describe,tile,ascii,verbose;
  int color,color_forced;
  int list_types,list_palettes,list_themes;
  int help,version,schema,demo;
} app_args;
void app_args_init(app_args *args);
void app_args_free(app_args *args);
int app_args_parse(app_args *args,int argc,char **argv,app_error *err);
void app_print_help(FILE *f);
void app_print_schema(FILE *f);
void app_print_types(FILE *f);
void app_print_palettes(FILE *f);
void app_print_themes(FILE *f);
void app_print_version(FILE *f);
void app_print_example(FILE *f,const char *what);
const char *app_demo_deck(void);
#endif
