#include "model.h"

#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *app_types[] = {
  "bar", "stacked", "hbar", "dumbbell", "line", "area", "pie", "pie3d",
  "donut", "scatter", "hist", "table", NULL
};
static const char *app_palettes[] = {
  "dos", "ega", "cga", "ice", "fire", "green", "amber", "mono", NULL
};
static const char *app_colors[] = {
  "black", "dark red", "dark green", "brown", "dark blue", "dark magenta",
  "dark cyan", "grey", "dark grey", "red", "green", "yellow", "blue",
  "magenta", "cyan", "white"
};

static char *norm_key(const char *s, app_error *err)
{
  char *p, *q;
  p = app_trim_copy(s, err);
  if (!p) return NULL;
  for (q = p; *q; q++) {
    if (*q == '-' || *q == ' ') *q = '_';
    else *q = (char)tolower((unsigned char)*q);
  }
  return p;
}

static int one_of(const char *s, const char *const *words)
{
  size_t i;
  for (i = 0; words[i]; i++) if (strcmp(s, words[i]) == 0) return 1;
  return 0;
}

static int prefix_ci(const char *s, const char *prefix)
{
  while (*prefix) {
    if (!*s || tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) return 0;
    s++; prefix++;
  }
  return 1;
}

const char *app_type_canonical(const char *raw)
{
  size_t i;
  char s[64];
  if (!raw || strlen(raw) >= sizeof s) return NULL;
  for (i = 0; raw[i]; i++) s[i] = (char)tolower((unsigned char)raw[i]);
  s[i] = 0;
  if (!strcmp(s,"column") || !strcmp(s,"grouped") || !strcmp(s,"bars") || !strcmp(s,"bar3d")) return "bar";
  if (!strcmp(s,"xy") || !strcmp(s,"points")) return "scatter";
  if (!strcmp(s,"histogram")) return "hist";
  if (!strcmp(s,"lines")) return "line";
  if (!strcmp(s,"doughnut") || !strcmp(s,"ring")) return "donut";
  if (!strcmp(s,"stack") || !strcmp(s,"stackedbar")) return "stacked";
  if (!strcmp(s,"barh") || !strcmp(s,"horizontal")) return "hbar";
  if (!strcmp(s,"dumbbells") || !strcmp(s,"before_after") || !strcmp(s,"beforeafter") ||
      !strcmp(s,"change") || !strcmp(s,"dots")) return "dumbbell";
  for (i = 0; app_types[i]; i++) if (!strcmp(s,app_types[i])) return app_types[i];
  return NULL;
}

int app_type_valid(const char *s) { return app_type_canonical(s) != NULL; }
const char *app_color_name(int c) { return c >= 0 && c < 16 ? app_colors[c] : "default"; }

int app_parse_color(const app_json *v, int *color)
{
  char s[64], *t;
  const char *src;
  size_t i, k;
  double number;
  if (!v || !color) return 0;
  if (v->type == APP_JSON_NUMBER) {
    if (v->number < 0 || v->number > 15 || v->number != floor(v->number)) return 0;
    *color = (int)v->number; return 1;
  }
  if (v->type != APP_JSON_STRING || !v->string || strlen(v->string) >= sizeof s) return 0;
  src = v->string;
  while (*src && isspace((unsigned char)*src)) src++;
  for (i = 0; src[i] && i < sizeof s - 1; i++) {
    s[i] = src[i] == '_' || src[i] == '-' ? ' ' : (char)tolower((unsigned char)src[i]);
  }
  s[i] = 0;
  while (i && isspace((unsigned char)s[i - 1])) s[--i] = 0;
  t = s;
  if (!strncmp(t,"bright ",7)) t += 7;
  else if (!strncmp(t,"light ",6)) t += 6;
  if (!strcmp(t,"gray")) t = "grey";
  if (!strcmp(t,"dark gray") || !strcmp(t,"darkgray") || !strcmp(t,"darkgrey")) t = "dark grey";
  if (!strcmp(t,"orange")) t = "brown";
  if (!strcmp(t,"purple") || !strcmp(t,"pink")) t = "magenta";
  for (i = 0; i < 16; i++) {
    char flat[32];
    size_t n;
    n = 0;
    for (k = 0; app_colors[i][k]; k++)
      if (app_colors[i][k] != ' ') flat[n++] = app_colors[i][k];
    flat[n] = 0;
    if (!strcmp(t,app_colors[i]) || !strcmp(t,flat)) { *color = (int)i; return 1; }
  }
  if (app_parse_number(t,&number) && number >= 0 && number <= 15 && floor(number) == number) {
    *color = (int)number; return 1;
  }
  return 0;
}

void app_spec_init(app_spec *s)
{
  if (!s) return;
  memset(s, 0, sizeof *s);
  s->grid = s->shadow = s->color = 1;
  s->depth = 2; s->bins = 10; s->prec = -1; s->explode = -2;
  s->label_col = s->series_col = -1;
}

