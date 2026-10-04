#ifndef LIBCHARTS_H
#define LIBCHARTS_H
/* libcharts: ISO C89 rendering, with no OS or terminal ownership.
 * All input strings and arrays are borrowed during the call. Scene text is
 * copied. Contexts and scenes own their allocations; destroy scenes first.
 * Independent contexts are independent; a context is not concurrently mutable.
 * Drawing routines record allocation failures in lc_scene_status().
 */
#include <stddef.h>
#include <limits.h>
#if CHAR_BIT != 8
#error libcharts requires eight-bit bytes
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define LC_VERSION "0.1.0"
#define LC_BG_NONE 255
#define LC_BOX_SINGLE 0
#define LC_BOX_DOUBLE 1
#define LC_BOX_HEAVY 2
#define LC_BOX_ASCII 3
#define LC_BOX_NONE 4

typedef unsigned long lc_codepoint;
typedef enum lc_status { LC_OK=0, LC_EINVAL, LC_ENOMEM, LC_EOVERFLOW, LC_EIO } lc_status;
typedef struct lc_allocator {
    void *user;
    void *(*alloc)(void *user, size_t bytes);
    void (*free)(void *user, void *ptr);
} lc_allocator;
typedef struct lc_context lc_context;
typedef struct lc_scene lc_scene;
typedef struct lc_surface lc_surface;
typedef struct lc_canvas lc_canvas;
typedef struct lc_rect { int x,y,w,h; } lc_rect;
typedef struct lc_pt { double x,y; } lc_pt;
typedef struct lc_ink { unsigned char a,b,level; } lc_ink;
typedef struct lc_cell { lc_codepoint ch; unsigned char fg,bg; } lc_cell;
typedef struct lc_mode { int pixel,ascii,color; } lc_mode;
typedef struct lc_skin {
    unsigned char slide_bg,panel_bg,frame,title,subtitle,axis,tick,grid,label;
    unsigned char xlabel,ylabel,legend,value,shadow,heading,text,dim,accent,bullet;
    unsigned char bar_fg,bar_bg,bar_key,note_fg,note_bg,cursor_fg,cursor_bg;
    unsigned char table_head,table_rule,table_row;
} lc_skin;
typedef struct lc_image {
    unsigned char *pixels;
    int width,height;
    size_t stride;
} lc_image;
typedef struct lc_sink {
    void *user;
    /* Return zero on success, nonzero on failure. Bytes are borrowed. */
    int (*write)(void *user,const unsigned char *bytes,size_t count);
} lc_sink;
typedef void (*lc_diagnostic_fn)(void *user,const char *path,const char *message);

/* Context allocation uses malloc/free when allocator is NULL. */
lc_status lc_context_create(const lc_allocator *allocator,lc_context **out);
void lc_context_destroy(lc_context *ctx);
/* Allocation errors are sticky until scratch_reset; no failed draw is hidden. */
lc_status lc_context_status(const lc_context *ctx);
void *lc_alloc(lc_context *ctx,size_t bytes);
void lc_free(lc_context *ctx,void *ptr);
/* Scratch lives until scratch_reset or context destruction. */
void *lc_scratch(lc_context *ctx,size_t bytes);
void lc_scratch_reset(lc_context *ctx);
char *lc_strdup(lc_context *ctx,const char *text);
const char *lc_status_string(lc_status status);
int lc_skin_init(lc_skin *skin,const char *name);
int lc_skin_set(lc_skin *skin,const char *key,int color);
extern const unsigned char lc_vga_rgb[16][3];
lc_ink lc_ink_make(int a,int b,int level);
lc_ink lc_ink_solid(int color);
lc_ink lc_side_ink(int color);
lc_ink lc_top_ink(int color);
int lc_contrast_on(int color);
int lc_dim_of(int color);
int lc_bright_of(int color);
int lc_finite(double value);
double lc_round(double value);
int lc_clip_segment(lc_pt *a,lc_pt *b,double x0,double y0,double x1,double y1);

lc_status lc_scene_create(lc_context *ctx,int cols,int rows,lc_mode mode,lc_scene **out);
void lc_scene_destroy(lc_scene *scene);
void lc_scene_reset(lc_scene *scene);
lc_status lc_scene_status(const lc_scene *scene);
int lc_scene_cols(const lc_scene *scene);
int lc_scene_rows(const lc_scene *scene);
lc_mode lc_scene_mode(const lc_scene *scene);
lc_skin *lc_scene_skin(lc_scene *scene);
lc_context *lc_scene_context(lc_scene *scene);
lc_canvas *lc_scene_canvas(lc_scene *scene);
void lc_scene_set_diagnostic(lc_scene *scene,lc_diagnostic_fn fn,void *user);
void lc_scene_where(lc_scene *scene,const char *path);
void lc_scene_fit(lc_scene *scene,const char *message);
lc_surface *lc_scene_surface(lc_scene *scene,lc_rect rect);
void lc_scene_text(lc_scene *scene,double x,double y,const char *text,int fg,int bg,int scale);
void lc_scene_big(lc_scene *scene,int x,int y,int w,const char *text,int fg,int scale,int align);
void lc_scene_cover(lc_scene *scene,lc_rect rect);
void lc_scene_panel(lc_scene *scene,lc_rect rect,int bg);
/* Returned images/cells are newly allocated; release with lc_free(ctx,...). */
/* Caller-owned indexed target: exact scene size (cols*8 by rows*16),
 * stride >= width, storage >= stride*height. Padding bytes are untouched. */
