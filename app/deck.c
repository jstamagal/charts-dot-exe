#include "model.h"
#include "os.h"
#include "platform.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__DOS__) || defined(MSDOS) || defined(__MSDOS__)
#define APP_DOS_PATHS 1
#endif

typedef struct deck_parser {
  app_deck *deck;
  const app_load_opts *opts;
  app_error *err;
  int depth, wide_warned;
} deck_parser;

static void block_free(app_block *b)
{
  size_t i;
  free(b->path); free(b->data_ref); free(b->data_file); free(b->error);
  free(b->title); free(b->value); free(b->label); free(b->delta);
  app_dataset_free(&b->data); app_spec_free(&b->spec);
  for (i=0;i<b->line_count;i++) free(b->lines[i]);
  free(b->lines);
  app_json_free(b->shapes); app_json_free(b->flow);
  for (i=0;i<b->child_count;i++) block_free(b->children+i);
  free(b->children);
  memset(b,0,sizeof *b);
}

static void slide_free(app_slide *s)
{
  size_t i;
  free(s->path); free(s->title); free(s->subtitle); free(s->notes); free(s->layout);
  for (i=0;i<s->block_count;i++) block_free(s->blocks+i);
  free(s->blocks); memset(s,0,sizeof *s);
}

void app_deck_init(app_deck *d)
{
  if (!d) return;
  memset(d,0,sizeof *d);
  d->ascii=d->color=-1;
  d->mtime.exists=-1;
}

void app_deck_free(app_deck *d)
{
  size_t i;
  if (!d) return;
  free(d->file); free(d->dir); free(d->title); free(d->theme);
  free(d->palette); free(d->footer); free(d->gfx);
  app_json_free(d->root);
  for (i=0;i<d->slide_count;i++) slide_free(d->slides+i);
  free(d->slides);
  for (i=0;i<d->issue_count;i++) {
    free(d->issues[i].path); free(d->issues[i].message);
  }
  free(d->issues); app_deck_init(d);
}

int app_deck_add_issue(app_deck *d, int is_error, const char *path,
                       const char *message, app_error *err)
{
  app_issue issue;
  issue.error=is_error;
  issue.path=app_strdup(path ? path : "",err);
  if (!issue.path) return 0;
  issue.message=app_strdup(message ? message : "",err);
  if (!issue.message) { free(issue.path); return 0; }
  if (!app_reserve((void **)&d->issues,&d->issue_capacity,d->issue_count+1,
                   sizeof issue,err)) {
    free(issue.path); free(issue.message); return 0;
  }
  d->issues[d->issue_count++]=issue;
  return 1;
}

int app_deck_ok(const app_deck *d)
{
  size_t i;
  for (i=0;i<d->issue_count;i++) if (d->issues[i].error) return 0;
  return 1;
}

static char *join2(const char *a, const char *b, app_error *err)
{
  size_t n,m;
  char *s;
  n=strlen(a ? a : ""); m=strlen(b ? b : "");
  if (n>(size_t)-1-m-1) { app_error_set(err,1,"string too large"); return NULL; }
  s=(char *)malloc(n+m+1);
  if (!s) { app_error_set(err,1,"out of memory"); return NULL; }
  memcpy(s,a,n); memcpy(s+n,b,m); s[n+m]=0; return s;
}

static char *path_field(const char *base, const char *field, app_error *err)
{
  char *a,*b;
  if (!base || !*base) return app_strdup(field,err);
  a=join2(base,".",err);
  if (!a) return NULL;
  b=join2(a,field,err); free(a); return b;
}

static char *path_index(const char *base, size_t index, app_error *err)
{
  char buf[40];
  sprintf(buf,"[%lu]",(unsigned long)index);
  return join2(base,buf,err);
}

static char *str_value(const app_json *v, const char *fallback, app_error *err)
{
  if (!v) return app_strdup(fallback,err);
  if (v->type==APP_JSON_STRING) return app_strdup(v->string,err);
  if (v->type==APP_JSON_NUMBER) return app_format_number(v->number,-1,err);
  return app_strdup(fallback,err);
}

static app_stamp deck_mtime(const char *path)
{
  return app_file_stamp(path);
}

static char *dir_of(const char *path, app_error *err)
{
  const char *s;
  s=strrchr(path ? path : "",'/');
#ifdef APP_DOS_PATHS
  {
    const char *back=strrchr(path ? path : "",'\\');
    if (back && (!s || back>s)) s=back;
  }
#endif
  if (!s) return app_strdup("",err);
  return app_strndup(path,(size_t)(s-path),err);
}

static char *resolve(const char *dir, const char *ref, app_error *err)
{
  char *p,*q;
  const char *home;
  if (!ref) return app_strdup("",err);
  if (ref[0]=='~' && ref[1]=='/') {
    home=getenv("HOME");
    if (home) return join2(home,ref+1,err);
  }
  if (!*ref || ref[0]=='/' || !dir || !*dir) return app_strdup(ref,err);
#ifdef APP_DOS_PATHS
  if (ref[0]=='\\' || (ref[0] && ref[1]==':')) return app_strdup(ref,err);
  p=join2(dir,"\\",err);
#else
  p=join2(dir,"/",err);
#endif
  if (!p) return NULL;
  q=join2(p,ref,err); free(p); return q;
}

static int private_key(const char *key)
{
  return !key || !*key || key[0]=='_' || key[0]=='$' ||
         !strcmp(key,"comment") || !strcmp(key,"comments");
}

static int key_in(const char *key, const char *const *known)
{
  size_t i;
  for (i=0;known[i];i++) if (!strcmp(key,known[i])) return 1;
  return 0;
}

static size_t edit_distance(const char *a, const char *b, size_t cap)
{
  size_t n,m,i,j,lo,hi,d,t,*prev,*cur,*swap;
  n=strlen(a); m=strlen(b);
  if (n>m+cap || m>n+cap) return cap+1;
  prev=(size_t *)malloc((m+1)*sizeof *prev);
  cur=(size_t *)malloc((m+1)*sizeof *cur);
  if (!prev || !cur) { free(prev); free(cur); return cap+1; }
  for (j=0;j<=m;j++) prev[j]=j<=cap ? j : cap+1;
  for (i=1;i<=n;i++) {
    lo=i>cap ? i-cap : 0; hi=m<i+cap ? m : i+cap;
    if (lo) cur[lo-1]=cap+1; else cur[0]=i;
    for (j=lo>1 ? lo : 1;j<=hi;j++) {
      d=prev[j-1]+(tolower((unsigned char)a[i-1])!=tolower((unsigned char)b[j-1]));
      t=prev[j]+1; if (t<d) d=t;
      t=cur[j-1]+1; if (t<d) d=t;
      cur[j]=d<cap+1 ? d : cap+1;
    }
    if (hi<m) cur[hi+1]=cap+1;
    swap=prev; prev=cur; cur=swap;
  }
  d=prev[m]; free(prev); free(cur); return d;
}

static const char *nearest(const char *key, const char *const *known)
{
  size_t i,d,best;
  const char *result;
  best=1000; result=NULL;
  for (i=0;known[i];i++) {
    d=edit_distance(key,known[i],3);
    if (d>0 && d<3 && d<best) { best=d; result=known[i]; }
  }
  return result;
}

static int issue(deck_parser *p, int error, const char *path, const char *message)
{
  return app_deck_add_issue(p->deck,error,path,message,p->err);
}

static int issue_field(deck_parser *p, int error, const char *base,
                       const char *field, const char *message)
{
  char *path;
  int ok;
  path=path_field(base,field,p->err);
  if (!path) return 0;
  ok=issue(p,error,path,message); free(path); return ok;
}

static int unknown(deck_parser *p, const char *base, const char *key,
                   const char *const *known, int chart)
{
  const char *hint;
  char *path,*message,*both;
  int ok;
  if (private_key(key) || key_in(key,known) || (chart && app_spec_is_key(key))) return 1;
  hint=nearest(key,known);
  if (!hint) message=app_strdup("unknown key, ignored",p->err);
  else {
    message=join2("unknown key, ignored (did you mean \"",hint,p->err);
    if (!message) return 0;
    both=join2(message,"\"?)",p->err); free(message); message=both;
  }
  if (!message) return 0;
  path=path_field(base,key,p->err);
  if (!path) { free(message); return 0; }
  ok=issue(p,0,path,message);
  free(path); free(message); return ok;
}

app_json *app_deck_node(app_deck *d, const char *path)
{
  app_json *node;
  const char *p,*end;
  char *key;
  unsigned long index;
  app_error ignored;
  if (!d || !d->root || !path) return NULL;
  p=path; node=d->root;
  while (*p && node) {
    if (*p=='.') { p++; continue; }
    if (*p=='[') {
      p++;
      index=0;
      if (!isdigit((unsigned char)*p)) return NULL;
      while (isdigit((unsigned char)*p)) { index=index*10+(unsigned long)(*p-'0'); p++; }
      if (*p!=']' || node->type!=APP_JSON_ARRAY || index>=node->count) return NULL;
      node=node->items[index]; p++; continue;
    }
    end=p;
    while (*end && *end!='.' && *end!='[') end++;
    key=app_strndup(p,(size_t)(end-p),&ignored);
    if (!key) return NULL;
    node=app_json_find(node,key); free(key); p=end;
  }
  return node;
}

int app_is_deck_file(const char *path)
{
  /* static: 64 KiB is the whole DOS/4GW default stack */
  static char head[65537];
  FILE *f;
  size_t n;
  app_stat_info st;
  char *p;
  if (!path || !strcmp(path,"-") || (app_stat(path,&st)==0 && APP_ISDIR(st.st_mode))) return 0;
  f=fopen(path,"rb"); if (!f) return 0;
  n=fread(head,1,65536,f); fclose(f); head[n]=0;
  p=head;
  while (*p && isspace((unsigned char)*p)) p++;
  return *p=='{' && strstr(p,"\"slides\"")!=NULL;
}

static int block_push(app_block *parent, app_block *child, app_error *err)
{
  if (!app_reserve((void **)&parent->children,&parent->child_capacity,
                   parent->child_count+1,sizeof *parent->children,err)) return 0;
  parent->children[parent->child_count++]=*child;
  memset(child,0,sizeof *child); return 1;
}

static int slide_block_push(app_slide *s, app_block *b, app_error *err)
{
  if (!app_reserve((void **)&s->blocks,&s->block_capacity,s->block_count+1,
                   sizeof *s->blocks,err)) return 0;
  s->blocks[s->block_count++]=*b;
  memset(b,0,sizeof *b); return 1;
}

static int deck_slide_push(app_deck *d, app_slide *s, app_error *err)
{
  if (!app_reserve((void **)&d->slides,&d->slide_capacity,d->slide_count+1,
                   sizeof *d->slides,err)) return 0;
  d->slides[d->slide_count++]=*s;
  memset(s,0,sizeof *s); return 1;
}

static int line_push(app_block *b, const char *line, app_error *err)
{
  char *copy;
  copy=app_strdup(line,err);
  if (!copy) return 0;
  if (!app_reserve((void **)&b->lines,&b->line_capacity,b->line_count+1,
                   sizeof *b->lines,err)) { free(copy); return 0; }
  b->lines[b->line_count++]=copy; return 1;
}

static int value_lines(app_block *b, const app_json *value, int bullets,
                       app_error *err)
{
  size_t i;
  char *line,*prefixed;
  const char *p,*end;
  if (value->type==APP_JSON_STRING && !bullets) {
    p=value->string;
    for (;;) {
      end=strchr(p,'\n'); if (!end) end=p+strlen(p);
      line=app_strndup(p,(size_t)(end-p),err);
      if (!line) return 0;
      if (!line_push(b,line,err)) { free(line); return 0; }
      free(line);
      if (!*end) break;
      p=end+1;
    }
    return 1;
  }
  if (value->type!=APP_JSON_ARRAY) return 0;
  for (i=0;i<value->count;i++) {
    line=str_value(value->items[i],"",err);
    if (!line) return 0;
    if (bullets) {
      prefixed=join2("- ",line,err); free(line); line=prefixed;
      if (!line) return 0;
    }
    if (!line_push(b,line,err)) { free(line); return 0; }
    free(line);
  }
  return 1;
}

static int set_owned(char **field, const char *value, app_error *err)
{
  char *s;
  s=app_strdup(value,err);
  if (!s) return 0;
  free(*field); *field=s; return 1;
}