static void note_free(app_annotation *a)
{
  free(a->text); free(a->label); free(a->series);
  memset(a,0,sizeof *a);
}

void app_spec_free(app_spec *s)
{
  size_t i;
  if (!s) return;
  free(s->type); free(s->palette); free(s->frame);
  free(s->title); free(s->subtitle); free(s->xlabel); free(s->ylabel);
  free(s->label_key);
  for (i = 0; i < s->note_count; i++) note_free(s->notes + i);
  free(s->notes);
  for (i = 0; i < s->error_count; i++) {
    free(s->errors[i].series); free(s->errors[i].lo); free(s->errors[i].hi);
  }
  free(s->errors);
  for (i = 0; i < s->color_count; i++) free(s->colors[i].name);
  free(s->colors);
  app_spec_init(s);
}

static int copy_string(char **out, const char *src, app_error *err)
{
  char *p;
  p = src ? app_strdup(src, err) : NULL;
  if (src && !p) return 0;
  free(*out);
  *out = p;
  return 1;
}

int app_spec_clone(app_spec *dst, const app_spec *src, app_error *err)
{
  app_spec t;
  size_t i;
  app_spec_init(&t);
  t.flags = src->flags;
  t.legend = src->legend; t.values = src->values; t.grid = src->grid;
  t.shadow = src->shadow; t.color = src->color; t.ascii = src->ascii;
  t.depth = src->depth; t.bins = src->bins; t.prec = src->prec;
  t.explode = src->explode; t.width = src->width; t.height = src->height;
  t.lo = src->lo; t.hi = src->hi; t.xy = src->xy;
  t.transpose = src->transpose; t.no_header = src->no_header;
  t.label_col = src->label_col; t.series_col = src->series_col;
  t.delim = src->delim;
#define CP(f) if (!copy_string(&t.f,src->f,err)) goto fail
  CP(type); CP(palette); CP(frame); CP(title); CP(subtitle);
  CP(xlabel); CP(ylabel); CP(label_key);
#undef CP
  for (i = 0; i < src->note_count; i++) {
    app_annotation n;
    n = src->notes[i]; n.text = n.label = n.series = NULL;
    if (!copy_string(&n.text,src->notes[i].text,err) ||
        !copy_string(&n.label,src->notes[i].label,err) ||
        !copy_string(&n.series,src->notes[i].series,err)) { note_free(&n); goto fail; }
    if (!app_reserve((void **)&t.notes,&t.note_capacity,t.note_count + 1,sizeof n,err)) {
      note_free(&n); goto fail;
    }
    t.notes[t.note_count++] = n;
  }
  for (i = 0; i < src->error_count; i++) {
    app_error_bars b;
    b = src->errors[i]; b.series = b.lo = b.hi = NULL;
    if (!copy_string(&b.series,src->errors[i].series,err) ||
        !copy_string(&b.lo,src->errors[i].lo,err) ||
        !copy_string(&b.hi,src->errors[i].hi,err)) {
      free(b.series); free(b.lo); free(b.hi); goto fail;
    }
    if (!app_reserve((void **)&t.errors,&t.error_capacity,t.error_count + 1,sizeof b,err)) {
      free(b.series); free(b.lo); free(b.hi); goto fail;
    }
    t.errors[t.error_count++] = b;
  }
  for (i = 0; i < src->color_count; i++) {
    app_named_color c;
    c.color = src->colors[i].color; c.name = NULL;
    if (!copy_string(&c.name,src->colors[i].name,err)) goto fail;
    if (!app_reserve((void **)&t.colors,&t.color_capacity,t.color_count + 1,sizeof c,err)) {
      free(c.name); goto fail;
    }
    t.colors[t.color_count++] = c;
  }
  app_spec_free(dst);
  *dst = t;
  return 1;
fail:
  app_spec_free(&t);
  return 0;
}

