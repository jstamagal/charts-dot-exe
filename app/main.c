/* charts: CLI around the portable libcharts renderer. */
#include "cli.h"
#include "sources.h"
#include "bridge.h"
#include "platform.h"
#include "tui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int report(const app_deck *d,int to_stderr,app_error *err)
{
  char *s;
  if(!d->issue_count)return 0;
  s=app_deck_issues_text(d,err);if(!s)return 1;
  fputs(s,to_stderr?stderr:stdout);free(s);
  return app_deck_ok(d)?0:1;
}
static int count_charts(const app_block *b,size_t n,const app_block **single)
{
  size_t i;int count=0;
  for(i=0;i<n;i++){
    if(b[i].kind==APP_BLOCK_CHART){count++;*single=b+i;}
    else count+=count_charts(b[i].children,b[i].child_count,single);
  }
  return count;
}
static void text_size(const app_args *a,const app_deck *d,int *w,int *h)
{
  const app_block *single=NULL;
  int tty=app_is_tty(1)&&(!a->out||!*a->out);
  if(tty){app_term_size(w,h);(*h)--;}
  else {*w=100;*h=32;}
  if(d->implicit&&d->slide_count==1&&count_charts(d->slides[0].blocks,d->slides[0].block_count,&single)==1){
    if(single->data.spec.flags&APP_SPEC_WIDTH)*w=single->data.spec.width;
    if(single->data.spec.flags&APP_SPEC_HEIGHT)*h=single->data.spec.height;
  }
  if(a->width>0)*w=a->width;
  if(a->height>0)*h=a->height;
  if(*w<20)*w=20;
  if(*h<6)*h=6;
}
static void fit_issue(void *user,const char *path,const char *message)
{
  app_deck *d=(app_deck *)user;
  app_error ignored;
  ignored.code=0;ignored.message[0]=0;
  app_deck_add_issue(d,0,path,message,&ignored);
}
/* The canvas for --check and --png when no size is given: the slide area of
   the screen the deck will be shown on. A 1080p console at scale 2 is 120 x 33;
   DOS shows VGA, 640 x 480 less the status row, which also keeps a 4 MiB
   machine from rendering pixels it will never display. */