static int data_from_block(deck_parser *p, app_block *b, const app_json *v)
{
  app_error data_error;
  app_load_opts lo;
  char *path;
  app_stat_info st;
  int ok;
  const char **keep=NULL;
  size_t i,keep_count=0;
  data_error.code=0; data_error.message[0]=0;
  if (p->opts) lo=*p->opts; else app_load_opts_init(&lo);
#define SET_LO(field,flag) if ((b->spec.flags & flag) && !(lo.flags & flag)) lo.field=b->spec.field
  SET_LO(delim,APP_SPEC_DELIM);
  SET_LO(transpose,APP_SPEC_TRANSPOSE);
  SET_LO(xy,APP_SPEC_XY);
  SET_LO(no_header,APP_SPEC_NO_HEADER);
  SET_LO(label_col,APP_SPEC_LABEL_COL);
  SET_LO(series_col,APP_SPEC_SERIES_COL);
  SET_LO(label_key,APP_SPEC_LABEL_KEY);
#undef SET_LO
  if (b->spec.error_count) {
    size_t cap=lo.keep_count+b->spec.error_count*3;
    keep=(const char **)malloc(cap*sizeof *keep);
    if (!keep) { app_error_set(p->err,1,"out of memory"); return 0; }
    for (i=0;i<lo.keep_count;i++) keep[keep_count++]=lo.keep[i];
    for (i=0;i<b->spec.error_count;i++) {
      app_error_bars *e=b->spec.errors+i;
      if (e->series) keep[keep_count++]=e->series;
      if (e->lo) keep[keep_count++]=e->lo;
      if (e->hi) keep[keep_count++]=e->hi;
    }
    lo.keep=keep; lo.keep_count=keep_count;
  }
  if (!v) {
    b->error=app_strdup("a chart block needs \"data\": a file path or inline data",p->err);
    if (!b->error) return 0;
  } else if (v->type==APP_JSON_STRING) {
    b->data_ref=app_strdup(v->string,p->err);
    b->data_file=resolve(p->deck->dir,v->string,p->err);
    if (!b->data_ref || !b->data_file) { free(keep); return 0; }
    b->mtime=deck_mtime(b->data_file);
    if (app_stat(b->data_file,&st)==0 && APP_ISDIR(st.st_mode)) {
      b->error=join2(b->data_ref," is a directory, not a data file",p->err);
      if (!b->error) { free(keep); return 0; }
    } else {
      ok=app_data_load_file(&b->data,b->data_file,&lo,&data_error);
      if (!ok) {
        b->error=app_strdup(data_error.message,p->err);
        if (!b->error) { free(keep); return 0; }
      }
    }
  } else if (v->type==APP_JSON_OBJECT || v->type==APP_JSON_ARRAY) {
    ok=app_data_from_json(&b->data,v,&lo,&data_error);
    if (!ok) {
      b->error=app_strdup(data_error.message,p->err);
      if (!b->error) { free(keep); return 0; }
    }
  } else {
    b->error=app_strdup("\"data\" must be a file path or inline JSON data",p->err);
    if (!b->error) { free(keep); return 0; }
  }
  free(keep);
  if (b->error) {
    path=path_field(b->path,"data",p->err);
    if (!path) return 0;
    ok=issue(p,1,path,b->error); free(path); return ok;
  }
  if (b->data.hint && *b->data.hint &&
      !issue_field(p,0,b->path,"data",b->data.hint)) return 0;
  return 1;
}

static int like_target(deck_parser *p, const app_json *j, const app_json **target,
                       int *whole_slide, const char **reason)
{
  const app_json *like,*slides,*sl,*blocks;
  size_t i,index;
  *target=NULL; *whole_slide=0; *reason=NULL;
  like=app_json_get(j,"like");
  if (!like) return 0;
  slides=app_json_get(p->deck->root,"slides");
  if (like->type==APP_JSON_NUMBER) {
    if (!slides || slides->type!=APP_JSON_ARRAY || like->number<1 ||
        like->number>(double)slides->count || like->number!=(double)(size_t)like->number) {
      *reason="wants a slide number from 1 to the slide count, or a path like \"slides[2].blocks[0]\"";
      return 0;
    }
    index=(size_t)like->number-1;
    sl=slides->items[index];
  } else if (like->type==APP_JSON_STRING) {
    sl=app_deck_node(p->deck,like->string);
    if (!sl) { *reason="there is no block at that path"; return 0; }
    if (strchr(like->string,'.')) {
      if (sl==j) { *reason="points at itself"; return 0; }
      *target=sl; return 1;
    }
  } else {
    *reason="wants a slide number, or a path like \"slides[2].blocks[0]\"";
    return 0;
  }
  if (!sl || sl->type!=APP_JSON_OBJECT) { *reason="there is no block at that path"; return 0; }
  blocks=app_json_get(sl,"blocks");
  if (blocks && blocks->type==APP_JSON_ARRAY) {
    for (i=0;i<blocks->count;i++) {
      const app_json *b=blocks->items[i];
      if (b->type==APP_JSON_OBJECT && app_json_get(b,"data")) { *target=b; break; }
    }
    if (!*target && blocks->count) *target=blocks->items[0];
  } else { *target=sl; *whole_slide=1; }
  if (!*target || (*target)->type!=APP_JSON_OBJECT) {
    *reason="there is no block at that path"; return 0;
  }
  if (*target==j) { *reason="points at itself"; return 0; }
  return 1;
}

static app_json *merge_like(deck_parser *p, const app_json *j, int depth,
                            const char **reason)
{
  const app_json *base;
  app_json *merged,*deeper,*value;
  size_t i;
  int whole_slide;
  if (depth>8) { *reason="goes round in a circle (or more than 8 deep)"; return NULL; }
  if (!like_target(p,j,&base,&whole_slide,reason)) return NULL;
  if (app_json_get(base,"like")) {
    deeper=merge_like(p,base,depth+1,reason);
    if (!deeper) return NULL;
    base=deeper;
  } else deeper=NULL;
  merged=app_json_new(APP_JSON_OBJECT,p->err);
  if (!merged) { app_json_free(deeper); return NULL; }
  for (i=0;i<base->count;i++) {
    const char *key=base->members[i].key;
    if (!strcmp(key,"at") || !strcmp(key,"weight")) continue;
    if (whole_slide && (!strcmp(key,"title") || !strcmp(key,"subtitle") ||
                        !strcmp(key,"notes") || !strcmp(key,"layout") || !strcmp(key,"blocks"))) continue;
    value=app_json_clone(base->members[i].value,p->err);
    if (!value || !app_json_set(merged,key,value,p->err)) {
      app_json_free(value); app_json_free(merged); app_json_free(deeper); return NULL;
    }
  }
  for (i=0;i<j->count;i++) {
    const char *key=j->members[i].key;
    if (!strcmp(key,"like")) continue;
    value=app_json_clone(j->members[i].value,p->err);
    if (!value || !app_json_set(merged,key,value,p->err)) {
      app_json_free(value); app_json_free(merged); app_json_free(deeper); return NULL;
    }
  }
  app_json_free(deeper);
  return merged;
}

static int chart_spec_key(deck_parser *p, app_block *b, const char *key,
                          const app_json *value)
{
  static const char *const types[]={"bar","stacked","hbar","dumbbell","line","area","pie","pie3d","donut","scatter","hist","table",NULL};
  app_json one;
  app_json_member member;
  app_error se;
  char *path;
  const char *message,*hint;
  char *with_hint,*part,*more;
  se.code=0; se.message[0]=0;
  memset(&one,0,sizeof one);
  one.type=APP_JSON_OBJECT;
  member.key=(char *)key; member.value=(app_json *)value;
  one.members=&member; one.count=1;
  if (app_spec_parse(&b->spec,&one,&se)) return 1;
  path=path_field(b->path,key,p->err);
  if (!path) return 0;
  message=se.message;
  if (!strncmp(message,"chart spec: ",12)) message+=12;
  with_hint=NULL;
  if (!strcmp(key,"type") && value->type==APP_JSON_STRING) {
    part=join2("unknown chart type \"",value->string,p->err);
    if (!part) { free(path); return 0; }
    with_hint=join2(part,"\" (see --list-types)",p->err); free(part);
    if (!with_hint) { free(path); return 0; }
    message=with_hint;
    hint=nearest(value->string,types);
    if (hint) {
      part=join2(message," -- did you mean \"",p->err);
      free(with_hint);
      if (!part) { free(path); return 0; }
      more=join2(part,hint,p->err); free(part);
      if (!more) { free(path); return 0; }
      with_hint=join2(more,"\"?",p->err); free(more);
      if (!with_hint) { free(path); return 0; }
      message=with_hint;
    }
  }
  if (!issue(p,1,path,message)) { free(path); free(with_hint); return 0; }
  free(with_hint);
  free(path); return 1;
}

static int data_named(const app_dataset *d,const char *name,int labels)
{
  size_t i;
  char *a,*b;
  int same;
  app_error ignored;
  ignored.code=0;ignored.message[0]=0;
  a=app_trim_copy(name,&ignored); if (!a) return 0;
  for (i=0;i<(labels ? d->label_count : d->series_count);i++) {
    const char *candidate=labels ? d->labels[i] : d->series[i].name;
    b=app_trim_copy(candidate,&ignored); if (!b) continue;
    same=app_streq_ci(a,b); free(b);
    if (same) { free(a); return 1; }
  }
  free(a); return 0;
}

static int check_block_colors(deck_parser *p,const app_block *b)
{
  size_t i;
  char *base,*path,*message,*part;
  const char *hint;
  int ok;
  for (i=0;i<b->spec.color_count;i++) {
    const char *name=b->spec.colors[i].name;
    if (!name || !*name || data_named(&b->data,name,0) || data_named(&b->data,name,1)) continue;
    hint=NULL;
    if (b->data.series_count) {
      size_t k; for (k=0;k<b->data.series_count;k++)
        if (edit_distance(name,b->data.series[k].name,3)<3) { hint=b->data.series[k].name; break; }
    }
    base=path_field(b->path,"colors",p->err); if (!base) return 0;
    path=path_field(base,name,p->err); free(base); if (!path) return 0;
    message=join2("no series or slice called \"",name,p->err); if (!message) { free(path); return 0; }
    part=join2(message,"\"",p->err); free(message); message=part;
    if (!message) { free(path); return 0; }
    if (hint) {
      part=join2(message," (did you mean \"",p->err); free(message); message=part;
      if (!message) { free(path); return 0; }
      part=join2(message,hint,p->err); free(message); message=part;
      if (!message) { free(path); return 0; }
      part=join2(message,"\"?)",p->err); free(message); message=part;
      if (!message) { free(path); return 0; }
    }
    ok=issue(p,0,path,message); free(path); free(message); if (!ok) return 0;
  }
  return 1;
}

static int check_block_notes(deck_parser *p,const app_block *b)
{
  const app_spec *spec=(b->spec.flags&APP_SPEC_NOTES) ? &b->spec : &b->data.spec;
  size_t i;
  char *base,*path,*message,*part;
  const char *hint;
  double x;
  int found,ok;
  base=path_field(b->path,"annotations",p->err); if (!base) return 0;
  for (i=0;i<spec->note_count;i++) {
    const app_annotation *a=spec->notes+i;
    path=path_index(base,i,p->err); if (!path) { free(base); return 0; }
    if (a->kind==APP_NOTE_POINT || a->kind==APP_NOTE_VLINE) {
      found=a->index>=0 && (size_t)a->index<app_data_rows(&b->data);
      if (a->label && data_named(&b->data,a->label,1)) found=1;
      if (a->kind==APP_NOTE_VLINE && b->data.series_count &&
          b->data.series[0].name && b->data.series[0].name[0]=='\001' &&
          app_parse_number(a->label,&x)) found=1;
      if (!found) {
        hint=NULL;
        if (b->data.label_count) {
          size_t k; for (k=0;k<b->data.label_count;k++)
            if (edit_distance(a->label ? a->label : "",b->data.labels[k],3)<3) { hint=b->data.labels[k]; break; }
        }
        message=join2("no category \"",a->label ? a->label : "",p->err);
        if (!message) { free(path); free(base); return 0; }
        part=join2(message,"\" in the data, so this is not drawn",p->err);free(message);message=part;
        if (!message) { free(path); free(base); return 0; }
        if (hint) {
          part=join2(message," (did you mean \"",p->err);free(message);message=part;
          if (!message) { free(path); free(base); return 0; }
          part=join2(message,hint,p->err);free(message);message=part;
          if (!message) { free(path); free(base); return 0; }
          part=join2(message,"\"?)",p->err);free(message);message=part;
          if (!message) { free(path); free(base); return 0; }
        }
        ok=issue(p,0,path,message);free(message);if (!ok) { free(path); free(base); return 0; }
      }
    }
    if (a->kind==APP_NOTE_POINT && a->series && *a->series && !data_named(&b->data,a->series,0)) {
      message=join2("no series \"",a->series,p->err);
      if (!message) { free(path); free(base); return 0; }
      part=join2(message,"\"; the first series is used",p->err);free(message);message=part;
      if (!message) { free(path); free(base); return 0; }
      ok=issue(p,0,path,message);free(message);if (!ok) { free(path); free(base); return 0; }
    }
    free(path);
  }
  free(base); return 1;
}