int app_spec_merge(app_spec *dst, const app_spec *over, app_error *err)
{
  app_spec t;
  size_t i;
  app_spec_init(&t);
  if (!app_spec_clone(&t,dst,err)) return 0;
#define MERGE_STR(f,flag) if (over->flags & flag) { if (!copy_string(&t.f,over->f,err)) goto fail; t.flags |= flag; }
#define MERGE_NUM(f,flag) if (over->flags & flag) { t.f = over->f; t.flags |= flag; }
  MERGE_STR(type,APP_SPEC_TYPE) MERGE_STR(palette,APP_SPEC_PALETTE)
  MERGE_STR(frame,APP_SPEC_FRAME) MERGE_STR(title,APP_SPEC_TITLE)
  MERGE_STR(subtitle,APP_SPEC_SUBTITLE) MERGE_STR(xlabel,APP_SPEC_XLABEL)
  MERGE_STR(ylabel,APP_SPEC_YLABEL) MERGE_STR(label_key,APP_SPEC_LABEL_KEY)
  MERGE_NUM(legend,APP_SPEC_LEGEND) MERGE_NUM(values,APP_SPEC_VALUES)
  MERGE_NUM(grid,APP_SPEC_GRID) MERGE_NUM(shadow,APP_SPEC_SHADOW)
  MERGE_NUM(color,APP_SPEC_COLOR) MERGE_NUM(ascii,APP_SPEC_ASCII)
  MERGE_NUM(depth,APP_SPEC_DEPTH) MERGE_NUM(bins,APP_SPEC_BINS)
  MERGE_NUM(prec,APP_SPEC_PREC) MERGE_NUM(explode,APP_SPEC_EXPLODE)
  MERGE_NUM(lo,APP_SPEC_LO) MERGE_NUM(hi,APP_SPEC_HI)
  MERGE_NUM(width,APP_SPEC_WIDTH) MERGE_NUM(height,APP_SPEC_HEIGHT)
  MERGE_NUM(xy,APP_SPEC_XY) MERGE_NUM(transpose,APP_SPEC_TRANSPOSE)
  MERGE_NUM(no_header,APP_SPEC_NO_HEADER) MERGE_NUM(label_col,APP_SPEC_LABEL_COL)
  MERGE_NUM(series_col,APP_SPEC_SERIES_COL) MERGE_NUM(delim,APP_SPEC_DELIM)
#undef MERGE_STR
#undef MERGE_NUM
  if (over->flags & (APP_SPEC_NOTES | APP_SPEC_ERRORS | APP_SPEC_COLORS)) {
    app_spec o;
    app_spec_init(&o);
    if (!app_spec_clone(&o,over,err)) goto fail;
    if (over->flags & APP_SPEC_NOTES) {
      for (i = 0; i < t.note_count; i++) note_free(t.notes + i);
      free(t.notes); t.notes = o.notes; t.note_count = o.note_count;
      t.note_capacity = o.note_capacity; o.notes = NULL; o.note_count = o.note_capacity = 0;
      t.flags |= APP_SPEC_NOTES;
    }
    if (over->flags & APP_SPEC_ERRORS) {
      for (i = 0; i < t.error_count; i++) {
        free(t.errors[i].series); free(t.errors[i].lo); free(t.errors[i].hi);
      }
      free(t.errors); t.errors = o.errors; t.error_count = o.error_count;
      t.error_capacity = o.error_capacity; o.errors = NULL; o.error_count = o.error_capacity = 0;
      t.flags |= APP_SPEC_ERRORS;
    }
    if (over->flags & APP_SPEC_COLORS) {
      for (i = 0; i < t.color_count; i++) free(t.colors[i].name);
      free(t.colors); t.colors = o.colors; t.color_count = o.color_count;
      t.color_capacity = o.color_capacity; o.colors = NULL; o.color_count = o.color_capacity = 0;
      t.flags |= APP_SPEC_COLORS;
    }
    app_spec_free(&o);
  }
  app_spec_free(dst); *dst = t; return 1;
fail:
  app_spec_free(&t); return 0;
}

static int spec_bad(app_error *err, const char *key, const char *want)
{
  char buf[512];
  size_t n;
  strcpy(buf,"chart spec: ");
  n = strlen(buf);
  if (key) strncat(buf,key,sizeof buf - n - 1);
  n = strlen(buf);
  strncat(buf," wants ",sizeof buf - n - 1);
  n = strlen(buf);
  strncat(buf,want,sizeof buf - n - 1);
  app_error_set(err,1,buf);
  return 0;
}

static int spec_bool(const app_json *v, int *out, const char *key, app_error *err)
{
  if (v->type == APP_JSON_BOOL) { *out = v->boolean; return 1; }
  if (v->type == APP_JSON_NUMBER) {
    if (v->number == 0 || v->number == 1) { *out = v->number == 1; return 1; }
  }
  if (v->type == APP_JSON_STRING) {
    const char *s;
    s = v->string;
    if (app_streq_ci(s,"true") || app_streq_ci(s,"yes") ||
        app_streq_ci(s,"on") || !strcmp(s,"1")) { *out = 1; return 1; }
    if (app_streq_ci(s,"false") || app_streq_ci(s,"no") ||
        app_streq_ci(s,"off") || !strcmp(s,"0")) { *out = 0; return 1; }
  }
  return spec_bad(err,key,"true or false");
}

static int spec_number(const app_json *v, double *out, double lo, double hi,
                       const char *key, app_error *err)
{
  double d;
  if (v->type == APP_JSON_NUMBER) d = v->number;
  else if (v->type == APP_JSON_STRING && app_parse_number(v->string,&d)) { }
  else return spec_bad(err,key,"a number");
  if (d != d || d < lo || d > hi) return spec_bad(err,key,"a number in range");
  *out = d; return 1;
}

static char *spec_string(const app_json *v, const char *key, app_error *err)
{
  if (v->type == APP_JSON_STRING) return app_strdup(v->string,err);
  if (v->type == APP_JSON_NUMBER) return app_format_number(v->number,-1,err);
  if (v->type == APP_JSON_BOOL) return app_strdup(v->boolean ? "true" : "false",err);
  spec_bad(err,key,"a string"); return NULL;
}

