#include "model.h"
#include "platform.h"

#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct data_row { char **fields; size_t count, capacity; } data_row;
typedef struct data_table { data_row *rows; size_t count, capacity; } data_table;

static void bounded_append(char *dst,size_t capacity,const char *src)
{
  size_t used=strlen(dst),n=strlen(src);
  if (used>=capacity) return;
  if (n>capacity-used-1) n=capacity-used-1;
  memcpy(dst+used,src,n);dst[used+n]=0;
}

void app_load_opts_init(app_load_opts *o)
{
  memset(o,0,sizeof *o);
  o->label_col = o->series_col = -1;
}

static void load_spec(app_load_opts *o, const app_spec *s)
{
#define APPLY(f,flag) if ((s->flags & flag) && !(o->flags & flag)) o->f=s->f
  APPLY(delim,APP_SPEC_DELIM);
  APPLY(transpose,APP_SPEC_TRANSPOSE);
  APPLY(xy,APP_SPEC_XY);
  APPLY(no_header,APP_SPEC_NO_HEADER);
  APPLY(label_col,APP_SPEC_LABEL_COL);
  APPLY(series_col,APP_SPEC_SERIES_COL);
  APPLY(label_key,APP_SPEC_LABEL_KEY);
#undef APPLY
}

static void series_free(app_series *s)
{
  free(s->name); free(s->values); free(s->valid);
  memset(s,0,sizeof *s);
}

static void text_free(app_text_column *t)
{
  size_t i;
  free(t->name);
  for (i = 0; i < t->count; i++) free(t->values[i]);
  free(t->values);
  memset(t,0,sizeof *t);
}

void app_dataset_init(app_dataset *d)
{
  if (!d) return;
  memset(d,0,sizeof *d);
  d->had_header = 1;
  app_spec_init(&d->spec);
}

void app_dataset_free(app_dataset *d)
{
  size_t i;
  if (!d) return;
  free(d->title); free(d->source); free(d->label_name); free(d->x_name);
  free(d->lossy_why); free(d->hint);
  for (i = 0; i < d->label_count; i++) free(d->labels[i]);
  free(d->labels);
  for (i = 0; i < d->series_count; i++) series_free(d->series + i);
  free(d->series);
  for (i = 0; i < d->text_count; i++) text_free(d->text + i);
  free(d->text);
  app_spec_free(&d->spec);
  app_dataset_init(d);
}

static char *index_name(size_t index, app_error *err)
{
  char buf[32];
  sprintf(buf,"%lu",(unsigned long)index);
  return app_strdup(buf,err);
}

static int labels_push(app_dataset *d, const char *s, app_error *err)
{
  char *copy;
  copy = app_strdup(s,err);
  if (!copy) return 0;
  if (!app_reserve((void **)&d->labels,&d->label_capacity,d->label_count+1,
                   sizeof *d->labels,err)) { free(copy); return 0; }
  d->labels[d->label_count++] = copy;
  return 1;
}

static int series_init(app_series *s, const char *name, size_t count, app_error *err)
{
  memset(s,0,sizeof *s);
  s->name = app_strdup(name,err);
  if (!s->name) return 0;
  s->decimals = -1;
  s->color = 7;
  if (count) {
    if (count > (size_t)-1 / sizeof *s->values) {
      app_error_set(err,1,"too many data values"); series_free(s); return 0;
    }
    s->values = (double *)calloc(count,sizeof *s->values);
    s->valid = (unsigned char *)calloc(count,sizeof *s->valid);
    if (!s->values || !s->valid) {
      app_error_set(err,1,"out of memory"); series_free(s); return 0;
    }
    s->count = s->capacity = count;
  }
  return 1;
}

static int series_push(app_dataset *d, app_series *s, app_error *err)
{
  if (!app_reserve((void **)&d->series,&d->series_capacity,d->series_count+1,
                   sizeof *d->series,err)) return 0;
  d->series[d->series_count++] = *s;
  memset(s,0,sizeof *s);
  return 1;
}

static int series_fill_json(app_series *s, const app_json *a)
{
  size_t i;
  if (!a || a->type != APP_JSON_ARRAY || a->count != s->count) return 0;
  for (i = 0; i < a->count; i++) {
    if (a->items[i]->type == APP_JSON_NUMBER) {
      s->values[i] = a->items[i]->number; s->valid[i] = 1;
    } else if (a->items[i]->type != APP_JSON_NULL) return 0;
  }
  return 1;
}

size_t app_data_rows(const app_dataset *d)
{
  size_t i, n;
  n = d->label_count;
  for (i = 0; i < d->series_count; i++) if (d->series[i].count > n) n = d->series[i].count;
  return n;
}

int app_dataset_clone(app_dataset *dst, const app_dataset *src, app_error *err)
{
  app_dataset t;
  app_series s;
  app_text_column tc;
  size_t i, k;
  app_dataset_init(&t);
#define DCOPY(f) if (src->f && !(t.f=app_strdup(src->f,err))) goto fail
  DCOPY(title); DCOPY(source); DCOPY(label_name); DCOPY(x_name);
  DCOPY(lossy_why); DCOPY(hint);
#undef DCOPY
  t.label_column=src->label_column; t.had_header=src->had_header; t.lossy=src->lossy;
  if (!app_spec_clone(&t.spec,&src->spec,err)) goto fail;
  for (i=0;i<src->label_count;i++) if (!labels_push(&t,src->labels[i],err)) goto fail;
  for (i=0;i<src->series_count;i++) {
    if (!series_init(&s,src->series[i].name,src->series[i].count,err)) goto fail;
    if (s.count) {
      memcpy(s.values,src->series[i].values,s.count*sizeof *s.values);
      memcpy(s.valid,src->series[i].valid,s.count*sizeof *s.valid);
    }
    s.color=src->series[i].color; s.column=src->series[i].column;
    s.decimals=src->series[i].decimals;
    if (!series_push(&t,&s,err)) { series_free(&s); goto fail; }
  }
  for (i=0;i<src->text_count;i++) {
    memset(&tc,0,sizeof tc);
    tc.name=app_strdup(src->text[i].name,err);
    if (!tc.name) goto fail;
    tc.column=src->text[i].column;
    for (k=0;k<src->text[i].count;k++) {
      char *word;
      word=app_strdup(src->text[i].values[k],err);
      if (!word || !app_reserve((void **)&tc.values,&tc.capacity,tc.count+1,
                                sizeof *tc.values,err)) { free(word); text_free(&tc); goto fail; }
      tc.values[tc.count++]=word;
    }
    if (!app_reserve((void **)&t.text,&t.text_capacity,t.text_count+1,
                     sizeof tc,err)) { text_free(&tc); goto fail; }
    t.text[t.text_count++]=tc;
  }
  app_dataset_free(dst); *dst=t; return 1;
fail:
  app_dataset_free(&t); return 0;
}

static int json_number_array(const app_json *a)
{
  size_t i;
  if (!a || a->type != APP_JSON_ARRAY) return 0;
  for (i=0;i<a->count;i++)
    if (a->items[i]->type != APP_JSON_NUMBER && a->items[i]->type != APP_JSON_NULL) return 0;
  return 1;
}

static char *json_text(const app_json *j, const char *fallback, app_error *err)
{
  if (!j) return app_strdup(fallback,err);
  if (j->type == APP_JSON_STRING) return app_strdup(j->string,err);
  if (j->type == APP_JSON_NUMBER) return app_format_number(j->number,-1,err);
  return app_strdup(fallback,err);
}

static int json_labels(app_dataset *d, const app_json *array, app_error *err)
{
  size_t i;
  char *s;
  if (!array || array->type != APP_JSON_ARRAY) return 1;
  for (i=0;i<array->count;i++) {
    s=json_text(array->items[i],"?",err);
    if (!s) return 0;
    if (!labels_push(d,s,err)) { free(s); return 0; }
    free(s);
  }
  return 1;
}