static int check_error_name(deck_parser *p,const app_block *b,const char *name,
                            const char *role)
{
  char *path,*message,*part;
  const char *hint=NULL;
  size_t i;
  int ok;
  if (!name || !*name || data_named(&b->data,name,0)) return 1;
  path=path_field(b->path,"errors",p->err); if (!path) return 0;
  message=join2("no column \"",name,p->err); if (!message) { free(path); return 0; }
  part=join2(message,"\" for the ",p->err);free(message);message=part;
  if (!message) { free(path); return 0; }
  part=join2(message,role,p->err);free(message);message=part;
  if (!message) { free(path); return 0; }
  part=join2(message,", so these error bars are not drawn",p->err);free(message);message=part;
  if (!message) { free(path); return 0; }
  for (i=0;i<b->data.series_count;i++)
    if (edit_distance(name,b->data.series[i].name,3)<3) { hint=b->data.series[i].name; break; }
  if (hint) {
    part=join2(message," (did you mean \"",p->err);free(message);message=part;
    if (!message) { free(path); return 0; }
    part=join2(message,hint,p->err);free(message);message=part;
    if (!message) { free(path); return 0; }
    part=join2(message,"\"?)",p->err);free(message);message=part;
    if (!message) { free(path); return 0; }
  }
  ok=issue(p,0,path,message);free(path);free(message);return ok;
}

static int check_block_errors(deck_parser *p,const app_block *b)
{
  const app_spec *spec=(b->spec.flags&APP_SPEC_ERRORS) ? &b->spec : &b->data.spec;
  size_t i;
  for (i=0;i<spec->error_count;i++) {
    const app_error_bars *e=spec->errors+i;
    if (!check_error_name(p,b,e->series,"series") ||
        !check_error_name(p,b,e->lo,e->plus_minus ? "+- amount" : "low end") ||
        (!e->plus_minus && !check_error_name(p,b,e->hi,"high end"))) return 0;
  }
  return 1;
}

static int wide_text(const char *s)
{
  const unsigned char *p=(const unsigned char *)s;
  unsigned long cp;
  int n,i;
  if (!p) return 0;
  while (*p) {
    if (*p<0x80) { p++; continue; }
    if ((*p&0xe0)==0xc0) { cp=*p&0x1f; n=2; }
    else if ((*p&0xf0)==0xe0) { cp=*p&0x0f; n=3; }
    else if ((*p&0xf8)==0xf0) { cp=*p&7; n=4; }
    else { p++; continue; }
    for (i=1;i<n;i++) {
      if (!p[i] || (p[i]&0xc0)!=0x80) break;
      cp=(cp<<6)|(p[i]&0x3f);
    }
    if (i!=n) { p++; continue; }
    if ((cp>=0x1100UL && cp<=0x115fUL) ||
        (cp>=0x2e80UL && cp<=0xa4cfUL) ||
        (cp>=0xac00UL && cp<=0xd7a3UL) ||
        (cp>=0xf900UL && cp<=0xfaffUL) ||
        (cp>=0xfe30UL && cp<=0xfe4fUL) ||
        (cp>=0xff00UL && cp<=0xff60UL) ||
        (cp>=0xffe0UL && cp<=0xffe6UL) ||
        (cp>=0x1f300UL && cp<=0x1faffUL) ||
        (cp>=0x20000UL && cp<=0x3fffdUL)) return 1;
    p+=n;
  }
  return 0;
}

static int warn_wide(deck_parser *p,const char *path,const char *what)
{
  char *message;
  int ok;
  if (p->wide_warned++>=3) return 1;
  message=join2(what," characters two cells wide (CJK, emoji): the font has none, so they are drawn as ?",p->err);
  if (!message) return 0;
  ok=issue(p,0,path,message);free(message);return ok;
}

static int check_wide_node(deck_parser *p,const app_json *j,const char *path)
{
  size_t i;
  char *next;
  int ok;
  if (!j) return 1;
  if (j->type==APP_JSON_STRING && wide_text(j->string)) return warn_wide(p,path,"has");
  if (j->type==APP_JSON_ARRAY) {
    for (i=0;i<j->count;i++) {
      next=path_index(path,i,p->err);if (!next) return 0;
      ok=check_wide_node(p,j->items[i],next);free(next);if (!ok) return 0;
    }
  } else if (j->type==APP_JSON_OBJECT) {
    for (i=0;i<j->count;i++) {
      next=path_field(path,j->members[i].key,p->err);if (!next) return 0;
      ok=check_wide_node(p,j->members[i].value,next);free(next);if (!ok) return 0;
    }
  }
  return 1;
}

static int check_wide_file_data(deck_parser *p,const app_block *b)
{
  size_t i,k;
  int hit=0,ok;
  char *path,*message;
  if (!b->data_ref || !*b->data_ref || b->error) return 1;
  for (i=0;i<b->data.label_count;i++) if (wide_text(b->data.labels[i])) hit=1;
  for (i=0;i<b->data.series_count;i++) if (wide_text(b->data.series[i].name)) hit=1;
  for (i=0;i<b->data.text_count;i++) {
    if (wide_text(b->data.text[i].name)) hit=1;
    for (k=0;k<b->data.text[i].count;k++) if (wide_text(b->data.text[i].values[k])) hit=1;
  }
  if (!hit) return 1;
  path=path_field(b->path,"data",p->err);if (!path) return 0;
  message=join2(b->data_ref," has",p->err);if (!message) { free(path); return 0; }
  ok=warn_wide(p,path,message);free(path);free(message);return ok;
}

static const char *const deck_keys[]={"title","theme","palette","footer","slides","author","display",NULL};
static const char *const slide_keys[]={"title","subtitle","notes","layout","blocks",NULL};
static const char *const chart_keys[]={"data","type","palette","frame","title","subtitle","xlabel","ylabel","legend","values","grid","shadow","explode","depth","bins","prec","min","max","xy","transpose","no_header","labels_col","series_col","label_key","delim","annotations","colors","errors",NULL};
static const char *const text_keys[]={"text","bullets","title","size","align","valign","color","colour","box",NULL};
static const char *const stat_keys[]={"stat","label","delta","color","colour","title",NULL};
static const char *const shapes_keys[]={"shapes","title","box",NULL};
static const char *const flow_keys[]={"flow","edges","labels","dir","title","box",NULL};
static const char *const group_keys[]={"rows","cols",NULL};

static int parse_block(deck_parser *p, app_block *b, const app_json *j,
                       const char *path, int title_taken);

static int valid_point(const app_json *v)
{
  return v && v->type==APP_JSON_ARRAY && v->count==2 &&
         v->items[0]->type==APP_JSON_NUMBER && v->items[1]->type==APP_JSON_NUMBER;
}

static int validate_shapes(deck_parser *p, const app_json *arr, const char *base)
{
  static const char *const kinds[]={"rect","ellipse","poly","line","arrow","label",NULL};
  static const char *const keys[]={"rect","ellipse","poly","line","arrow","label","text","color","colour","text_color","border","dither","depth","width","size","align","shadow","fill","dash","head",NULL};
  size_t i,k,n;
  const app_json *v,*g;
  char *path,*gp,*kp;
  const char *kind;
  int count,ok,c;
  if (arr->type!=APP_JSON_ARRAY) return issue(p,1,base,"wants an array of shapes");
  for (i=0;i<arr->count;i++) {
    v=arr->items[i]; path=path_index(base,i,p->err); if (!path) return 0;
    if (v->type!=APP_JSON_OBJECT) {
      ok=issue(p,1,path,"a shape must be an object like {\"rect\": [1, 1, 4, 2], \"text\": \"...\"}");
      free(path); if (!ok) return 0; continue;
    }
    kind=NULL; count=0;
    for (k=0;k<6;k++) if (app_json_get(v,kinds[k])) { kind=kinds[k]; count++; }
    if (count!=1) {
      char *message=NULL,*part;
      if (count>1) {
        const char *first=NULL,*second=NULL;
        for (k=0;k<6;k++) if (app_json_get(v,kinds[k])) { if (!first) first=kinds[k]; else { second=kinds[k]; break; } }
        message=join2("one shape, one of rect / ellipse / poly / line / arrow / label: this has both ",first,p->err);
        if (message) { part=join2(message," and ",p->err);free(message);message=part; }
        if (message) { part=join2(message,second,p->err);free(message);message=part; }
        if (!message) { free(path); return 0; }
      }
      ok=issue(p,1,path,message ? message : "a shape needs one of: rect, ellipse, poly, line, arrow, label (rect/ellipse [x, y, w, h], poly/line/arrow [[x, y], ...], label [x, y])");
      free(message);
      free(path); if (!ok) return 0; continue;
    }
    g=app_json_get(v,kind); gp=path_field(path,kind,p->err); if (!gp) { free(path); return 0; }
    if (!strcmp(kind,"rect") || !strcmp(kind,"ellipse")) {
      ok=g->type==APP_JSON_ARRAY && g->count==4;
      for (k=0;ok && k<4;k++) ok=g->items[k]->type==APP_JSON_NUMBER;
      if (ok) ok=g->items[2]->number>0 && g->items[3]->number>0;
      if (!ok && !issue(p,1,gp,"wants [x, y, w, h] on the block's 12 x 12 grid, w and h above 0")) { free(gp); free(path); return 0; }
      if (ok && (g->items[0]->number < -.001 || g->items[1]->number < -.001 ||
                 g->items[0]->number+g->items[2]->number > 12.001 ||
                 g->items[1]->number+g->items[3]->number > 12.001))
        if (!issue(p,0,gp,"reaches outside the block's 12 x 12 grid and is cut off there")) { free(gp); free(path); return 0; }
    } else if (!strcmp(kind,"label")) {
      if (!valid_point(g) && !issue(p,1,gp,"wants [x, y]: where the text starts, on the block's 12 x 12 grid")) { free(gp); free(path); return 0; }
      if (valid_point(g) && (g->items[0]->number < -.001 || g->items[1]->number < -.001 ||
                             g->items[0]->number > 12.001 || g->items[1]->number > 12.001))
        if (!issue(p,0,gp,"reaches outside the block's 12 x 12 grid and is cut off there")) { free(gp); free(path); return 0; }
    } else {
      n=!strcmp(kind,"poly") ? 3 : 2;
      ok=g->type==APP_JSON_ARRAY && g->count>=n;
      for (k=0;ok && k<g->count;k++) ok=valid_point(g->items[k]);
      if (!ok && !issue(p,1,gp,n==3 ? "wants at least 3 points: [[x, y], [x, y], ...]" : "wants at least 2 points: [[x, y], [x, y], ...]")) { free(gp); free(path); return 0; }
      if (ok) for (k=0;k<g->count;k++) {
        const app_json *pt=g->items[k];
        if (pt->items[0]->number < -.001 || pt->items[1]->number < -.001 ||
            pt->items[0]->number > 12.001 || pt->items[1]->number > 12.001) {
          if (!issue(p,0,gp,"reaches outside the block's 12 x 12 grid and is cut off there")) { free(gp); free(path); return 0; }
          break;
        }
      }
    }
    for (k=0;k<v->count;k++) {
      const char *key=v->members[k].key;
      const app_json *x=v->members[k].value;
      if (key_in(key,kinds)) continue;
      kp=path_field(path,key,p->err); if (!kp) { free(gp); free(path); return 0; }
      if (!strcmp(key,"color") || !strcmp(key,"colour") || !strcmp(key,"text_color") || !strcmp(key,"border")) {
        if (!(!strcmp(key,"border") && x->type==APP_JSON_STRING && app_streq_ci(x->string,"none")) && !app_parse_color(x,&c))
          if (!issue(p,1,kp,"unknown color; use a name like \"yellow\" or 0..15")) { free(kp); free(gp); free(path); return 0; }
      } else if (!strcmp(key,"dither") || !strcmp(key,"depth") || !strcmp(key,"width") || !strcmp(key,"size")) {
        int lo=0,hi=3;
        if (!strcmp(key,"depth")) hi=6;
        if (!strcmp(key,"width")) { lo=1; hi=4; }
        if (!strcmp(key,"size")) { lo=1; hi=3; }
        if (x->type!=APP_JSON_NUMBER || x->number<lo || x->number>hi) {
          char msg[40]; sprintf(msg,"wants %d..%d",lo,hi);
          if (!issue(p,1,kp,msg)) { free(kp); free(gp); free(path); return 0; }
        }
      } else if (!strcmp(key,"align")) {
        if (x->type!=APP_JSON_STRING || (!app_streq_ci(x->string,"left") && !app_streq_ci(x->string,"center") && !app_streq_ci(x->string,"centre") && !app_streq_ci(x->string,"right")))
          if (!issue(p,1,kp,"wants \"left\", \"center\" or \"right\"")) { free(kp); free(gp); free(path); return 0; }
      } else if (!strcmp(key,"head")) {
        if (x->type!=APP_JSON_BOOL && (x->type!=APP_JSON_STRING || (!app_streq_ci(x->string,"end") && !app_streq_ci(x->string,"start") && !app_streq_ci(x->string,"both") && !app_streq_ci(x->string,"none"))))
          if (!issue(p,1,kp,"wants \"end\", \"start\", \"both\" or \"none\"")) { free(kp); free(gp); free(path); return 0; }
      } else if (!key_in(key,keys)) {
        if (!unknown(p,path,key,keys,0)) { free(kp); free(gp); free(path); return 0; }
      }
      free(kp);
    }
    if (!strcmp(kind,"label") && !app_json_get(v,"text"))
      if (!issue(p,0,path,"a label with no \"text\" draws nothing")) { free(gp); free(path); return 0; }
    free(gp); free(path);
  }
  return 1;
}

