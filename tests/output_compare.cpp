/* The old C++ encoder is an oracle only; normal library builds need no C++. */
#include "../include/charts.h"
#include "../src/gfx.hpp"
#include <string>
#include <cstdio>
#include <cstdlib>
static int append(void *user,const unsigned char *p,size_t n) {
    static_cast<std::string *>(user)->append(reinterpret_cast<const char *>(p),n);
    return 0;
}
int main() {
    lc_context *ctx;
    if(lc_context_create(NULL,&ctx)!=LC_OK) return 1;
    unsigned long seed=947;
    for(int k=0;k<50;k++) {
        int w=k==0 ? 1 : 31+k*13,h=k==0 ? 1 : 17+k*11;
        ch::Image original(w,h,0);
        for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
            seed=(seed*1664525UL+1013904223UL)&0xffffffffUL;
            original.px[static_cast<size_t>(y)*w+x]=k%3==0 ? ((x+y)&15) : (k%3==1 ? (seed>>28)&15 : (x/40+y/30)&15);
        }
        lc_image image;image.pixels=original.px.data();image.width=w;image.height=h;image.stride=w;
        std::string actual;lc_sink sink;sink.user=&actual;sink.write=append;
        if(lc_write_png(ctx,&image,&sink)!=LC_OK || actual!=ch::png_encode(original)) {
            std::fprintf(stderr,"PNG mismatch case %d (%dx%d)\n",k,w,h);return 1;
        }
    }
    lc_context_destroy(ctx);std::puts("output: 50 byte-exact PNG comparisons passed");return 0;
}
