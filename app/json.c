#include "model.h"
#include "charts.h"

#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct json_parser {
  const char *text;
  size_t length, pos;
  int depth;
  app_error *err;
} json_parser;

typedef struct json_buffer {
  char *bytes;
  size_t length, capacity;
  app_error *err;
} json_buffer;

app_json *app_json_new(app_json_type type, app_error *err)
{
  app_json *j;
  j = (app_json *)calloc(1, sizeof *j);
  if (!j) { app_error_set(err, 1, "out of memory"); return NULL; }
  j->type = type;
  return j;
}

void app_json_free(app_json *j)
{
  size_t i;
  if (!j) return;
  free(j->string);
  for (i = 0; i < j->count; i++) {
    if (j->type == APP_JSON_ARRAY) app_json_free(j->items[i]);
    else if (j->type == APP_JSON_OBJECT) {
      free(j->members[i].key);
      app_json_free(j->members[i].value);
    }
  }
  free(j->items);
  free(j->members);
  free(j);
}

int app_json_push(app_json *array, app_json *value, app_error *err)
{
  if (!array || array->type != APP_JSON_ARRAY || !value) {
    app_error_set(err, 1, "expected a JSON array"); return 0;
  }
  if (!app_reserve((void **)&array->items, &array->capacity,
                   array->count + 1, sizeof *array->items, err)) return 0;
  array->items[array->count++] = value;
  return 1;
}

const app_json *app_json_get(const app_json *obj, const char *key)
{
  size_t i;
  if (!obj || obj->type != APP_JSON_OBJECT || !key) return NULL;
  for (i = 0; i < obj->count; i++)
    if (strcmp(obj->members[i].key, key) == 0) return obj->members[i].value;
  return NULL;
}

app_json *app_json_find(app_json *obj, const char *key)
{
  return (app_json *)app_json_get(obj, key);
}

int app_json_set(app_json *obj, const char *key, app_json *value,
                 app_error *err)
{
  size_t i;
  char *copy;
  if (!obj || obj->type != APP_JSON_OBJECT || !key || !value) {
    app_error_set(err, 1, "expected a JSON object"); return 0;
  }
  for (i = 0; i < obj->count; i++) {
    if (strcmp(obj->members[i].key, key) == 0) {
      app_json_free(obj->members[i].value);
      obj->members[i].value = value;
      return 1;
    }
  }
  copy = app_strdup(key, err);
  if (!copy) return 0;
  if (!app_reserve((void **)&obj->members, &obj->capacity,
                   obj->count + 1, sizeof *obj->members, err)) {
    free(copy); return 0;
  }
  obj->members[obj->count].key = copy;
  obj->members[obj->count].value = value;
  obj->count++;
  return 1;
}

void app_json_erase(app_json *obj, const char *key)
{
  size_t i;
  if (!obj || obj->type != APP_JSON_OBJECT || !key) return;
  for (i = 0; i < obj->count; i++) {
    if (strcmp(obj->members[i].key, key) == 0) {
      free(obj->members[i].key);
      app_json_free(obj->members[i].value);
      memmove(obj->members + i, obj->members + i + 1,
              (obj->count - i - 1) * sizeof *obj->members);
      obj->count--;
      return;
    }
  }
}

app_json *app_json_clone(const app_json *src, app_error *err)
{
  app_json *dst, *child;
  size_t i;
  if (!src) return NULL;
  dst = app_json_new(src->type, err);
  if (!dst) return NULL;
  dst->boolean = src->boolean;
  dst->number = src->number;
  if (src->string) {
    dst->string = app_strdup(src->string, err);
    if (!dst->string) goto fail;
  }
  for (i = 0; i < src->count; i++) {
    if (src->type == APP_JSON_ARRAY) {
      child = app_json_clone(src->items[i], err);
      if (!child) goto fail;
      if (!app_json_push(dst, child, err)) { app_json_free(child); goto fail; }
    } else if (src->type == APP_JSON_OBJECT) {
      child = app_json_clone(src->members[i].value, err);
      if (!child) goto fail;
      if (!app_json_set(dst, src->members[i].key, child, err)) {
        app_json_free(child); goto fail;
      }
    }
  }
  return dst;
fail:
  app_json_free(dst);
  return NULL;
}

