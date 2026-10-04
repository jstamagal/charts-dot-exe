/* Portable codec failure propagation and checked image/cell geometry. */
#include "charts.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct heap { size_t calls,fail_at,live; } heap;
static void *allocate(void *user,size_t n)
{
    heap *h=(heap *)user;
    void *p;
    if(h->calls++==h->fail_at) return NULL;
    p=malloc(n);if(p) h->live++;return p;
}
static void release(void *user,void *p)
{
    heap *h=(heap *)user;
    if(p) { assert(h->live);h->live--;free(p); }
}
static int discard(void *user,const unsigned char *p,size_t n)
{ (void)user;(void)p;(void)n;return 0; }
static int reject(void *user,const unsigned char *p,size_t n)
{ (void)user;(void)p;(void)n;return -1; }
static lc_status encode(int kind,lc_context *ctx,const lc_image *image,const lc_sink *sink)
{
    if(kind==0) return lc_write_png(ctx,image,sink);
    if(kind==1) return lc_write_sixel(ctx,image,sink);
    return lc_write_kitty_ex(ctx,image,sink,1,kind==3);
}
int main(void)
{
    unsigned char pixels[16*16];
    lc_image image,huge;
    lc_context *ctx;
    lc_sink sink;
    lc_allocator allocator;
    lc_cell cell;
    lc_status status;
    heap h;
    size_t fail,i;
    int kind;
    for(i=0;i<sizeof pixels;i++) pixels[i]=(unsigned char)(i&15);
    image.pixels=pixels;image.width=16;image.height=16;image.stride=16;
    sink.user=NULL;sink.write=discard;
    assert(lc_context_create(NULL,&ctx)==LC_OK);
    huge=image;huge.width=INT_MAX;huge.height=INT_MAX;huge.stride=(size_t)INT_MAX;
    assert(lc_write_png(ctx,&huge,&sink)==LC_EOVERFLOW);
    assert(lc_write_kitty_ex(ctx,&huge,&sink,1,0)==LC_EOVERFLOW);
    assert(lc_write_kitty_ex(ctx,&huge,&sink,1,1)==LC_EOVERFLOW);
    memset(&cell,0,sizeof cell);
    assert(lc_write_cells(&cell,1,1,(size_t)-1/sizeof cell+1,0,0,0,&sink)==LC_EOVERFLOW);
    sink.write=reject;
    for(kind=0;kind<4;kind++) assert(encode(kind,ctx,&image,&sink)==LC_EIO);
    lc_context_destroy(ctx);
    allocator.user=&h;allocator.alloc=allocate;allocator.free=release;sink.write=discard;
    for(kind=0;kind<4;kind++) for(fail=0;fail<20;fail++) {
        h.calls=0;h.fail_at=fail;h.live=0;
        if(lc_context_create(&allocator,&ctx)==LC_OK) {
            status=encode(kind,ctx,&image,&sink);
            assert(status==LC_OK || status==LC_ENOMEM);
            lc_context_destroy(ctx);
        }
        assert(h.live==0);
    }
    puts("output geometry, failed sinks and 80 allocation-failure positions passed");
    return 0;
}
