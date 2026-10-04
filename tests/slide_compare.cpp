/* Optional original-vs-C slide migration oracle. */
#include "src/deck.hpp"
#include "src/util.hpp"
#include "include/libcharts.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <memory>
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
struct BlockView {
 lc_block b;ch::RenderOpts opts;std::unique_ptr<View> data;
 std::vector<const char*> lines;std::vector<lc_shape> shapes;std::vector<std::vector<lc_pt>> points;
 std::vector<lc_flow_node> nodes;std::vector<lc_flow_edge> edges;std::vector<std::unique_ptr<BlockView>> children;std::vector<lc_block> child_blocks;
 BlockView(ch::Deck &deck,ch::Block &old,const ch::SlideView &v){
  lc_block_init(&b);b.kind=old.kind;b.tag=&old;b.path=old.path.c_str();b.weight=old.weight;b.has_at=old.has_at;for(int i=0;i<4;i++)b.at[i]=old.at[i];
  b.error=old.error.c_str();b.data_ref=old.data_ref.c_str();b.title=old.title.c_str();b.size=old.size;b.align=old.align;b.color=old.color;b.box=old.box;b.middle=old.middle;b.value=old.value.c_str();b.label=old.label.c_str();b.delta=old.delta.c_str();
  opts=ch::block_opts(deck,old,v);data.reset(new View(old.ds,opts));b.chart=data->opts;b.data=data->ds;
  for(auto &s:old.lines)lines.push_back(s.c_str());b.lines=lines.data();b.line_count=lines.size();
  for(auto &s:old.shapes){points.emplace_back();for(auto &p:s.pts)points.back().push_back({p.x,p.y});lc_shape c={};c.kind=s.kind;c.points=points.back().data();c.point_count=points.back().size();c.text=s.text.c_str();c.color=s.color;c.text_color=s.text_color;c.border=s.border;c.dither=s.dither;c.depth=s.depth;c.width=s.width;c.size=s.size;c.align=s.align;c.shadow=s.shadow;c.fill=s.fill;c.dash=s.dash;c.head=s.head;c.tail=s.tail;shapes.push_back(c);}b.shapes=shapes.data();b.shape_count=shapes.size();
  for(auto &n:old.flow.nodes)nodes.push_back({n.id.c_str(),n.text.c_str(),n.color});for(auto &e:old.flow.edges)edges.push_back({e.from,e.to,e.text.c_str(),e.color,e.dash});b.flow={nodes.data(),nodes.size(),edges.data(),edges.size(),old.flow.down};
  for(auto &child:old.kids){children.emplace_back(new BlockView(deck,child,v));child_blocks.push_back(children.back()->b);}b.children=child_blocks.data();b.child_count=child_blocks.size();
 }
};
static void skin(lc_skin *s){
#define C(n) s->n=ch::S.n
 C(slide_bg);C(panel_bg);C(frame);C(title);C(subtitle);C(axis);C(tick);C(grid);C(label);C(xlabel);C(ylabel);C(legend);C(value);C(shadow);C(heading);C(text);C(dim);C(accent);C(bullet);C(bar_fg);C(bar_bg);C(bar_key);C(note_fg);C(note_bg);C(cursor_fg);C(cursor_bg);C(table_head);C(table_rule);C(table_row);
#undef C
}
static bool layout(const std::vector<ch::Block>&old,const lc_block *blocks){for(size_t i=0;i<old.size();i++){auto a=old[i].r;auto b=blocks[i].rect;if(a.x!=b.x||a.y!=b.y||a.w!=b.w||a.h!=b.h){fprintf(stderr,"layout mismatch at %s\n",old[i].path.c_str());return false;}if(!layout(old[i].kids,blocks[i].children))return false;}return true;}
int main(int argc,char**argv){
 lc_context *ctx;lc_scene *sc;int cases=0;lc_context_create(NULL,&ctx);
 std::vector<std::string> files;if(argc>1)for(int i=1;i<argc;i++)files.push_back(argv[i]);else files={"examples/demo/deck.json","examples/deck.json","skill/charts/assets/gallery/deck.json"};
 for(auto &file:files){ch::Deck d=ch::load_deck(file,ch::LoadOpts());for(size_t index=0;index<d.slides.size();index++)for(int mode=0;mode<3;mode++)for(int variant=0;variant<3;variant++){
  ch::use_deck_theme(d,variant==2?"light":"");if(mode==2)ch::use_ascii_glyphs();else ch::use_unicode_glyphs();ch::Mode m;m.pixel=mode==0;m.ascii=mode==2;m.color=true;
  int width=variant==1?60:100,height=variant==1?22:35;ch::Scene old(width,height,m);lc_mode cm={m.pixel,m.ascii,1};lc_scene_create(ctx,width,height,cm,&sc);skin(lc_scene_skin(sc));lc_scene_reset(sc);
  ch::SlideView v;v.notes=variant==1;v.hint="e edit  n notes  q quit";if(variant==2){auto leaves=ch::leaf_blocks(d.slides[index]);if(!leaves.empty())v.focus=leaves[0];v.message="Saved file";}
  auto &s=d.slides[index];std::vector<std::unique_ptr<BlockView>> views;std::vector<lc_block> blocks;for(auto &b:s.blocks){views.emplace_back(new BlockView(d,b,v));blocks.push_back(views.back()->b);}
  lc_slide slide={s.path.c_str(),s.title.c_str(),s.subtitle.c_str(),s.notes.c_str(),s.layout.c_str(),blocks.data(),blocks.size()};
  lc_slide_view view;lc_slide_view_init(&view);view.focus=v.focus;view.cur_series=v.cur_series;view.cur_index=v.cur_index;view.notes=v.notes;view.chrome=v.chrome;view.bare=d.implicit;std::string footer=d.footer.empty()?d.title:d.footer;if(footer.empty()&&!d.file.empty())footer=d.file.substr(d.file.find_last_of('/')+1);view.footer=footer.c_str();view.hint=v.hint.c_str();view.message=v.message.c_str();view.message_bad=v.message_bad;view.index=index;view.count=d.slides.size();
  ch::render_slide(old,d,(int)index,v);lc_render_slide(sc,&slide,&view);
  if(!compare(old,sc,ctx,file.c_str(),mode,variant,(int)index)||!layout(s.blocks,slide.blocks))return 1;
  lc_scene_destroy(sc);lc_scratch_reset(ctx);cases++;
 }}
 lc_context_destroy(ctx);printf("%d complete slide scene and layout differentials passed\n",cases);
}
