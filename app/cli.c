#include "cli.h"
#include "platform.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const types[]={"bar","stacked","hbar","dumbbell","line","area","pie","pie3d","donut","scatter","hist","table",NULL};
static const char *const themes[]={"dos","black","light",NULL};
static const char *const modes[]={"auto","fb","kitty","sixel","cells","ascii","vga",NULL};

void app_args_init(app_args *a)
{
  memset(a,0,sizeof *a); app_spec_init(&a->cli); app_load_opts_init(&a->load);
  a->color=1;
}
void app_args_free(app_args *a)
{
  size_t i;
  for(i=0;i<a->file_count;i++) free(a->files[i]);
  free(a->files);
  for(i=0;i<a->type_count;i++) free(a->types[i]);
  free(a->types);
  free(a->out); free(a->png); free(a->png_dir); free(a->launcher);
  free(a->example); free(a->gfx); free(a->theme); free(a->load.label_key);
  app_spec_free(&a->cli); app_args_init(a);
}
static int cli_set(char **dst,const char *value,app_error *err)
{
  char *p=app_strdup(value,err); if(!p)return 0;
  free(*dst); *dst=p; return 1;
}
static int cli_push(char ***arr,size_t *count,size_t *cap,const char *value,app_error *err)
{
  char *p=app_strdup(value,err);
  if(!p)return 0;
  if(!app_reserve((void **)arr,cap,*count+1,sizeof **arr,err)){free(p);return 0;}
  (*arr)[(*count)++]=p;return 1;
}
static int cli_one_of(const char *name,const char *const *names)
{
  size_t i;for(i=0;names[i];i++)if(!strcmp(name,names[i]))return 1;return 0;
}
static char *cli_lower(const char *s,app_error *err)
{
  char *p=app_strdup(s,err),*q;
  if(!p)return NULL;
  for(q=p;*q;q++)*q=(char)tolower((unsigned char)*q);
  return p;
}
static int cli_usage(app_error *err,const char *first,const char *second,const char *third)
{
  char buf[512]; size_t n;
  buf[0]=0;
  if(first)strncat(buf,first,sizeof buf-1);
  n=strlen(buf);if(second)strncat(buf,second,sizeof buf-n-1);
  n=strlen(buf);if(third)strncat(buf,third,sizeof buf-n-1);
  app_error_set(err,2,buf);return 0;
}
static int cli_spec_checked(app_spec *s,const char *key,app_json_type type,
                            const char *text,int boolean,app_error *err)
{
  app_json obj,v;
  app_json_member member;
  app_error nested;
  const char *m;
  char buf[512];
  memset(&obj,0,sizeof obj);memset(&v,0,sizeof v);
  obj.type=APP_JSON_OBJECT;obj.members=&member;obj.count=1;
  member.key=(char *)key;member.value=&v;
  v.type=type;v.string=(char *)text;v.boolean=boolean;
  nested.code=0;nested.message[0]=0;
  if(app_spec_parse(s,&obj,&nested))return 1;
  m=nested.message;
  if(!strncmp(m,"chart spec: ",12))m+=12;
  sprintf(buf,"--%s: ",key);
  strncat(buf,m,sizeof buf-strlen(buf)-1);
  app_error_set(err,2,buf);return 0;
}
static int cli_integer(const char *s,const char *name,int lo,int hi,int *out,app_error *err)
{
  double d;char buf[96];
  if(app_parse_number(s,&d)&&d>=lo&&d<=hi&&d==(double)(int)d){*out=(int)d;return 1;}
  sprintf(buf,"%s wants a whole number from %d to %d, not \"",name,lo,hi);
  return cli_usage(err,buf,s,"\"");
}
static int cli_need(int *i,int argc,char **argv,const char *name,
                    int has_val,const char *inline_val,const char **out,app_error *err)
{
  if(has_val){*out=inline_val;return 1;}
  if(*i+1>=argc)return cli_usage(err,"option ",name," needs a value");
  *out=argv[++*i];return 1;
}
static int cli_toggle(app_spec *s,const char *name,const char *key,app_error *err)
{
  char yes[40],no[44];
  sprintf(yes,"--%s",key);sprintf(no,"--no-%s",key);
  if(!strcmp(name,yes))return cli_spec_checked(s,key,APP_JSON_BOOL,NULL,1,err)?1:-1;
  if(!strcmp(name,no))return cli_spec_checked(s,key,APP_JSON_BOOL,NULL,0,err)?1:-1;
  return 0;
}
static int cli_types(app_args *a,const char *value,app_error *err)
{
  const char *p,*end,*canon;
  char *item,*trim;
  p=value;
  for(;;){
    end=strchr(p,',');if(!end)end=p+strlen(p);
    item=app_strndup(p,(size_t)(end-p),err);if(!item)return 0;
    trim=app_trim_copy(item,err);free(item);if(!trim)return 0;
    if(*trim){
      canon=app_type_canonical(trim);
      if(!canon){int ok=cli_usage(err,"unknown chart type: ",trim," (see --list-types)");free(trim);return ok;}
      if(!cli_push(&a->types,&a->type_count,&a->type_capacity,canon,err)){free(trim);return 0;}
    }
    free(trim);
    if(!*end)break;
    p=end+1;
  }
  return 1;
}
int app_args_parse(app_args *a,int argc,char **argv,app_error *err)
{
  int i,end_opts=0,has_val,t,two;
  const char *arg,*name,*v,*eq;
  char *name_copy=NULL,*lower;
  const char *no_color=getenv("NO_COLOR");
  if(no_color&&*no_color)a->color=0;
  if(!cli_set(&a->gfx,"auto",err))return 0;
  for(i=1;i<argc;i++){
    arg=argv[i];
    if(end_opts||!*arg||*arg!='-'||!strcmp(arg,"-")){
      if(!cli_push(&a->files,&a->file_count,&a->file_capacity,arg,err))return 0;
      continue;
    }
    if(!strcmp(arg,"--")){end_opts=1;continue;}
    eq=arg[0]=='-'&&arg[1]=='-'?strchr(arg,'='):NULL;
    has_val=eq!=NULL;
    name=arg;v=NULL;
    if(has_val){name_copy=app_strndup(arg,(size_t)(eq-arg),err);if(!name_copy)return 0;name=name_copy;v=eq+1;}
#define VALUE() if(!cli_need(&i,argc,argv,name,has_val,v,&v,err))goto fail
#define SPEC(k) VALUE();if(!cli_spec_checked(&a->cli,k,APP_JSON_STRING,v,0,err))goto fail
    if(!strcmp(name,"-h")||!strcmp(name,"--help"))a->help=1;
    else if(!strcmp(name,"-V")||!strcmp(name,"--version"))a->version=1;
    else if(!strcmp(name,"-t")||!strcmp(name,"--type")){VALUE();if(!cli_types(a,v,err))goto fail;}
    else if(!strcmp(name,"-T")||!strcmp(name,"--title")){SPEC("title");}
    else if(!strcmp(name,"--subtitle")){SPEC("subtitle");}
    else if(!strcmp(name,"--xlabel")){SPEC("xlabel");}
    else if(!strcmp(name,"--ylabel")){SPEC("ylabel");}
    else if(!strcmp(name,"-p")||!strcmp(name,"--palette")){SPEC("palette");}
    else if(!strcmp(name,"--frame")){SPEC("frame");}
    else if(!strcmp(name,"--theme")){
      VALUE();lower=cli_lower(v,err);if(!lower)goto fail;
      if(!cli_one_of(lower,themes)){cli_usage(err,"unknown theme: ",lower," (see --list-themes)");free(lower);goto fail;}
      free(a->theme);a->theme=lower;
    }else if(!strcmp(name,"--gfx")){
      VALUE();lower=cli_lower(v,err);if(!lower)goto fail;
      if(!cli_one_of(lower,modes)){cli_usage(err,"--gfx wants auto, fb, kitty, sixel, cells, ascii or vga",NULL,NULL);free(lower);goto fail;}
      free(a->gfx);a->gfx=lower;
    }else if(!strcmp(name,"-w")||!strcmp(name,"--width")){VALUE();if(!cli_integer(v,"--width",1,1000,&a->width,err))goto fail;}
    else if(!strcmp(name,"-H")||!strcmp(name,"--height")){VALUE();if(!cli_integer(v,"--height",1,500,&a->height,err))goto fail;}
    else if(!strcmp(name,"--size")){
      VALUE();eq=strchr(v,'x');if(!eq)eq=strchr(v,'X');
      if(!eq){cli_usage(err,"--size wants COLSxROWS, e.g. 120x33",NULL,NULL);goto fail;}
      lower=app_strndup(v,(size_t)(eq-v),err);if(!lower)goto fail;
      two=cli_integer(lower,"--size",20,1000,&a->width,err);free(lower);
      if(!two||!cli_integer(eq+1,"--size",6,500,&a->height,err))goto fail;
    }else if(!strcmp(name,"--scale")){VALUE();if(!cli_integer(v,"--scale",1,8,&a->scale,err))goto fail;}
    else if(!strcmp(name,"--slide")){VALUE();if(!cli_integer(v,"--slide",1,100000,&a->slide,err))goto fail;}
    else if(!strcmp(name,"-o")||!strcmp(name,"--out")){VALUE();if(!cli_set(&a->out,v,err))goto fail;}
    else if(!strcmp(name,"--png")){VALUE();if(!cli_set(&a->png,v,err))goto fail;}
    else if(!strcmp(name,"--png-dir")){VALUE();if(!cli_set(&a->png_dir,v,err))goto fail;}
    else if(!strcmp(name,"--launcher")){VALUE();if(!cli_set(&a->launcher,v,err))goto fail;}
    else if(!strcmp(name,"-i")||!strcmp(name,"--show")||!strcmp(name,"--interactive")||!strcmp(name,"--tui")||!strcmp(name,"--watch"))a->show=1;
    else if(!strcmp(name,"--print"))a->print=1;
    else if(!strcmp(name,"--check")||!strcmp(name,"--validate"))a->check=1;
    else if(!strcmp(name,"--json"))a->json=1;
    else if(!strcmp(name,"--describe")||!strcmp(name,"--dump"))a->describe=1;
    else if(!strcmp(name,"--schema"))a->schema=1;
    else if(!strcmp(name,"--tile"))a->tile=1;
    else if(!strcmp(name,"-v")||!strcmp(name,"--verbose"))a->verbose=1;
    else if((t=cli_toggle(&a->cli,name,"legend",err))||(t=cli_toggle(&a->cli,name,"values",err))||
            (t=cli_toggle(&a->cli,name,"grid",err))||(t=cli_toggle(&a->cli,name,"shadow",err))){if(t<0)goto fail;}
    else if(!strcmp(name,"--color")||!strcmp(name,"--colour")){a->color=1;a->color_forced=1;}
    else if(!strcmp(name,"--no-color")||!strcmp(name,"--no-colour")){a->color=0;a->color_forced=1;}
    else if(!strcmp(name,"--ascii"))a->ascii=1;
    else if(!strcmp(name,"--xy")){if(!cli_spec_checked(&a->cli,"xy",APP_JSON_BOOL,NULL,1,err))goto fail;a->load.xy=1;a->load.flags|=APP_SPEC_XY;}
    else if(!strcmp(name,"--transpose")){a->load.transpose=1;a->load.flags|=APP_SPEC_TRANSPOSE;}
    else if(!strcmp(name,"--no-header")){a->load.no_header=1;a->load.flags|=APP_SPEC_NO_HEADER;}
    else if(!strcmp(name,"--delim")||!strcmp(name,"--delimiter")){
      app_spec temp;app_spec_init(&temp);VALUE();
      two=cli_spec_checked(&temp,"delim",APP_JSON_STRING,v,0,err);
      if(two){a->load.delim=temp.delim;a->load.flags|=APP_SPEC_DELIM;}
      app_spec_free(&temp);if(!two)goto fail;
    }else if(!strcmp(name,"--labels-col")){VALUE();if(!cli_integer(v,name,1,1000,&a->load.label_col,err))goto fail;a->load.label_col--;a->load.flags|=APP_SPEC_LABEL_COL;}
    else if(!strcmp(name,"--series-col")){VALUE();if(!cli_integer(v,name,1,1000,&a->load.series_col,err))goto fail;a->load.series_col--;a->load.flags|=APP_SPEC_SERIES_COL;}
    else if(!strcmp(name,"--label-key")){VALUE();if(!cli_set(&a->load.label_key,v,err))goto fail;a->load.flags|=APP_SPEC_LABEL_KEY;}
    else if(!strcmp(name,"--min")){SPEC("min");}
    else if(!strcmp(name,"--max")){SPEC("max");}
    else if(!strcmp(name,"--depth")){SPEC("depth");}
    else if(!strcmp(name,"--bins")){SPEC("bins");}
    else if(!strcmp(name,"--prec")||!strcmp(name,"--decimals")){SPEC("prec");}
    else if(!strcmp(name,"--explode")){
      double number;
      if(has_val){
        if(!*v){if(!cli_spec_checked(&a->cli,"explode",APP_JSON_BOOL,NULL,1,err))goto fail;}
        else if(!cli_spec_checked(&a->cli,"explode",APP_JSON_STRING,v,0,err))goto fail;
      }else if(i+1<argc&&app_parse_number(argv[i+1],&number)){
        i++;if(!cli_spec_checked(&a->cli,"explode",APP_JSON_STRING,argv[i],0,err))goto fail;
      }else if(!cli_spec_checked(&a->cli,"explode",APP_JSON_BOOL,NULL,1,err))goto fail;
    }else if(!strcmp(name,"--example")){
      const char *what="csv";
      if(has_val)what=v;else if(i+1<argc&&argv[i+1][0]!='-')what=argv[++i];
      if(strcmp(what,"csv")&&strcmp(what,"json")&&strcmp(what,"deck")&&strcmp(what,"j")&&strcmp(what,"d")){
        cli_usage(err,"--example wants csv, json or deck",NULL,NULL);goto fail;
      }
      if(!cli_set(&a->example,what,err))goto fail;
    }else if(!strcmp(name,"--demo"))a->demo=1;
    else if(!strcmp(name,"--list-types")||!strcmp(name,"--types"))a->list_types=1;
    else if(!strcmp(name,"--list-palettes")||!strcmp(name,"--palettes"))a->list_palettes=1;
    else if(!strcmp(name,"--list-themes")||!strcmp(name,"--themes"))a->list_themes=1;
    else if(!strcmp(name,"-q")||!strcmp(name,"--quiet")){}
    else {cli_usage(err,"unknown option: ",arg,"  (charts -h lists them)");goto fail;}
#undef VALUE
#undef SPEC
    free(name_copy);name_copy=NULL;
  }
  if(a->png&&*a->png&&a->png_dir&&*a->png_dir)return cli_usage(err,"--png and --png-dir are one or the other",NULL,NULL);
  return 1;
fail:
  free(name_copy);return 0;
}