static int row_index(double value)
{
  return value >= 0 && value < 1e9 && floor(value) == value ? (int)value : -1;
}

static int note_parse(app_annotation *out, const app_json *v, app_error *err)
{
  size_t i;
  char *key;
  const app_json *field;
  int placed;
  memset(out,0,sizeof *out);
  out->index = out->series_index = out->color = -1;
  out->fx = .98; out->fy = .02;
  if (!v || v->type != APP_JSON_OBJECT)
    return spec_bad(err,"annotation","an object like {\"at\":\"Mar\",\"text\":\"launch\"}");
  placed = 0;
  for (i = 0; i < v->count; i++) {
    field = v->members[i].value;
    key = norm_key(v->members[i].key,err);
    if (!key) goto fail;
    if (!strcmp(key,"text") || !strcmp(key,"label") || !strcmp(key,"say")) {
      char *s = spec_string(field,key,err);
      if (!s) { free(key); goto fail; }
      free(out->text); out->text = s;
    } else if (!strcmp(key,"at") || !strcmp(key,"point") || !strcmp(key,"category") ||
               !strcmp(key,"x") || !strcmp(key,"vline")) {
      char *s;
      if (field->type != APP_JSON_STRING && field->type != APP_JSON_NUMBER) {
        free(key); spec_bad(err,"annotation","a label or 0-based row number"); goto fail;
      }
      s = spec_string(field,key,err);
      if (!s) { free(key); goto fail; }
      free(out->label); out->label = s;
      if (field->type == APP_JSON_NUMBER) out->index = row_index(field->number);
      out->kind = !strcmp(key,"x") || !strcmp(key,"vline") ? APP_NOTE_VLINE : APP_NOTE_POINT;
      placed = 1;
    } else if (!strcmp(key,"series")) {
      if (field->type == APP_JSON_NUMBER) out->series_index = row_index(field->number);
      else { out->series = spec_string(field,key,err); if (!out->series) { free(key); goto fail; } }
    } else if (!strcmp(key,"y") || !strcmp(key,"hline") || !strcmp(key,"value")) {
      if (!spec_number(field,&out->value,-1e15,1e15,key,err)) { free(key); goto fail; }
      out->kind = APP_NOTE_HLINE; placed = 1;
    } else if (!strcmp(key,"note") || !strcmp(key,"pos") || !strcmp(key,"xy")) {
      if (field->type != APP_JSON_ARRAY || field->count != 2 ||
          field->items[0]->type != APP_JSON_NUMBER || field->items[1]->type != APP_JSON_NUMBER) {
        free(key); spec_bad(err,"note","[x, y], each 0..1 across the plot"); goto fail;
      }
      out->fx = field->items[0]->number;
      out->fy = field->items[1]->number;
      if (out->fx < 0) out->fx = 0;
      if (out->fx > 1) out->fx = 1;
      if (out->fy < 0) out->fy = 0;
      if (out->fy > 1) out->fy = 1;
      out->kind = APP_NOTE_FREE; placed = 1;
    } else if (!strcmp(key,"color") || !strcmp(key,"colour")) {
      if (!app_parse_color(field,&out->color)) {
        free(key); spec_bad(err,"annotation color","a named colour or 0..15"); goto fail;
      }
    }
    free(key);
  }
  if (!placed) out->kind = APP_NOTE_FREE;
  if ((!out->text || !*out->text) && out->kind != APP_NOTE_HLINE && out->kind != APP_NOTE_VLINE) {
    spec_bad(err,"annotation","text"); goto fail;
  }
  return 1;
fail:
  note_free(out); return 0;
}