static void json_fail(json_parser *p, const char *what)
{
  size_t i;
  int line, col;
  char buf[512];
  line = col = 1;
  for (i = 0; i < p->pos && i < p->length; i++) {
    if (p->text[i] == '\n') { line++; col = 1; }
    else col++;
  }
  sprintf(buf, "json: %s at line %d col %d", what, line, col);
  app_error_set(p->err, 1, buf);
}

static int json_skip(json_parser *p)
{
  for (;;) {
    while (p->pos < p->length &&
           (p->text[p->pos] == ' ' || p->text[p->pos] == '\t' ||
            p->text[p->pos] == '\r' || p->text[p->pos] == '\n')) p->pos++;
    if (p->pos + 1 < p->length && p->text[p->pos] == '/' &&
        p->text[p->pos + 1] == '/') {
      p->pos += 2;
      while (p->pos < p->length && p->text[p->pos] != '\n') p->pos++;
    } else if (p->pos + 1 < p->length && p->text[p->pos] == '/' &&
               p->text[p->pos + 1] == '*') {
      p->pos += 2;
      while (p->pos + 1 < p->length &&
             !(p->text[p->pos] == '*' && p->text[p->pos + 1] == '/')) p->pos++;
      if (p->pos + 1 >= p->length) {
        json_fail(p, "unterminated comment"); return 0;
      }
      p->pos += 2;
    } else break;
  }
  return 1;
}

static int json_peek(json_parser *p)
{
  if (!json_skip(p)) return -1;
  if (p->pos >= p->length) {
    json_fail(p, "unexpected end of input"); return -1;
  }
  return (unsigned char)p->text[p->pos];
}

static int json_take(json_parser *p, int c)
{
  if (json_peek(p) != c) {
    char buf[32];
    sprintf(buf, "expected '%c'", c);
    json_fail(p, buf);
    return 0;
  }
  p->pos++;
  return 1;
}

static int json_buf_add(json_buffer *b, const char *s, size_t n)
{
  if (n > (size_t)-1 - b->length - 1 ||
      !app_reserve((void **)&b->bytes, &b->capacity,
                   b->length + n + 1, 1, b->err)) return 0;
  memcpy(b->bytes + b->length, s, n);
  b->length += n;
  b->bytes[b->length] = 0;
  return 1;
}

static int json_buf_char(json_buffer *b, char c)
{
  return json_buf_add(b, &c, 1);
}

