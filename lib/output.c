#include "internal.h"

/* Output is deliberately callback based: codecs do not know about files,
 * terminals, sockets, or platform framebuffer APIs. */
static lc_status image_status(const lc_image *im)
{
    if(!im || !im->pixels || im->width<=0 || im->height<=0 || im->stride<(size_t)im->width) return LC_EINVAL;
    if((size_t)im->height>1U && im->stride>((size_t)-1-(size_t)im->width)/((size_t)im->height-1U)) return LC_EOVERFLOW;
    return LC_OK;
}
static int out_bytes(const lc_sink *sink,const unsigned char *p,size_t n)
{
    if (n==0) return 0;
    if (!sink || !sink->write) return -1;
    return sink->write(sink->user,p,n);
}
static int out_text(const lc_sink *sink,const char *s)
{
    size_t n=0;
    while (s[n]) n++;
    return out_bytes(sink,(const unsigned char *)s,n);
}
static int out_char(const lc_sink *sink,int c)
{
    unsigned char b=(unsigned char)c;
    return out_bytes(sink,&b,1);
}
static int out_uint(const lc_sink *sink,unsigned long n)
{
    char b[3*sizeof(unsigned long)+1]; int i=0;
    do { b[i++]=(char)('0'+n%10UL); n/=10UL; } while(n && i<(int)sizeof(b));
    while(i) if(out_char(sink,b[--i])!=0) return -1;
    return 0;
}
static void put_be32(unsigned char *p,unsigned long n)
{
    p[0]=(unsigned char)(n>>24); p[1]=(unsigned char)(n>>16);
    p[2]=(unsigned char)(n>>8); p[3]=(unsigned char)n;
}
static unsigned long crc_update(unsigned long c,const unsigned char *p,size_t n)
{
    static const unsigned long table[16]={
        0x00000000UL,0x1db71064UL,0x3b6e20c8UL,0x26d930acUL,
        0x76dc4190UL,0x6b6b51f4UL,0x4db26158UL,0x5005713cUL,
        0xedb88320UL,0xf00f9344UL,0xd6d6a3e8UL,0xcb61b38cUL,
        0x9b64c2b0UL,0x86d3d2d4UL,0xa00ae278UL,0xbdbdf21cUL
    };
    size_t i;
    for(i=0;i<n;i++) {
        c^=p[i]; c=(c>>4)^table[c&15UL]; c=(c>>4)^table[c&15UL];
    }
    return c;
}
static unsigned long adler32_data(const unsigned char *p,size_t n)
{
    unsigned long a=1,b=0; size_t i,chunk;
    /* 5552 bounds the worst-case sum below 2^32, even on a 32-bit long. */
    while(n) {
        chunk=n>5552U ? 5552U : n;
        for(i=0;i<chunk;i++) { a+=p[i]; b+=a; }
        a%=65521UL; b%=65521UL; p+=chunk; n-=chunk;
    }
    return (b<<16)|a;
}

/* Fixed Huffman deflate, with the original short hash chain (24 candidates)
 * and 32 KiB window. This keeps the encoder deterministic and dependency-free. */