app_json *app_annotation_json(const app_annotation *note, app_error *err)
{
  app_json *obj, *field, *array;
  const char *key;
  obj = app_json_new(APP_JSON_OBJECT,err);
  if (!obj) return NULL;
  array = NULL;
  key = NULL;
  if (note->kind == APP_NOTE_POINT) key = "at";
  else if (note->kind == APP_NOTE_VLINE) key = "x";
  else if (note->kind == APP_NOTE_HLINE) key = "y";
  if (key) {
    if (note->kind == APP_NOTE_HLINE) {
      field = app_json_new(APP_JSON_NUMBER,err); if (field) field->number = note->value;
    } else if (note->label) {
      field = app_json_new(APP_JSON_STRING,err);
      if (field) field->string = app_strdup(note->label,err);
    } else {
      field = app_json_new(APP_JSON_NUMBER,err); if (field) field->number = note->index;
    }
    if (!field || (field->type == APP_JSON_STRING && !field->string)) { app_json_free(field); goto fail; }
    if (!app_json_set(obj,key,field,err)) { app_json_free(field); goto fail; }
  } else {
    array = app_json_new(APP_JSON_ARRAY,err);
    if (!array) goto fail;
    field = app_json_new(APP_JSON_NUMBER,err); if (!field) goto fail;
    field->number = note->fx;
    if (!app_json_push(array,field,err)) { app_json_free(field); goto fail; }
    field = app_json_new(APP_JSON_NUMBER,err); if (!field) goto fail;
    field->number = note->fy;
    if (!app_json_push(array,field,err)) { app_json_free(field); goto fail; }
    if (!app_json_set(obj,"note",array,err)) goto fail;
    array = NULL;
  }
  if (note->kind == APP_NOTE_POINT && (note->series || note->series_index >= 0)) {
    field = app_json_new(note->series ? APP_JSON_STRING : APP_JSON_NUMBER,err);
    if (!field) goto fail;
    if (note->series) field->string = app_strdup(note->series,err);
    else field->number = note->series_index;
    if (note->series && !field->string) { app_json_free(field); goto fail; }
    if (!app_json_set(obj,"series",field,err)) { app_json_free(field); goto fail; }
  }
  field = app_json_new(APP_JSON_STRING,err); if (!field) goto fail;
  field->string = app_strdup(note->text ? note->text : "",err);
  if (!field->string || !app_json_set(obj,"text",field,err)) { app_json_free(field); goto fail; }
  if (note->color >= 0) {
    field = app_json_new(APP_JSON_STRING,err); if (!field) goto fail;
    field->string = app_strdup(app_color_name(note->color),err);
    if (!field->string || !app_json_set(obj,"color",field,err)) { app_json_free(field); goto fail; }
  }
  return obj;
fail:
  app_json_free(array); app_json_free(obj); return NULL;
}

static int set_text(char **field, const app_json *v, const char *key,
                    app_error *err)
{
  char *s;
  s = spec_string(v,key,err);
  if (!s) return 0;
  free(*field); *field = s;
  return 1;
}

static int add_error(app_spec *s, const char *series, const app_json *v,
                     const char *key, app_error *err)
{
  app_error_bars b;
  memset(&b,0,sizeof b);
  b.series = app_strdup(series ? series : "",err);
  if (!b.series) return 0;
  if (v->type == APP_JSON_STRING && v->string && *v->string) {
    b.lo = app_trim_copy(v->string,err);
    b.plus_minus = 1;
  } else if (v->type == APP_JSON_ARRAY && v->count == 2 &&
             v->items[0]->type == APP_JSON_STRING &&
             v->items[1]->type == APP_JSON_STRING) {
    b.lo = app_trim_copy(v->items[0]->string,err);
    b.hi = app_trim_copy(v->items[1]->string,err);
  } else {
    spec_bad(err,key,"a column name (+-), a [low, high] pair of column names, or an object of those by series");
    free(b.series); return 0;
  }
  if (!b.lo || (!b.plus_minus && !b.hi)) {
    free(b.series); free(b.lo); free(b.hi); return 0;
  }
  if (!app_reserve((void **)&s->errors,&s->error_capacity,s->error_count + 1,
                   sizeof b,err)) {
    free(b.series); free(b.lo); free(b.hi); return 0;
  }
  s->errors[s->error_count++] = b;
  return 1;
}

static void clear_errors(app_spec *s)
{
  size_t i;
  for (i = 0; i < s->error_count; i++) {
    free(s->errors[i].series); free(s->errors[i].lo); free(s->errors[i].hi);
  }
  free(s->errors);
  s->errors = NULL; s->error_count = s->error_capacity = 0;
}

static void clear_colors(app_spec *s)
{
  size_t i;
  for (i = 0; i < s->color_count; i++) free(s->colors[i].name);
  free(s->colors);
  s->colors = NULL; s->color_count = s->color_capacity = 0;
}

static int add_color(app_spec *s, const char *name, const app_json *v,
                     app_error *err)
{
  app_named_color c;
  if (!app_parse_color(v,&c.color))
    return spec_bad(err,"colors","a named colour or 0..15");
  c.name = app_strdup(name ? name : "",err);
  if (!c.name) return 0;
  if (!app_reserve((void **)&s->colors,&s->color_capacity,s->color_count + 1,
                   sizeof c,err)) { free(c.name); return 0; }
  s->colors[s->color_count++] = c;
  return 1;
}

static int palette_list(app_spec *s, const app_json *v, app_error *err)
{
  char *p, *q;
  size_t i, capacity;
  int color;
  if (v->count == 0 || v->count > ((size_t)-1 - 8) / 4)
    return spec_bad(err,"palette","a nonempty list of colours");
  capacity = 8 + v->count * 4;
  p = (char *)malloc(capacity);
  if (!p) { app_error_set(err,1,"out of memory"); return 0; }
  strcpy(p,"custom:"); q = p + 7;
  for (i = 0; i < v->count; i++) {
    if (!app_parse_color(v->items[i],&color)) {
      free(p); return spec_bad(err,"palette","a named colour or 0..15");
    }
    if (i) *q++ = ',';
    q += sprintf(q,"%d",color);
  }
  free(s->palette); s->palette = p; s->flags |= APP_SPEC_PALETTE;
  return 1;
}