static int json_hex(int c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static int json_u4(json_parser *p, unsigned long *cp)
{
  int i, n;
  unsigned long v;
  v = 0;
  for (i = 0; i < 4; i++) {
    if (p->pos >= p->length || (n = json_hex((unsigned char)p->text[p->pos])) < 0) {
      json_fail(p, "bad hex digit in \\u escape"); return 0;
    }
    v = v * 16UL + (unsigned long)n;
    p->pos++;
  }
  *cp = v;
  return 1;
}

static int json_utf8(json_buffer *b, unsigned long cp)
{
  char s[4];
  size_t n;
  if (cp >= 0xD800UL && cp <= 0xDFFFUL) cp = 0xFFFDUL;
  if (cp > 0x10FFFFUL) cp = 0xFFFDUL;
  if (cp <= 0x7FUL) { s[0] = (char)cp; n = 1; }
  else if (cp <= 0x7FFUL) {
    s[0] = (char)(0xC0UL | (cp >> 6));
    s[1] = (char)(0x80UL | (cp & 63UL)); n = 2;
  } else if (cp <= 0xFFFFUL) {
    s[0] = (char)(0xE0UL | (cp >> 12));
    s[1] = (char)(0x80UL | ((cp >> 6) & 63UL));
    s[2] = (char)(0x80UL | (cp & 63UL)); n = 3;
  } else {
    s[0] = (char)(0xF0UL | (cp >> 18));
    s[1] = (char)(0x80UL | ((cp >> 12) & 63UL));
    s[2] = (char)(0x80UL | ((cp >> 6) & 63UL));
    s[3] = (char)(0x80UL | (cp & 63UL)); n = 4;
  }
  return json_buf_add(b, s, n);
}

static char *json_string(json_parser *p)
{
  json_buffer b;
  unsigned long cp, lo;
  int c, e;
  memset(&b, 0, sizeof b);
  b.err = p->err;
  if (!json_take(p, '"')) return NULL;
  for (;;) {
    if (p->pos >= p->length) { json_fail(p, "unterminated string"); goto fail; }
    c = (unsigned char)p->text[p->pos++];
    if (c == '"') break;
    if (c != '\\') {
      /* raw control bytes are accepted, as the original did: decks written
         by heredocs and agents carry literal tabs and newlines */
      if (!json_buf_char(&b, (char)c)) goto fail;
      continue;
    }
    if (p->pos >= p->length) { json_fail(p, "unterminated escape"); goto fail; }
    e = (unsigned char)p->text[p->pos++];
    if (e == 'n') c = '\n';
    else if (e == 't') c = '\t';
    else if (e == 'r') c = '\r';
    else if (e == 'b') c = '\b';
    else if (e == 'f') c = '\f';
    else if (e == '/' || e == '\\' || e == '"') c = e;
    else if (e == 'u') {
      if (!json_u4(p, &cp)) goto fail;
      if (cp >= 0xD800UL && cp <= 0xDBFFUL && p->pos + 5 < p->length &&
          p->text[p->pos] == '\\' && p->text[p->pos + 1] == 'u') {
        p->pos += 2;
        if (!json_u4(p, &lo)) goto fail;
        if (lo >= 0xDC00UL && lo <= 0xDFFFUL)
          cp = 0x10000UL + ((cp - 0xD800UL) << 10) + lo - 0xDC00UL;
        else { json_fail(p, "unpaired surrogate"); goto fail; }
      }
      if (!json_utf8(&b, cp)) goto fail;
      continue;
    } else { json_fail(p, "unknown escape"); goto fail; }
    if (!json_buf_char(&b, (char)c)) goto fail;
  }
  if (!b.bytes) return app_strdup("", p->err);
  return b.bytes;
fail:
  free(b.bytes);
  return NULL;
}

static app_json *json_value(json_parser *p);

static app_json *json_array(json_parser *p)
{
  app_json *a, *child;
  int c;
  a = app_json_new(APP_JSON_ARRAY, p->err);
  if (!a) return NULL;
  if (!json_take(p, '[')) goto fail;
  c = json_peek(p);
  if (c == ']') { p->pos++; return a; }
  while (c >= 0) {
    child = json_value(p);
    if (!child) goto fail;
    if (!app_json_push(a, child, p->err)) { app_json_free(child); goto fail; }
    c = json_peek(p);
    if (c == ']') { p->pos++; return a; }
    if (c != ',') { json_fail(p, "expected ',' or ']'"); goto fail; }
    p->pos++;
    c = json_peek(p);
  }
fail:
  app_json_free(a);
  return NULL;
}

static app_json *json_object(json_parser *p)
{
  app_json *o, *child;
  char *key;
  int c;
  o = app_json_new(APP_JSON_OBJECT, p->err);
  if (!o) return NULL;
  if (!json_take(p, '{')) goto fail;
  c = json_peek(p);
  if (c == '}') { p->pos++; return o; }
  while (c >= 0) {
    key = json_string(p);
    if (!key) goto fail;
    if (!json_take(p, ':')) { free(key); goto fail; }
    child = json_value(p);
    if (!child) { free(key); goto fail; }
    if (!app_json_set(o, key, child, p->err)) {
      free(key); app_json_free(child); goto fail;
    }
    free(key);
    c = json_peek(p);
    if (c == '}') { p->pos++; return o; }
    if (c != ',') { json_fail(p, "expected ',' or '}'"); goto fail; }
    p->pos++;
    c = json_peek(p);
  }
fail:
  app_json_free(o);
  return NULL;
}

static app_json *json_value(json_parser *p)
{
  int c, digits;
  size_t start;
  app_json *j;
  char *num, *end;
  double value;
  p->depth++;
  if (p->depth > 96) { json_fail(p, "too deeply nested"); p->depth--; return NULL; }
  c = json_peek(p);
  j = NULL;
  if (c == '{') j = json_object(p);
  else if (c == '[') j = json_array(p);
  else if (c == '"') {
    j = app_json_new(APP_JSON_STRING, p->err);
    if (j && !(j->string = json_string(p))) { app_json_free(j); j = NULL; }
  } else if (c == 't' || c == 'f' || c == 'n') {
    const char *lit;
    app_json_type type;
    if (c == 't') { lit = "true"; type = APP_JSON_BOOL; }
    else if (c == 'f') { lit = "false"; type = APP_JSON_BOOL; }
    else { lit = "null"; type = APP_JSON_NULL; }
    if (p->length - p->pos >= strlen(lit) &&
        strncmp(p->text + p->pos, lit, strlen(lit)) == 0) {
      p->pos += strlen(lit);
      j = app_json_new(type, p->err);
      if (j) j->boolean = c == 't';
    } else json_fail(p, "bad literal");
  } else if (c >= 0) {
    start = p->pos;
    if (p->text[p->pos] == '-' || p->text[p->pos] == '+') p->pos++;
    digits = 0;
    while (p->pos < p->length && isdigit((unsigned char)p->text[p->pos])) {
      p->pos++; digits = 1;
    }
    if (p->pos < p->length && p->text[p->pos] == '.') {
      p->pos++;
      while (p->pos < p->length && isdigit((unsigned char)p->text[p->pos])) {
        p->pos++; digits = 1;
      }
    }
    if (p->pos < p->length && (p->text[p->pos] == 'e' || p->text[p->pos] == 'E')) {
      p->pos++;
      if (p->pos < p->length && (p->text[p->pos] == '-' || p->text[p->pos] == '+')) p->pos++;
      while (p->pos < p->length && isdigit((unsigned char)p->text[p->pos])) p->pos++;
    }
    if (!digits) json_fail(p, "expected a value");
    else {
      num = app_strndup(p->text + start, p->pos - start, p->err);
      if (num) {
        value = strtod(num, &end);
        if (*end || value != value || value > DBL_MAX || value < -DBL_MAX)
          json_fail(p, "number out of range");
        else {
          j = app_json_new(APP_JSON_NUMBER, p->err);
          if (j) j->number = value;
        }
        free(num);
      }
    }
  }
  p->depth--;
  return j;
}

app_json *app_json_parse(const char *text, app_error *err)
{
  json_parser p;
  app_json *j;
  if (!text) { app_error_set(err, 1, "json: empty input"); return NULL; }
  p.text = text;
  p.length = strlen(text);
  p.pos = 0;
  p.depth = 0;
  p.err = err;
  if (!json_skip(&p)) return NULL;
  if (p.pos == p.length) { app_error_set(err, 1, "json: empty input"); return NULL; }
  j = json_value(&p);
  if (!j) return NULL;
  if (!json_skip(&p)) { app_json_free(j); return NULL; }
  if (p.pos < p.length) { json_fail(&p, "trailing junk after value"); app_json_free(j); return NULL; }
  return j;
}

/* What is written is always valid JSON: malformed UTF-8 becomes U+FFFD,
   as the original's clean_utf8 did. */
static int json_write_string(json_buffer *b, const char *s)
{
  const unsigned char *p;
  const char *next;
  char esc[7], utf8[5];
  if (!json_buf_char(b, '"')) return 0;
  for (p = (const unsigned char *)(s ? s : ""); *p; p++) {
    if (*p >= 0x80) {
      next = (const char *)p;
      if (!json_buf_add(b, utf8, lc_utf8_encode(lc_utf8_next(&next), utf8))) return 0;
      p = (const unsigned char *)next - 1;
    } else if (*p == '"' || *p == '\\') {
      if (!json_buf_char(b, '\\') || !json_buf_char(b, (char)*p)) return 0;
    } else if (*p == '\n' || *p == '\r' || *p == '\t') {
      if (!json_buf_char(b, '\\') ||
          !json_buf_char(b, *p == '\n' ? 'n' : (*p == '\r' ? 'r' : 't'))) return 0;
    } else if (*p < 32) {
      sprintf(esc, "\\u%04x", (unsigned)*p);
      if (!json_buf_add(b, esc, 6)) return 0;
    } else if (!json_buf_char(b, (char)*p)) return 0;
  }
  return json_buf_char(b, '"');
}

static int json_indent(json_buffer *b, int depth)
{
  int i;
  for (i = 0; i < depth * 2; i++)
    if (!json_buf_char(b, ' ')) return 0;
  return 1;
}

static int json_write_value(json_buffer *b, const app_json *j, int depth)
{
  size_t i;
  int flat;
  char num[512];
  if (!j) return json_buf_add(b, "null", 4);
  if (j->type == APP_JSON_NULL) return json_buf_add(b, "null", 4);
  if (j->type == APP_JSON_BOOL)
    return json_buf_add(b, j->boolean ? "true" : "false", j->boolean ? 4 : 5);
  if (j->type == APP_JSON_NUMBER) {
    if (j->number != j->number || j->number > DBL_MAX || j->number < -DBL_MAX)
      return json_buf_add(b, "null", 4);
    /* the original's fmt_raw: integers whole, everything else to 12 digits */
    if (j->number == floor(j->number) && fabs(j->number) < 1e15)
      sprintf(num, "%.0f", j->number);
    else sprintf(num, "%.12g", j->number);
    return json_buf_add(b, num, strlen(num));
  }
  if (j->type == APP_JSON_STRING) return json_write_string(b, j->string);
  if (j->type == APP_JSON_ARRAY) {
    if (!json_buf_char(b, '[')) return 0;
    flat = 1;
    for (i = 0; i < j->count; i++)
      if (j->items[i]->type == APP_JSON_ARRAY ||
          j->items[i]->type == APP_JSON_OBJECT) flat = 0;
    for (i = 0; i < j->count; i++) {
      if (i && !json_buf_char(b, ',')) return 0;
      if (flat) { if (i && !json_buf_char(b, ' ')) return 0; }
      else if (!json_buf_char(b, '\n') || !json_indent(b, depth + 1)) return 0;
      if (!json_write_value(b, j->items[i], depth + 1)) return 0;
    }
    if (!flat && j->count && (!json_buf_char(b, '\n') || !json_indent(b, depth))) return 0;
    return json_buf_char(b, ']');
  }
  if (j->type == APP_JSON_OBJECT) {
    size_t len = 0;
    /* A small object of scalars (an annotation, a series header) reads better
       on one line. */
    flat = j->count && j->count <= 6 && depth > 0;
    for (i = 0; i < j->count; i++) {
      const app_json *v = j->members[i].value;
      if (v && (v->type == APP_JSON_ARRAY || v->type == APP_JSON_OBJECT)) flat = 0;
      len += strlen(j->members[i].key) + 8;
      if (v && v->type == APP_JSON_STRING) len += strlen(v->string);
    }
    if (flat && len < 90) {
      if (!json_buf_char(b, '{')) return 0;
      for (i = 0; i < j->count; i++) {
        if (i && !json_buf_add(b, ", ", 2)) return 0;
        if (!json_write_string(b, j->members[i].key) ||
            !json_buf_add(b, ": ", 2) ||
            !json_write_value(b, j->members[i].value, depth + 1)) return 0;
      }
      return json_buf_char(b, '}');
    }
    if (!json_buf_char(b, '{')) return 0;
    for (i = 0; i < j->count; i++) {
      if (i && !json_buf_char(b, ',')) return 0;
      if (!json_buf_char(b, '\n') || !json_indent(b, depth + 1) ||
          !json_write_string(b, j->members[i].key) ||
          !json_buf_add(b, ": ", 2) ||
          !json_write_value(b, j->members[i].value, depth + 1)) return 0;
    }
    if (j->count && (!json_buf_char(b, '\n') || !json_indent(b, depth))) return 0;
    return json_buf_char(b, '}');
  }
  app_error_set(b->err, 1, "unknown JSON type");
  return 0;
}

char *app_json_write(const app_json *node, app_error *err)
{
  json_buffer b;
  memset(&b, 0, sizeof b);
  b.err = err;
  if (!json_write_value(&b, node, 0) || !json_buf_char(&b, '\n')) {
    free(b.bytes); return NULL;
  }
  return b.bytes;
}
