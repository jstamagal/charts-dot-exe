#include "src/scene.hpp"
#include "src/util.hpp"
#include "include/libcharts.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
static unsigned rng=42;
static unsigned nextnum() {rng=rng*1664525u+1013904223u;return rng;}
static double coord(int n) {return (int)(nextnum()%n)-20+(nextnum()%100)/100.0;}
int main() {
 lc_context *ctx; lc_scene *sc; lc_image im; lc_cell *cells; int total=0;
 if(lc_context_create(NULL,&ctx))return 2;
 ch::use_unicode_glyphs();
 for(int theme=0;theme<3;theme++) for(int mode=0;mode<3;mode++) {
  const char *name=theme==0?"dos":theme==1?"black":"light";
  ch::set_theme(name); ch::Mode m; m.pixel=mode==0;m.ascii=mode==2;m.color=true;
  ch::Scene old(50,25,m); lc_mode cm={mode==0,mode==2,1};
  if(lc_scene_create(ctx,50,25,cm,&sc))return 2;
  lc_skin_init(lc_scene_skin(sc),name);lc_scene_reset(sc);
  old.cv.box(1,1,45,22,1,14);lc_canvas_box(lc_scene_canvas(sc),1,1,45,22,1,14);
  old.cv.shadow(1,1,45,22);lc_canvas_shadow(lc_scene_canvas(sc),1,1,45,22);
  old.cv.text(2,2,"TITLE\té漢\x1bTEST",11);lc_canvas_text(lc_scene_canvas(sc),2,2,"TITLE\té漢\x1bTEST",11,-1);
  old.cv.text_c(3,4,30,"long title with words and ü",12);lc_canvas_text_c(lc_scene_canvas(sc),3,4,30,"long title with words and ü",12);
  ch::Rect r={3,5,38,12};lc_rect cr={3,5,38,12};
  ch::Surface &os=old.surface(r);lc_surface *s=lc_scene_surface(sc,cr);
  for(int i=0;i<200;i++) {
   double a=coord(os.w()+40),b=coord(os.h()+40),c=coord(os.w()+40),d=coord(os.h()+40);int kind=nextnum()%8,col=nextnum()%16,lev=nextnum()%4;
   ch::Ink ink(col,3,lev);lc_ink k=lc_ink_make(col,3,lev);
   switch(kind) {
   case 0:os.rect(a,b,c,d,ink);lc_surface_rect(s,a,b,c,d,k);break;
   case 1:os.line(a,b,c,d,ink,2);lc_surface_line(s,a,b,c,d,k,2);break;
   case 2:os.disc(a,b,7,ink);lc_surface_disc(s,a,b,7,k);break;
   case 3:os.ring(a,b,7,ink);lc_surface_ring(s,a,b,7,k);break;
   case 4:{int sh=nextnum()%7;os.marker(a,b,sh,5,ink);lc_surface_marker(s,a,b,sh,5,k);break;}
   case 5:{std::vector<ch::Pt> p={{a,b},{c,d},{c+10,b+7}};lc_pt p2[3]={{a,b},{c,d},{c+10,b+7}};os.poly(p,ink);lc_surface_poly(s,p2,3,k);break;}
   case 6:os.dotted_h((int)a,(int)c,(int)b,ink,3);lc_surface_dotted_h(s,(int)a,(int)c,(int)b,k,3);break;
   case 7:os.vline((int)a,(int)b,(int)d,ink);lc_surface_vline(s,(int)a,(int)b,(int)d,k);break;
   }
  }
  ch::Rect clipped={-2,-1,12,7};lc_rect cclip={-2,-1,12,7};
  auto &clipold=old.surface(clipped);auto *clipnew=lc_scene_surface(sc,cclip);
  clipold.rect(1,1,70,75,ch::Ink(12,4,2));lc_surface_rect(clipnew,1,1,70,75,lc_ink_make(12,4,2));
  clipold.erase(10,12,23,35);lc_surface_erase(clipnew,10,12,23,35);
  old.text(4.25,18.5,"Hello é \xff 😀",14,-1,1);lc_scene_text(sc,4.25,18.5,"Hello é \xff 😀",14,-1,1);
  old.big(3,20,35,"BIG LABEL",11,2,0);lc_scene_big(sc,3,20,35,"BIG LABEL",11,2,0);
  auto oi=old.to_image();if(lc_scene_to_image(sc,&im))return 2;
  for(size_t i=0;i<oi.px.size();i++)if(oi.px[i]!=im.pixels[i]){fprintf(stderr,"image mismatch theme%d mode%d at%zu (%d != %d)\n",theme,mode,i,oi.px[i],im.pixels[i]);return 1;}
  auto oc=old.to_cells();if(lc_scene_to_cells(sc,&cells))return 2;
  for(int y=0;y<25;y++)for(int x=0;x<50;x++){auto a=oc.at(x,y);auto b=cells[y*50+x];if(a.ch!=b.ch||a.fg!=b.fg||a.bg!=b.bg){fprintf(stderr,"cells mismatch theme%d mode%d at%d,%d\n",theme,mode,x,y);return 1;}}
  lc_free(ctx,im.pixels);lc_free(ctx,cells);lc_scene_destroy(sc);lc_scratch_reset(ctx);total++;
 }
 lc_context_destroy(ctx);printf("%d scene differential checks passed (1800 randomized primitive operations)\n",total);
}
