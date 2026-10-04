#ifndef CHARTS_APP_MODEL_H
#define CHARTS_APP_MODEL_H

/* Mutable input/deck model. All functions return zero on failure and set err.
   A caller owns every initialized object and releases it with its free function.
   This header and its implementations require only C89 and the C library. */

#include <stddef.h>
#include <time.h>

typedef struct app_stamp {
  time_t seconds;
  long nanoseconds;
  unsigned long size;
  int exists;
} app_stamp;

int app_stamp_equal(const app_stamp *a,const app_stamp *b);

typedef struct app_error {
  int code;                 /* 1 input/IO/allocation, 2 command usage */
  char message[512];
} app_error;

void app_error_set(app_error *err, int code, const char *message);
void app_error_path(app_error *err, int code, const char *path,
                    const char *message);
char *app_strdup(const char *s, app_error *err);
char *app_strndup(const char *s, size_t n, app_error *err);
char *app_read_file(const char *path, size_t *length, app_error *err);
int app_write_file(const char *path, const char *data, size_t n,
                   app_error *err);
int app_reserve(void **items, size_t *capacity, size_t count,
                size_t item_size, app_error *err);
int app_parse_number(const char *s, double *out);
char *app_format_number(double value, int decimals, app_error *err);
char *app_trim_copy(const char *s, app_error *err);
int app_streq_ci(const char *a, const char *b);
int app_is_missing(double value);
double app_missing(void);

typedef enum app_json_type {
  APP_JSON_NULL, APP_JSON_BOOL, APP_JSON_NUMBER,
  APP_JSON_STRING, APP_JSON_ARRAY, APP_JSON_OBJECT
} app_json_type;

typedef struct app_json app_json;
typedef struct app_json_member {
  char *key;
  app_json *value;
} app_json_member;

struct app_json {
  app_json_type type;
  int boolean;
  double number;
  char *string;
  app_json **items;
  app_json_member *members;
  size_t count, capacity;
};

app_json *app_json_new(app_json_type type, app_error *err);
void app_json_free(app_json *node);
app_json *app_json_clone(const app_json *node, app_error *err);
app_json *app_json_parse(const char *text, app_error *err);
char *app_json_write(const app_json *node, app_error *err);
const app_json *app_json_get(const app_json *obj, const char *key);
app_json *app_json_find(app_json *obj, const char *key);
int app_json_set(app_json *obj, const char *key, app_json *value,
                 app_error *err); /* takes value on success only */
int app_json_push(app_json *array, app_json *value, app_error *err);
void app_json_erase(app_json *obj, const char *key);

typedef enum app_note_kind {
  APP_NOTE_POINT, APP_NOTE_HLINE, APP_NOTE_VLINE, APP_NOTE_FREE
} app_note_kind;

typedef struct app_annotation {
  app_note_kind kind;
  char *text, *label, *series;
  int index, series_index, color;
  double value, fx, fy;
} app_annotation;

typedef struct app_error_bars {
  char *series, *lo, *hi;
  int plus_minus;
} app_error_bars;

typedef struct app_named_color {
  char *name;
  int color;
} app_named_color;

typedef struct app_spec {
  char *type, *palette, *frame, *title, *subtitle, *xlabel, *ylabel;
  unsigned long flags;
  int legend, values, grid, shadow, color, ascii;
  int depth, bins, prec, explode, width, height;
  double lo, hi;
  int xy, transpose, no_header, label_col, series_col;
  char *label_key;
  char delim;
  app_annotation *notes;
  size_t note_count, note_capacity;
  app_error_bars *errors;
  size_t error_count, error_capacity;
  app_named_color *colors;
  size_t color_count, color_capacity;
} app_spec;

#define APP_SPEC_TYPE          (1UL << 0)
#define APP_SPEC_PALETTE       (1UL << 1)
#define APP_SPEC_FRAME         (1UL << 2)
#define APP_SPEC_TITLE         (1UL << 3)
#define APP_SPEC_SUBTITLE      (1UL << 4)
#define APP_SPEC_XLABEL        (1UL << 5)
#define APP_SPEC_YLABEL        (1UL << 6)
#define APP_SPEC_LEGEND        (1UL << 7)
#define APP_SPEC_VALUES        (1UL << 8)
#define APP_SPEC_GRID          (1UL << 9)
#define APP_SPEC_SHADOW        (1UL << 10)
#define APP_SPEC_COLOR         (1UL << 11)
#define APP_SPEC_ASCII         (1UL << 12)
#define APP_SPEC_DEPTH         (1UL << 13)
#define APP_SPEC_BINS          (1UL << 14)
#define APP_SPEC_PREC          (1UL << 15)
#define APP_SPEC_EXPLODE       (1UL << 16)
#define APP_SPEC_LO            (1UL << 17)
#define APP_SPEC_HI            (1UL << 18)
#define APP_SPEC_WIDTH         (1UL << 19)
#define APP_SPEC_HEIGHT        (1UL << 20)
#define APP_SPEC_XY            (1UL << 21)
#define APP_SPEC_TRANSPOSE     (1UL << 22)
#define APP_SPEC_NO_HEADER     (1UL << 23)
#define APP_SPEC_LABEL_COL     (1UL << 24)
#define APP_SPEC_SERIES_COL    (1UL << 25)
#define APP_SPEC_LABEL_KEY     (1UL << 26)
#define APP_SPEC_DELIM         (1UL << 27)
#define APP_SPEC_NOTES         (1UL << 28)
#define APP_SPEC_ERRORS        (1UL << 29)
#define APP_SPEC_COLORS        (1UL << 30)