#if defined(__DOS__) || defined(MSDOS) || defined(__MSDOS__)
#define DEFAULT_COLS 80
#define DEFAULT_ROWS 29
#else
#define DEFAULT_COLS 120
#define DEFAULT_ROWS 33
#endif
static int render_one(lc_context *ctx,app_deck *deck,size_t index,const app_spec *cli,
                      const char *theme,int cols,int rows,lc_mode mode,int chrome,
                      lc_scene **out,app_error *err)
{
  lc_scene *scene;
  lc_slide_view view;
  lc_status status;
  status=lc_scene_create(ctx,cols,rows,mode,&scene);
  if(status!=LC_OK){app_error_set(err,1,lc_status_string(status));return 0;}
  lc_scene_set_diagnostic(scene,fit_issue,deck);
  lc_slide_view_init(&view);view.chrome=chrome;
  status=app_render_deck(ctx,scene,deck,(int)index,&view,cli,theme);
  if(status==LC_OK)status=lc_scene_status(scene);
  if(status!=LC_OK){app_error_set(err,1,lc_status_string(status));lc_scene_destroy(scene);return 0;}
  *out=scene;return 1;
}
static int render_png(lc_context *ctx,app_deck *deck,size_t index,
                      const app_spec *cli,const app_args *a,const char *theme,
                      const char *path,app_error *err)
{
  lc_scene *scene;
  lc_mode mode;
  lc_image image,scaled;
  lc_sink sink;
  lc_status status;
  FILE *f;
  int w=a->width>0?a->width:DEFAULT_COLS,h=a->height>0?a->height:DEFAULT_ROWS;
  mode.pixel=1;mode.ascii=0;mode.color=1;
  if(!render_one(ctx,deck,index,cli,theme,w,h,mode,!deck->implicit,&scene,err))return 0;
  memset(&image,0,sizeof image);memset(&scaled,0,sizeof scaled);
  status=lc_scene_to_image(scene,&image);
  if(status==LC_OK&&a->scale>1)status=lc_image_scale(ctx,&image,a->scale,&scaled);
  if(status!=LC_OK){app_error_set(err,1,lc_status_string(status));lc_free(ctx,image.pixels);lc_scene_destroy(scene);return 0;}
  f=fopen(path,"wb");
  if(!f){app_error_path(err,1,"cannot write",path);lc_free(ctx,scaled.pixels);lc_free(ctx,image.pixels);lc_scene_destroy(scene);return 0;}
  sink.user=f;sink.write=app_file_sink;
  status=lc_write_png(ctx,scaled.pixels?&scaled:&image,&sink);
  if(fclose(f)!=0&&status==LC_OK)status=LC_EIO;
  lc_free(ctx,scaled.pixels);lc_free(ctx,image.pixels);lc_scene_destroy(scene);
  if(status!=LC_OK){app_error_path(err,1,"write failed",path);return 0;}
  lc_scratch_reset(ctx);return 1;
}
static int render_cells(lc_context *ctx,app_deck *deck,const app_spec *cli,
                        const app_args *a,const char *theme,int color,app_error *err)
{
  lc_mode mode;
  lc_scene *scene;
  lc_cell *cells;
  lc_status status;
  lc_sink sink;
  FILE *f;
  size_t i,n=0;
  int w,h,bright_bg;
  const char *term=getenv("TERM");
  text_size(a,deck,&w,&h);
  f=a->out&&*a->out?fopen(a->out,"wb"):stdout;
  if(!f){app_error_path(err,1,"cannot write",a->out);return 0;}
  sink.user=f;sink.write=app_file_sink;
  mode.pixel=0;mode.ascii=a->ascii;mode.color=color;
  bright_bg=!term||strcmp(term,"linux")!=0;
  for(i=0;i<deck->slide_count;i++){
    if(a->slide>0&&(int)i!=a->slide-1)continue;
    if(n++&&fputc('\n',f)==EOF){app_error_set(err,1,"write failed");goto fail;}
    if(!render_one(ctx,deck,i,cli,theme,w,h,mode,!deck->implicit, &scene,err))goto fail;
    cells=NULL;status=lc_scene_to_cells(scene,&cells);
    if(status==LC_OK)status=lc_write_cells(cells,w,h,(size_t)w,color,0,bright_bg,&sink);
    lc_free(ctx,cells);lc_scene_destroy(scene);lc_scratch_reset(ctx);
    if(status!=LC_OK){app_error_set(err,1,lc_status_string(status));goto fail;}
  }
  if(f!=stdout&&fclose(f)!=0){app_error_path(err,1,"write failed",a->out);return 0;}
  if(f==stdout)fflush(stdout);
  return 1;
fail:
  if(f!=stdout)fclose(f);
  return 0;
}
static int describe_blocks(app_block *blocks,size_t count,FILE *f,int many,app_error *err)
{
  size_t i;char *s;
  for(i=0;i<count;i++){
    app_block *b=blocks+i;
    if(b->kind==APP_BLOCK_CHART&&!b->error){
      if(!b->data.source||!*b->data.source){
        free(b->data.source);b->data.source=app_strdup("<stdin>",err);
        if(!b->data.source)return 0;
      }
      s=app_data_describe(&b->data,err);if(!s)return 0;
      fputs(s,f);if(many)fputc('\n',f);free(s);
    }else if(b->child_count&&!describe_blocks(b->children,b->child_count,f,many,err))return 0;
  }
  return 1;
}
static void clear_files(char **files,size_t count)
{
  size_t i;for(i=0;i<count;i++)free(files[i]);free(files);
}
int main(int argc,char **argv)
{
  app_args a;
  app_deck deck;
  app_error err;
  lc_context *ctx=NULL;
  char **files=NULL;
  size_t file_count=0,i,decks=0,errors=0;
  const char **file_views=NULL;
  const char *theme;
  int code=1,to_png,tty_out,wants_screen,present,color,tiling;
  char *s;
  char *created_path=NULL;
  app_present_opts po;
  app_spec cli;
  lc_scene *scene;
  lc_mode mode;
  app_args_init(&a);app_deck_init(&deck);app_spec_init(&cli);
  err.code=0;err.message[0]=0;
  if(!app_args_parse(&a,argc,argv,&err)){code=err.code?err.code:2;goto done;}
  if(a.help){app_print_help(stdout);code=0;goto done;}
  if(a.version){app_print_version(stdout);code=0;goto done;}
  if(a.schema){app_print_schema(stdout);code=0;goto done;}
  if(a.list_types){app_print_types(stdout);code=0;goto done;}
  if(a.list_palettes){app_print_palettes(stdout);code=0;goto done;}
  if(a.list_themes){app_print_themes(stdout);code=0;goto done;}
  if(a.example){app_print_example(stdout,a.example);code=0;goto done;}
  if(!app_sources_expand((const char *const *)a.files,a.file_count,&files,&file_count,&err))goto done;
  if(a.demo&&file_count){app_error_set(&err,2,"--demo shows its own deck: give it no files");goto done;}
  if(!file_count&&!a.demo){
    if(!app_is_tty(0)){
      int c=getchar();if(c!=EOF){ungetc(c,stdin);files=(char **)malloc(sizeof *files);if(!files){app_error_set(&err,1,"out of memory");goto done;}
        files[0]=app_strdup("-",&err);if(!files[0])goto done;file_count=1;}
    }
    if(!file_count){
      fputs("charts: no input.  To start:\n"
            "  charts --demo                       a short deck, built in: arrows to page, q to quit\n"
            "  charts --example deck > deck.json   a deck to start from (charts --example csv > revenue.csv beside it)\n"
            "  charts deck.json                    present it\n"
            "  charts data.csv                     one chart, printed into the shell\n"
            "  charts -h                           the whole manual, written for the agents that make decks\n",stderr);
      code=2;goto done;
    }
  }
  decks=a.demo?1:0;
  for(i=0;i<file_count;i++)if(app_is_deck_file(files[i]))decks++;
  if(decks>1||(decks==1&&file_count>1)){
    app_error_set(&err,2,"give one deck, or data files, not a mix: put the data files in the deck instead");goto done;
  }
  file_views=(const char **)malloc(file_count*sizeof *file_views);
  if(file_count&&!file_views){app_error_set(&err,1,"out of memory");goto done;}
  for(i=0;i<file_count;i++)file_views[i]=files[i];
  if(a.demo){if(!app_deck_load_text(&deck,app_demo_deck(),&a.load,&err))goto done;}
  else if(decks==1){if(!app_deck_load_file(&deck,files[0],&a.load,&err))goto done;}
  else {
    tiling=a.tile||(a.type_count>1&&file_count==1);
    if(!app_deck_from_files(&deck,file_views,file_count,(const char **)a.types,a.type_count,tiling,&a.load,&err))goto done;
  }
  if(!app_spec_clone(&cli,&a.cli,&err))goto done;
  if(decks==1&&a.type_count==1){
    free(cli.type);cli.type=app_strdup(a.types[0],&err);if(!cli.type)goto done;
    cli.flags|=APP_SPEC_TYPE;
  }
  to_png=(a.png&&*a.png)||(a.png_dir&&*a.png_dir);
  tty_out=app_is_tty(1)&&(!a.out||!*a.out);
  wants_screen=!a.check&&!a.describe&&!to_png&&!a.print;
  if(wants_screen&&a.show&&!(tty_out&&app_is_tty(0))){
    app_error_set(&err,2,"-i needs a terminal on stdin and stdout (use --print or --png without one)");goto done;
  }
  present=wants_screen&&tty_out&&app_is_tty(0)&&(a.show||decks==1);
  theme=a.theme&&*a.theme?a.theme:(deck.theme&&*deck.theme?NULL:
        (present||to_png||decks==1?"dos":"black"));
  if(a.gfx&&strcmp(a.gfx,"auto")==0&&deck.gfx){free(a.gfx);a.gfx=app_strdup(deck.gfx,&err);if(!a.gfx)goto done;}
  if(!a.scale)a.scale=deck.scale;
  if(!a.width)a.width=deck.cols;
  if(!a.height)a.height=deck.rows;
  if(!a.ascii&&deck.ascii==1)a.ascii=1;
  if(!a.color_forced&&deck.color==0)a.color=0;
  if(a.launcher&&*a.launcher){
    if(decks!=1||a.demo){app_error_set(&err,2,"--launcher wants a deck file: charts DECK.json --launcher NAME");goto done;}
    if(!app_launch_file(a.launcher,files[0],argv[0],&created_path,&err))goto done;
    printf("%s\n",created_path);
    free(created_path);created_path=NULL;
    code=report(&deck,1,&err);goto done;
  }
  if(lc_context_create(NULL,&ctx)!=LC_OK){app_error_set(&err,1,"out of memory");goto done;}
  if(a.check){
    mode.pixel=1;mode.ascii=0;mode.color=1;
    for(i=0;i<deck.slide_count;i++){
      if(!render_one(ctx,&deck,i,&cli,theme,a.width?a.width:DEFAULT_COLS,a.height?a.height:DEFAULT_ROWS,mode,1,&scene,&err)){
        if(app_deck_ok(&deck))goto done;
        err.code=0;err.message[0]=0;
        lc_scratch_reset(ctx);continue;
      }
      lc_scene_destroy(scene);lc_scratch_reset(ctx);
    }
    if(a.json){s=app_deck_issues_json(&deck,&err);if(!s)goto done;fputs(s,stdout);free(s);}
    else {
      report(&deck,0,&err);
      for(i=0;i<deck.issue_count;i++)if(deck.issues[i].error)errors++;
      printf("%s: %lu slide%s, %lu error%s, %lu warning%s\n",
        app_deck_ok(&deck)?"ok":"FAILED",(unsigned long)deck.slide_count,
        deck.slide_count==1?"":"s",(unsigned long)errors,errors==1?"":"s",
        (unsigned long)(deck.issue_count-errors),deck.issue_count-errors==1?"":"s");
    }
    code=app_deck_ok(&deck)?0:1;goto done;
  }
  if(a.describe){
    if(decks==1){s=app_deck_outline(&deck,&err);if(!s)goto done;fputs(s,stdout);free(s);}
    else for(i=0;i<deck.slide_count;i++)
      if(!describe_blocks(deck.slides[i].blocks,deck.slides[i].block_count,stdout,deck.slide_count>1,&err))goto done;
    code=report(&deck,1,&err);goto done;
  }
  if(!deck.slide_count){code=report(&deck,1,&err);if(!code)code=1;goto done;}
  if(a.slide>(int)deck.slide_count){char message[160];
    sprintf(message,"--slide %d: the deck has %lu slides",a.slide,(unsigned long)deck.slide_count);
    app_error_set(&err,1,message);goto done;
  }
  if(present){
    memset(&po,0,sizeof po);po.gfx=a.ascii?"ascii":a.gfx;po.theme=a.theme;
    po.scale=a.scale;po.slide=a.slide?a.slide-1:0;po.color=a.color;po.verbose=a.verbose;
    po.load=&a.load;po.cli=&cli;
    code=app_run_presenter(&deck,&po);goto done;
  }
  if(to_png){
    if(a.png&&*a.png){
      if(!render_png(ctx,&deck,a.slide?(size_t)(a.slide-1):0,&cli,&a,theme,a.png,&err))goto done;
      if(a.verbose)fprintf(stderr,"charts: wrote %s\n",a.png);
    }else{
      if(!app_mkdir(a.png_dir,&err))goto done;
      for(i=0;i<deck.slide_count;i++){
        char number[40];char *path;size_t length;
        if(a.slide&&i!=(size_t)(a.slide-1))continue;
        sprintf(number,"/slide-%02lu.png",(unsigned long)(i+1));
        length=strlen(a.png_dir)+strlen(number)+1;
        path=(char *)malloc(length);if(!path){app_error_set(&err,1,"out of memory");goto done;}
        strcpy(path,a.png_dir);strcat(path,number);
        if(!render_png(ctx,&deck,i,&cli,&a,theme,path,&err)){free(path);goto done;}
        puts(path);free(path);
      }
    }
    code=report(&deck,1,&err);goto done;
  }
  color=a.color;if(!a.color_forced&&!tty_out)color=0;
  if(!render_cells(ctx,&deck,&cli,&a,theme,color,&err))goto done;
  code=report(&deck,1,&err);
done:
  if(err.code==2)code=2;
  if(err.code)fprintf(stderr,"charts: %s\n",err.message);
  lc_context_destroy(ctx);app_deck_free(&deck);app_spec_free(&cli);
  app_args_free(&a);clear_files(files,file_count);free(file_views);
  free(created_path);
  return code;
}
