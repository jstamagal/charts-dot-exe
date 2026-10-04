/* Migration oracle; optional C++ test against retained original sources. */
#include "src/diagram.hpp"
#include "src/util.hpp"
#include "include/libcharts.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
static bool compare(ch::Scene &old,lc_scene *sc,lc_context *ctx,const char *what,int mode) {
    lc_image im;lc_cell *cells;
    auto expected=old.to_image(); if(lc_scene_to_image(sc,&im))return false;
    for(size_t i=0;i<expected.px.size();i++)if(expected.px[i]!=im.pixels[i]){fprintf(stderr,"%s pixel mode %d at %zu: %d / %d\n",what,mode,i,expected.px[i],im.pixels[i]);return false;}
    lc_free(ctx,im.pixels);auto cv=old.to_cells();if(lc_scene_to_cells(sc,&cells))return false;
    for(int y=0;y<old.rows();y++)for(int x=0;x<old.cols();x++){auto a=cv.at(x,y);auto b=cells[y*old.cols()+x];if(a.ch!=b.ch||a.fg!=b.fg||a.bg!=b.bg){fprintf(stderr,"%s cell mode %d at %d,%d\n",what,mode,x,y);return false;}}
    lc_free(ctx,cells);return true;
}
int main() {
    lc_context *ctx;lc_scene *sc;int cases=0;lc_context_create(NULL,&ctx);ch::use_unicode_glyphs();
    const char *wraps[]={"","one two\nthree\n\n", " abcdefghijklmnop  words\twith tabs","ééééééé x", "one\xfftwo\xfflongword", "alpha\n beta gamma delta"};
    for(auto text:wraps)for(size_t width=1;width<12;width++){auto old=ch::wrap_words(text,width);size_t n;char **now=lc_text_wrap(ctx,text,width,&n);if(n!=old.size()){fprintf(stderr,"wrap count width%zu\n",width);return 1;}for(size_t i=0;i<n;i++)if(old[i]!=now[i]){fprintf(stderr,"wrap value width%zu item%zu <%s>/<%s>\n",width,i,old[i].c_str(),now[i]);return 1;}lc_scratch_reset(ctx);}
    for(int mode=0;mode<3;mode++)for(int theme=0;theme<3;theme++)for(int down=0;down<2;down++) {
        const char *name=theme==0?"dos":theme==1?"black":"light";ch::set_theme(name);
        ch::Mode m;m.pixel=mode==0;m.ascii=mode==2;m.color=true;ch::Scene old(100,35,m);lc_mode cm={m.pixel,m.ascii,1};lc_scene_create(ctx,100,35,cm,&sc);lc_skin_init(lc_scene_skin(sc),name);lc_scene_reset(sc);
        ch::Flow f;f.down=down;f.nodes={{"a","Start here",-1},{"b","An unusuallylongword here",11},{"c","Choice",-1},{"d","End",3},{"e","Detached",-1}};
        f.edges={{0,1,"next",-1,false},{0,2,"maybe",12,true},{1,3,"",-1,false},{2,3,"join",-1,false},{3,0,"again",14,false},{4,4,"self",-1,false}};
        std::vector<lc_flow_node> nodes;std::vector<lc_flow_edge> edges;for(auto &n:f.nodes)nodes.push_back({n.id.c_str(),n.text.c_str(),n.color});for(auto &e:f.edges)edges.push_back({e.from,e.to,e.text.c_str(),e.color,e.dash});lc_flow flow={nodes.data(),nodes.size(),edges.data(),edges.size(),down};
        ch::Rect r={4,2,90,30};lc_rect rr={4,2,90,30};ch::draw_flow(old,r,f,ch::S.slide_bg==255?0:ch::S.slide_bg);lc_draw_flow(sc,rr,&flow,ch::S.slide_bg==255?0:ch::S.slide_bg);
        if(!compare(old,sc,ctx,"flow",mode))return 1;lc_scene_destroy(sc);lc_scratch_reset(ctx);cases++;
    }
    for(int mode=0;mode<3;mode++) {
        ch::set_theme("dos");ch::Mode m;m.pixel=mode==0;m.ascii=mode==2;m.color=true;ch::Scene old(100,35,m);lc_mode cm={m.pixel,m.ascii,1};lc_scene_create(ctx,100,35,cm,&sc);
        std::vector<ch::Shape> shapes;
        for(int i=0;i<5;i++){ch::Shape s;s.kind=(ch::Shape::Kind)i;s.pts={{1.0+i,1.0+i},{7.0+i,7.0+i}};if(i==2)s.pts.push_back({1.0,10.0});s.color=i%2?-1:11;s.text="Label wraps\nwith text";s.size=1;s.shadow=true;s.depth=2;s.head=true;s.tail=true;s.dash=i%2;s.dither=i%3;shapes.push_back(s);}
        std::vector<lc_shape> view;std::vector<std::vector<lc_pt>> points;
        for(auto &s:shapes){points.emplace_back();for(auto &p:s.pts)points.back().push_back({p.x,p.y});lc_shape c={};c.kind=s.kind;c.points=points.back().data();c.point_count=points.back().size();c.text=s.text.c_str();c.color=s.color;c.text_color=s.text_color;c.border=s.border;c.dither=s.dither;c.depth=s.depth;c.width=s.width;c.size=s.size;c.align=s.align;c.shadow=s.shadow;c.fill=s.fill;c.dash=s.dash;c.head=s.head;c.tail=s.tail;view.push_back(c);}
        ch::Rect r={4,2,90,30};lc_rect rr={4,2,90,30};ch::draw_shapes(old,r,shapes,4);lc_draw_shapes(sc,rr,view.data(),view.size(),4);
        if(!compare(old,sc,ctx,"shapes",mode))return 1;lc_scene_destroy(sc);lc_scratch_reset(ctx);cases++;
    }
    lc_context_destroy(ctx);printf("%d diagram scene differentials and 66 wrapping differentials passed\n",cases);
}