lc_status lc_scene_draw_image(lc_scene *scene,lc_image *target);
lc_status lc_scene_to_image(lc_scene *scene,lc_image *out);
lc_status lc_scene_to_cells(lc_scene *scene,lc_cell **out);
lc_status lc_image_scale(lc_context *ctx,const lc_image *image,int scale,lc_image *out);
void lc_image_rect(lc_image *image,int x,int y,int w,int h,int color);
void lc_image_glyph(lc_image *image,int x,int y,lc_codepoint cp,int fg,int bg,int scale);
const unsigned char *lc_glyph_rows(lc_codepoint cp);
int lc_glyph_known(lc_codepoint cp);

int lc_surface_width(const lc_surface *s);
int lc_surface_height(const lc_surface *s);
int lc_surface_sx(const lc_surface *s);
int lc_surface_sy(const lc_surface *s);
int lc_surface_fine(const lc_surface *s);
double lc_surface_aspect(const lc_surface *s);
int lc_surface_touched(const lc_surface *s,int x,int y);
lc_ink lc_surface_at(const lc_surface *s,int x,int y);
int lc_surface_resolve(const lc_surface *s,int x,int y);
void lc_surface_set(lc_surface *s,int x,int y,lc_ink ink);
void lc_surface_erase(lc_surface *s,int x0,int y0,int x1,int y1);
void lc_surface_rect(lc_surface *s,double x0,double y0,double x1,double y1,lc_ink ink);
void lc_surface_hline(lc_surface *s,int x0,int x1,int y,lc_ink ink);
void lc_surface_vline(lc_surface *s,int x,int y0,int y1,lc_ink ink);
void lc_surface_dotted_h(lc_surface *s,int x0,int x1,int y,lc_ink ink,int gap);
void lc_surface_dotted_v(lc_surface *s,int x,int y0,int y1,lc_ink ink,int gap);
void lc_surface_line(lc_surface *s,double x0,double y0,double x1,double y1,lc_ink ink,int width);
void lc_surface_poly(lc_surface *s,const lc_pt *points,size_t count,lc_ink ink);
void lc_surface_disc(lc_surface *s,double cx,double cy,double r,lc_ink ink);
void lc_surface_ring(lc_surface *s,double cx,double cy,double r,lc_ink ink);
void lc_surface_marker(lc_surface *s,double cx,double cy,int shape,double r,lc_ink ink);

void lc_canvas_clear(lc_canvas *c,int fg,int bg);
void lc_canvas_put(lc_canvas *c,int x,int y,lc_codepoint cp,int fg,int bg);
void lc_canvas_fill_bg(lc_canvas *c,int x,int y,int w,int h,int bg);
void lc_canvas_text(lc_canvas *c,int x,int y,const char *text,int fg,int bg);
void lc_canvas_text_c(lc_canvas *c,int x,int y,int w,const char *text,int fg);
void lc_canvas_text_r(lc_canvas *c,int x,int y,int w,const char *text,int fg);
void lc_canvas_fill(lc_canvas *c,int x,int y,int w,int h,lc_codepoint cp,int fg);
void lc_canvas_hline(lc_canvas *c,int x,int y,int len,lc_codepoint cp,int fg);
void lc_canvas_vline(lc_canvas *c,int x,int y,int len,lc_codepoint cp,int fg);
void lc_canvas_box(lc_canvas *c,int x,int y,int w,int h,int style,int fg);
void lc_canvas_shadow(lc_canvas *c,int x,int y,int w,int h);
void lc_canvas_vtext(lc_canvas *c,int x,int y,const char *text,int fg);

/* Unicode input is decoded strictly; controls cannot escape into terminals. */
lc_codepoint lc_utf8_next(const char **text);
size_t lc_utf8_encode(lc_codepoint cp,char bytes[5]);
lc_codepoint lc_cell_of(lc_codepoint cp);
size_t lc_text_width(const char *text);
char *lc_text_truncate(lc_context *ctx,const char *text,size_t width);
int lc_ieq(const char *a,const char *b);
/* Lines and strings are scratch-owned. Returns NULL on allocation failure. */
char **lc_text_wrap(lc_context *ctx,const char *text,size_t width,size_t *count);
/* Numeric helpers return scratch strings, valid until scratch_reset. */
char *lc_fmt_val(lc_context *ctx,double value,int precision);
char *lc_fmt_axis(lc_context *ctx,double value);
char *lc_fmt_raw(lc_context *ctx,double value);
double lc_nice_step(double raw);