static int flow_id(char **ids,size_t count,const char *name)
{
  size_t i;
  for (i=0;i<count;i++) if (!strcmp(ids[i],name)) return (int)i;
  return -1;
}

static int flow_need(deck_parser *p,char **ids,size_t count,
                     const char *name,const char *path)
{
  size_t i;
  int at;
  const char *hint=NULL;
  char *a,*b,*message;
  at=flow_id(ids,count,name);
  if (at>=0) return at;
  for (i=0;i<count;i++) if (edit_distance(name,ids[i],3)<3) { hint=ids[i]; break; }
  a=join2("no step called \"",name,p->err);
  if (!a) return -2;
  b=join2(a,"\"",p->err);free(a);if (!b) return -2;
  message=b;
  if (hint) {
    a=join2(message," (did you mean \"",p->err);free(message);if (!a) return -2;
    b=join2(a,hint,p->err);free(a);if (!b) return -2;
    message=join2(b,"\"?)",p->err);free(b);if (!message) return -2;
  }
  at=issue(p,1,path,message) ? -1 : -2;
  free(message);return at;
}

static int validate_flow(deck_parser *p, const app_json *j, const char *path)
{
  const app_json *nodes,*edges,*labels,*n,*x;
  char **ids;
  size_t i,k,count;
  char *np,*ep,*kp,*id,*to;
  int ok,c;
  nodes=app_json_get(j,"flow");
  np=path_field(path,"flow",p->err); if (!np) return 0;
  if (!nodes || nodes->type!=APP_JSON_ARRAY || !nodes->count) {
    ok=issue(p,1,np,"wants the steps: [\"read\", \"think\", {\"id\": \"out\", \"text\": \"write\\nit\", \"color\": \"green\"}]");
    free(np); return ok;
  }
  ids=(char **)calloc(nodes->count,sizeof *ids);
  if (!ids) { free(np); app_error_set(p->err,1,"out of memory"); return 0; }
  count=0;
  for (i=0;i<nodes->count;i++) {
    n=nodes->items[i]; ep=path_index(np,i,p->err); if (!ep) goto oom;
    if (n->type==APP_JSON_OBJECT) {
      x=app_json_get(n,"id"); if (!x) x=app_json_get(n,"text");
      id=str_value(x,"",p->err);
      x=app_json_get(n,"color"); if (!x) x=app_json_get(n,"colour");
      if (x && !app_parse_color(x,&c)) if (!issue_field(p,1,ep,"color","unknown color; use a name like \"yellow\" or 0..15")) { free(id); free(ep); goto oom; }
      for (k=0;k<n->count;k++) {
        const char *key=n->members[k].key;
        if (strcmp(key,"id") && strcmp(key,"text") && strcmp(key,"color") && strcmp(key,"colour")) {
          static const char *const nk[]={"id","text","color",NULL};
          if (!unknown(p,ep,key,nk,0)) { free(id); free(ep); goto oom; }
        }
      }
    } else if (n->type==APP_JSON_STRING || n->type==APP_JSON_NUMBER) id=str_value(n,"",p->err);
    else {
      if (!issue(p,1,ep,"a step is a string, or {\"id\": ..., \"text\": ..., \"color\": ...}")) { free(ep); goto oom; }
      free(ep); continue;
    }
    if (!id) { free(ep); goto oom; }
    if (!*id) {
      if (!issue(p,1,ep,"a step needs a name")) { free(id); free(ep); goto oom; }
      free(id); free(ep); continue;
    }
    for (k=0;k<count;k++) if (!strcmp(ids[k],id)) break;
    if (k<count) {
      kp=join2("there is already a step called \"",id,p->err);
      if (!kp) { free(id); free(ep); goto oom; }
      to=join2(kp,"\"; give one an \"id\"",p->err); free(kp);
      if (!to || !issue(p,1,ep,to)) { free(to); free(id); free(ep); goto oom; }
      free(to); free(id);
    } else ids[count++]=id;
    free(ep);
  }
  edges=app_json_get(j,"edges");
  if (edges) {
    ep=path_field(path,"edges",p->err); if (!ep) goto oom;
    if (edges->type!=APP_JSON_ARRAY) { if (!issue(p,1,ep,"wants [[\"from\", \"to\"], [\"from\", \"to\", \"label\"], ...]")) { free(ep); goto oom; } }
    else for (i=0;i<edges->count;i++) {
      const app_json *e=edges->items[i];
      char *eidx=path_index(ep,i,p->err); if (!eidx) { free(ep); goto oom; }
      if ((e->type!=APP_JSON_ARRAY || (e->count!=2 && e->count!=3)) && e->type!=APP_JSON_OBJECT) {
        if (!issue(p,1,eidx,"an edge is [\"from\", \"to\"], [\"from\", \"to\", \"label\"] or {\"from\", \"to\", \"text\", \"color\", \"dash\"}")) { free(eidx); free(ep); goto oom; }
      } else if (e->type==APP_JSON_OBJECT) {
        x=app_json_get(e,"color"); if (!x) x=app_json_get(e,"colour");
        if (x && !app_parse_color(x,&c)) if (!issue_field(p,1,eidx,"color","unknown color; use a name like \"yellow\" or 0..15")) { free(eidx); free(ep); goto oom; }
      }
      if ((e->type==APP_JSON_ARRAY && (e->count==2 || e->count==3)) || e->type==APP_JSON_OBJECT) {
        const app_json *a=e->type==APP_JSON_ARRAY ? e->items[0] : app_json_get(e,"from");
        const app_json *b=e->type==APP_JSON_ARRAY ? e->items[1] : app_json_get(e,"to");
        const char *from=a && a->type==APP_JSON_STRING ? a->string : "";
        const char *dest=b && b->type==APP_JSON_STRING ? b->string : "";
        int left=flow_need(p,ids,count,from,eidx);
        int right=flow_need(p,ids,count,dest,eidx);
        if (left==-2 || right==-2) { free(eidx); free(ep); goto oom; }
        if (left>=0 && left==right && !issue(p,0,eidx,"a step pointing at itself is not drawn")) { free(eidx); free(ep); goto oom; }
      }
      free(eidx);
    }
    free(ep);
  }
  labels=app_json_get(j,"labels");
  if (labels && labels->type!=APP_JSON_OBJECT) if (!issue_field(p,1,path,"labels","wants {\"from>to\": \"label\", ...}")) goto oom;
  if (labels && labels->type==APP_JSON_OBJECT) for (i=0;i<labels->count;i++) {
    const char *key=labels->members[i].key,*gt=strstr(key,"->");
    size_t sep=2;
    char *left,*right,*lp;
    int from,to_index,hit=0;
    if (!gt) { gt=strchr(key,'>');sep=1; }
    lp=path_field(path,"labels",p->err);if (!lp) goto oom;
    kp=path_field(lp,key,p->err);free(lp);if (!kp) goto oom;
    if (!gt) {
      ok=issue(p,1,kp,"name an edge as \"from>to\"");free(kp);if (!ok) goto oom;continue;
    }
    left=app_strndup(key,(size_t)(gt-key),p->err);
    right=app_trim_copy(gt+sep,p->err);
    if (!left || !right) { free(left);free(right);free(kp);goto oom; }
    {
      char *trimmed=app_trim_copy(left,p->err);free(left);left=trimmed;
      if (!left) { free(right);free(kp);goto oom; }
    }
    from=flow_need(p,ids,count,left,kp);
    to_index=flow_need(p,ids,count,right,kp);
    if (from==-2 || to_index==-2) { free(left);free(right);free(kp);goto oom; }
    if (from>=0 && to_index>=0) {
      if (!edges) hit=to_index==from+1;
      else if (edges->type==APP_JSON_ARRAY) {
        for (k=0;k<edges->count;k++) {
          const app_json *e=edges->items[k];
          const app_json *a=e->type==APP_JSON_ARRAY && e->count>=2 ? e->items[0] : app_json_get(e,"from");
          const app_json *b=e->type==APP_JSON_ARRAY && e->count>=2 ? e->items[1] : app_json_get(e,"to");
          if (a && b && a->type==APP_JSON_STRING && b->type==APP_JSON_STRING &&
              !strcmp(a->string,left) && !strcmp(b->string,right)) { hit=1; break; }
        }
      }
      if (!hit) {
        char *m=join2("there is no arrow from \"",left,p->err);
        char *b1,*b2;
        if (!m) { free(left);free(right);free(kp);goto oom; }
        b1=join2(m,"\" to \"",p->err);free(m);
        if (!b1) { free(left);free(right);free(kp);goto oom; }
        b2=join2(b1,right,p->err);free(b1);
        if (!b2) { free(left);free(right);free(kp);goto oom; }
        m=join2(b2,"\" to label",p->err);free(b2);
        if (!m) { free(left);free(right);free(kp);goto oom; }
        ok=issue(p,0,kp,m);free(m);
        if (!ok) { free(left);free(right);free(kp);goto oom; }
      }
    }
    free(left);free(right);free(kp);
  }
  for (i=0;i<count;i++) free(ids[i]);
  free(ids); free(np); return 1;
oom:
  for (i=0;i<count;i++) free(ids[i]);
  free(ids); free(np); return 0;
}