static int json_named_arrays(app_dataset *d, const app_json *obj, app_error *err)
{
  size_t i;
  app_series s;
  for (i=0;i<obj->count;i++) {
    const app_json *v=obj->members[i].value;
    if (!json_number_array(v)) continue;
    if (!series_init(&s,obj->members[i].key,v->count,err)) return 0;
    if (!series_fill_json(&s,v)) { series_free(&s); continue; }
    if (!series_push(d,&s,err)) { series_free(&s); return 0; }
  }
  return 1;
}

static int json_series_array(app_dataset *d, const app_json *a, app_error *err)
{
  size_t i;
  app_series s;
  char *name;
  const app_json *item,*values,*name_j;
  for (i=0;i<a->count;i++) {
    item=a->items[i];
    if (item->type != APP_JSON_OBJECT) continue;
    name_j=app_json_get(item,"name");
    if (!name_j) name_j=app_json_get(item,"label");
    values=app_json_get(item,"values");
    if (!values) values=app_json_get(item,"data");
    if (!values) values=app_json_get(item,"v");
    if (!json_number_array(values)) continue;
    name=name_j ? json_text(name_j,"",err) : index_name(d->series_count+1,err);
    if (!name) return 0;
    if (!series_init(&s,name,values->count,err)) { free(name); return 0; }
    free(name);
    series_fill_json(&s,values);
    if (!series_push(d,&s,err)) { series_free(&s); return 0; }
  }
  return 1;
}

static int key_is_label(const char *k)
{
  static const char *names[]={"label","name","key","category","cat",
      "x","date","time","month","year",NULL};
  size_t i;
  for (i=0;names[i];i++) if (app_streq_ci(k,names[i])) return 1;
  return 0;
}

static int key_seen(char **keys,size_t count,const char *key)
{
  size_t i;
  for (i=0;i<count;i++) if (!strcmp(keys[i],key)) return 1;
  return 0;
}

/* 1 on success, 0 on an error already in err, -1 when the array holds no
   usable records: the caller decides whether that is fatal, as the original's
   dataset_from_records returned false for it */
static int records(app_dataset *d, const app_json *arr,
                   const app_load_opts *opts, app_error *err)
{
  int soft=0;
  char **keys;
  unsigned char *numeric;
  size_t key_count,key_cap,i,k,r,nn,tot;
  const char *label_key;
  const app_json *rec,*v;
  char *text;
  app_series s;
  app_text_column tc;
  keys=NULL; key_count=key_cap=0; numeric=NULL;
  for (r=0;r<arr->count;r++) {
    rec=arr->items[r];
    if (rec->type!=APP_JSON_OBJECT) { soft=1; goto fail; }
    for (k=0;k<rec->count;k++) {
      const char *key=rec->members[k].key;
      if (!key_seen(keys,key_count,key)) {
        if (!app_reserve((void **)&keys,&key_cap,key_count+1,sizeof *keys,err)) goto fail;
        keys[key_count]=app_strdup(key,err);
        if (!keys[key_count]) goto fail;
        key_count++;
      }
    }
  }
  if (!key_count) { soft=1; goto fail; }
  label_key=opts && opts->label_key ? opts->label_key : NULL;
  if (!label_key || !*label_key) {
    label_key=NULL;
    for (i=0;i<key_count;i++) {
      if (!key_is_label(keys[i])) continue;
      nn=tot=0;
      for (r=0;r<arr->count;r++) {
        v=app_json_get(arr->items[r],keys[i]);
        if (!v || v->type==APP_JSON_NULL) continue;
        tot++; if (v->type==APP_JSON_NUMBER) nn++;
      }
      if (tot && nn*2>tot) continue;
      label_key=keys[i]; break;
    }
  }
  numeric=(unsigned char *)calloc(key_count,1);
  if (!numeric) { app_error_set(err,1,"out of memory"); goto fail; }
  nn=0;
  for (i=0;i<key_count;i++) {
    if (label_key && !strcmp(keys[i],label_key)) continue;
    k=tot=0;
    for (r=0;r<arr->count;r++) {
      v=app_json_get(arr->items[r],keys[i]);
      if (!v) continue;
      tot++; if (v->type==APP_JSON_NUMBER) k++;
    }
    if (tot && k*2>=tot) { numeric[i]=1; nn++; }
  }
  if (!nn) { soft=1; goto fail; }
  if (label_key) {
    d->label_name=app_strdup(label_key,err);
    if (!d->label_name) goto fail;
  }
  for (r=0;r<arr->count;r++) {
    v=label_key ? app_json_get(arr->items[r],label_key) : NULL;
    if (v && (v->type==APP_JSON_STRING || v->type==APP_JSON_NUMBER)) text=json_text(v,"",err);
    else text=index_name(r+1,err);
    if (!text || !labels_push(d,text,err)) { free(text); goto fail; }
    free(text);
  }
  for (i=0;i<key_count;i++) if (numeric[i]) {
    if (!series_init(&s,keys[i],arr->count,err)) goto fail;
    for (r=0;r<arr->count;r++) {
      v=app_json_get(arr->items[r],keys[i]);
      if (v && v->type==APP_JSON_NUMBER) { s.values[r]=v->number; s.valid[r]=1; }
    }
    if (!series_push(d,&s,err)) { series_free(&s); goto fail; }
  }
  for (i=0;i<key_count;i++) if (!numeric[i] && (!label_key || strcmp(keys[i],label_key))) {
    memset(&tc,0,sizeof tc);
    tc.name=app_strdup(keys[i],err);
    if (!tc.name) goto fail;
    for (r=0;r<arr->count;r++) {
      v=app_json_get(arr->items[r],keys[i]);
      text=json_text(v,"",err);
      if (!text || !app_reserve((void **)&tc.values,&tc.capacity,tc.count+1,
                                sizeof *tc.values,err)) { free(text); text_free(&tc); goto fail; }
      tc.values[tc.count++]=text;
    }
    if (!app_reserve((void **)&d->text,&d->text_capacity,d->text_count+1,
                     sizeof tc,err)) { text_free(&tc); goto fail; }
    d->text[d->text_count++]=tc;
    d->lossy=1;
    if (!d->lossy_why) d->lossy_why=app_strdup("a field of words is only shown by tables",err);
  }
  for (i=0;i<key_count;i++) free(keys[i]);
  free(keys); free(numeric); return 1;
fail:
  for (i=0;i<key_count;i++) free(keys[i]);
  free(keys); free(numeric); return soft ? -1 : 0;
}

static int dataset_number_series(app_dataset *d, int series_col,
                                 const app_load_opts *opts, app_error *err)
{
  size_t i, w;
  int col, found, keep;
  char buf[256];
  col=2; found=0;
  for (i=0;i<d->series_count;i++) {
    if (d->series[i].name && d->series[i].name[0]=='\001') continue;
    d->series[i].column=col++;
    if (series_col>=0 && d->series[i].column==series_col+1) found=1;
  }
  if (series_col<0) return 1;
  if (!found) {
    sprintf(buf,"series_col %d %s; columns count from 1 with the labels included",
            series_col+1,series_col==0 ? "is the labels" : "is past the last series");
    app_error_set(err,1,buf); return 0;
  }
  w=0;
  for (i=0;i<d->series_count;i++) {
    size_t j;
    keep=d->series[i].name && d->series[i].name[0]=='\001';
    if (d->series[i].column==series_col+1) keep=1;
    if (opts) for (j=0;j<opts->keep_count;j++)
      if (app_streq_ci(d->series[i].name,opts->keep[j])) keep=1;
    if (keep) { if (w!=i) d->series[w]=d->series[i]; w++; }
    else series_free(d->series+i);
  }
  d->series_count=w;
  for (i=0;i<d->text_count;i++) text_free(d->text+i);
  d->text_count=0;
  d->lossy=1;
  free(d->lossy_why);
  d->lossy_why=app_strdup("series_col hides the other columns",err);
  return d->lossy_why!=NULL;
}