typedef struct lc_series {
    const char *name;
    const double *values;
    const unsigned char *valid;
    size_t count;
    int color,col,decimals;
} lc_series;
typedef struct lc_text_column {
    const char *name;
    const char *const *values;
    size_t count;
    int col;
} lc_text_column;
typedef struct lc_dataset {
    const char *title,*source,*label_name,*x_name;
    const char *const *labels;
    size_t label_count;
    const lc_series *series;
    size_t series_count;
    const lc_text_column *text;
    size_t text_count;
} lc_dataset;
typedef struct lc_annotation {
    int kind; /* 0 point, 1 horizontal line, 2 vertical line, 3 note */
    const char *text,*label,*series;
    int index,series_i,color;
    double value,fx,fy;
} lc_annotation;
typedef struct lc_error_bars { const char *series,*lo,*hi; int plus_minus; } lc_error_bars;
typedef struct lc_color { const char *name; int color; } lc_color;
typedef struct lc_chart_options {
    const char *type,*title,*subtitle,*xlabel,*ylabel,*palette,*frame,*source;
    int legend,values,grid,color,shadow,zero_base,xy;
    int explode,depth,bins,prec;
    double lo,hi;
    int has_lo,has_hi;
    const lc_annotation *notes; size_t note_count;
    const lc_color *colors; size_t color_count;
    const lc_error_bars *errors; size_t error_count;
    int cur_series,cur_index,frame_color;
} lc_chart_options;
void lc_chart_options_init(lc_chart_options *options);
lc_status lc_render_chart(lc_scene *scene,lc_rect rect,const lc_dataset *data,const lc_chart_options *options);

typedef struct lc_shape {
    int kind; /* 0 rectangle, 1 ellipse, 2 polygon, 3 line, 4 label */
    const lc_pt *points; size_t point_count;
    const char *text;
    int color,text_color,border,dither,depth,width,size,align;
    int shadow,fill,dash,head,tail;
} lc_shape;
typedef struct lc_flow_node { const char *id,*text; int color; } lc_flow_node;
typedef struct lc_flow_edge { int from,to; const char *text; int color,dash; } lc_flow_edge;
typedef struct lc_flow {
    const lc_flow_node *nodes; size_t node_count;
    const lc_flow_edge *edges; size_t edge_count;
    int down;
} lc_flow;
void lc_draw_shapes(lc_scene *scene,lc_rect rect,const lc_shape *shapes,size_t count,int bg);
void lc_draw_flow(lc_scene *scene,lc_rect rect,const lc_flow *flow,int bg);

/* Slide input borrows all data. Layout writes only each block's rect field.
 * Kind: chart=0, text=1, stat=2, rows=3, cols=4, shapes=5, flow=6.
 * tag identifies the caller's block for focus; it is never dereferenced. */
typedef struct lc_block {
    int kind;
    const void *tag;
    const char *path;
    double weight,at[4];
    int has_at;
    lc_rect rect;
    lc_dataset data;
    lc_chart_options chart;
    const char *error,*data_ref,*title;
    const char *const *lines; size_t line_count;
    int size,align,color,box,middle;
    const char *value,*label,*delta;
    const lc_shape *shapes; size_t shape_count;
    lc_flow flow;
    struct lc_block *children; size_t child_count;
} lc_block;
typedef struct lc_slide {
    const char *path,*title,*subtitle,*notes,*layout;
    lc_block *blocks; size_t block_count;
} lc_slide;
typedef struct lc_slide_view {
    const void *focus;
    int cur_series,cur_index,notes,chrome,bare,message_bad;
    const char *footer,*hint,*message;
    size_t index,count;
} lc_slide_view;
void lc_block_init(lc_block *block);
void lc_slide_view_init(lc_slide_view *view);
lc_status lc_render_slide(lc_scene *scene,lc_slide *slide,const lc_slide_view *view);
void lc_draw_status(lc_scene *scene,const char *left,const char *hint,const char *position,int bad);

/* Portable codecs only emit bytes. Device probing/ownership belongs to apps. */
lc_status lc_write_png(lc_context *ctx,const lc_image *image,const lc_sink *sink);
lc_status lc_write_sixel(lc_context *ctx,const lc_image *image,const lc_sink *sink);
lc_status lc_write_kitty(lc_context *ctx,const lc_image *image,const lc_sink *sink);
/* Kitty image id is 1..255. raw selects compressed RGB payload (f=24,o=z);
 * otherwise a compressed indexed PNG is sent (f=100). */
lc_status lc_write_kitty_ex(lc_context *ctx,const lc_image *image,const lc_sink *sink,int id,int raw);
lc_status lc_write_cells(const lc_cell *cells,int cols,int rows,size_t stride,int color,int crlf,int bright_bg,const lc_sink *sink);
#ifdef __cplusplus
}
#endif
#endif