static int parse_block(deck_parser *p, app_block *b, const app_json *j,
                       const char *path, int title_taken)
{
  const app_json *v,*rows,*cols,*data;
  app_json *merged;
  size_t i,k;
  char *kp,*child_path;
  const char *key,*reason,*group_key;
  int ok,c;
  app_block child;
  memset(b,0,sizeof *b); app_dataset_init(&b->data); app_spec_init(&b->spec);
  b->weight=1; b->color=-1; b->size=1; b->align=-1; b->mtime.exists=-1;
  b->path=app_strdup(path,p->err); if (!b->path) return 0;
  if (j->type!=APP_JSON_OBJECT) { b->kind=APP_BLOCK_TEXT; return issue(p,1,path,"a block must be an object"); }
  if (app_json_get(j,"like")) {
    reason=NULL; merged=merge_like(p,j,0,&reason);
    if (!merged) {
      const app_json *like=app_json_get(j,"like");
      if (p->err && p->err->code) return 0;
      if (like && like->type==APP_JSON_NUMBER && reason &&
          !strncmp(reason,"wants a slide number from 1",27)) {
        char msg[180];
        const app_json *slides=app_json_get(p->deck->root,"slides");
        sprintf(msg,"wants a slide number from 1 to %lu, or a path like \"slides[2].blocks[0]\"",
                (unsigned long)(slides && slides->type==APP_JSON_ARRAY ? slides->count : 0));
        if (!issue_field(p,1,path,"like",msg)) return 0;
      } else if (!issue_field(p,1,path,"like",reason ? reason : "invalid reference")) return 0;
      merged=app_json_clone(j,p->err); if (!merged) return 0;
      app_json_erase(merged,"like");
      if (!app_json_get(merged,"data") && !app_json_get(merged,"text") &&
          !app_json_get(merged,"bullets") && !app_json_get(merged,"stat") &&
          !app_json_get(merged,"shapes") && !app_json_get(merged,"flow") &&
          !app_json_get(merged,"rows") && !app_json_get(merged,"cols")) {
        b->kind=APP_BLOCK_TEXT; app_json_free(merged); return 1;
      }
    }
    block_free(b);
    ok=parse_block(p,b,merged,path,title_taken);
    app_json_free(merged); return ok;
  }
  rows=app_json_get(j,"rows"); cols=app_json_get(j,"cols"); data=app_json_get(j,"data");
  if (data) b->kind=APP_BLOCK_CHART;
  else if (app_json_get(j,"text") || app_json_get(j,"bullets")) b->kind=APP_BLOCK_TEXT;
  else if (app_json_get(j,"stat")) b->kind=APP_BLOCK_STAT;
  else if (app_json_get(j,"shapes")) b->kind=APP_BLOCK_SHAPES;
  else if (app_json_get(j,"flow")) b->kind=APP_BLOCK_FLOW;
  else if (rows && rows->type==APP_JSON_ARRAY) b->kind=APP_BLOCK_ROWS;
  else if (cols && cols->type==APP_JSON_ARRAY) b->kind=APP_BLOCK_COLS;
  else { b->kind=APP_BLOCK_TEXT; return issue(p,1,path,"a block needs one of: \"data\" (chart), \"text\", \"bullets\", \"stat\", \"shapes\", \"flow\", \"rows\", \"cols\""); }
  for (i=0;i<j->count;i++) {
    key=j->members[i].key; v=j->members[i].value;
    kp=path_field(path,key,p->err); if (!kp) return 0;
    if (!strcmp(key,"at")) {
      ok=v->type==APP_JSON_ARRAY && v->count==4;
      for (k=0;ok && k<4;k++) ok=v->items[k]->type==APP_JSON_NUMBER;
      if (!ok) ok=issue(p,1,kp,"wants [x, y, w, h] on a 12 x 12 grid, e.g. [0, 0, 8, 12]");
      else {
        for (k=0;k<4;k++) b->at[k]=v->items[k]->number;
        if (b->at[0]<0 || b->at[1]<0 || b->at[2]<=0 || b->at[3]<=0 || b->at[0]+b->at[2]>12.001 || b->at[1]+b->at[3]>12.001) ok=issue(p,1,kp,"falls outside the 12 x 12 grid");
        else { b->has_at=1; ok=1; }
      }
    } else if (!strcmp(key,"weight")) {
      ok=(v->type==APP_JSON_NUMBER && v->number>0) ? (b->weight=v->number,1) : issue(p,1,kp,"wants a positive number");
    } else if (b->kind==APP_BLOCK_CHART) {
      if (!strcmp(key,"data") || (!strcmp(key,"title") && title_taken)) ok=1;
      else if (app_spec_is_key(key)) ok=chart_spec_key(p,b,key,v);
      else ok=unknown(p,path,key,chart_keys,1);
    } else if (b->kind==APP_BLOCK_TEXT) {
      if (!strcmp(key,"text")) {
        if (v->type!=APP_JSON_STRING && v->type!=APP_JSON_ARRAY) ok=issue(p,1,kp,"wants a string or an array of lines");
        else ok=value_lines(b,v,0,p->err);
      } else if (!strcmp(key,"bullets")) {
        if (v->type!=APP_JSON_ARRAY) ok=issue(p,1,kp,"wants an array of strings");
        else ok=value_lines(b,v,1,p->err);
      } else if (!strcmp(key,"title")) ok=title_taken ? 1 : (b->title=str_value(v,"",p->err))!=NULL;
      else if (!strcmp(key,"size")) ok=(v->type==APP_JSON_NUMBER && v->number>=1 && v->number<=4) ? (b->size=(int)v->number,1) : issue(p,1,kp,"wants 1, 2, 3 or 4");
      else if (!strcmp(key,"align")) {
        if (v->type==APP_JSON_STRING && app_streq_ci(v->string,"left")) { b->align=-1; ok=1; }
        else if (v->type==APP_JSON_STRING && (app_streq_ci(v->string,"center") || app_streq_ci(v->string,"centre"))) { b->align=0; ok=1; }
        else if (v->type==APP_JSON_STRING && app_streq_ci(v->string,"right")) { b->align=1; ok=1; }
        else ok=issue(p,1,kp,"wants \"left\", \"center\" or \"right\"");
      } else if (!strcmp(key,"valign")) {
        if (v->type==APP_JSON_STRING && (app_streq_ci(v->string,"middle") || app_streq_ci(v->string,"center") || app_streq_ci(v->string,"centre"))) { b->middle=1; ok=1; }
        else if (v->type==APP_JSON_STRING && app_streq_ci(v->string,"top")) ok=1;
        else ok=issue(p,1,kp,"wants \"top\" or \"middle\"");
      } else if (!strcmp(key,"color") || !strcmp(key,"colour")) ok=app_parse_color(v,&c) ? (b->color=c,1) : issue(p,1,kp,"unknown color; use a name like \"yellow\" or 0..15");
      else if (!strcmp(key,"box")) { b->box=v->type==APP_JSON_BOOL ? v->boolean : 1; ok=1; }
      else ok=unknown(p,path,key,text_keys,0);
    } else if (b->kind==APP_BLOCK_STAT) {
      if (!strcmp(key,"stat")) ok=(b->value=str_value(v,"",p->err))!=NULL;
      else if (!strcmp(key,"label")) ok=(b->label=str_value(v,"",p->err))!=NULL;
      else if (!strcmp(key,"delta")) ok=(b->delta=str_value(v,"",p->err))!=NULL;
      else if (!strcmp(key,"title")) ok=title_taken ? 1 : (b->title=str_value(v,"",p->err))!=NULL;
      else if (!strcmp(key,"color") || !strcmp(key,"colour")) ok=app_parse_color(v,&c) ? (b->color=c,1) : issue(p,1,kp,"unknown color; use a name like \"yellow\" or 0..15");
      else ok=unknown(p,path,key,stat_keys,0);
    } else if (b->kind==APP_BLOCK_SHAPES) {
      if (!strcmp(key,"title")) ok=title_taken ? 1 : (b->title=str_value(v,"",p->err))!=NULL;
      else if (!strcmp(key,"box")) { b->box=v->type==APP_JSON_BOOL ? v->boolean : 1; ok=1; }
      else if (!strcmp(key,"shapes")) {
        b->shapes=app_json_clone(v,p->err);
        ok=b->shapes && validate_shapes(p,v,kp);
      } else ok=unknown(p,path,key,shapes_keys,0);
    } else if (b->kind==APP_BLOCK_FLOW) {
      if (!strcmp(key,"title")) ok=title_taken ? 1 : (b->title=str_value(v,"",p->err))!=NULL;
      else if (!strcmp(key,"box")) { b->box=v->type==APP_JSON_BOOL ? v->boolean : 1; ok=1; }
      else if (!strcmp(key,"dir")) {
        ok=v->type==APP_JSON_STRING && (app_streq_ci(v->string,"down") || app_streq_ci(v->string,"vertical") || app_streq_ci(v->string,"right") || app_streq_ci(v->string,"across") || app_streq_ci(v->string,"horizontal"));
        if (!ok) ok=issue(p,1,kp,"wants \"right\" or \"down\"");
      } else if (!strcmp(key,"flow")) ok=validate_flow(p,j,path);
      else if (!strcmp(key,"edges") || !strcmp(key,"labels")) ok=1;
      else ok=unknown(p,path,key,flow_keys,0);
    } else ok=(!strcmp(key,"rows") || !strcmp(key,"cols")) ? 1 : unknown(p,path,key,group_keys,0);
    free(kp); if (!ok) return 0;
  }
  if (b->kind==APP_BLOCK_CHART) {
    if (!data_from_block(p,b,data)) return 0;
    if (!b->error && (!check_block_notes(p,b) || !check_block_colors(p,b) ||
                      !check_block_errors(p,b) || !check_wide_file_data(p,b))) return 0;
  } else if (b->kind==APP_BLOCK_FLOW) {
    b->flow=app_json_clone(j,p->err); if (!b->flow) return 0;
  } else if (b->kind==APP_BLOCK_ROWS || b->kind==APP_BLOCK_COLS) {
    group_key=b->kind==APP_BLOCK_ROWS ? "rows" : "cols";
    v=b->kind==APP_BLOCK_ROWS ? rows : cols;
    if (!v->count && !issue_field(p,1,path,group_key,"is empty")) return 0;
    kp=path_field(path,group_key,p->err); if (!kp) return 0;
    for (i=0;i<v->count;i++) {
      child_path=path_index(kp,i,p->err); if (!child_path) { free(kp); return 0; }
      if (!parse_block(p,&child,v->items[i],child_path,0)) { free(child_path); free(kp); block_free(&child); return 0; }
      free(child_path);
      if (!block_push(b,&child,p->err)) { free(kp); block_free(&child); return 0; }
    }
    free(kp);
  }
  return 1;
}

static int parse_slide(deck_parser *p, app_slide *s, const app_json *j,
                       const char *path)
{
  const app_json *v;
  size_t i,k,start;
  char *sp,*line;
  int shorthand,ok;
  app_block b;
  memset(s,0,sizeof *s);
  s->path=app_strdup(path,p->err); s->layout=app_strdup("auto",p->err);
  if (!s->path || !s->layout) return 0;
  if (j->type!=APP_JSON_OBJECT) return issue(p,1,path,"a slide must be an object");
  shorthand=!app_json_get(j,"blocks") &&
    (app_json_get(j,"data") || app_json_get(j,"text") || app_json_get(j,"bullets") ||
     app_json_get(j,"stat") || app_json_get(j,"shapes") || app_json_get(j,"flow") || app_json_get(j,"like"));
  for (i=0;i<j->count;i++) {
    const char *key=j->members[i].key;
    v=j->members[i].value;
    if (!strcmp(key,"title")) { s->title=str_value(v,"",p->err); if (!s->title) return 0; }
    else if (!strcmp(key,"subtitle")) { s->subtitle=str_value(v,"",p->err); if (!s->subtitle) return 0; }
    else if (!strcmp(key,"notes")) {
      if (v->type!=APP_JSON_ARRAY) s->notes=str_value(v,"",p->err);
      else {
        size_t len=0,pos=0;
        for (k=0;k<v->count;k++) {
          line=str_value(v->items[k],"",p->err); if (!line) return 0;
          len+=strlen(line)+(k>0); free(line);
        }
        s->notes=(char *)malloc(len+1);
        if (!s->notes) { app_error_set(p->err,1,"out of memory"); return 0; }
        for (k=0;k<v->count;k++) {
          line=str_value(v->items[k],"",p->err); if (!line) return 0;
          if (k) s->notes[pos++]='\n';
          memcpy(s->notes+pos,line,strlen(line)); pos+=strlen(line); free(line);
        }
        s->notes[pos]=0;
      }
      if (!s->notes) return 0;
    } else if (!strcmp(key,"layout")) {
      const char *layout=v->type==APP_JSON_STRING ? v->string : "";
      if (app_streq_ci(layout,"columns") || app_streq_ci(layout,"row")) layout="cols";
      else if (app_streq_ci(layout,"column")) layout="rows";
      if (!app_streq_ci(layout,"auto") && !app_streq_ci(layout,"cols") &&
          !app_streq_ci(layout,"rows") && !app_streq_ci(layout,"grid")) {
        if (!issue_field(p,1,path,"layout","wants one of: auto, cols, rows, grid")) return 0;
        layout="auto";
      }
      free(s->layout); s->layout=app_strdup(layout,p->err); if (!s->layout) return 0;
    } else if (!strcmp(key,"blocks")) {
      if (v->type!=APP_JSON_ARRAY) {
        if (!issue_field(p,1,path,"blocks","wants an array")) return 0;
        continue;
      }
      sp=path_field(path,"blocks",p->err); if (!sp) return 0;
      for (k=0;k<v->count;k++) {
        char *bp=path_index(sp,k,p->err); if (!bp) { free(sp); return 0; }
        if (!parse_block(p,&b,v->items[k],bp,0)) { free(bp); free(sp); block_free(&b); return 0; }
        free(bp);
        if (!slide_block_push(s,&b,p->err)) { free(sp); block_free(&b); return 0; }
      }
      free(sp);
    } else if (!shorthand) {
      if (!unknown(p,path,key,slide_keys,0)) return 0;
    }
  }
  if (shorthand) {
    start=p->deck->issue_count;
    if (!parse_block(p,&b,j,path,1)) { block_free(&b); return 0; }
    b.spec.flags&=~APP_SPEC_SUBTITLE;
    for (i=start;i<p->deck->issue_count;) {
      app_issue *iss=p->deck->issues+i;
      size_t plen=strlen(path);
      const char *suffix=iss->path+plen;
      if (!iss->error && !strncmp(iss->path,path,plen) &&
          (!strcmp(suffix,".notes") || !strcmp(suffix,".layout") || !strcmp(suffix,".subtitle"))) {
        free(iss->path); free(iss->message);
        memmove(iss,iss+1,(p->deck->issue_count-i-1)*sizeof *iss);
        p->deck->issue_count--;
      } else i++;
    }
    ok=slide_block_push(s,&b,p->err);
    if (!ok) { block_free(&b); return 0; }
  }
  return 1;
}