static int set_field(app_spec *s, const char *raw, const app_json *v,
                     app_error *err)
{
  char *k, *text;
  double d;
  size_t i;
  int b;
  k = norm_key(raw,err);
  if (!k) return 0;
#define IS(x) (strcmp(k,x) == 0)
#define TXT(x,f,flag) if (IS(x)) { if (!set_text(&s->f,v,raw,err)) goto fail; s->flags |= flag; goto done; }
#define BOOL(x,f,flag) if (IS(x)) { if (!spec_bool(v,&s->f,raw,err)) goto fail; s->flags |= flag; goto done; }
#define NUM(x,f,lo_,hi_,flag) if (IS(x)) { if (!spec_number(v,&d,lo_,hi_,raw,err)) goto fail; s->f = (int)d; s->flags |= flag; goto done; }
  if (IS("type") || IS("chart") || IS("kind")) {
    text = spec_string(v,raw,err);
    if (!text) goto fail;
    {
      const char *canonical;
      canonical = app_type_canonical(text);
      if (!canonical) {
        app_error_path(err,1,"chart spec: unknown chart type",text);
        free(text); goto fail;
      }
      if (!copy_string(&s->type,canonical,err)) { free(text); goto fail; }
    }
    free(text); s->flags |= APP_SPEC_TYPE; goto done;
  }
  if (IS("annotations") || IS("notes_on_chart")) {
    if (v->type != APP_JSON_ARRAY) { spec_bad(err,raw,"an array of annotation objects"); goto fail; }
    for (i = 0; i < s->note_count; i++) note_free(s->notes + i);
    free(s->notes); s->notes = NULL; s->note_count = s->note_capacity = 0;
    for (i = 0; i < v->count; i++) {
      app_annotation a;
      if (!note_parse(&a,v->items[i],err)) goto fail;
      if (!app_reserve((void **)&s->notes,&s->note_capacity,s->note_count + 1,
                       sizeof a,err)) { note_free(&a); goto fail; }
      s->notes[s->note_count++] = a;
    }
    s->flags |= APP_SPEC_NOTES; goto done;
  }
  if ((IS("colours") || IS("colors") || IS("series_colors")) &&
      (v->type == APP_JSON_ARRAY || v->type == APP_JSON_OBJECT)) {
    clear_colors(s);
    for (i = 0; i < v->count; i++) {
      const char *name;
      const app_json *cv;
      name = v->type == APP_JSON_OBJECT ? v->members[i].key : "";
      cv = v->type == APP_JSON_OBJECT ? v->members[i].value : v->items[i];
      if (!add_color(s,name,cv,err)) goto fail;
    }
    s->flags |= APP_SPEC_COLORS; goto done;
  }
  if (IS("errors") || IS("error") || IS("error_bars") || IS("errorbars") || IS("ranges")) {
    clear_errors(s);
    if (v->type == APP_JSON_OBJECT) {
      for (i = 0; i < v->count; i++)
        if (!add_error(s,v->members[i].key,v->members[i].value,raw,err)) goto fail;
    } else if (!add_error(s,"",v,raw,err)) goto fail;
    s->flags |= APP_SPEC_ERRORS; goto done;
  }
  if (IS("palette") && v->type == APP_JSON_ARRAY) {
    if (!palette_list(s,v,err)) goto fail;
    goto done;
  }
  if (IS("palette") || IS("colours") || IS("colors")) {
    text = spec_string(v,raw,err);
    if (!text) goto fail;
    b = !strncmp(text,"custom:",7) || one_of(text,app_palettes);
    if (!b) { app_error_path(err,1,"chart spec: unknown palette",text); free(text); goto fail; }
    free(s->palette); s->palette = text; s->flags |= APP_SPEC_PALETTE; goto done;
  }
  if (IS("frame") || IS("border") || IS("box")) {
    if (v->type == APP_JSON_BOOL) text = app_strdup(v->boolean ? "double" : "none",err);
    else text = spec_string(v,raw,err);
    if (!text) goto fail;
    if (strcmp(text,"double") && strcmp(text,"single") && strcmp(text,"heavy") &&
        strcmp(text,"ascii") && strcmp(text,"none")) {
      app_error_path(err,1,"chart spec: unknown frame",text); free(text); goto fail;
    }
    free(s->frame); s->frame = text; s->flags |= APP_SPEC_FRAME; goto done;
  }
  TXT("title",title,APP_SPEC_TITLE)
  if (IS("subtitle") || IS("sub")) { if (!set_text(&s->subtitle,v,raw,err)) goto fail; s->flags |= APP_SPEC_SUBTITLE; goto done; }
  if (IS("xlabel") || IS("xtitle") || IS("x_label")) { if (!set_text(&s->xlabel,v,raw,err)) goto fail; s->flags |= APP_SPEC_XLABEL; goto done; }
  if (IS("ylabel") || IS("ytitle") || IS("y_label")) { if (!set_text(&s->ylabel,v,raw,err)) goto fail; s->flags |= APP_SPEC_YLABEL; goto done; }
  if (IS("legend") || IS("show_legend")) { if (!spec_bool(v,&s->legend,raw,err)) goto fail; s->flags |= APP_SPEC_LEGEND; goto done; }
  if (IS("values") || IS("show_values") || IS("labels_on_bars")) { if (!spec_bool(v,&s->values,raw,err)) goto fail; s->flags |= APP_SPEC_VALUES; goto done; }
  if (IS("grid") || IS("show_grid")) { if (!spec_bool(v,&s->grid,raw,err)) goto fail; s->flags |= APP_SPEC_GRID; goto done; }
  if (IS("shadow") || IS("drop_shadow")) { if (!spec_bool(v,&s->shadow,raw,err)) goto fail; s->flags |= APP_SPEC_SHADOW; goto done; }
  if (IS("color") || IS("colour")) { if (!spec_bool(v,&s->color,raw,err)) goto fail; s->flags |= APP_SPEC_COLOR; goto done; }
  BOOL("ascii",ascii,APP_SPEC_ASCII)
  if (IS("explode")) {
    if (v->type == APP_JSON_BOOL) s->explode = v->boolean ? -1 : -2;
    else { if (!spec_number(v,&d,-1,512,raw,err)) goto fail; s->explode = (int)d; }
    s->flags |= APP_SPEC_EXPLODE; goto done;
  }
  if (IS("depth") || IS("depth3d") || IS("extrude")) { if (!spec_number(v,&d,0,6,raw,err)) goto fail; s->depth=(int)d; s->flags|=APP_SPEC_DEPTH; goto done; }
  if (IS("bins") || IS("buckets")) { if (!spec_number(v,&d,1,60,raw,err)) goto fail; s->bins=(int)d; s->flags|=APP_SPEC_BINS; goto done; }
  if (IS("prec") || IS("decimals") || IS("precision")) { if (!spec_number(v,&d,-1,8,raw,err)) goto fail; s->prec=(int)d; s->flags|=APP_SPEC_PREC; goto done; }
  if (IS("min") || IS("lo") || IS("ymin")) { if (!spec_number(v,&s->lo,-1e15,1e15,raw,err)) goto fail; s->flags|=APP_SPEC_LO; goto done; }
  if (IS("max") || IS("hi") || IS("ymax")) { if (!spec_number(v,&s->hi,-1e15,1e15,raw,err)) goto fail; s->flags|=APP_SPEC_HI; goto done; }
  if (IS("width") || IS("w")) { if (!spec_number(v,&d,20,1000,raw,err)) goto fail; s->width=(int)d; s->flags|=APP_SPEC_WIDTH; goto done; }
  if (IS("height") || IS("h")) { if (!spec_number(v,&d,6,500,raw,err)) goto fail; s->height=(int)d; s->flags|=APP_SPEC_HEIGHT; goto done; }
  BOOL("xy",xy,APP_SPEC_XY)
  BOOL("transpose",transpose,APP_SPEC_TRANSPOSE)
  BOOL("no_header",no_header,APP_SPEC_NO_HEADER)
  if (IS("header")) { if (!spec_bool(v,&b,raw,err)) goto fail; s->no_header=!b; s->flags|=APP_SPEC_NO_HEADER; goto done; }
  if (IS("labels_col") || IS("label_col")) { if (!spec_number(v,&d,1,1000,raw,err)) goto fail; s->label_col=(int)d-1; s->flags|=APP_SPEC_LABEL_COL; goto done; }
  if (IS("series_col")) { if (!spec_number(v,&d,1,1000,raw,err)) goto fail; s->series_col=(int)d-1; s->flags|=APP_SPEC_SERIES_COL; goto done; }
  TXT("label_key",label_key,APP_SPEC_LABEL_KEY)
  if (IS("delim") || IS("delimiter")) {
    text = spec_string(v,raw,err); if (!text) goto fail;
    if (!strcmp(text,"tab") || !strcmp(text,"\\t")) s->delim='\t';
    else if (!strcmp(text,"comma")) s->delim=',';
    else if (!strcmp(text,"semi") || !strcmp(text,"semicolon")) s->delim=';';
    else if (!strcmp(text,"pipe")) s->delim='|';
    else if (strlen(text)==1) s->delim=text[0];
    else { free(text); spec_bad(err,"delim","one character or tab/comma/semi/pipe"); goto fail; }
    free(text); s->flags|=APP_SPEC_DELIM; goto done;
  }
done:
  free(k); return 1;
fail:
  free(k); return 0;
#undef IS
#undef TXT
#undef BOOL
#undef NUM
}

