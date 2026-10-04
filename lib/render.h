#ifndef LC_RENDER_PRIV_H
#define LC_RENDER_PRIV_H
#include "internal.h"
#include <stdio.h>
#include <ctype.h>
/* Shared render helpers. All temporary results belong to context scratch. */
typedef struct lc_lines { const char **v; size_t n; } lc_lines;
void *lc_render_array(lc_scene *sc,size_t count,size_t size);
char *lc_render_join(lc_scene *sc,const char *a,const char *b,const char *c);
char *lc_render_uint(lc_scene *sc,size_t value);
lc_lines lc_render_wrap(lc_scene *sc,const char *text,size_t width);
lc_rect lc_rect_make(int x,int y,int w,int h);
int lc_name_eq(const char *a,const char *b);
const char *lc_chart_type(const char *name);
int lc_chart_palette(lc_scene *sc,const char *name,size_t index,size_t count);
#endif
