#include "model.h"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int app_stamp_equal(const app_stamp *a,const app_stamp *b)
{
  if (!a || !b) return a==b;
  return a->exists==b->exists && a->seconds==b->seconds &&
         a->nanoseconds==b->nanoseconds && a->size==b->size;
}

void app_error_set(app_error *err, int code, const char *message)
{
  size_t n;
  if (!err) return;
  err->code = code;
  if (!message) message = "unknown error";
  n = strlen(message);
  if (n >= sizeof err->message) n = sizeof err->message - 1;
  memcpy(err->message, message, n);
  err->message[n] = 0;
}

void app_error_path(app_error *err, int code, const char *path,
                    const char *message)
{
  size_t n, left;
  if (!err) return;
  app_error_set(err, code, path ? path : "");
  n = strlen(err->message);
  left = sizeof err->message - n - 1;
  if (left && n) { err->message[n++] = ':'; left--; }
  if (left) { err->message[n++] = ' '; left--; }
  if (!message) message = "error";
  if (strlen(message) < left) left = strlen(message);
  memcpy(err->message + n, message, left);
  err->message[n + left] = 0;
}

char *app_strndup(const char *s, size_t n, app_error *err)
{
  char *p;
  if (!s) s = "";
  if (n == (size_t)-1) { app_error_set(err, 1, "string too large"); return NULL; }
  p = (char *)malloc(n + 1);
  if (!p) { app_error_set(err, 1, "out of memory"); return NULL; }
  memcpy(p, s, n);
  p[n] = 0;
  return p;
}

char *app_strdup(const char *s, app_error *err)
{
  return app_strndup(s ? s : "", strlen(s ? s : ""), err);
}

int app_reserve(void **items, size_t *capacity, size_t count,
                size_t item_size, app_error *err)
{
  size_t n, limit;
  void *p;
  if (count <= *capacity) return 1;
  if (!item_size) { app_error_set(err, 1, "invalid allocation size"); return 0; }
  limit = ((size_t)-1) / item_size;
  if (count > limit) { app_error_set(err, 1, "allocation too large"); return 0; }
  n = *capacity ? *capacity : 4;
  while (n < count) {
    if (n > limit / 2) { n = count; break; }
    n *= 2;
  }
  if (n > limit) n = count;
  p = realloc(*items, n * item_size);
  if (!p) { app_error_set(err, 1, "out of memory"); return 0; }
  *items = p;
  *capacity = n;
  return 1;
}

char *app_read_file(const char *path, size_t *length, app_error *err)
{
  FILE *f;
  char *body;
  size_t cap, len, n;
  int close_file;
  if (length) *length = 0;
  close_file = path && strcmp(path, "-") != 0;
  f = close_file ? fopen(path, "rb") : stdin;
  if (!f) { app_error_path(err, 1, "cannot open", path); return NULL; }
  cap = 4096;
  body = (char *)malloc(cap);
  if (!body) { if (close_file) fclose(f); app_error_set(err, 1, "out of memory"); return NULL; }
  len = 0;
  for (;;) {
    if (len + 1 == cap && !app_reserve((void **)&body, &cap, cap + 1, 1, err)) break;
    n = fread(body + len, 1, cap - len - 1, f);
    len += n;
    if (n == 0) {
      if (ferror(f)) app_error_path(err, 1, "read failed", path);
      else { body[len] = 0; if (length) *length = len; if (close_file) fclose(f); return body; }
      break;
    }
  }
  free(body);
  if (close_file) fclose(f);
  return NULL;
}

int app_write_file(const char *path, const char *data, size_t n,
                   app_error *err)
{
  FILE *f;
  int ok;
  if (!path) { app_error_set(err, 1, "no output path"); return 0; }
  f = fopen(path, "wb");
  if (!f) { app_error_path(err, 1, "cannot write", path); return 0; }
  ok = fwrite(data, 1, n, f) == n;
  if (fclose(f) != 0) ok = 0;
  if (!ok) {
    app_error_path(err, 1, "write failed", path);
    return 0;
  }
  return 1;
}

char *app_trim_copy(const char *s, app_error *err)
{
  const char *end;
  if (!s) s = "";
  while (*s && isspace((unsigned char)*s)) s++;
  end = s + strlen(s);
  while (end > s && isspace((unsigned char)end[-1])) end--;
  return app_strndup(s, (size_t)(end - s), err);
}

int app_streq_ci(const char *a, const char *b)
{
  if (!a || !b) return a == b;
  while (*a && *b) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    a++; b++;
  }
  return *a == *b;
}

int app_parse_number(const char *raw, double *out)
{
  char *s, *t, *end;
  size_t n, i, j;
  int digits, dot, exp, valid;
  double v;
  app_error ignored;
  if (!raw || !out) return 0;
  s = app_trim_copy(raw, &ignored);
  if (!s) return 0;
  n = strlen(s);
  if (n && s[n - 1] == '%') s[--n] = 0;
  t = (char *)malloc(n + 1);
  if (!t) { free(s); return 0; }
  i = j = 0;
  if (s[i] == '-' || s[i] == '+') t[j++] = s[i++];
  if (s[i] == '$') i++;
  if (j == 0 && (s[i] == '-' || s[i] == '+')) t[j++] = s[i++];
  digits = dot = exp = 0;
  for (; i < n; i++) {
    if (isdigit((unsigned char)s[i])) { t[j++] = s[i]; digits = 1; }
    else if ((s[i] == ',' || s[i] == '_') && !dot && !exp && i > 0 &&
             isdigit((unsigned char)s[i - 1]) && i + 1 < n &&
             isdigit((unsigned char)s[i + 1])) { /* grouping */ }
    else if (s[i] == '.' && !dot && !exp) { t[j++] = s[i]; dot = 1; }
    else if ((s[i] == 'e' || s[i] == 'E') && digits && !exp && i + 1 < n &&
             (isdigit((unsigned char)s[i + 1]) ||
              ((s[i + 1] == '+' || s[i + 1] == '-') && i + 2 < n &&
               isdigit((unsigned char)s[i + 2])))) {
      t[j++] = s[i]; exp = 1;
      if (!isdigit((unsigned char)s[i + 1])) t[j++] = s[++i];
    } else { free(t); free(s); return 0; }
  }
  if (!digits) { free(t); free(s); return 0; }
  t[j] = 0;
  errno = 0;
  v = strtod(t, &end);
  valid = *end == 0 && v == v && v <= DBL_MAX && v >= -DBL_MAX;
  free(t);
  free(s);
  if (!valid) return 0;
  *out = v;
  return 1;
}

char *app_format_number(double value, int decimals, app_error *err)
{
  char buf[512], fmt[16];
  if (value != value || value > DBL_MAX || value < -DBL_MAX)
    return app_strdup("", err);
  if (decimals >= 0) {
    if (decimals > 8) decimals = 8;
    sprintf(fmt, "%%.%df", decimals);
    sprintf(buf, fmt, value);
  } else if (value == floor(value) && fabs(value) < 1e15) {
    sprintf(buf, "%.0f", value);
  } else {
    sprintf(buf, "%.12g", value);
  }
  return app_strdup(buf, err);
}

/* Gaps are represented by app_series.valid, never by this compatibility value. */
int app_is_missing(double value) { return value != value; }
double app_missing(void) { return 0.0; }