static int known_theme(const char *name)
{
  static const char *const names[]={"dos","black","light",NULL};
  size_t i;
  for (i=0;names[i];i++) if (app_streq_ci(name,names[i])) return 1;
  return 0;
}

static int parse_deck(deck_parser *p)
{
  app_deck *d=p->deck;
  const app_json *j=d->root,*v,*x;
  size_t i,k;
  int c,ok;
  char *path,*message,*part;
  app_slide s;
  static const char *const palette_names[]={"dos","ega","cga","ice","fire","green","amber","mono",NULL};
  static const char *const display_modes[]={"auto","fb","kitty","sixel","cells","ascii","vga",NULL};
  static const char *const theme_keys[]={"base","slide_bg","panel_bg","frame","title","subtitle","axis","tick","grid","label","xlabel","ylabel","legend","value","heading","text","dim","accent","bullet","bar_fg","bar_bg","bar_key","note_fg","note_bg","cursor_fg","cursor_bg","table_head","table_rule","table_row",NULL};
  for (i=0;i<j->count;i++) {
    const char *key=j->members[i].key; v=j->members[i].value;
    if (!strcmp(key,"title")) { d->title=str_value(v,"",p->err); if (!d->title) return 0; }
    else if (!strcmp(key,"footer")) { d->footer=str_value(v,"",p->err); if (!d->footer) return 0; }
    else if (!strcmp(key,"author")) continue;
    else if (!strcmp(key,"theme")) {
      if (v->type==APP_JSON_OBJECT) {
        x=app_json_get(v,"base");
        d->theme=str_value(x,"dos",p->err); if (!d->theme) return 0;
        if (!known_theme(d->theme)) {
          message=join2("unknown theme \"",d->theme,p->err); if (!message) return 0;
          part=join2(message,"\"; use one of: dos, black, light",p->err); free(message);
          if (!part || !issue_field(p,1,"theme","base",part)) { free(part); return 0; }
          free(part); if (!set_owned(&d->theme,"dos",p->err)) return 0;
        }
        for (k=0;k<v->count;k++) {
          const char *tk=v->members[k].key;
          if (!strcmp(tk,"base") || private_key(tk)) continue;
          x=v->members[k].value;
          if (!(strcmp(tk,"slide_bg")==0 && x->type==APP_JSON_STRING && app_streq_ci(x->string,"none")) && !app_parse_color(x,&c))
            if (!issue_field(p,1,"theme",tk,"is not a colour (use a name like \"dark cyan\" or 0..15)")) return 0;
          if (!key_in(tk,theme_keys) && !unknown(p,"theme",tk,theme_keys,0)) return 0;
        }
      } else {
        d->theme=str_value(v,"",p->err); if (!d->theme) return 0;
        if (!known_theme(d->theme)) {
          message=join2("unknown theme \"",d->theme,p->err); if (!message) return 0;
          part=join2(message,"\"; use one of: dos, black, light, or an object of colours",p->err); free(message);
          if (!part || !issue(p,1,"theme",part)) { free(part); return 0; }
          free(part); free(d->theme); d->theme=NULL;
        }
      }
    } else if (!strcmp(key,"display")) {
      if (v->type!=APP_JSON_OBJECT) { if (!issue(p,1,"display","wants an object like {\"scale\": 2, \"gfx\": \"auto\"}")) return 0; continue; }
      for (k=0;k<v->count;k++) {
        const char *dk=v->members[k].key;
        x=v->members[k].value;
        path=path_field("display",dk,p->err); if (!path) return 0;
        if (!strcmp(dk,"gfx")) {
          d->gfx=str_value(x,"",p->err); if (!d->gfx) { free(path); return 0; }
          if (!key_in(d->gfx,display_modes)) { ok=issue(p,1,path,"wants one of: auto, fb, kitty, sixel, cells, ascii, vga"); free(d->gfx); d->gfx=NULL; }
          else ok=1;
        } else if (!strcmp(dk,"scale")) ok=(x->type==APP_JSON_NUMBER && x->number>=1 && x->number<=8) ? (d->scale=(int)x->number,1) : issue(p,1,path,"wants 1..8");
        else if (!strcmp(dk,"size")) {
          ok=x->type==APP_JSON_ARRAY && x->count==2 && x->items[0]->type==APP_JSON_NUMBER && x->items[1]->type==APP_JSON_NUMBER;
          if (ok) ok=x->items[0]->number>=20 && x->items[0]->number<=1000 && x->items[1]->number>=6 && x->items[1]->number<=500;
          if (ok) { d->cols=(int)x->items[0]->number; d->rows=(int)x->items[1]->number; }
          else ok=issue(p,1,path,"wants [columns, rows], from [20, 6] to [1000, 500]");
        } else if (!strcmp(dk,"ascii")) { d->ascii=x->type==APP_JSON_BOOL ? x->boolean : 1; ok=1; }
        else if (!strcmp(dk,"color") || !strcmp(dk,"colour")) { d->color=x->type==APP_JSON_BOOL ? x->boolean : 1; ok=1; }
        else ok=private_key(dk) ? 1 : issue(p,0,path,"unknown key, ignored (gfx, scale, size, ascii, color)");
        free(path); if (!ok) return 0;
      }
    } else if (!strcmp(key,"palette")) {
      if (v->type==APP_JSON_ARRAY) {
        size_t capacity,length;
        char *palette,*grown;
        if (!v->count) {
          if (!issue(p,1,"palette","palette: a custom palette is a list of colours, e.g. [\"yellow\", \"cyan\", \"red\"]")) return 0;
          continue;
        }
        capacity=8+v->count*4;
        palette=(char *)malloc(capacity);
        if (!palette) { app_error_set(p->err,1,"out of memory"); return 0; }
        strcpy(palette,"custom:");length=7;
        for (k=0;k<v->count;k++) {
          if (!app_parse_color(v->items[k],&c)) {
            if (!issue(p,1,"palette","palette: is not a colour (use a name like \"green\", \"dark red\", or 0..15)")) { free(palette); return 0; }
            c=0;
          }
          if (length+4>=capacity) {
            capacity=capacity*2+8;
            grown=(char *)realloc(palette,capacity);
            if (!grown) { free(palette); app_error_set(p->err,1,"out of memory"); return 0; }
            palette=grown;
          }
          if (k) palette[length++]=',';
          if (c>=10) palette[length++]=(char)('0'+c/10);
          palette[length++]=(char)('0'+c%10);palette[length]=0;
        }
        free(d->palette);d->palette=palette;
      } else {
        d->palette=str_value(v,"",p->err); if (!d->palette) return 0;
        if (!key_in(d->palette,palette_names)) {
          message=join2("unknown palette \"",d->palette,p->err); if (!message) return 0;
          part=join2(message,"\"; use one of: dos, ega, cga, ice, fire, green, amber, mono",p->err); free(message);
          if (!part || !issue(p,1,"palette",part)) { free(part); return 0; }
          free(part); free(d->palette); d->palette=NULL;
        }
      }
    } else if (!strcmp(key,"slides")) {
      if (v->type!=APP_JSON_ARRAY) { if (!issue(p,1,"slides","wants an array of slides")) return 0; continue; }
      if (!v->count && !issue(p,1,"slides","is empty: a deck needs at least one slide")) return 0;
      for (k=0;k<v->count;k++) {
        path=path_index("slides",k,p->err); if (!path) return 0;
        if (!parse_slide(p,&s,v->items[k],path)) { free(path); slide_free(&s); return 0; }
        free(path);
        if (!deck_slide_push(d,&s,p->err)) { slide_free(&s); return 0; }
      }
      if (!check_wide_node(p,v,"slides")) return 0;
    } else if (!unknown(p,"",key,deck_keys,0)) return 0;
  }
  return 1;
}

static int build_deck(app_deck *d, const app_load_opts *opts, app_error *err)
{
  deck_parser p;
  memset(&p,0,sizeof p); p.deck=d; p.opts=opts; p.err=err;
  return parse_deck(&p);
}

int app_deck_load_text(app_deck *out, const char *text,
                       const app_load_opts *opts, app_error *err)
{
  app_deck d;
  app_deck_init(&d);
  d.root=app_json_parse(text,err);
  if (!d.root) return 0;
  if (d.root->type!=APP_JSON_OBJECT || !app_json_get(d.root,"slides")) {
    app_error_set(err,1,"not a deck (no \"slides\" array)");
    app_deck_free(&d); return 0;
  }
  if (!build_deck(&d,opts,err)) { app_deck_free(&d); return 0; }
  app_deck_free(out); *out=d; return 1;
}

int app_deck_load_file(app_deck *out, const char *path,
                       const app_load_opts *opts, app_error *err)
{
  app_deck d;
  char *body;
  app_error nested;
  app_deck_init(&d);
  d.file=app_strdup(path,err); d.dir=dir_of(path,err);
  if (!d.file || !d.dir) { app_deck_free(&d); return 0; }
  d.mtime=deck_mtime(path);
  body=app_read_file(path,NULL,err);
  if (!body) { app_deck_free(&d); return 0; }
  nested.code=0; nested.message[0]=0;
  d.root=app_json_parse(body,&nested); free(body);
  if (!d.root) { app_error_path(err,1,path,nested.message); app_deck_free(&d); return 0; }
  if (d.root->type!=APP_JSON_OBJECT || !app_json_get(d.root,"slides")) {
    app_error_path(err,1,path,"not a deck (no \"slides\" array)");
    app_deck_free(&d); return 0;
  }
  if (!build_deck(&d,opts,err)) { app_deck_free(&d); return 0; }
  app_deck_free(out); *out=d; return 1;
}

static app_json *json_string(const char *s, app_error *err)
{
  app_json *j=app_json_new(APP_JSON_STRING,err);
  if (!j) return NULL;
  j->string=app_strdup(s,err);
  if (!j->string) { app_json_free(j); return NULL; }
  return j;
}

static app_json *json_number(double n, app_error *err)
{
  app_json *j=app_json_new(APP_JSON_NUMBER,err);
  if (j) j->number=n;
  return j;
}

static app_json *json_bool(int b, app_error *err)
{
  app_json *j=app_json_new(APP_JSON_BOOL,err);
  if (j) j->boolean=b;
  return j;
}

static int json_put(app_json *obj, const char *key, app_json *value,
                    app_error *err)
{
  if (!value) return 0;
  if (!app_json_set(obj,key,value,err)) { app_json_free(value); return 0; }
  return 1;
}