static int json_matrix(app_dataset *d, const app_json *matrix, app_error *err)
{
  const app_json *first,*row,*cell;
  size_t r,c,cols;
  app_series s;
  char *name,*label;
  if (!matrix->count) return 0;
  first=matrix->items[0];
  if (first->type!=APP_JSON_ARRAY) return 0;
  cols=first->count;
  if (cols<2) return 0;
  for (r=1;r<matrix->count;r++) {
    row=matrix->items[r];
    if (row->type!=APP_JSON_ARRAY) continue;
    cell=row->count ? row->items[0] : NULL;
    label=cell ? json_text(cell,"?",err) : index_name(r,err);
    if (!label || !labels_push(d,label,err)) { free(label); return 0; }
    free(label);
  }
  for (c=1;c<cols;c++) {
    cell=first->items[c];
    if (cell->type==APP_JSON_STRING && cell->string && *cell->string)
      name=app_strdup(cell->string,err);
    else name=index_name(c,err);
    if (!name) return 0;
    if (!series_init(&s,name,d->label_count,err)) { free(name); return 0; }
    free(name);
    {
      size_t out_r;
      out_r=0;
      for (r=1;r<matrix->count;r++) {
        row=matrix->items[r];
        if (row->type!=APP_JSON_ARRAY) continue;
        if (c<row->count && row->items[c]->type==APP_JSON_NUMBER) {
          s.values[out_r]=row->items[c]->number; s.valid[out_r]=1;
        }
        out_r++;
      }
    }
    if (!series_push(d,&s,err)) { series_free(&s); return 0; }
  }
  return 1;
}

static int fill_missing_labels(app_dataset *d, app_error *err)
{
  size_t i,n;
  char *name;
  if (d->label_count) return 1;
  n=app_data_rows(d);
  for (i=0;i<n;i++) {
    name=index_name(i+1,err);
    if (!name || !labels_push(d,name,err)) { free(name); return 0; }
    free(name);
  }
  return 1;
}

static int json_data_impl(app_dataset *d, const app_json *j,
                          const app_load_opts *opts, app_error *err)
{
  app_load_opts local;
  app_series s;
  const app_json *chart,*rows,*series,*x,*labels,*v,*name_j;
  size_t i,k;
  if (!j) { app_error_set(err,1,"json: no data"); return 0; }
  if (opts) local=*opts; else app_load_opts_init(&local);
  if (j->type==APP_JSON_OBJECT) {
    chart=app_json_get(j,"chart");
    if (chart && !app_spec_parse(&d->spec,chart,err)) return 0;
    for (i=0;i<j->count;i++) {
      app_json one;
      app_json_member m;
      v=j->members[i].value;
      if (!app_spec_is_key(j->members[i].key) || !strcmp(j->members[i].key,"chart")) continue;
      if (v->type==APP_JSON_NUMBER && !app_spec_numeric_key(j->members[i].key)) continue;
      memset(&one,0,sizeof one); one.type=APP_JSON_OBJECT;
      m=j->members[i]; one.members=&m; one.count=1;
      if (!app_spec_parse(&d->spec,&one,err)) return 0;
    }
  }
  load_spec(&local,&d->spec);
  if (j->type==APP_JSON_OBJECT) {
    v=app_json_get(j,"title");
    if (v && v->type==APP_JSON_STRING) {
      d->title=app_strdup(v->string,err); if (!d->title) return 0;
    }
    labels=app_json_get(j,"labels");
    if (labels && !json_labels(d,labels,err)) return 0;
    v=app_json_get(j,"label_name");
    if (v && v->type==APP_JSON_STRING) {
      d->label_name=app_strdup(v->string,err); if (!d->label_name) return 0;
    }
    x=app_json_get(j,"x");
    rows=app_json_get(j,"rows");
    if (!rows) rows=app_json_get(j,"data");
    if (!rows) rows=app_json_get(j,"records");
    series=app_json_get(j,"series");
    if (rows && rows->type==APP_JSON_ARRAY) {
      if (rows->count && rows->items[0]->type==APP_JSON_ARRAY) {
        if (!json_matrix(d,rows,err)) return 0;
      } else if (!records(d,rows,&local,err)) return 0; /* -1: "no numeric series" below */
    } else if (series) {
      if (series->type==APP_JSON_ARRAY) {
        if (!json_series_array(d,series,err)) return 0;
        if (!d->series_count && !json_matrix(d,series,err)) return 0;
      } else if (series->type==APP_JSON_OBJECT) {
        if (!json_named_arrays(d,series,err)) return 0;
      }
    } else if (x && x->type==APP_JSON_ARRAY) {
      for (i=0;i<j->count;i++) {
        v=j->members[i].value;
        if (!strcmp(j->members[i].key,"x") || !json_number_array(v)) continue;
        if (!series_init(&s,j->members[i].key,v->count,err)) return 0;
        series_fill_json(&s,v);
        if (!series_push(d,&s,err)) { series_free(&s); return 0; }
      }
      if (d->series_count && d->label_count==0 && !json_labels(d,x,err)) return 0;
    } else {
      k=0;
      for (i=0;i<j->count;i++)
        if (j->members[i].value->type==APP_JSON_NUMBER &&
            !app_spec_numeric_key(j->members[i].key)) k++;
      if (k) {
        if (!series_init(&s,"value",k,err)) return 0;
        k=0;
        for (i=0;i<j->count;i++) {
          v=j->members[i].value;
          if (v->type!=APP_JSON_NUMBER || app_spec_numeric_key(j->members[i].key)) continue;
          if (!labels_push(d,j->members[i].key,err)) { series_free(&s); return 0; }
          s.values[k]=v->number; s.valid[k++]=1;
        }
        if (!series_push(d,&s,err)) { series_free(&s); return 0; }
      }
    }
    if (x && x->type==APP_JSON_ARRAY && series && json_number_array(x)) {
      if (!series_init(&s,"\001x",x->count,err)) return 0;
      series_fill_json(&s,x);
      if (!app_reserve((void **)&d->series,&d->series_capacity,d->series_count+1,
                       sizeof s,err)) { series_free(&s); return 0; }
      memmove(d->series+1,d->series,d->series_count*sizeof s);
      d->series[0]=s; d->series_count++;
      if (d->label_count==0 && !json_labels(d,x,err)) return 0;
    }
  } else if (j->type==APP_JSON_ARRAY) {
    if (!j->count) { app_error_set(err,1,"json: empty array"); return 0; }
    if (j->items[0]->type==APP_JSON_NUMBER || j->items[0]->type==APP_JSON_NULL) {
      if (!series_init(&s,"value",j->count,err)) return 0;
      for (i=0;i<j->count;i++) if (j->items[i]->type==APP_JSON_NUMBER) {
        s.values[i]=j->items[i]->number; s.valid[i]=1;
      }
      if (!series_push(d,&s,err)) { series_free(&s); return 0; }
    } else if (j->items[0]->type==APP_JSON_OBJECT) {
      int r=records(d,j,&local,err);
      if (r<0) app_error_set(err,1,"json: no numeric fields in records");
      if (r<=0) return 0;
    } else if (j->items[0]->type==APP_JSON_ARRAY) {
      if (!json_matrix(d,j,err)) return 0;
    }
  }
  if (!d->series_count) { app_error_set(err,1,"json: no numeric series found"); return 0; }
  if (!fill_missing_labels(d,err)) return 0;
  if (!dataset_number_series(d,local.series_col,&local,err)) return 0;
  if (!d->title && (d->spec.flags & APP_SPEC_TITLE)) {
    d->title=app_strdup(d->spec.title,err); if (!d->title) return 0;
  }
  name_j=app_json_get(j,"x_name");
  d->x_name=json_text(name_j,"x",err);
  return d->x_name!=NULL;
}

int app_data_from_json(app_dataset *out, const app_json *value,
                       const app_load_opts *opts, app_error *err)
{
  app_dataset t;
  app_dataset_init(&t);
  if (!json_data_impl(&t,value,opts,err)) { app_dataset_free(&t); return 0; }
  app_dataset_free(out); *out=t; return 1;
}

static void row_free(data_row *r)
{
  size_t i;
  for (i=0;i<r->count;i++) free(r->fields[i]);
  free(r->fields); memset(r,0,sizeof *r);
}