typedef struct lc_bitwriter { unsigned char *p; size_t cap,n; unsigned long acc; int bits; int failed; } lc_bitwriter;
static void bw_bits(lc_bitwriter *w,unsigned long v,int n)
{
    w->acc |= v << w->bits; w->bits+=n;
    while(w->bits>=8) { if(w->n>=w->cap) { w->failed=1; return; }
        w->p[w->n++]=(unsigned char)w->acc; w->acc>>=8; w->bits-=8; }
}
static void bw_huff(lc_bitwriter *w,unsigned long code,int n)
{
    unsigned long r=0; int i;
    for(i=0;i<n;i++) r|=((code>>i)&1UL)<<(n-1-i);
    bw_bits(w,r,n);
}
static void bw_symbol(lc_bitwriter *w,int s)
{
    if(s<144) bw_huff(w,(unsigned long)(0x30+s),8);
    else if(s<256) bw_huff(w,(unsigned long)(0x190+s-144),9);
    else if(s<280) bw_huff(w,(unsigned long)(s-256),7);
    else bw_huff(w,(unsigned long)(0xc0+s-280),8);
}
static const int lc_len_base[29]={3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const int lc_len_extra[29]={0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const int lc_dist_base[30]={1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const int lc_dist_extra[30]={0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
static void bw_match(lc_bitwriter *w,int len,int dist)
{
    int li=28,di=29;
    while(li>0 && lc_len_base[li]>len) li--;
    bw_symbol(w,257+li); if(lc_len_extra[li]) bw_bits(w,(unsigned long)(len-lc_len_base[li]),lc_len_extra[li]);
    while(di>0 && lc_dist_base[di]>dist) di--;
    bw_huff(w,(unsigned long)di,5); if(lc_dist_extra[di]) bw_bits(w,(unsigned long)(dist-lc_dist_base[di]),lc_dist_extra[di]);
}
static unsigned int hash3(const unsigned char *p,int i)
{ return (unsigned int)(((unsigned int)p[i]<<10)^((unsigned int)p[i+1]<<5)^p[i+2])&32767U; }
static int deflate_fixed(lc_context *ctx,const unsigned char *in,size_t n,unsigned char **encoded,size_t *encoded_n)
{
    int *head,*prev; size_t cap,prev_bytes,j; lc_bitwriter w; int i;
    if(n>(size_t)INT_MAX) return 0;
    if(!lc_size_mul(n,2,&cap) || cap>((size_t)-1)-64 || !lc_size_mul(32768U,sizeof(int),&prev_bytes)) return 0;
    cap+=64;
    w.p=(unsigned char *)lc_alloc(ctx,cap); head=(int *)lc_alloc(ctx,32768U*sizeof(int));
    prev=(int *)lc_alloc(ctx,prev_bytes);
    if(!w.p || !head || !prev) { lc_free(ctx,w.p); lc_free(ctx,head); lc_free(ctx,prev); return 0; }
    for(j=0;j<32768U;j++) { head[j]=-1; prev[j]=-1; }
    w.cap=cap; w.n=0; w.acc=0; w.bits=0; w.failed=0;
    bw_bits(&w,1,1); bw_bits(&w,1,2);
    i=0;
    while(i<(int)n && !w.failed) {
        int best=0,bdist=0,step=1;
        if(i<(int)n-2) {
            int cand=head[hash3(in,i)],limit=(int)((n-(size_t)i)>258U?258U:n-(size_t)i),chain;
            for(chain=0;cand>=0 && i-cand<=32768 && chain<24;chain++) {
                int l=0; while(l<limit && in[cand+l]==in[i+l]) l++;
                if(l>best) { best=l; bdist=i-cand; if(l==limit) break; }
                cand=prev[cand&32767];
            }
        }
        if(best>=3) { bw_match(&w,best,bdist); step=best; }
        else bw_symbol(&w,in[i]);
        while(step-- && !w.failed) {
            if(i<(int)n-2) { unsigned int h=hash3(in,i); prev[i&32767]=head[h]; head[h]=i; }
            i++;
        }
    }
    bw_symbol(&w,256);
    if(w.bits>0 && !w.failed) { if(w.n>=w.cap) w.failed=1; else w.p[w.n++]=(unsigned char)w.acc; }
    lc_free(ctx,head); lc_free(ctx,prev);
    if(w.failed) { lc_free(ctx,w.p); return 0; }
    *encoded=w.p; *encoded_n=w.n; return 1;
}
static int png_chunk(const lc_sink *sink,const char type[4],const unsigned char *data,size_t n)
{
    unsigned char len[4],crc[4]; unsigned long c;
    put_be32(len,(unsigned long)n);
    if(out_bytes(sink,len,4)!=0 || out_bytes(sink,(const unsigned char *)type,4)!=0) return -1;
    if(n && out_bytes(sink,data,n)!=0) return -1;
    c=crc_update(0xffffffffUL,(const unsigned char *)type,4);
    c=crc_update(c,data,n)^0xffffffffUL; put_be32(crc,c);
    return out_bytes(sink,crc,4);
}
lc_status lc_write_png(lc_context *ctx,const lc_image *im,const lc_sink *sink)
{
    static const unsigned char sig[8]={137,80,78,71,13,10,26,10};
    unsigned char ihdr[13],palette[48]; unsigned char *raw,*def,*z; size_t row,raw_n,def_n,z_n,y,x; int i,k; unsigned long a;
    if(!ctx || !sink || !sink->write) return LC_EINVAL;
    { lc_status vst=image_status(im); if(vst!=LC_OK) return vst; }
    row=((size_t)im->width+1U)/2U;
    if(row==((size_t)-1) || !lc_size_mul(row+1U,(size_t)im->height,&raw_n) || raw_n>(size_t)INT_MAX) return LC_EOVERFLOW;
    raw=(unsigned char *)lc_alloc(ctx,raw_n); if(!raw) return LC_ENOMEM;
    for(y=0;y<(size_t)im->height;y++) {
        const unsigned char *src=im->pixels+y*im->stride; unsigned char *dst=raw+y*(row+1U); dst[0]=0;
        for(x=0;x<(size_t)im->width;x+=2U) dst[1U+x/2U]=(unsigned char)(((src[x]&15U)<<4)|((x+1U<(size_t)im->width)?(src[x+1U]&15U):0));
    }
    if(!deflate_fixed(ctx,raw,raw_n,&def,&def_n)) { lc_free(ctx,raw); return lc_context_status(ctx)==LC_ENOMEM?LC_ENOMEM:LC_EOVERFLOW; }
    if(def_n>((size_t)-1)-6U) { lc_free(ctx,raw);lc_free(ctx,def);return LC_EOVERFLOW; }
    z_n=def_n+6U; z=(unsigned char *)lc_alloc(ctx,z_n); if(!z) {lc_free(ctx,raw);lc_free(ctx,def);return LC_ENOMEM;}
    z[0]=0x78; z[1]=0x01; memcpy(z+2,def,def_n); a=adler32_data(raw,raw_n); put_be32(z+2+def_n,a);
    put_be32(ihdr,(unsigned long)im->width); put_be32(ihdr+4,(unsigned long)im->height); ihdr[8]=4; ihdr[9]=3; ihdr[10]=0;ihdr[11]=0;ihdr[12]=0;
    for(i=0;i<16;i++) for(k=0;k<3;k++) palette[i*3+k]=lc_vga_rgb[i][k];
    i=out_bytes(sink,sig,8);
    if(!i) i=png_chunk(sink,"IHDR",ihdr,13);
    if(!i) i=png_chunk(sink,"PLTE",palette,48);
    if(!i) i=png_chunk(sink,"IDAT",z,z_n);
    if(!i) i=png_chunk(sink,"IEND",NULL,0);
    lc_free(ctx,raw);lc_free(ctx,def);lc_free(ctx,z);
    return i?LC_EIO:LC_OK;
}

lc_status lc_write_sixel(lc_context *ctx,const lc_image *im,const lc_sink *sink)
{
    int y0,rows,y,x,c,first,end,run; unsigned long present; unsigned char *six;
    if(!ctx || !sink || !sink->write) return LC_EINVAL;
    { lc_status vst=image_status(im); if(vst!=LC_OK) return vst; }
    six=(unsigned char *)lc_alloc(ctx,(size_t)im->width); if(!six) return LC_ENOMEM;
    if(out_text(sink,"\033P0;1;0q\"1;1;") || out_uint(sink,(unsigned long)im->width) || out_char(sink,';') || out_uint(sink,(unsigned long)im->height)) { lc_free(ctx,six); return LC_EIO; }
    for(c=0;c<16;c++) {
        const unsigned char *rgb=lc_vga_rgb[c];
        if(out_char(sink,'#') || out_uint(sink,(unsigned long)c) || out_text(sink,";2;") ||
           out_uint(sink,(unsigned long)rgb[0]*100UL/255UL) || out_char(sink,';') ||
           out_uint(sink,(unsigned long)rgb[1]*100UL/255UL) || out_char(sink,';') ||
           out_uint(sink,(unsigned long)rgb[2]*100UL/255UL)) { lc_free(ctx,six); return LC_EIO; }
    }
    for(y0=0;y0<im->height;) {
        rows=im->height-y0; if(rows>6) rows=6; present=0;
        for(y=0;y<rows;y++) { const unsigned char *row=im->pixels+(size_t)(y0+y)*im->stride; for(x=0;x<im->width;x++) present|=1UL<<(row[x]&15); }
        first=1;
        for(c=0;c<16;c++) if(present&(1UL<<c)) {
            memset(six,0,(size_t)im->width);
            for(y=0;y<rows;y++) { const unsigned char *row=im->pixels+(size_t)(y0+y)*im->stride; for(x=0;x<im->width;x++) if((row[x]&15)==c) six[x]=(unsigned char)(six[x]|(1U<<y)); }
            if(!first && out_char(sink,'$')) { lc_free(ctx,six); return LC_EIO; }
            first=0;
            if(out_char(sink,'#') || out_uint(sink,(unsigned long)c)) { lc_free(ctx,six); return LC_EIO; }
            end=im->width; while(end>0 && six[end-1]==0) end--;
            for(x=0;x<end;) {
                run=1; while(x+run<end && six[x+run]==six[x]) run++;
                if(run>3) { if(out_char(sink,'!') || out_uint(sink,(unsigned long)run) || out_char(sink,63+six[x])) { lc_free(ctx,six); return LC_EIO; } }
                else { int k; for(k=0;k<run;k++) if(out_char(sink,63+six[x])) { lc_free(ctx,six); return LC_EIO; } }
                x+=run;
            }
        }
        if(out_char(sink,'-')) { lc_free(ctx,six); return LC_EIO; }
        y0+=rows;
    }
    lc_free(ctx,six);
    return out_text(sink,"\033\\\033[?2026l")?LC_EIO:LC_OK;
}

/* A bounded sink is used only by Kitty's PNG mode; its exact upper bound is
 * calculated before allocation and prevents realloc/copy churn. */
typedef struct lc_mem_sink { unsigned char *p; size_t cap,n; } lc_mem_sink;
static int memory_write(void *user,const unsigned char *p,size_t n)
{
    lc_mem_sink *m=(lc_mem_sink *)user;
    if(n>m->cap-m->n) return -1;
    memcpy(m->p+m->n,p,n); m->n+=n; return 0;
}
static lc_status make_zlib(lc_context *ctx,const unsigned char *raw,size_t raw_n,unsigned char **z,size_t *z_n)
{
    unsigned char *def; size_t def_n;
    unsigned long a;
    if(!deflate_fixed(ctx,raw,raw_n,&def,&def_n)) return lc_context_status(ctx)==LC_ENOMEM ? LC_ENOMEM : LC_EOVERFLOW;
    if(def_n>(size_t)-1-6U) { lc_free(ctx,def); return LC_EOVERFLOW; }
    *z_n=def_n+6U; *z=(unsigned char *)lc_alloc(ctx,*z_n);
    if(!*z) { lc_free(ctx,def); return LC_ENOMEM; }
    (*z)[0]=0x78; (*z)[1]=0x01; memcpy(*z+2,def,def_n);
    a=adler32_data(raw,raw_n); put_be32(*z+2+def_n,a); lc_free(ctx,def); return LC_OK;
}
static void base64_group(const unsigned char *p,size_t n,char *out)
{
    static const char tab[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned long v=(unsigned long)p[0]<<16;
    if(n>1) v|=(unsigned long)p[1]<<8;
    if(n>2) v|=p[2];
    out[0]=tab[(v>>18)&63]; out[1]=tab[(v>>12)&63];
    out[2]=n>1?tab[(v>>6)&63]:'='; out[3]=n>2?tab[v&63]:'=';
}
static lc_status kitty_payload(lc_context *ctx,const lc_image *im,int raw,unsigned char **p,size_t *n)
{
    if(raw) {
        unsigned char *rgb; size_t pixels,rgb_n,x,y; lc_status status;
        if(!lc_size_mul((size_t)im->width,(size_t)im->height,&pixels) || !lc_size_mul(pixels,3,&rgb_n) || rgb_n>(size_t)INT_MAX) return LC_EOVERFLOW;
        rgb=(unsigned char *)lc_alloc(ctx,rgb_n); if(!rgb) return LC_ENOMEM;
        for(y=0;y<(size_t)im->height;y++) {
            const unsigned char *src=im->pixels+y*im->stride;
            for(x=0;x<(size_t)im->width;x++) memcpy(rgb+((y*(size_t)im->width+x)*3),lc_vga_rgb[src[x]&15],3);
        }
        status=make_zlib(ctx,rgb,rgb_n,p,n);
        lc_free(ctx,rgb); return status;
    } else {
        size_t row=((size_t)im->width+1U)/2U,raw_n,def_cap,png_cap;
        lc_mem_sink mem; lc_sink sink; lc_status status;
        if(!lc_size_mul(row+1U,(size_t)im->height,&raw_n) || raw_n>(size_t)INT_MAX || !lc_size_mul(raw_n,2,&def_cap) || def_cap>(size_t)-1-256U) return LC_EOVERFLOW;
        png_cap=def_cap+256U;
        mem.p=(unsigned char *)lc_alloc(ctx,png_cap); if(!mem.p) return LC_ENOMEM; mem.cap=png_cap; mem.n=0;
        sink.user=&mem; sink.write=memory_write;
        status=lc_write_png(ctx,im,&sink);
        if(status!=LC_OK) { lc_free(ctx,mem.p); return status; }
        *p=mem.p; *n=mem.n; return LC_OK;
    }
}
lc_status lc_write_kitty_ex(lc_context *ctx,const lc_image *im,const lc_sink *sink,int id,int raw)
{
    unsigned char *payload; size_t n,b64_n,off,chunkbytes,chars,used;
    char encoded[4096]; int i;
    if(!ctx || !sink || !sink->write || id<1 || id>255) return LC_EINVAL;
    { lc_status vst=image_status(im); if(vst!=LC_OK) return vst; }
    if(raw!=0 && raw!=1) return LC_EINVAL;
    { lc_status st=kitty_payload(ctx,im,raw,&payload,&n); if(st!=LC_OK) return st; }
    if(n>(size_t)-1-2U || (n+2U)/3U>((size_t)-1)/4U) { lc_free(ctx,payload); return LC_EOVERFLOW; }
    b64_n=((n+2U)/3U)*4U;
    off=0;
    do {
        size_t input_off=(off/4U)*3U;
        chunkbytes=n-input_off; if(chunkbytes>3072U) chunkbytes=3072U;
        chars=((chunkbytes+2U)/3U)*4U;
        if(chars>sizeof(encoded)) { lc_free(ctx,payload); return LC_EOVERFLOW; }
        used=0;
        for(i=0;(size_t)i<chunkbytes;i+=3) {
            size_t take=chunkbytes-(size_t)i; if(take>3U) take=3U;
            base64_group(payload+input_off+(size_t)i,take,encoded+used); used+=4U;
        }
        if(chars && out_text(sink,"\033_G")) { lc_free(ctx,payload); return LC_EIO; }
        if(off==0) {
            if(out_text(sink,raw?"a=T,f=24,o=z,s=":"a=T,f=100,i=") || (raw && out_uint(sink,(unsigned long)im->width)) ||
               (raw && (out_text(sink,",v=") || out_uint(sink,(unsigned long)im->height))) || out_text(sink,raw?",i=":"" ) ||
               out_uint(sink,(unsigned long)id) || out_text(sink,",p=1,q=2,C=1,")) { lc_free(ctx,payload); return LC_EIO; }
        }
        if(out_text(sink,(off+chars<b64_n)?"m=1;":"m=0;") || out_bytes(sink,(const unsigned char *)encoded,chars) || out_text(sink,"\033\\")) { lc_free(ctx,payload); return LC_EIO; }
        off+=chars;
    } while(off<b64_n || off==0);
    lc_free(ctx,payload);
    return LC_OK;
}
lc_status lc_write_kitty(lc_context *ctx,const lc_image *im,const lc_sink *sink)
{ return lc_write_kitty_ex(ctx,im,sink,1,0); }

static int sgr_fg_sink(const lc_sink *s,int n)
{
    if(n<8) { if(out_text(s,"3")) return -1; return out_char(s,'0'+n); }
    if(out_text(s,"1;3")) return -1;
    return out_char(s,'0'+n-8);
}
static int sgr_bg_sink(const lc_sink *s,int n)
{
    if(n<8) { if(out_text(s,"4")) return -1; return out_char(s,'0'+n); }
    if(out_text(s,"10")) return -1;
    return out_char(s,'0'+n-8);
}
lc_status lc_write_cells(const lc_cell *cells,int cols,int rows,size_t stride,int color,int crlf,int bright_bg,const lc_sink *sink)
{
    int y,x,last,fg,bg; char utf8[5]; size_t n;
    size_t span;
    if(!sink || !sink->write || cols<0 || rows<0 || stride<(size_t)cols || (cols && rows && !cells)) return LC_EINVAL;
    if(!lc_size_mul((size_t)rows,stride,&span) || !lc_size_mul(span,sizeof *cells,&span)) return LC_EOVERFLOW;
    (void)span;
    for(y=0;y<rows;y++) {
        last=cols-1;
        while(last>=0) { const lc_cell *cl=&cells[(size_t)y*stride+last]; if(cl->ch!=' ' || cl->bg!=LC_BG_NONE) break; last--; }
        fg=-1; bg=-1;
        for(x=0;x<=last;x++) {
            const lc_cell *cl=&cells[(size_t)y*stride+x];
            if(color && (cl->fg!=fg || cl->bg!=bg)) {
                if(out_text(sink,"\033[0;") || sgr_fg_sink(sink,cl->fg&15) || (cl->bg!=LC_BG_NONE && (out_char(sink,';') || sgr_bg_sink(sink,bright_bg?(cl->bg&15):(cl->bg&7)))) || out_char(sink,'m')) return LC_EIO;
                fg=cl->fg; bg=cl->bg;
            }
            n=lc_utf8_encode(lc_cell_of(cl->ch),utf8); if(n && out_bytes(sink,(const unsigned char *)utf8,n)) return LC_EIO;
        }
        if(color && fg>=0 && out_text(sink,"\033[0m")) return LC_EIO;
        if(out_text(sink,crlf?"\r\n":"\n")) return LC_EIO;
    }
    return LC_OK;
}