int app_deck_from_files(app_deck *out, const char **files, size_t count,
                        const char **types, size_t type_count, int tile,
                        const app_load_opts *opts, app_error *err)
{
  app_deck d;
  app_json *slides,*tiled,*b,*s,*value,*piped=NULL,*chart,*source;
  char *stdin_text=NULL;
  app_dataset ds;
  size_t i,n;
  app_error nested;
  if (!count) { app_error_set(err,2,"no data files"); return 0; }
  app_deck_init(&d); d.implicit=1;
  d.root=app_json_new(APP_JSON_OBJECT,err);
  slides=app_json_new(APP_JSON_ARRAY,err);
  tiled=tile ? app_json_new(APP_JSON_ARRAY,err) : NULL;
  if (!d.root || !slides || (tile && !tiled)) goto fail;
  n=count;
  if (tile && type_count>n) n=type_count;
  for (i=0;i<n;i++) {
    b=app_json_new(APP_JSON_OBJECT,err); if (!b) goto fail;
    if (type_count && !json_put(b,"type",json_string(types[i%type_count],err),err)) { app_json_free(b); goto fail; }
    if (!strcmp(files[i%count],"-")) {
      if (!piped) {
        stdin_text=app_read_file("-",NULL,err); if (!stdin_text) { app_json_free(b); goto fail; }
        app_dataset_init(&ds); nested.code=0; nested.message[0]=0;
        if (!app_data_load_text(&ds,stdin_text,"<stdin>",opts,&nested)) {
          app_error_path(err,1,"<stdin>",nested.message); app_dataset_free(&ds); app_json_free(b); goto fail;
        }
        if (!app_data_to_json(&ds,&piped,err)) { app_dataset_free(&ds); app_json_free(b); goto fail; }
        source=app_json_parse(stdin_text,&nested);
        if (source && source->type==APP_JSON_OBJECT) {
          const app_json *a=app_json_get(source,"chart");
          if (a && !json_put(piped,"chart",app_json_clone(a,err),err)) { app_json_free(source); app_dataset_free(&ds); app_json_free(b); goto fail; }
          a=app_json_get(source,"title");
          if (a && !json_put(piped,"title",app_json_clone(a,err),err)) { app_json_free(source); app_dataset_free(&ds); app_json_free(b); goto fail; }
        } else if (ds.spec.flags) {
          chart=app_json_new(APP_JSON_OBJECT,err);
          if (!chart) { app_json_free(source); app_dataset_free(&ds); app_json_free(b); goto fail; }
          if ((ds.spec.flags&APP_SPEC_TYPE) && !json_put(chart,"type",json_string(ds.spec.type,err),err)) { app_json_free(chart); app_json_free(source); app_dataset_free(&ds); app_json_free(b); goto fail; }
          if ((ds.spec.flags&APP_SPEC_TITLE) && !json_put(chart,"title",json_string(ds.spec.title,err),err)) { app_json_free(chart); app_json_free(source); app_dataset_free(&ds); app_json_free(b); goto fail; }
          if ((ds.spec.flags&APP_SPEC_PALETTE) && !json_put(chart,"palette",json_string(ds.spec.palette,err),err)) { app_json_free(chart); app_json_free(source); app_dataset_free(&ds); app_json_free(b); goto fail; }
          if ((ds.spec.flags&APP_SPEC_VALUES) && !json_put(chart,"values",json_bool(ds.spec.values,err),err)) { app_json_free(chart); app_json_free(source); app_dataset_free(&ds); app_json_free(b); goto fail; }
          if (!json_put(piped,"chart",chart,err)) { app_json_free(source); app_dataset_free(&ds); app_json_free(b); goto fail; }
        }
        app_json_free(source); app_dataset_free(&ds); free(stdin_text); stdin_text=NULL;
      }
      value=app_json_clone(piped,err);
    } else value=json_string(files[i%count],err);
    if (!json_put(b,"data",value,err)) { app_json_free(b); goto fail; }
    if (!app_json_push(tile ? tiled : slides,b,err)) { app_json_free(b); goto fail; }
  }
  if (tile) {
    s=app_json_new(APP_JSON_OBJECT,err); if (!s) goto fail;
    if (!json_put(s,"layout",json_string("grid",err),err)) { app_json_free(s); goto fail; }
    value=tiled; tiled=NULL; /* json_put owns it from here, even on failure */
    if (!json_put(s,"blocks",value,err)) { app_json_free(s); goto fail; }
    if (!app_json_push(slides,s,err)) { app_json_free(s); goto fail; }
  }
  value=slides; slides=NULL;
  if (!json_put(d.root,"slides",value,err)) goto fail;
  if (!build_deck(&d,opts,err)) goto fail;
  app_deck_free(out); *out=d; app_json_free(piped); return 1;
fail:
  free(stdin_text); app_json_free(piped); app_json_free(tiled); app_json_free(slides);
  app_deck_free(&d); return 0;
}

static int refresh_blocks(app_deck *d, app_block *blocks, size_t count,
                          const app_load_opts *opts, app_error *err)
{
  size_t i;
  int changed=0,child;
  app_stamp mtime;
  app_error nested;
  app_load_opts lo;
  app_dataset fresh;
  for (i=0;i<count;i++) {
    app_block *b=blocks+i;
    if (b->kind==APP_BLOCK_ROWS || b->kind==APP_BLOCK_COLS) {
      child=refresh_blocks(d,b->children,b->child_count,opts,err);
      if (child<0) return -1;
      if (child) changed=1;
    }
    if (b->kind!=APP_BLOCK_CHART || !b->data_file || b->data_dirty) continue;
    mtime=deck_mtime(b->data_file);
    if (app_stamp_equal(&mtime,&b->mtime)) continue;
    b->mtime=mtime;
    if (opts) lo=*opts; else app_load_opts_init(&lo);
#define SET_LO(field,flag) if ((b->spec.flags & flag) && !(lo.flags & flag)) lo.field=b->spec.field
    SET_LO(delim,APP_SPEC_DELIM);
    SET_LO(transpose,APP_SPEC_TRANSPOSE);
    SET_LO(xy,APP_SPEC_XY);
    SET_LO(no_header,APP_SPEC_NO_HEADER);
    SET_LO(label_col,APP_SPEC_LABEL_COL);
    SET_LO(series_col,APP_SPEC_SERIES_COL);
    SET_LO(label_key,APP_SPEC_LABEL_KEY);
#undef SET_LO
    app_dataset_init(&fresh); nested.code=0; nested.message[0]=0;
    if (app_data_load_file(&fresh,b->data_file,&lo,&nested)) {
      app_dataset_free(&b->data); b->data=fresh; free(b->error); b->error=NULL;
    } else {
      app_dataset_free(&fresh); app_dataset_free(&b->data); app_dataset_init(&b->data);
      free(b->error); b->error=app_strdup(nested.message,err);
      if (!b->error) return -1;
    }
    changed=1;
  }
  (void)d;
  return changed;
}

int app_deck_refresh(app_deck *deck, const app_load_opts *opts,
                     app_error *err)
{
  size_t i;
  int changed=0,c;
  for (i=0;i<deck->slide_count;i++) {
    c=refresh_blocks(deck,deck->slides[i].blocks,deck->slides[i].block_count,opts,err);
    if (c<0) return -1;
    if (c) changed=1;
  }
  return changed;
}

static const app_block *find_block(const app_block *blocks,size_t count,
                                   const char *path)
{
  size_t i;
  const app_block *found;
  for (i=0;i<count;i++) {
    if (blocks[i].path && !strcmp(blocks[i].path,path)) return blocks+i;
    found=find_block(blocks[i].children,blocks[i].child_count,path);
    if (found) return found;
  }
  return NULL;
}

static int carry_dirty(const app_deck *old,
                       app_block *blocks,size_t count,app_error *err)
{
  size_t i,k;
  const app_block *source=NULL;
  for (i=0;i<count;i++) {
    app_block *b=blocks+i;
    if (b->kind==APP_BLOCK_CHART && b->path) {
      source=NULL;
      for (k=0;k<old->slide_count && !source;k++)
        source=find_block(old->slides[k].blocks,old->slides[k].block_count,b->path);
      if (source && source->kind==APP_BLOCK_CHART && source->data_dirty) {
        if (!app_dataset_clone(&b->data,&source->data,err)) return 0;
        b->data_dirty=1;
        b->mtime=source->mtime;
        free(b->error);b->error=source->error ? app_strdup(source->error,err) : NULL;
        if (source->error && !b->error) return 0;
      }
    }
    if (!carry_dirty(old,b->children,b->child_count,err)) return 0;
  }
  return 1;
}

int app_deck_reparse(app_deck *deck, const app_load_opts *opts,
                     app_error *err)
{
  app_deck fresh;
  size_t i;
  app_deck_init(&fresh);
  fresh.file=app_strdup(deck->file ? deck->file : "",err);
  fresh.dir=app_strdup(deck->dir ? deck->dir : "",err);
  fresh.root=app_json_clone(deck->root,err);
  if (!fresh.file || !fresh.dir || !fresh.root) { app_deck_free(&fresh); return 0; }
  fresh.implicit=deck->implicit; fresh.dirty=deck->dirty; fresh.tweaked=deck->tweaked;
  fresh.mtime=deck->mtime;
  if (!build_deck(&fresh,opts,err)) { app_deck_free(&fresh); return 0; }
  for (i=0;i<fresh.slide_count;i++)
    if (!carry_dirty(deck,fresh.slides[i].blocks,fresh.slides[i].block_count,err)) {
      app_deck_free(&fresh); return 0;
    }
  app_deck_free(deck); *deck=fresh; return 1;
}

static int store_spec_value(app_json *node, const char *key,
                            app_json *value, app_error *err)
{
  return json_put(node,key,value,err);
}

int app_deck_store_spec(app_deck *deck, app_block *block, int content,
                        app_error *err)
{
  app_json *n,*arr,*v;
  app_spec *s=&block->spec;
  size_t i;
  n=app_deck_node(deck,block->path);
  if (!n || n->type!=APP_JSON_OBJECT) return 1;
  if ((s->flags&APP_SPEC_TYPE) && !store_spec_value(n,"type",json_string(s->type,err),err)) return 0;
  if ((s->flags&APP_SPEC_PALETTE) && !store_spec_value(n,"palette",json_string(s->palette,err),err)) return 0;
  if ((s->flags&APP_SPEC_VALUES) && !store_spec_value(n,"values",json_bool(s->values,err),err)) return 0;
  if ((s->flags&APP_SPEC_GRID) && !store_spec_value(n,"grid",json_bool(s->grid,err),err)) return 0;
  if ((s->flags&APP_SPEC_LEGEND) && !store_spec_value(n,"legend",json_bool(s->legend,err),err)) return 0;
  if ((s->flags&APP_SPEC_DEPTH) && !store_spec_value(n,"depth",json_number(s->depth,err),err)) return 0;
  if ((s->flags&APP_SPEC_EXPLODE) && !store_spec_value(n,"explode",s->explode==-2 ? json_bool(0,err) : json_number(s->explode,err),err)) return 0;
  if (s->flags&APP_SPEC_NOTES) {
    if (!s->note_count) app_json_erase(n,"annotations");
    else {
      arr=app_json_new(APP_JSON_ARRAY,err); if (!arr) return 0;
      for (i=0;i<s->note_count;i++) {
        v=app_annotation_json(s->notes+i,err);
        if (!v || !app_json_push(arr,v,err)) { app_json_free(v); app_json_free(arr); return 0; }
      }
      if (!json_put(n,"annotations",arr,err)) return 0;
    }
  }
  if (content) deck->dirty=1; else deck->tweaked=1;
  return 1;
}

int app_deck_store_text(app_deck *deck, app_block *block, app_error *err)
{
  app_json *n,*arr,*v;
  size_t i;
  n=app_deck_node(deck,block->path);
  if (!n || n->type!=APP_JSON_OBJECT) return 1;
  if (block->kind==APP_BLOCK_TEXT) {
    arr=app_json_new(APP_JSON_ARRAY,err); if (!arr) return 0;
    for (i=0;i<block->line_count;i++) {
      v=json_string(block->lines[i],err);
      if (!v || !app_json_push(arr,v,err)) { app_json_free(v); app_json_free(arr); return 0; }
    }
    app_json_erase(n,"bullets");
    if (!json_put(n,"text",arr,err)) return 0;
  } else if (block->kind==APP_BLOCK_STAT) {
    if (!json_put(n,"stat",json_string(block->value,err),err)) return 0;
    if (block->label && *block->label && !json_put(n,"label",json_string(block->label,err),err)) return 0;
  }
  deck->dirty=1; return 1;
}

