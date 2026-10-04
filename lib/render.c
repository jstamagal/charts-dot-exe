#include "render.h"

void *lc_render_array(lc_scene *sc,size_t count,size_t size)
{
    size_t bytes;
    void *p;
    if (!lc_size_mul(count,size,&bytes)) { lc_scene_fail(sc,LC_EOVERFLOW); return NULL; }
    p=lc_scratch(sc->ctx,bytes ? bytes : 1);
    if (!p) lc_scene_fail(sc,LC_ENOMEM);
    else memset(p,0,bytes);
    return p;
}

char *lc_render_join(lc_scene *sc,const char *a,const char *b,const char *c)
{
    size_t na,nb,nc,n;
    char *p;
    if (!a) a="";
    if (!b) b="";
    if (!c) c="";
    na=strlen(a); nb=strlen(b); nc=strlen(c);
    if (na>(size_t)-1-nb || na+nb>(size_t)-1-nc-1) {
        lc_scene_fail(sc,LC_EOVERFLOW); return NULL;
    }
    n=na+nb+nc;
    p=(char *)lc_render_array(sc,n+1,1);
    if (p) { memcpy(p,a,na); memcpy(p+na,b,nb); memcpy(p+na+nb,c,nc); p[n]=0; }
    return p;
}

char *lc_render_uint(lc_scene *sc,size_t value)
{
    char b[3*sizeof(size_t)+1];
    size_t n;
    n=sizeof b-1; b[n]=0;
    do { b[--n]=(char)('0'+value%10); value/=10; } while (value);
    return lc_render_join(sc,b+n,"","");
}

lc_rect lc_rect_make(int x,int y,int w,int h)
{
    lc_rect r;
    r.x=x; r.y=y; r.w=w; r.h=h;
    return r;
}

int lc_name_eq(const char *a,const char *b)
{
    const char *ae,*be;
    if (!a) a="";
    if (!b) b="";
    while (isspace((unsigned char)*a)) ++a;
    while (isspace((unsigned char)*b)) ++b;
    ae=a+strlen(a); be=b+strlen(b);
    while (ae>a && isspace((unsigned char)ae[-1])) --ae;
    while (be>b && isspace((unsigned char)be[-1])) --be;
    if (ae-a!=be-b) return 0;
    while (a<ae) if (tolower((unsigned char)*a++)!=tolower((unsigned char)*b++)) return 0;
    return 1;
}

lc_lines lc_render_wrap(lc_scene *sc,const char *text,size_t width)
{
    lc_lines out;
    out.n=0;
    out.v=(const char **)lc_text_wrap(sc->ctx,text,width,&out.n);
    if (!out.v) lc_scene_fail(sc,lc_context_status(sc->ctx)==LC_EOVERFLOW ? LC_EOVERFLOW : LC_ENOMEM);
    return out;
}