static void table_free(data_table *t)
{
  size_t i;
  for (i=0;i<t->count;i++) row_free(t->rows+i);
  free(t->rows); memset(t,0,sizeof *t);
}

static int row_push(data_row *r, const char *field, size_t n, app_error *err)
{
  char *copy;
  copy=app_strndup(field,n,err);
  if (!copy) return 0;
  if (!app_reserve((void **)&r->fields,&r->capacity,r->count+1,
                   sizeof *r->fields,err)) { free(copy); return 0; }
  r->fields[r->count++]=copy;
  return 1;
}

static int table_push(data_table *t, data_row *r, app_error *err)
{
  if (!app_reserve((void **)&t->rows,&t->capacity,t->count+1,
                   sizeof *t->rows,err)) return 0;
  t->rows[t->count++]=*r;
  memset(r,0,sizeof *r);
  return 1;
}

static char sniff_delim(const char *line)
{
  static const char candidates[]={',',';','\t','|'};
  int best, n;
  size_t i,k;
  char out;
  best=-1; out=',';
  for (i=0;i<4;i++) {
    n=0;
    for (k=0;line[k] && line[k]!='\n';k++) if (line[k]==candidates[i]) n++;
    if (n>best) { best=n; out=candidates[i]; }
  }
  return out;
}

static int parse_table(data_table *t, const char *text, char delim, app_error *err)
{
  data_row row;
  char *field;
  size_t fcap,flen,i,n,k;
  int quoted,saw,blank;
  memset(&row,0,sizeof row);
  fcap=128; flen=0; quoted=saw=0;
  field=(char *)malloc(fcap);
  if (!field) { app_error_set(err,1,"out of memory"); return 0; }
  n=strlen(text);
  for (i=0;i<=n;i++) {
    char c;
    c=i<n ? text[i] : '\n';
    if (quoted) {
      if (c=='"') {
        if (i+1<n && text[i+1]=='"') { c='"'; i++; }
        else { quoted=0; continue; }
      }
    } else {
      if (c=='"') { quoted=1; saw=1; continue; }
      if (c=='#' && !saw && row.count==0) {
        blank=1;
        for (k=0;k<flen;k++) if (!isspace((unsigned char)field[k])) blank=0;
        if (blank) {
          flen=0;
          while (i<n && text[i]!='\n') i++;
          continue;
        }
      }
      if (c==delim || c=='\n' || c=='\r' || i==n) {
        if (c=='\r' && i+1<n && text[i+1]=='\n') continue;
        if (!row_push(&row,field,flen,err)) goto fail;
        flen=0;
        if (c==delim) { saw=1; continue; }
        blank=1;
        for (k=0;k<row.count;k++) {
          const char *p=row.fields[k];
          while (*p) { if (!isspace((unsigned char)*p)) { blank=0; break; } p++; }
        }
        if (!blank && !table_push(t,&row,err)) goto fail;
        if (blank) row_free(&row);
        saw=0;
        continue;
      }
    }
    if (flen+1>=fcap && !app_reserve((void **)&field,&fcap,flen+2,1,err)) goto fail;
    field[flen++]=c;
    if (c!=' ' && c!='\t') saw=1;
  }
  free(field); row_free(&row); return 1;
fail:
  free(field); row_free(&row); return 0;
}

static int transpose_table(data_table *t, app_error *err)
{
  data_table o;
  data_row row;
  size_t cols,c,r;
  memset(&o,0,sizeof o);
  cols=0;
  for (r=0;r<t->count;r++) if (t->rows[r].count>cols) cols=t->rows[r].count;
  for (c=0;c<cols;c++) {
    memset(&row,0,sizeof row);
    for (r=0;r<t->count;r++) {
      const char *s=c<t->rows[r].count ? t->rows[r].fields[c] : "";
      if (!row_push(&row,s,strlen(s),err)) { row_free(&row); table_free(&o); return 0; }
    }
    if (!table_push(&o,&row,err)) { row_free(&row); table_free(&o); return 0; }
  }
  table_free(t); *t=o; return 1;
}

static int csv_decimals(const char *f)
{
  const char *dot,*p;
  int n;
  if (strchr(f,'e') || strchr(f,'E')) return -1;
  dot=strrchr(f,'.');
  if (!dot) return 0;
  n=0;
  for (p=dot+1;isdigit((unsigned char)*p);p++) n++;
  return n;
}

static int csv_header(const data_row *r, int no_header)
{
  size_t c;
  int any_text,any_num;
  double value;
  char *trim;
  app_error ignored;
  if (no_header) return 0;
  any_text=any_num=0;
  for (c=0;c<r->count;c++) {
    trim=app_trim_copy(r->fields[c],&ignored);
    if (!trim) continue;
    if (app_parse_number(trim,&value)) any_num=1;
    else if (*trim) any_text=1;
    free(trim);
  }
  return any_text || !any_num;
}

static int scan_directives(app_spec *spec, const char *text, app_error *err)
{
  const char *p,*end;
  char *line;
  int noncomments,recognized;
  p=text; noncomments=0;
  while (*p) {
    end=strchr(p,'\n'); if (!end) end=p+strlen(p);
    line=app_strndup(p,(size_t)(end-p),err);
    if (!line) return 0;
    {
      char *t=app_trim_copy(line,err);
      if (!t) { free(line); return 0; }
      if (*t && *t!='#') noncomments++;
      if (*t=='#' && !app_spec_directive(spec,t,&recognized,err)) {
        char reason[512];
        strcpy(reason,"bad #chart line: ");
        bounded_append(reason,sizeof reason,err->message);
        app_error_set(err,1,reason);
        free(t); free(line); return 0;
      }
      free(t);
    }
    free(line);
    if (noncomments>40) break;
    p=*end ? end+1 : end;
  }
  return 1;
}