int app_deck_store_data(app_deck *deck, app_block *block, app_error *err)
{
  app_json *n,*fresh,*old,*chart;
  if (block->data_file && *block->data_file) {
    if (!app_data_save(&block->data,block->data_file,err)) return 0;
    block->mtime=deck_mtime(block->data_file);
  } else {
    n=app_deck_node(deck,block->path);
    if (!n || n->type!=APP_JSON_OBJECT) { app_error_set(err,1,"the block is gone from the deck"); return 0; }
    if (block->data.lossy) {
      char *msg=join2("not saving the inline data: ",block->data.lossy_why ? block->data.lossy_why : "would lose source fields",err);
      if (!msg) return 0;
      app_error_set(err,1,msg); free(msg); return 0;
    }
    if (!app_data_to_json(&block->data,&fresh,err)) return 0;
    old=app_json_find(n,"data");
    if (old && old->type==APP_JSON_OBJECT && app_json_get(old,"chart")) {
      chart=app_json_clone(app_json_get(old,"chart"),err);
      if (!chart || !json_put(fresh,"chart",chart,err)) { app_json_free(fresh); return 0; }
    }
    if (!json_put(n,"data",fresh,err)) return 0;
    deck->dirty=1;
  }
  block->data_dirty=0;
  return 1;
}

static void find_first_data(app_block *blocks, size_t count, char **path)
{
  size_t i;
  for (i=0;i<count && !*path;i++) {
    if (blocks[i].kind==APP_BLOCK_CHART && blocks[i].data_file && *blocks[i].data_file) *path=blocks[i].data_file;
    else find_first_data(blocks[i].children,blocks[i].child_count,path);
  }
}

static int rewrite_relative(app_deck *deck, app_block *blocks, size_t count,
                            app_error *err)
{
  size_t i,len;
  app_json *n;
  const char *relative;
  if (!deck->dir || !*deck->dir) return 1;
  len=strlen(deck->dir);
  for (i=0;i<count;i++) {
    app_block *b=blocks+i;
    if (b->kind==APP_BLOCK_CHART && b->data_ref &&
        !strncmp(b->data_ref,deck->dir,len) &&
        (b->data_ref[len]=='/'
#ifdef APP_DOS_PATHS
         || b->data_ref[len]=='\\'
#endif
        )) {
      relative=b->data_ref+len+1;
      n=app_deck_node(deck,b->path);
      if (n && n->type==APP_JSON_OBJECT) {
        if (!json_put(n,"data",json_string(relative,err),err)) return 0;
        if (!set_owned(&b->data_ref,relative,err)) return 0;
      }
    }
    if (!rewrite_relative(deck,b->children,b->child_count,err)) return 0;
  }
  return 1;
}

int app_deck_save(app_deck *deck, app_error *err)
{
  size_t i,len,dot,slash;
  char *first=NULL,*stem,*json,*dir;
  int ok;
  if (!deck->file || !*deck->file) {
    for (i=0;i<deck->slide_count && !first;i++)
      find_first_data(deck->slides[i].blocks,deck->slides[i].block_count,&first);
    stem=app_strdup(first ? first : "charts",err); if (!stem) return 0;
    len=strlen(stem); dot=len; slash=0;
    for (i=0;i<len;i++) {
      if (stem[i]=='/'
#ifdef APP_DOS_PATHS
          || stem[i]=='\\'
#endif
         ) slash=i+1;
      if (stem[i]=='.') dot=i;
    }
    if (dot>slash && dot<len) stem[dot]=0;
#ifdef APP_DOS_PATHS
    deck->file=join2(stem,".DCK",err);
#else
    deck->file=join2(stem,".deck.json",err);
#endif
    free(stem);
    if (!deck->file) return 0;
    dir=dir_of(deck->file,err); if (!dir) return 0;
    free(deck->dir); deck->dir=dir;
    for (i=0;i<deck->slide_count;i++)
      if (!rewrite_relative(deck,deck->slides[i].blocks,deck->slides[i].block_count,err)) return 0;
    deck->implicit=0;
  }
  json=app_json_write(deck->root,err); if (!json) return 0;
  ok=app_write_replace(deck->file,json,strlen(json),err); free(json);
  if (!ok) return 0;
  deck->dirty=deck->tweaked=0; deck->mtime=deck_mtime(deck->file);
  return 1;
}

typedef struct report_buffer {
  char *text;
  size_t length,capacity;
  app_error *err;
} report_buffer;

static int report_add(report_buffer *b, const char *s)
{
  size_t n=strlen(s ? s : "");
  if (n>(size_t)-1-b->length-1 ||
      !app_reserve((void **)&b->text,&b->capacity,b->length+n+1,1,b->err)) return 0;
  memcpy(b->text+b->length,s,n); b->length+=n; b->text[b->length]=0;
  return 1;
}

static int report_num(report_buffer *b, size_t n)
{
  char tmp[40]; sprintf(tmp,"%lu",(unsigned long)n); return report_add(b,tmp);
}

static int report_json_string(report_buffer *b, const char *s)
{
  app_json *v;
  char *encoded;
  int ok;
  v=json_string(s,b->err); if (!v) return 0;
  encoded=app_json_write(v,b->err); app_json_free(v);
  if (!encoded) return 0;
  {
    size_t n=strlen(encoded);
    while (n && (encoded[n-1]=='\n' || encoded[n-1]=='\r')) encoded[--n]=0;
  }
  ok=report_add(b,encoded); free(encoded); return ok;
}

char *app_deck_issues_text(const app_deck *d, app_error *err)
{
  report_buffer b;
  size_t i;
  memset(&b,0,sizeof b); b.err=err;
  for (i=0;i<d->issue_count;i++) {
    const app_issue *x=d->issues+i;
    if (!report_add(&b,x->error ? "error    " : "warning  ") ||
        !report_add(&b,x->path) || !report_add(&b,": ") ||
        !report_add(&b,x->message) || !report_add(&b,"\n")) { free(b.text); return NULL; }
  }
  if (!b.text) b.text=app_strdup("",err);
  return b.text;
}

char *app_deck_issues_json(const app_deck *d, app_error *err)
{
  report_buffer b;
  size_t i,errors=0,warnings=0;
  memset(&b,0,sizeof b); b.err=err;
  for (i=0;i<d->issue_count;i++) if (d->issues[i].error) errors++; else warnings++;
  if (!report_add(&b,"{\"ok\": ") || !report_add(&b,errors ? "false" : "true") ||
      !report_add(&b,", \"slides\": ") || !report_num(&b,d->slide_count) ||
      !report_add(&b,", \"errors\": ") || !report_num(&b,errors) ||
      !report_add(&b,", \"warnings\": ") || !report_num(&b,warnings) ||
      !report_add(&b,", \"issues\": [")) goto fail;
  for (i=0;i<d->issue_count;i++) {
    const app_issue *x=d->issues+i;
    if ((i && !report_add(&b,", ")) || !report_add(&b,"\n  {\"level\": ") ||
        !report_json_string(&b,x->error ? "error" : "warning") ||
        !report_add(&b,", \"path\": ") || !report_json_string(&b,x->path) ||
        !report_add(&b,", \"message\": ") || !report_json_string(&b,x->message) ||
        !report_add(&b,"}")) goto fail;
  }
  if ((d->issue_count && !report_add(&b,"\n")) || !report_add(&b,"]}\n")) goto fail;
  return b.text;
fail:
  free(b.text); return NULL;
}

static int report_indent(report_buffer *b, int depth)
{
  int i;
  for (i=0;i<depth*2+4;i++) if (!report_add(b," ")) return 0;
  return 1;
}

static int report_blocks(report_buffer *r, const app_block *blocks,
                         size_t count, int depth)
{
  size_t i,notes,steps,arrows;
  const char *type;
  for (i=0;i<count;i++) {
    const app_block *b=blocks+i;
    if (!report_indent(r,depth)) return 0;
    if (b->kind==APP_BLOCK_CHART) {
      type=(b->spec.flags&APP_SPEC_TYPE) ? b->spec.type :
           (b->data.spec.flags&APP_SPEC_TYPE) ? b->data.spec.type : "bar";
      if (!report_add(r,"chart ") || !report_add(r,type) || !report_add(r,"  ") ||
          !report_add(r,b->data_ref && *b->data_ref ? b->data_ref : "(inline data)")) return 0;
      if (b->error) {
        if (!report_add(r,"  ERROR: ") || !report_add(r,b->error)) return 0;
      } else {
        if (!report_add(r,"  ") || !report_num(r,app_data_rows(&b->data)) ||
            !report_add(r," rows x ") || !report_num(r,b->data.series_count) ||
            !report_add(r," series")) return 0;
        notes=(b->spec.flags&APP_SPEC_NOTES) ? b->spec.note_count : b->data.spec.note_count;
        if (notes && (!report_add(r,", ") || !report_num(r,notes) ||
                      !report_add(r,notes==1 ? " annotation" : " annotations"))) return 0;
        if ((b->spec.flags&APP_SPEC_ERRORS || b->data.spec.flags&APP_SPEC_ERRORS) &&
            !report_add(r,", error bars")) return 0;
      }
    } else if (b->kind==APP_BLOCK_TEXT) {
      if (!report_add(r,"text  ") || !report_num(r,b->line_count) ||
          !report_add(r,b->line_count==1 ? " line" : " lines")) return 0;
      if (b->line_count && (!report_add(r,": ") || !report_add(r,b->lines[0]))) return 0;
    } else if (b->kind==APP_BLOCK_STAT) {
      if (!report_add(r,"stat  ") || !report_add(r,b->value ? b->value : "") ||
          !report_add(r,"  ") || !report_add(r,b->label ? b->label : "")) return 0;
    } else if (b->kind==APP_BLOCK_SHAPES) {
      steps=b->shapes && b->shapes->type==APP_JSON_ARRAY ? b->shapes->count : 0;
      if (!report_add(r,"shapes  ") || !report_num(r,steps) ||
          !report_add(r,steps==1 ? " shape" : " shapes")) return 0;
    } else if (b->kind==APP_BLOCK_FLOW) {
      const app_json *f=app_json_get(b->flow,"flow"),*e=app_json_get(b->flow,"edges"),*dir=app_json_get(b->flow,"dir");
      steps=f && f->type==APP_JSON_ARRAY ? f->count : 0;
      arrows=e && e->type==APP_JSON_ARRAY ? e->count : (steps ? steps-1 : 0);
      if (!report_add(r,"flow  ") || !report_num(r,steps) || !report_add(r," steps, ") ||
          !report_num(r,arrows) || !report_add(r," arrows")) return 0;
      if (dir && dir->type==APP_JSON_STRING && (app_streq_ci(dir->string,"down") || app_streq_ci(dir->string,"vertical")))
        if (!report_add(r,", down")) return 0;
    } else {
      if (!report_add(r,b->kind==APP_BLOCK_ROWS ? "rows" : "cols")) return 0;
    }
    if (!report_add(r,"\n")) return 0;
    if (b->child_count && !report_blocks(r,b->children,b->child_count,depth+1)) return 0;
  }
  return 1;
}

char *app_deck_outline(const app_deck *d, app_error *err)
{
  report_buffer b;
  size_t i;
  memset(&b,0,sizeof b); b.err=err;
  if (!report_add(&b,"deck:    ") || !report_add(&b,d->file && *d->file ? d->file : "(from the command line)") ||
      !report_add(&b,"\n")) goto fail;
  if (d->title && *d->title && (!report_add(&b,"title:   ") || !report_add(&b,d->title) || !report_add(&b,"\n"))) goto fail;
  if (!report_add(&b,"theme:   ") || !report_add(&b,d->theme && *d->theme ? d->theme : "dos") ||
      !report_add(&b,"\nslides:  ") || !report_num(&b,d->slide_count) || !report_add(&b,"\n")) goto fail;
  for (i=0;i<d->slide_count;i++) {
    const app_slide *s=d->slides+i;
    if (!report_add(&b,"  ") || !report_num(&b,i+1) || !report_add(&b,". ") ||
        !report_add(&b,s->title && *s->title ? s->title : "(untitled)")) goto fail;
    if (s->layout && strcmp(s->layout,"auto"))
      if (!report_add(&b,"  [") || !report_add(&b,s->layout) || !report_add(&b,"]")) goto fail;
    if (s->notes && *s->notes && !report_add(&b,"  (notes)")) goto fail;
    if (!report_add(&b,"\n") || !report_blocks(&b,s->blocks,s->block_count,0)) goto fail;
  }
  return b.text;
fail:
  free(b.text); return NULL;
}
