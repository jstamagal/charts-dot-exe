/* Strict C89 core API, ownership, clipping and failure-path tests. */
#include "../include/libcharts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <assert.h>

typedef struct fail_heap { size_t calls,fail_at,live; } fail_heap;
static void *test_alloc(void *user,size_t bytes) {
    fail_heap *h=(fail_heap *)user; void *p;
    if (h->calls++==h->fail_at) return NULL;
    p=malloc(bytes); if (p) h->live++; return p;
}
static void test_free(void *user,void *p) {
    fail_heap *h=(fail_heap *)user; if (p) { assert(h->live>0); h->live--; free(p); }
}
static void exercise(lc_context *ctx) {
    lc_scene *sc; lc_surface *surface; lc_cell *cells; lc_image image;
    lc_rect rect; lc_mode mode; const char *p; lc_codepoint cp; char **lines; size_t n;
    mode.pixel=1; mode.ascii=0; mode.color=1;
    if (lc_scene_create(ctx,40,20,mode,&sc)!=LC_OK) return;
    rect.x=-2; rect.y=2; rect.w=24; rect.h=10;
    surface=lc_scene_surface(sc,rect);
    if (surface) {
        lc_surface_line(surface,-DBL_MAX,0,DBL_MAX,5,lc_ink_solid(3),2);
        lc_surface_line(surface,-1e9,0,1e9,100,lc_ink_solid(4),2);
        lc_surface_marker(surface,1e300,-1e300,4,1,lc_ink_solid(2));
        lc_surface_rect(surface,-1e300,-1e300,1e300,1e300,lc_ink_solid(5));
    }
    rect.x=INT_MIN;rect.y=INT_MAX;rect.w=1;rect.h=1;
    surface=lc_scene_surface(sc,rect);
    if(surface) lc_surface_rect(surface,0,0,8,16,lc_ink_solid(2));
    lc_scene_text(sc,3,4,"copy: \xff\xf0\x80 A",15,-1,1);
    lc_scene_big(sc,0,15,30,"large title",11,2,0);
    lc_canvas_fill_bg(lc_scene_canvas(sc),INT_MAX,INT_MAX,INT_MAX,INT_MAX,2);
    lc_canvas_box(lc_scene_canvas(sc),INT_MAX,INT_MAX,3,3,1,3);
    if (lc_scene_to_image(sc,&image)==LC_OK) {
        lc_image_rect(&image,INT_MAX,INT_MAX,INT_MAX,INT_MAX,4);
        lc_image_glyph(&image,INT_MAX,INT_MAX,'A',2,3,INT_MAX/16);
        lc_free(ctx,image.pixels);
    }
    if (lc_scene_to_cells(sc,&cells)==LC_OK) lc_free(ctx,cells);
    lines=lc_text_wrap(ctx,"one two\nabcdefgh\n\nlast",4,&n);
    if (lines) { assert(n==7); assert(!strcmp(lines[0],"one")); assert(!strcmp(lines[2],"abc.")); assert(!strcmp(lines[3],"def.")); assert(!strcmp(lines[4],"gh")); }
    p="\xf0\x80"; cp=lc_utf8_next(&p); assert(cp==0xfffdUL); cp=lc_utf8_next(&p); assert(cp==0xfffdUL); assert(!*p);
    {
        lc_flow_node nodes[3]; lc_flow_edge edges[3]; lc_flow flow;
        lc_shape shape; lc_pt points[3];
        nodes[0].id="a"; nodes[0].text="Alpha"; nodes[0].color=-1;
        nodes[1].id="b"; nodes[1].text="Beta"; nodes[1].color=4;
        nodes[2].id="c"; nodes[2].text="Gamma"; nodes[2].color=-1;
        edges[0].from=0; edges[0].to=1; edges[0].text="next"; edges[0].color=-1; edges[0].dash=0;
        edges[1]=edges[0]; edges[1].from=1; edges[1].to=2;
        edges[2]=edges[0]; edges[2].from=2; edges[2].to=0;
        flow.nodes=nodes; flow.node_count=3; flow.edges=edges; flow.edge_count=3; flow.down=0;
        rect.x=1; rect.y=1; rect.w=35; rect.h=17;
        lc_draw_flow(sc,rect,&flow,4);
        memset(&shape,0,sizeof shape); points[0].x=1; points[0].y=1; points[1].x=10; points[1].y=10;
        shape.points=points; shape.point_count=2; shape.color=11; shape.text_color=-1; shape.border=-1;
        shape.text="box"; shape.shadow=1; shape.depth=2; shape.fill=1; shape.width=1; shape.size=1;
        lc_draw_shapes(sc,rect,&shape,1,4);
    }
    lc_scene_destroy(sc);
}
int main(void) {
    lc_context *ctx; lc_allocator a; fail_heap heap; size_t i;
    assert(lc_context_create(NULL,&ctx)==LC_OK);
    assert(!strcmp(lc_fmt_val(ctx,-1234.5,-1),"-1,234.5"));
    assert(!strcmp(lc_fmt_axis(ctx,1200000),"1.2M"));
    assert(!strcmp(lc_fmt_raw(ctx,1e-300),"1e-300"));
    assert(strlen(lc_fmt_val(ctx,DBL_MAX,12))>300);
    {
        lc_scene *scene; lc_mode mode; lc_image image,packed; unsigned char *pixels;
        size_t stride,y;
        mode.pixel=1; mode.ascii=0; mode.color=1;
        assert(lc_scene_create(ctx,20,6,mode,&scene)==LC_OK);
        lc_scene_text(scene,2,2,"Pitched target",15,-1,1);
        assert(lc_scene_to_image(scene,&packed)==LC_OK);
        stride=(size_t)packed.width+13;
        pixels=(unsigned char *)malloc(stride*(size_t)packed.height); assert(pixels);
        memset(pixels,0xcd,stride*(size_t)packed.height);
        image=packed; image.pixels=pixels; image.stride=stride;
        assert(lc_scene_draw_image(scene,&image)==LC_OK);
        for (y=0;y<(size_t)image.height;y++) {
            assert(!memcmp(pixels+y*stride,packed.pixels+y*packed.stride,(size_t)image.width));
            assert(pixels[y*stride+image.width]==0xcd && pixels[(y+1)*stride-1]==0xcd);
        }
        image.stride=1; assert(lc_scene_draw_image(scene,&image)==LC_EINVAL);
        free(pixels); lc_free(ctx,packed.pixels); lc_scene_destroy(scene);
    }
    exercise(ctx); lc_context_destroy(ctx);
    a.user=&heap; a.alloc=test_alloc; a.free=test_free;
    for (i=0;i<150;i++) {
        heap.calls=0; heap.fail_at=i; heap.live=0;
        if (lc_context_create(&a,&ctx)==LC_OK) { exercise(ctx); lc_context_destroy(ctx); }
        assert(heap.live==0);
    }
    puts("core: numeric, Unicode, clipping and 150 allocation-failure positions passed");
    return 0;
}