static int csv_data_impl(app_dataset *d, const char *text,
                         const app_load_opts *opts, app_error *err)
{
  app_load_opts local;
  data_table table;
  char **names;
  int *nonblank,*nums;
  unsigned char *numeric;
  size_t cols,first,r,c,ndata,i;
  int label_col,xcol,bad_cells,head,dec;
  char delim;
  char *trim;
  char bad[512];
  app_series s;
  app_text_column tc;
  double value;
  memset(&table,0,sizeof table);
  names=NULL; numeric=NULL; nonblank=nums=NULL;
  if (!*text) { app_error_set(err,1,"empty input"); return 0; }
  if (!scan_directives(&d->spec,text,err)) return 0;
  if (opts) local=*opts; else app_load_opts_init(&local);
  load_spec(&local,&d->spec);
  delim=local.delim;
  if (!delim) {
    const char *p=text;
    while (*p) {
      const char *end=strchr(p,'\n');
      if (!end) end=p+strlen(p);
      trim=app_strndup(p,(size_t)(end-p),err);
      if (!trim) goto fail;
      {
        char *t=app_trim_copy(trim,err);
        free(trim); trim=t;
      }
      if (!trim) goto fail;
      if (*trim && *trim!='#') { delim=sniff_delim(trim); free(trim); break; }
      free(trim);
      p=*end ? end+1 : end;
    }
    if (!delim) delim=',';
  }
  if (!parse_table(&table,text,delim,err)) goto fail;
  if (local.transpose && !transpose_table(&table,err)) goto fail;
  if (!table.count) { app_error_set(err,1,"no rows found"); goto fail; }
  cols=0;
  for (r=0;r<table.count;r++) if (table.rows[r].count>cols) cols=table.rows[r].count;
  if (!cols) { app_error_set(err,1,"no columns found"); goto fail; }
  for (r=0;r<table.count;r++)
    while (table.rows[r].count<cols) if (!row_push(table.rows+r,"",0,err)) goto fail;
  head=csv_header(table.rows,local.no_header);
  first=head ? 1 : 0;
  if (table.count<=first) { app_error_set(err,1,"header row with no data under it"); goto fail; }
  ndata=table.count-first;
  names=(char **)calloc(cols,sizeof *names);
  numeric=(unsigned char *)calloc(cols,1);
  nums=(int *)calloc(cols,sizeof *nums);
  nonblank=(int *)calloc(cols,sizeof *nonblank);
  if (!names || !numeric || !nums || !nonblank) { app_error_set(err,1,"out of memory"); goto fail; }
  for (c=0;c<cols;c++) {
    if (head) names[c]=app_trim_copy(table.rows[0].fields[c],err);
    else names[c]=app_strdup("",err);
    if (!names[c]) goto fail;
    if (!*names[c]) {
      char colname[40];
      free(names[c]);sprintf(colname,"col%lu",(unsigned long)(c+1));
      names[c]=app_strdup(colname,err);if (!names[c]) goto fail;
    }
    for (r=first;r<table.count;r++) {
      trim=app_trim_copy(table.rows[r].fields[c],err); if (!trim) goto fail;
      if (*trim) { nonblank[c]++; if (app_parse_number(trim,&value)) nums[c]++; }
      free(trim);
    }
    numeric[c]=nums[c]>0 && nums[c]*2>nonblank[c];
  }
  label_col=local.label_col;
  if (label_col<0 && cols>1 && !numeric[0]) label_col=0;
  if (label_col>=(int)cols) { app_error_set(err,1,"label column out of range"); goto fail; }
  xcol=-1;
  if (local.xy) for (c=0;c<cols;c++) if ((int)c!=label_col && numeric[c]) { xcol=(int)c; break; }
  d->had_header=head; d->label_column=label_col+1;
  if (label_col>=0 && head) { d->label_name=app_strdup(names[label_col],err); if (!d->label_name) goto fail; }
  d->x_name=app_strdup(xcol>=0 ? names[xcol] : "x",err);
  if (!d->x_name) goto fail;
  for (r=first;r<table.count;r++) {
    if (label_col>=0) {
      trim=app_trim_copy(table.rows[r].fields[label_col],err);
      if (!trim) goto fail;
      if (!*trim) { free(trim); trim=index_name(r-first+1,err); if (!trim) goto fail; }
    } else { trim=index_name(r-first+1,err); if (!trim) goto fail; }
    if (!labels_push(d,trim,err)) { free(trim); goto fail; }
    free(trim);
  }
  if (local.transpose) {
    d->lossy=1; d->lossy_why=app_strdup("the file is read transposed",err);
    if (!d->lossy_why) goto fail;
  }
  if (local.series_col>=0) {
    int sc=local.series_col;
    char buf[512],piece[80];
    if (sc>=(int)cols || sc==label_col || sc==xcol || !numeric[sc]) {
      sprintf(buf,"series_col %d ",sc+1);
      if (sc>=(int)cols) {
        sprintf(piece,"is past the last column (there are %lu)",(unsigned long)cols);
        bounded_append(buf,sizeof buf,piece);
      } else {
        bounded_append(buf,sizeof buf,"is \"");
        bounded_append(buf,sizeof buf,names[sc]);
        if (sc==label_col) bounded_append(buf,sizeof buf,"\", the label column");
        else if (sc==xcol) bounded_append(buf,sizeof buf,"\", the X axis under xy");
        else bounded_append(buf,sizeof buf,"\", which is not numbers");
      }
      bounded_append(buf,sizeof buf,"; columns count from 1 with the labels included");
      for (c=0;c<cols;c++) if ((int)c!=label_col && (int)c!=xcol && numeric[c]) {
        int first_series=1;
        size_t prev;
        for (prev=0;prev<c;prev++) if ((int)prev!=label_col && (int)prev!=xcol && numeric[prev]) first_series=0;
        bounded_append(buf,sizeof buf,first_series ? ", so the series are " : ", ");
        sprintf(piece,"%lu \"",(unsigned long)(c+1));
        bounded_append(buf,sizeof buf,piece);
        bounded_append(buf,sizeof buf,names[c]);
        bounded_append(buf,sizeof buf,"\"");
      }
      app_error_set(err,1,buf); goto fail;
    }
  }
  bad_cells=0; bad[0]=0;
  for (c=0;c<cols;c++) {
    if ((int)c==label_col || (int)c==xcol) continue;
    if (!numeric[c]) {
      if (!nonblank[c]) continue;
      if (!d->lossy) {
        d->lossy=1; d->lossy_why=app_strdup("a text column is only shown by tables",err);
        if (!d->lossy_why) goto fail;
      }
      if (local.series_col>=0) continue;
      memset(&tc,0,sizeof tc);
      tc.name=app_strdup(names[c],err); tc.column=(int)c+1;
      if (!tc.name) goto fail;
      for (r=first;r<table.count;r++) {
        trim=app_trim_copy(table.rows[r].fields[c],err);
        if (!trim || !app_reserve((void **)&tc.values,&tc.capacity,tc.count+1,
                                  sizeof *tc.values,err)) { free(trim); text_free(&tc); goto fail; }
        tc.values[tc.count++]=trim;
      }
      if (!app_reserve((void **)&d->text,&d->text_capacity,d->text_count+1,
                       sizeof tc,err)) { text_free(&tc); goto fail; }
      d->text[d->text_count++]=tc;
      continue;
    }
    if (local.series_col>=0 && (int)c!=local.series_col) {
      int keep=0;
      for (i=0;i<local.keep_count;i++) if (app_streq_ci(local.keep[i],names[c])) keep=1;
      for (i=0;i<d->spec.error_count;i++)
        if (app_streq_ci(d->spec.errors[i].lo,names[c]) ||
            (d->spec.errors[i].hi && app_streq_ci(d->spec.errors[i].hi,names[c]))) keep=1;
      if (!keep) {
        if (!d->lossy) { d->lossy=1; d->lossy_why=app_strdup("series_col hides the other columns",err); if (!d->lossy_why) goto fail; }
        continue;
      }
    }
    if (!series_init(&s,names[c],ndata,err)) goto fail;
    s.column=(int)c+1;
    for (r=first;r<table.count;r++) {
      trim=app_trim_copy(table.rows[r].fields[c],err); if (!trim) { series_free(&s); goto fail; }
      if (*trim && app_parse_number(trim,&value)) {
        s.values[r-first]=value; s.valid[r-first]=1;
        dec=csv_decimals(trim); if (dec>s.decimals) s.decimals=dec;
      } else if (*trim) {
        bad_cells++;
        if (bad_cells<=3) {
          char rownum[40];
          if (bad[0]) bounded_append(bad,sizeof bad,", ");
          bounded_append(bad,sizeof bad,"\"");
          bounded_append(bad,sizeof bad,trim);
          bounded_append(bad,sizeof bad,"\" in \"");
          bounded_append(bad,sizeof bad,names[c]);
          sprintf(rownum,"\" (row %lu)",(unsigned long)(r-first+1));
          bounded_append(bad,sizeof bad,rownum);
        }
        if (!d->lossy) {
          d->lossy=1; d->lossy_why=app_strdup("a cell is not numeric; saving would lose it",err);
          if (!d->lossy_why) { free(trim); series_free(&s); goto fail; }
        }
      }
      free(trim);
    }
    if (!series_push(d,&s,err)) { series_free(&s); goto fail; }
  }
  if (xcol>=0) {
    if (!series_init(&s,"\001x",ndata,err)) goto fail;
    s.column=xcol+1;
    for (r=first;r<table.count;r++) {
      if (app_parse_number(table.rows[r].fields[xcol],&value)) {
        s.values[r-first]=value; s.valid[r-first]=1;
      }
    }
    if (!app_reserve((void **)&d->series,&d->series_capacity,d->series_count+1,
                     sizeof s,err)) { series_free(&s); goto fail; }
    memmove(d->series+1,d->series,d->series_count*sizeof s);
    d->series[0]=s; d->series_count++;
  }
  if (bad_cells) {
    if (bad_cells>3) {
      char rest[48];sprintf(rest," and %d more",bad_cells-3);
      bounded_append(bad,sizeof bad,rest);
    }
    bounded_append(bad,sizeof bad,bad_cells==1 ? " is not a number" : " are not numbers");
    bounded_append(bad,sizeof bad,", drawn as gaps (numbers are plain: 1234.5, -3, 1e6, \"1,234\", $12, 12%)");
    d->hint=app_strdup(bad,err); if (!d->hint) goto fail;
  }
  if (!d->series_count) {
    app_error_set(err,1,"no numeric columns found (csv needs a header row and numbers)"); goto fail;
  }
  if (d->spec.flags & APP_SPEC_TITLE) {
    d->title=app_strdup(d->spec.title,err); if (!d->title) goto fail;
  }
  for (c=0;c<cols;c++) free(names[c]);
  free(names); free(numeric); free(nums); free(nonblank);
  table_free(&table); return 1;
fail:
  if (names) for (c=0;c<cols;c++) free(names[c]);
  free(names); free(numeric); free(nums); free(nonblank);
  table_free(&table);
  return 0;
}