void app_print_version(FILE *f){fputs("charts 0.1.0\n",f);}
void app_print_types(FILE *f)
{
  static const char *const desc[]={
    "vertical bars, side by side per series; extruded unless --depth 0",
    "bars stacked into a total per category",
    "horizontal bars, names down the left: best for long labels and rankings",
    "before -> after: a dot per series on each row, an arrow to the last",
    "one line per series, a different marker each",
    "line with a dithered fill underneath",
    "flat pie: one series -> a slice per row; several -> a slice per series",
    "the extruded one; --explode N pops slice N out",
    "ring; --depth 0 for flat",
    "XY points; --xy takes the first numeric column as X",
    "histogram of the first series, --bins N buckets",
    "the numbers as a grid"
  };
  size_t i;for(i=0;types[i];i++)fprintf(f,"%-9s %s\n",types[i],desc[i]);
}
void app_print_palettes(FILE *f)
{
  fputs("dos      yellow, cyan, green, red ...: the bright eight first (default)\n"
        "ega      red, green, yellow, blue ...: EGA order\n"
        "cga      cyan, magenta, white, yellow: the four-colour card\n"
        "ice      cyans, blues, white\n"
        "fire     yellow, red, brown\n"
        "green    phosphor greens\n"
        "amber    amber terminal\n"
        "mono     white and grey; series are told apart by dither\n",f);
}
void app_print_themes(FILE *f)
{
  fputs("dos      blue desktop, black chart windows, grey status bar (default for decks)\n"
        "black    no background at all (default when printing into a shell)\n"
        "light    grey desktop, white chart windows, dark ink\n",f);
}