int app_spec_parse(app_spec *out, const app_json *object, app_error *err)
{
  size_t i;
  app_spec work;
  if (!object || object->type == APP_JSON_NULL) return 1;
  if (object->type != APP_JSON_OBJECT)
    return spec_bad(err,"chart spec","an object");
  app_spec_init(&work);
  if (!app_spec_clone(&work,out,err)) return 0;
  for (i = 0; i < object->count; i++) {
    if (!set_field(&work,object->members[i].key,object->members[i].value,err)) {
      app_spec_free(&work); return 0;
    }
  }
  app_spec_free(out); *out = work; return 1;
}

int app_spec_is_key(const char *key)
{
  static const char *names[] = {
    "type","chart","kind","palette","colours","colors","annotations",
    "series_colors","frame","border","box","title","subtitle","sub",
    "xlabel","xtitle","x_label","ylabel","ytitle","y_label","legend",
    "show_legend","values","show_values","labels_on_bars","grid","show_grid",
    "shadow","drop_shadow","color","colour","ascii","explode","depth",
    "depth3d","extrude","bins","buckets","prec","decimals","precision",
    "min","lo","ymin","max","hi","ymax","width","w","height","h",
    "xy","transpose","no_header","header","labels_col","label_col",
    "series_col","label_key","delim","delimiter","errors","error",
    "error_bars","errorbars","ranges",NULL
  };
  char *n;
  int result;
  app_error ignored;
  n = norm_key(key,&ignored);
  if (!n) return 0;
  result = one_of(n,names);
  free(n);
  return result;
}