int app_data_load_text(app_dataset *out, const char *text, const char *name,
                       const app_load_opts *opts, app_error *err)
{
  app_dataset t;
  app_json *j;
  const char *body;
  char *trim;
  int ok;
  if (!text) { app_error_set(err,1,"empty input"); return 0; }
  body=text;
  if ((unsigned char)body[0]==0xEF && (unsigned char)body[1]==0xBB &&
      (unsigned char)body[2]==0xBF) body+=3;
  trim=app_trim_copy(body,err);
  if (!trim) return 0;
  if (!*trim) { free(trim); app_error_path(err,1,"empty input",name); return 0; }
  app_dataset_init(&t);
  if (*trim=='{' || *trim=='[') {
    j=app_json_parse(body,err);
    if (!j) { free(trim); return 0; }
    ok=json_data_impl(&t,j,opts,err);
    app_json_free(j);
  } else ok=csv_data_impl(&t,body,opts,err);
  free(trim);
  if (!ok) { app_dataset_free(&t); return 0; }
  t.source=app_strdup(name && !strcmp(name,"-") ? "<stdin>" : name,err);
  if (!t.source) { app_dataset_free(&t); return 0; }
  app_dataset_free(out); *out=t;
  return 1;
}

int app_data_load_file(app_dataset *out, const char *path,
                       const app_load_opts *opts, app_error *err)
{
  char *text;
  int ok;
  text=app_read_file(path,NULL,err);
  if (!text) return 0;
  ok=app_data_load_text(out,text,path,opts,err);
  free(text);
  return ok;
}

static app_json *json_string_new(const char *text, app_error *err)
{
  app_json *j;
  j=app_json_new(APP_JSON_STRING,err);
  if (!j) return NULL;
  j->string=app_strdup(text ? text : "",err);
  if (!j->string) { app_json_free(j); return NULL; }
  return j;
}

static app_json *json_num_new(double value, app_error *err)
{
  app_json *j=app_json_new(APP_JSON_NUMBER,err);
  if (j) j->number=value;
  return j;
}

static int json_set_string(app_json *obj, const char *key, const char *text,
                           app_error *err)
{
  app_json *j=json_string_new(text,err);
  if (!j) return 0;
  if (!app_json_set(obj,key,j,err)) { app_json_free(j); return 0; }
  return 1;
}

static int json_set_clone(app_json *obj, const char *key, const app_json *value,
                          app_error *err)
{
  app_json *j=app_json_clone(value,err);
  if (!j) return 0;
  if (!app_json_set(obj,key,j,err)) { app_json_free(j); return 0; }
  return 1;
}

int app_data_to_json(const app_dataset *d, app_json **out, app_error *err)
{
  app_json *j,*labels,*series,*one,*values,*v;
  size_t i,r,n;
  j=app_json_new(APP_JSON_OBJECT,err);
  if (!j) return 0;
  if (d->label_name && *d->label_name &&
      !json_set_string(j,"label_name",d->label_name,err)) goto fail;
  labels=app_json_new(APP_JSON_ARRAY,err);
  if (!labels) goto fail;
  n=app_data_rows(d);
  for (r=0;r<n;r++) {
    char *fallback=NULL;
    if (r>=d->label_count) fallback=index_name(r+1,err);
    v=json_string_new(r<d->label_count ? d->labels[r] : fallback,err);
    free(fallback);
    if (!v || !app_json_push(labels,v,err)) { app_json_free(v); app_json_free(labels); goto fail; }
  }
  if (!app_json_set(j,"labels",labels,err)) { app_json_free(labels); goto fail; }
  series=app_json_new(APP_JSON_ARRAY,err);
  if (!series) goto fail;
  for (i=0;i<d->series_count;i++) {
    const app_series *s=d->series+i;
    values=app_json_new(APP_JSON_ARRAY,err);
    if (!values) { app_json_free(series); goto fail; }
    for (r=0;r<s->count;r++) {
      v=s->valid && s->valid[r] ? json_num_new(s->values[r],err) :
                             app_json_new(APP_JSON_NULL,err);
      if (!v || !app_json_push(values,v,err)) {
        app_json_free(v); app_json_free(values); app_json_free(series); goto fail;
      }
    }
    if (s->name && s->name[0]=='\001') {
      if (!app_json_set(j,"x",values,err)) { app_json_free(values); app_json_free(series); goto fail; }
      if (d->x_name && strcmp(d->x_name,"x") &&
          !json_set_string(j,"x_name",d->x_name,err)) { app_json_free(series); goto fail; }
      continue;
    }
    one=app_json_new(APP_JSON_OBJECT,err);
    if (!one) { app_json_free(values); app_json_free(series); goto fail; }
    if (!json_set_string(one,"name",s->name,err)) { app_json_free(one); app_json_free(values); app_json_free(series); goto fail; }
    if (!app_json_set(one,"values",values,err)) { app_json_free(one); app_json_free(values); app_json_free(series); goto fail; }
    if (!app_json_push(series,one,err)) { app_json_free(one); app_json_free(series); goto fail; }
  }
  if (!app_json_set(j,"series",series,err)) { app_json_free(series); goto fail; }
  *out=j; return 1;
fail:
  app_json_free(j); return 0;
}

typedef struct data_buffer { char *bytes; size_t count,capacity; app_error *err; } data_buffer;

static int buf_add(data_buffer *b, const char *s, size_t n)
{
  if (n>(size_t)-1-b->count-1 ||
      !app_reserve((void **)&b->bytes,&b->capacity,b->count+n+1,1,b->err)) return 0;
  memcpy(b->bytes+b->count,s,n); b->count+=n; b->bytes[b->count]=0; return 1;
}

static int buf_text(data_buffer *b, const char *s)
{ return buf_add(b,s ? s : "",strlen(s ? s : "")); }

static int buf_char(data_buffer *b, char c) { return buf_add(b,&c,1); }

static int csv_field(data_buffer *b, const char *field, char delim)
{
  size_t i,n;
  int quote;
  if (!field) field="";
  n=strlen(field);
  quote=n && (field[0]==' ' || field[n-1]==' ' || field[0]=='#');
  for (i=0;i<n;i++) if (field[i]==delim || field[i]=='"' ||
                       field[i]=='\n' || field[i]=='\r') quote=1;
  if (!quote) return buf_text(b,field);
  if (!buf_char(b,'"')) return 0;
  for (i=0;i<n;i++) {
    if (field[i]=='"' && !buf_char(b,'"')) return 0;
    if (!buf_char(b,field[i])) return 0;
  }
  return buf_char(b,'"');
}

