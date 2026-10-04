#ifndef LC_INTERNAL_H
#define LC_INTERNAL_H
#include "../include/charts.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#define LC_MIN(a,b) ((a)<(b)?(a):(b))
#define LC_MAX(a,b) ((a)>(b)?(a):(b))
#define LC_EMPTY 255
/* Flexible arrays are deliberately avoided for C89. */
typedef union lc_align { double d; long l; void *p; void (*f)(void); } lc_align;
typedef struct lc_arena { struct lc_arena *next; size_t used,capacity; lc_align alignment; } lc_arena;
struct lc_context { lc_allocator allocator; lc_arena *scratch; lc_status status; };
struct lc_canvas { int w,h; lc_cell *cells; lc_context *ctx; };
struct lc_surface { int w,h,sx,sy; lc_ink *pixels; lc_context *ctx; lc_scene *scene; };
typedef struct lc_layer { struct lc_layer *next; lc_rect rect; lc_surface surface; } lc_layer;
typedef struct lc_text_item { struct lc_text_item *next; double x,y; char *text; int fg,bg,scale; } lc_text_item;
struct lc_scene {
    lc_context *ctx; lc_canvas canvas; lc_mode mode; lc_skin skin; lc_status status;
    lc_layer *layers,*last_layer; lc_text_item *texts,*last_text;
    lc_diagnostic_fn diagnostic; void *diagnostic_user; const char *where;
};
int lc_size_mul(size_t a,size_t b,size_t *result);
int lc_clamp_int(double value,int lo,int hi);
void lc_scene_fail(lc_scene *scene,lc_status status);
#endif