int app_spec_numeric_key(const char *key)
{
  static const char *names[] = {
    "width","height","depth","bins","prec","explode","min","max",
    "lo","hi","ymin","ymax","w","h","buckets","decimals","precision",
    "extrude","depth3d",NULL
  };
  char *n;
  int result;
  app_error ignored;
  n = norm_key(key,&ignored);
  if (!n) return 0;
  result = one_of(n,names);
  free(n);
  return result;
}

int app_spec_directive(app_spec *out, const char *line, int *recognized,
                       app_error *err)
{
  char *s, *p, *key, *value;
  int explicit_colon, flag, i;
  app_json v;
  app_json *json;
  double number;
  if (recognized) *recognized = 0;
  s = app_trim_copy(line,err);
  if (!s) return 0;
  if (s[0] != '#') { free(s); return 1; }
  p = s + 1;
  while (*p && isspace((unsigned char)*p)) p++;
  if (!prefix_ci(p,"chart")) { free(s); return 1; }
  p += 5;
  if (*p == 's' || *p == 'S') p++;
  while (*p && isspace((unsigned char)*p)) p++;
  explicit_colon = *p == ':';
  if (explicit_colon) p++;
  while (*p && isspace((unsigned char)*p)) p++;
  if (!*p) { if (recognized) *recognized=explicit_colon; free(s); return 1; }
  if (!explicit_colon && *p != '{') {
    char *e;
    e = p;
    while (*e && *e != '=' && *e != ',' && !isspace((unsigned char)*e)) e++;
    key = app_strndup(p,(size_t)(e-p),err);
    if (!key) { free(s); return 0; }
    flag = app_spec_is_key(key);
    free(key);
    if (!flag) { free(s); return 1; }
  }
  if (recognized) *recognized = 1;
  if (*p == '{') {
    json = app_json_parse(p,err);
    if (!json) { free(s); return 0; }
    i = app_spec_parse(out,json,err);
    app_json_free(json); free(s); return i;
  }
  while (*p) {
    while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
    if (!*p) break;
    {
      char *start;
      start = p;
      while (*p && *p != '=' && *p != ',' && !isspace((unsigned char)*p)) p++;
      key = app_strndup(start,(size_t)(p-start),err);
      if (!key) { free(s); return 0; }
    }
    while (*p && isspace((unsigned char)*p)) p++;
    memset(&v,0,sizeof v);
    v.type = APP_JSON_BOOL; v.boolean = 1;
    value = NULL;
    if (*p == '=') {
      p++;
      while (*p && isspace((unsigned char)*p)) p++;
      if (*p == '"' || *p == '\'') {
        char q, *start;
        q = *p++; start = p;
        while (*p && *p != q) {
          if (*p == '\\' && p[1]) p++;
          p++;
        }
        value = app_strndup(start,(size_t)(p-start),err);
        if (*p) p++;
        while (*p && *p != ',') p++;
        v.type = APP_JSON_STRING; v.string = value;
      } else {
        char *start;
        start = p;
        while (*p && *p != ',') p++;
        value = app_strndup(start,(size_t)(p-start),err);
        if (value) {
          char *trimmed;
          trimmed = app_trim_copy(value,err);
          free(value); value = trimmed;
        }
        if (value && *value) {
          if (app_streq_ci(value,"true") || app_streq_ci(value,"yes") || app_streq_ci(value,"on")) {
            v.boolean = 1;
          } else if (app_streq_ci(value,"false") || app_streq_ci(value,"no") || app_streq_ci(value,"off")) {
            v.boolean = 0;
          } else if (app_parse_number(value,&number)) {
            v.type = APP_JSON_NUMBER; v.number = number;
          } else { v.type = APP_JSON_STRING; v.string = value; }
        }
      }
      if (!value) { free(key); free(s); return 0; }
    }
    i = *key ? set_field(out,key,&v,err) : 1;
    free(value); free(key);
    if (!i) { free(s); return 0; }
  }
  free(s); return 1;
}