static char *dataset_csv(const app_dataset *d, const char *original,
                         char delim, app_error *err)
{
  data_buffer b;
  const char *p,*end;
  size_t i,r,n;
  char *line,*trim,*number,*fallback;
  memset(&b,0,sizeof b); b.err=err;
  p=original;
  while (p && *p) {
    end=strchr(p,'\n'); if (!end) end=p+strlen(p);
    line=app_strndup(p,(size_t)(end-p),err);
    if (!line) goto fail;
    trim=app_trim_copy(line,err);
    if (!trim) { free(line); goto fail; }
    if (*trim && *trim!='#') { free(line); free(trim); break; }
    if (*trim=='#' && (!buf_text(&b,line) || !buf_char(&b,'\n'))) {
      free(line); free(trim); goto fail;
    }
    free(line); free(trim);
    p=*end ? end+1 : end;
  }
  if (d->had_header) {
    if (!csv_field(&b,d->label_name && *d->label_name ? d->label_name : "label",delim)) goto fail;
    for (i=0;i<d->series_count;i++) {
      if (!buf_char(&b,delim) || !csv_field(&b,d->series[i].name &&
          d->series[i].name[0]=='\001' ? d->x_name : d->series[i].name,delim)) goto fail;
    }
    if (!buf_char(&b,'\n')) goto fail;
  }
  n=app_data_rows(d);
  for (r=0;r<n;r++) {
    fallback=NULL;
    if (r>=d->label_count) fallback=index_name(r+1,err);
    if ((r>=d->label_count && !fallback) ||
        !csv_field(&b,r<d->label_count ? d->labels[r] : fallback,delim)) {
      free(fallback); goto fail;
    }
    free(fallback);
    for (i=0;i<d->series_count;i++) {
      if (!buf_char(&b,delim)) goto fail;
      if (r<d->series[i].count && d->series[i].valid && d->series[i].valid[r]) {
        number=app_format_number(d->series[i].values[r],-1,err);
        if (!number || !buf_text(&b,number)) { free(number); goto fail; }
        free(number);
      }
    }
    if (!buf_char(&b,'\n')) goto fail;
  }
  return b.bytes;
fail:
  free(b.bytes); return NULL;
}

static int data_key(const char *key)
{
  static const char *names[]={"labels","series","rows","data","records",
      "x","x_name","label_name",NULL};
  size_t i;
  for (i=0;names[i];i++) if (!strcmp(key,names[i])) return 1;
  return 0;
}

int app_data_save(const app_dataset *d, const char *path, app_error *err)
{
  char *original,*trim,*output;
  char delim;
  const char *p,*end;
  app_json *old,*fresh,*result;
  size_t i;
  int ok;
  if (!path || !*path || !strcmp(path,"-") || !strcmp(path,"<stdin>")) {
    app_error_set(err,1,"this data came from a pipe; there is no file to save to"); return 0;
  }
  if (d->lossy) { app_error_path(err,1,"not saving over",path); return 0; }
  original=app_read_file(path,NULL,err);
  if (!original) return 0;
  trim=app_trim_copy(original,err);
  if (!trim) { free(original); return 0; }
  output=NULL;
  if (*trim=='{' || *trim=='[') {
    old=app_json_parse(original,err); fresh=NULL; result=NULL;
    if (!old || !app_data_to_json(d,&fresh,err)) { app_json_free(old); free(trim); free(original); return 0; }
    result=app_json_new(APP_JSON_OBJECT,err);
    if (!result) { app_json_free(old); app_json_free(fresh); free(trim); free(original); return 0; }
    i=0;
    if (old->type==APP_JSON_OBJECT) for (i=0;i<old->count;i++) {
      const app_json_member *m=old->members+i;
      int skip=data_key(m->key) || m->value->type==APP_JSON_NUMBER ||
               json_number_array(m->value);
      if (m->value->type==APP_JSON_NUMBER && app_spec_numeric_key(m->key)) skip=0;
      if (!skip && !json_set_clone(result,m->key,m->value,err)) break;
    }
    if (old->type==APP_JSON_OBJECT && i<old->count) {
      app_json_free(old); app_json_free(fresh); app_json_free(result);
      free(trim); free(original); return 0;
    }
    for (i=0;i<fresh->count;i++) if (!json_set_clone(result,fresh->members[i].key,
                                                    fresh->members[i].value,err)) break;
    if (i==fresh->count) output=app_json_write(result,err);
    app_json_free(old); app_json_free(fresh); app_json_free(result);
  } else {
    delim=(d->spec.flags & APP_SPEC_DELIM) ? d->spec.delim : 0;
    if (!delim) {
      p=original;
      while (*p) {
        end=strchr(p,'\n'); if (!end) end=p+strlen(p);
        {
          char *line=app_strndup(p,(size_t)(end-p),err);
          char *t=line ? app_trim_copy(line,err) : NULL;
          free(line);
          if (!t) { free(trim); free(original); return 0; }
          if (*t && *t!='#') { delim=sniff_delim(t); free(t); break; }
          free(t);
        }
        p=*end ? end+1 : end;
      }
      if (!delim) delim=',';
    }
    output=dataset_csv(d,original,delim,err);
  }
  free(original); free(trim);
  if (!output) return 0;
  ok=app_write_replace(path,output,strlen(output),err);
  free(output);
  return ok;
}

static int desc_spec_field(data_buffer *b, int *first, const char *name,
                           const char *value)
{
  if (!*first && !buf_text(b,", ")) return 0;
  *first=0;
  return buf_text(b,name) && buf_char(b,'=') && buf_text(b,value);
}

static char *describe_num(double value, app_error *err)
{
  char raw[384],*out;
  size_t len,dot,digits,commas,i,j,start;
  int negative;
  if (value!=value || value>DBL_MAX || value < -DBL_MAX) return app_strdup("n/a",err);
  if (value==floor(value) && fabs(value)<1e15) sprintf(raw,"%.0f",value);
  else if (fabs(value)>=1e15) sprintf(raw,"%.12g",value);
  else {
    sprintf(raw,fabs(value)<1 ? "%.3f" : "%.2f",value);
    len=strlen(raw);
    while (len && raw[len-1]=='0') raw[--len]=0;
    if (len && raw[len-1]=='.') raw[--len]=0;
  }
  len=strlen(raw);negative=raw[0]=='-';dot=len;
  for (i=0;i<len;i++) if (raw[i]=='.' || raw[i]=='e' || raw[i]=='E') { dot=i; break; }
  start=negative ? 1 : 0;
  digits=dot-start;commas=digits>3 ? (digits-1)/3 : 0;
  out=(char *)malloc(len+commas+1);
  if (!out) { app_error_set(err,1,"out of memory"); return NULL; }
  j=0;
  for (i=0;i<len;i++) {
    if (i>start && i<dot && (dot-i)%3==0) out[j++]=',';
    out[j++]=raw[i];
  }
  out[j]=0;return out;
}

static int desc_padded(data_buffer *b,const char *s,size_t width)
{
  size_t i,n=strlen(s ? s : "");
  if (!buf_text(b,s)) return 0;
  for (i=n;i<width;i++) if (!buf_char(b,' ')) return 0;
  return 1;
}