void app_spec_init(app_spec *spec);
void app_spec_free(app_spec *spec);
int app_spec_clone(app_spec *dst, const app_spec *src, app_error *err);
int app_spec_merge(app_spec *dst, const app_spec *over, app_error *err);
int app_spec_parse(app_spec *out, const app_json *object, app_error *err);
int app_spec_directive(app_spec *out, const char *line, int *recognized,
                       app_error *err);
int app_spec_is_key(const char *key);
int app_spec_numeric_key(const char *key);
int app_parse_color(const app_json *value, int *color);
const char *app_color_name(int color);
const char *app_type_canonical(const char *type);
int app_type_valid(const char *type);
app_json *app_annotation_json(const app_annotation *note, app_error *err);

typedef struct app_series {
  char *name;
  double *values;
  unsigned char *valid; /* 0 = a gap; finite values never use a sentinel */
  size_t count, capacity;
  int color, column, decimals;
} app_series;

typedef struct app_text_column {
  char *name;
  char **values;
  size_t count, capacity;
  int column;
} app_text_column;

typedef struct app_dataset {
  char *title, *source, *label_name, *x_name;
  char **labels;
  size_t label_count, label_capacity;
  int label_column, had_header, lossy;
  char *lossy_why, *hint;
  app_series *series;
  size_t series_count, series_capacity;
  app_text_column *text;
  size_t text_count, text_capacity;
  app_spec spec;
} app_dataset;

typedef struct app_load_opts {
  char delim;
  int transpose, xy, no_header, label_col, series_col;
  char *label_key;
  const char **keep;
  size_t keep_count;
  unsigned long flags; /* APP_SPEC_DELIM/TRANSPOSE/XY/etc: CLI overrides */
} app_load_opts;

void app_load_opts_init(app_load_opts *opts);

void app_dataset_init(app_dataset *data);
void app_dataset_free(app_dataset *data);
int app_dataset_clone(app_dataset *dst, const app_dataset *src,
                      app_error *err);
int app_data_load_text(app_dataset *out, const char *text, const char *name,
                       const app_load_opts *opts, app_error *err);
int app_data_load_file(app_dataset *out, const char *path,
                       const app_load_opts *opts, app_error *err);
int app_data_from_json(app_dataset *out, const app_json *value,
                       const app_load_opts *opts, app_error *err);
int app_data_to_json(const app_dataset *data, app_json **out,
                     app_error *err);
int app_data_save(const app_dataset *data, const char *path, app_error *err);
char *app_data_describe(const app_dataset *data, app_error *err);
size_t app_data_rows(const app_dataset *data);

typedef enum app_block_kind {
  APP_BLOCK_CHART, APP_BLOCK_TEXT, APP_BLOCK_STAT,
  APP_BLOCK_ROWS, APP_BLOCK_COLS, APP_BLOCK_SHAPES, APP_BLOCK_FLOW
} app_block_kind;

typedef struct app_block {
  app_block_kind kind;
  char *path, *data_ref, *data_file, *error, *title;
  app_dataset data;
  app_spec spec;
  double weight, at[4];
  int x, y, w, h; /* last rendered cell rectangle, for TUI focus/hit testing */
  int has_at, size, align, color, box, middle, data_dirty;
  char **lines;
  size_t line_count, line_capacity;
  char *value, *label, *delta;
  app_json *shapes, *flow;
  struct app_block *children;
  size_t child_count, child_capacity;
  app_stamp mtime;
} app_block;

typedef struct app_slide {
  char *path, *title, *subtitle, *notes, *layout;
  app_block *blocks;
  size_t block_count, block_capacity;
} app_slide;

typedef struct app_issue {
  int error;
  char *path, *message;
} app_issue;

typedef struct app_deck {
  char *file, *dir, *title, *theme, *palette, *footer, *gfx;
  app_json *root;
  int implicit, dirty, tweaked, scale, cols, rows, ascii, color;
  app_slide *slides;
  size_t slide_count, slide_capacity;
  app_issue *issues;
  size_t issue_count, issue_capacity;
  app_stamp mtime;
} app_deck;

void app_deck_init(app_deck *deck);
void app_deck_free(app_deck *deck);
int app_deck_load_file(app_deck *out, const char *path,
                       const app_load_opts *opts, app_error *err);
int app_deck_load_text(app_deck *out, const char *text,
                       const app_load_opts *opts, app_error *err);
int app_deck_from_files(app_deck *out, const char **files, size_t count,
                        const char **types, size_t type_count, int tile,
                        const app_load_opts *opts, app_error *err);
int app_is_deck_file(const char *path);
int app_deck_ok(const app_deck *deck);
int app_deck_add_issue(app_deck *deck, int is_error, const char *path,
                       const char *message, app_error *err);
int app_deck_refresh(app_deck *deck, const app_load_opts *opts,
                     app_error *err);
int app_deck_reparse(app_deck *deck, const app_load_opts *opts,
                     app_error *err);
int app_deck_save(app_deck *deck, app_error *err);
char *app_deck_issues_text(const app_deck *deck, app_error *err);
char *app_deck_issues_json(const app_deck *deck, app_error *err);
char *app_deck_outline(const app_deck *deck, app_error *err);
app_json *app_deck_node(app_deck *deck, const char *path);
int app_deck_store_spec(app_deck *deck, app_block *block, int content,
                        app_error *err);
int app_deck_store_data(app_deck *deck, app_block *block, app_error *err);
int app_deck_store_text(app_deck *deck, app_block *block, app_error *err);

#endif
