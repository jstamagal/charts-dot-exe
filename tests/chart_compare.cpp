/* Optional migration oracle: every pixel/cell versus the original renderer. */
#include "src/chart.hpp"
#include "src/util.hpp"
#include "include/libcharts.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <limits>
struct View {
 std::vector<const char*> labels;std::vector<lc_series> series;std::vector<std::vector<const char*>> tv;
 std::vector<lc_text_column> text;std::vector<lc_annotation> notes;std::vector<lc_color> colors;std::vector<lc_error_bars> errors;
 lc_dataset ds={};lc_chart_options opts;
 View(ch::Dataset &d,ch::RenderOpts &o){
  for(auto &x:d.labels)labels.push_back(x.c_str());for(auto &s:d.series)series.push_back({s.name.c_str(),s.v.data(),NULL,s.v.size(),s.color,s.col,s.decimals});
  for(auto &t:d.text){tv.emplace_back();for(auto &s:t.v)tv.back().push_back(s.c_str());text.push_back({t.name.c_str(),tv.back().data(),tv.back().size(),t.col});}
  ds={d.title.c_str(),d.source.c_str(),d.label_name.c_str(),d.x_name.c_str(),labels.data(),labels.size(),series.data(),series.size(),text.data(),text.size()};
  lc_chart_options_init(&opts);opts.type=o.type.c_str();opts.title=o.title.c_str();opts.subtitle=o.subtitle.c_str();opts.xlabel=o.xlabel.c_str();opts.ylabel=o.ylabel.c_str();opts.palette=o.palette.c_str();opts.frame=o.frame.c_str();opts.source=o.source.c_str();
  opts.legend=o.legend;opts.values=o.values;opts.grid=o.grid;opts.color=o.color;opts.shadow=o.shadow;opts.zero_base=o.zero_base;opts.xy=o.xy;opts.explode=o.explode;opts.depth=o.depth;opts.bins=o.bins;opts.prec=o.prec;opts.lo=o.lo;opts.hi=o.hi;opts.has_lo=o.has_lo;opts.has_hi=o.has_hi;opts.cur_series=o.cur_series;opts.cur_index=o.cur_index;opts.frame_color=o.frame_color;
  for(auto &a:o.notes)notes.push_back({a.kind,a.text.c_str(),a.label.c_str(),a.series.c_str(),a.index,a.series_i,a.color,a.value,a.fx,a.fy});
  for(auto &c:o.colors)colors.push_back({c.first.c_str(),c.second});for(auto &e:o.errors)errors.push_back({e.series.c_str(),e.lo.c_str(),e.hi.c_str(),e.plus_minus});
  opts.notes=notes.data();opts.note_count=notes.size();opts.colors=colors.data();opts.color_count=colors.size();opts.errors=errors.data();opts.error_count=errors.size();
 }
};
static bool compare(ch::Scene &old,lc_scene *sc,lc_context *ctx,const char *type,int mode,int theme,int test){
 lc_image im;lc_cell *cells;auto expected=old.to_image();auto status=lc_scene_to_image(sc,&im);if(status){fprintf(stderr,"status %d\n",status);return false;}
 size_t diff=0,first=0;for(size_t i=0;i<expected.px.size();i++)if(expected.px[i]!=im.pixels[i]){if(!diff)first=i;diff++;}
 if(diff){fprintf(stderr,"%s theme%d mode%d case%d: %zu pixel diffs; first %zu (%zu,%zu) old%d new%d\n",type,theme,mode,test,diff,first,first%im.width,first/im.width,expected.px[first],im.pixels[first]);FILE*f=fopen("/tmp/chart-old.pgm","wb");fprintf(f,"P5\n%d %d\n15\n",im.width,im.height);fwrite(expected.px.data(),1,expected.px.size(),f);fclose(f);f=fopen("/tmp/chart-new.pgm","wb");fprintf(f,"P5\n%d %d\n15\n",im.width,im.height);fwrite(im.pixels,1,expected.px.size(),f);fclose(f);return false;}
 lc_free(ctx,im.pixels);auto cv=old.to_cells();if(lc_scene_to_cells(sc,&cells))return false;
 for(int y=0;y<old.rows();y++)for(int x=0;x<old.cols();x++){auto a=cv.at(x,y);auto b=cells[y*old.cols()+x];if(a.ch!=b.ch||a.fg!=b.fg||a.bg!=b.bg){fprintf(stderr,"%s theme%d mode%d case%d: cell %d,%d old%lx,%d,%d new%lx,%d,%d\n",type,theme,mode,test,x,y,(unsigned long)a.ch,a.fg,a.bg,b.ch,b.fg,b.bg);return false;}}
 lc_free(ctx,cells);return true;
}
int main(int argc,char**argv){
 lc_context *ctx;lc_scene *sc;int cases=0;lc_context_create(NULL,&ctx);
 for(auto type:ch::type_names())for(int test=0;test<7;test++)for(int theme=0;theme<3;theme++)for(int mode=0;mode<3;mode++){
  if(argc>1 && type!=argv[1])continue;
  const char *name=theme==0?"dos":theme==1?"black":"light";ch::set_theme(name);if(mode==2)ch::use_ascii_glyphs();else ch::use_unicode_glyphs();
  ch::Mode m;m.pixel=mode==0;m.ascii=mode==2;m.color=test!=4;int width=test==6?35:80,height=test==6?14:30;ch::Scene old(width,height,m);lc_mode cm={m.pixel,m.ascii,m.color};lc_scene_create(ctx,width,height,cm,&sc);lc_skin_init(lc_scene_skin(sc),name);lc_scene_reset(sc);
  ch::Dataset d;d.title="Data title";d.label_name="Month";d.x_name="Time";d.labels={"Jan","Feb","March","April","May"};
  ch::Series s;s.name="Revenue";s.v={12.3,22,18,32,29};s.col=2;d.series.push_back(s);s.name="Cost";s.v={9,13,18.5,22,20};s.col=4;d.series.push_back(s);
  ch::TextColumn tc;tc.name="Status";tc.v={"one","two","three","four","five"};tc.col=3;d.text.push_back(tc);
  ch::RenderOpts o;o.type=type;o.title="Chart title";o.subtitle="Subtitle";o.xlabel="X label";o.ylabel="Y label";o.values=true;o.source="/a/long/path/file.csv";
  if(test==1){d.series[0].v={-10,25,-3,12,8};d.series[1].v={12,-8,18,-4,20};o.depth=0;o.palette="mono";o.frame="single";}
  if(test==2){d.series[0].v[2]=std::numeric_limits<double>::quiet_NaN();d.series[1].v.resize(4);o.has_lo=true;o.lo=5;o.has_hi=true;o.hi=25;o.explode=-1;o.cur_series=1;o.cur_index=1;}
  if(test==3){s.name="sd";s.v={1,2,1,3,2};d.series.push_back(s);ch::ErrorBars e;e.series="Revenue";e.lo="sd";e.plus_minus=true;o.errors.push_back(e);o.colors={{"Revenue",12},{"Cost",10}};ch::Annotation a;a.text="Launch note";a.label="Feb";a.series="Revenue";o.notes.push_back(a);a.kind=ch::Annotation::HLINE;a.value=15;a.text="Target";o.notes.push_back(a);a.kind=ch::Annotation::VLINE;a.label="March";a.text="Milestone";o.notes.push_back(a);a.kind=ch::Annotation::NOTE;a.text="free note";o.notes.push_back(a);}
  if(test==4){o.color=false;o.frame="none";o.legend=false;o.explode=2;}
  if(test==5){s.name="\001x";s.v={1,2,4,8,16};d.series.insert(d.series.begin(),s);o.xy=true;o.palette="ice";o.depth=3;}
  View v(d,o);ch::Rect r={1,1,width-4,height-3};lc_rect rr={1,1,width-4,height-3};ch::render_chart(old,r,d,o);lc_render_chart(sc,rr,&v.ds,&v.opts);
  if(!compare(old,sc,ctx,type.c_str(),mode,theme,test))return 1;
  lc_scene_destroy(sc);lc_scratch_reset(ctx);cases++;
 }
 lc_context_destroy(ctx);printf("%d chart scene differentials passed\n",cases);
}