char *app_data_describe(const app_dataset *d, app_error *err)
{
  data_buffer b;
  char nbuf[128];
  char *val,*lo_s,*hi_s,*sum_s;
  size_t i,r,valid;
  int first;
  double lo,hi,sum;
  const app_spec *s=&d->spec;
  memset(&b,0,sizeof b); b.err=err;
  if (!buf_text(&b,"source:  ") || !buf_text(&b,d->source) || !buf_char(&b,'\n')) goto fail;
  if (d->title && *d->title)
    if (!buf_text(&b,"title:   ") || !buf_text(&b,d->title) || !buf_char(&b,'\n')) goto fail;
  sprintf(nbuf,"rows:    %lu\nlabels:  %lu",(unsigned long)app_data_rows(d),
          (unsigned long)d->label_count);
  if (!buf_text(&b,nbuf)) goto fail;
  if (d->label_column>0) {
    sprintf(nbuf,", column %d",d->label_column);
    if (!buf_text(&b,nbuf)) goto fail;
    if (d->label_name && *d->label_name)
      if (!buf_text(&b," \"") || !buf_text(&b,d->label_name) || !buf_char(&b,'"')) goto fail;
  }
  sprintf(nbuf,"\nseries:  %lu\n",(unsigned long)d->series_count);
  if (!buf_text(&b,nbuf)) goto fail;
  if (!s->flags) {
    if (!buf_text(&b,"chart:   (none in the file; a bare 'charts file' uses the defaults)\n")) goto fail;
  } else {
    if (!buf_text(&b,"chart:   ")) goto fail;
    first=1;
#define DS_STR(bit,name,f) if (s->flags & bit) if (!desc_spec_field(&b,&first,name,s->f)) goto fail
#define DS_BOOL(bit,name,f) if (s->flags & bit) if (!desc_spec_field(&b,&first,name,s->f ? "true" : "false")) goto fail
#define DS_INT(bit,name,f) if (s->flags & bit) { sprintf(nbuf,"%d",s->f); if (!desc_spec_field(&b,&first,name,nbuf)) goto fail; }
    DS_STR(APP_SPEC_TYPE,"type",type);
    DS_STR(APP_SPEC_PALETTE,"palette",palette);
    DS_STR(APP_SPEC_FRAME,"frame",frame);
    if (s->flags & APP_SPEC_TITLE) {
      val=app_strdup(s->title,err);if (!val) goto fail;
      if (!first && !buf_text(&b,", ")) { free(val); goto fail; }
      first=0;
      if (!buf_text(&b,"title=\"") || !buf_text(&b,val) || !buf_char(&b,'"')) { free(val); goto fail; }
      free(val);
    }
    if (s->flags & APP_SPEC_SUBTITLE) {
      if (!first && !buf_text(&b,", ")) goto fail;
      first=0;
      if (!buf_text(&b,"subtitle=\"") || !buf_text(&b,s->subtitle) || !buf_char(&b,'"')) goto fail;
    }
    if (s->flags & APP_SPEC_XLABEL) {
      if (!first && !buf_text(&b,", ")) goto fail;
      first=0;
      if (!buf_text(&b,"xlabel=\"") || !buf_text(&b,s->xlabel) || !buf_char(&b,'"')) goto fail;
    }
    if (s->flags & APP_SPEC_YLABEL) {
      if (!first && !buf_text(&b,", ")) goto fail;
      first=0;
      if (!buf_text(&b,"ylabel=\"") || !buf_text(&b,s->ylabel) || !buf_char(&b,'"')) goto fail;
    }
    DS_BOOL(APP_SPEC_LEGEND,"legend",legend);
    DS_BOOL(APP_SPEC_VALUES,"values",values);
    DS_BOOL(APP_SPEC_GRID,"grid",grid);
    DS_BOOL(APP_SPEC_SHADOW,"shadow",shadow);
    DS_STR(APP_SPEC_FRAME,"frame",frame);
    DS_STR(APP_SPEC_TITLE,"title",title);
    DS_STR(APP_SPEC_SUBTITLE,"subtitle",subtitle);
    DS_STR(APP_SPEC_XLABEL,"xlabel",xlabel);
    DS_STR(APP_SPEC_YLABEL,"ylabel",ylabel);
    DS_BOOL(APP_SPEC_LEGEND,"legend",legend);
    DS_BOOL(APP_SPEC_VALUES,"values",values);
    DS_BOOL(APP_SPEC_GRID,"grid",grid);
    DS_BOOL(APP_SPEC_SHADOW,"shadow",shadow);
    DS_BOOL(APP_SPEC_COLOR,"color",color);
    DS_BOOL(APP_SPEC_ASCII,"ascii",ascii);
    DS_INT(APP_SPEC_DEPTH,"depth",depth);
    DS_INT(APP_SPEC_BINS,"bins",bins);
    DS_INT(APP_SPEC_PREC,"prec",prec);
    DS_INT(APP_SPEC_EXPLODE,"explode",explode);
    if (s->flags & APP_SPEC_LO) {
      val=app_format_number(s->lo,-1,err);
      if (!val) goto fail;
      if (!desc_spec_field(&b,&first,"min",val)) { free(val); goto fail; }
      free(val);
    }
    if (s->flags & APP_SPEC_HI) {
      val=app_format_number(s->hi,-1,err);
      if (!val) goto fail;
      if (!desc_spec_field(&b,&first,"max",val)) { free(val); goto fail; }
      free(val);
    }
    DS_INT(APP_SPEC_WIDTH,"width",width);
    DS_INT(APP_SPEC_HEIGHT,"height",height);
    DS_BOOL(APP_SPEC_XY,"xy",xy);
    DS_BOOL(APP_SPEC_TRANSPOSE,"transpose",transpose);
    DS_BOOL(APP_SPEC_NO_HEADER,"no_header",no_header);
    if (s->flags & APP_SPEC_LABEL_COL) {
      sprintf(nbuf,"%d",s->label_col+1);
      if (!desc_spec_field(&b,&first,"labels_col",nbuf)) goto fail;
    }
    if (s->flags & APP_SPEC_SERIES_COL) {
      sprintf(nbuf,"%d",s->series_col+1);
      if (!desc_spec_field(&b,&first,"series_col",nbuf)) goto fail;
    }
    DS_STR(APP_SPEC_LABEL_KEY,"label_key",label_key);
    if (s->flags & APP_SPEC_DELIM) {
      nbuf[0]=s->delim; nbuf[1]=0;
      if (!desc_spec_field(&b,&first,"delim",s->delim=='\t' ? "tab" : nbuf)) goto fail;
    }
#undef DS_STR
#undef DS_BOOL
#undef DS_INT
    if (!buf_char(&b,'\n')) goto fail;
  }
  for (i=0;i<d->series_count;i++) {
    const app_series *ser=d->series+i;
    valid=0; lo=hi=sum=0;
    for (r=0;r<ser->count;r++) if (!ser->valid || ser->valid[r]) {
      double v=ser->values[r];
      if (!valid || v<lo) lo=v;
      if (!valid || v>hi) hi=v;
      sum+=v; valid++;
    }
    lo_s=describe_num(lo,err);
    hi_s=describe_num(hi,err);
    sum_s=describe_num(sum,err);
    if (!lo_s || !hi_s || !sum_s) { free(lo_s); free(hi_s); free(sum_s); goto fail; }
    if (ser->column>0) sprintf(nbuf,"col %d",ser->column);
    else sprintf(nbuf,"[%lu]",(unsigned long)i);
    if (!buf_text(&b,"  ") || !desc_padded(&b,nbuf,6) || !buf_char(&b,' ')) {
      free(lo_s); free(hi_s); free(sum_s); goto fail;
    }
    if (ser->name && ser->name[0]=='\001') {
      const char *name=d->x_name ? d->x_name : "x";
      size_t n=strlen(name)+10;
      char *xname=(char *)malloc(n);
      if (!xname) { free(lo_s);free(hi_s);free(sum_s);app_error_set(err,1,"out of memory");goto fail; }
      strcpy(xname,name);strcat(xname," (X axis)");
      if (!desc_padded(&b,xname,16)) { free(xname);free(lo_s);free(hi_s);free(sum_s);goto fail; }
      free(xname);
    } else if (!desc_padded(&b,ser->name,16)) { free(lo_s);free(hi_s);free(sum_s);goto fail; }
    sprintf(nbuf," n=%-4lu min=",(unsigned long)valid);
    if (!buf_text(&b,nbuf) || !desc_padded(&b,lo_s,12) || !buf_text(&b," max=") ||
        !desc_padded(&b,hi_s,12) || !buf_text(&b," sum=") || !buf_text(&b,sum_s)) {
      free(lo_s); free(hi_s); free(sum_s); goto fail;
    }
    free(lo_s); free(hi_s); free(sum_s);
    if (valid<ser->count) {
      sprintf(nbuf,"  gaps=%lu",(unsigned long)(ser->count-valid));
      if (!buf_text(&b,nbuf)) goto fail;
    }
    if (!buf_char(&b,'\n')) goto fail;
  }
  if (d->label_count) {
    if (!buf_text(&b,"first labels: ")) goto fail;
    for (i=0;i<d->label_count && i<6;i++) {
      if (i && !buf_text(&b,", ")) goto fail;
      if (!buf_text(&b,d->labels[i])) goto fail;
    }
    if (d->label_count>6 && !buf_text(&b,", ...")) goto fail;
    if (!buf_char(&b,'\n')) goto fail;
  }
  return b.bytes;
fail:
  free(b.bytes); return NULL;
}
