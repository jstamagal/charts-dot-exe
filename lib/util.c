#include "internal.h"
#include <ctype.h>
#include <stdio.h>

int lc_ieq(const char *a,const char *b) {
    if (!a) a="";
    if (!b) b="";
    while (*a && *b) { if (tolower((unsigned char)*a)!=tolower((unsigned char)*b)) return 0; a++; b++; }
    return *a==*b;
}
lc_codepoint lc_utf8_next(const char **text) {
    const unsigned char *p; lc_codepoint cp,min; int extra,k;
    if (!text || !*text) return 0;
    p=(const unsigned char *)*text;
    if (!*p) return 0;
    if (*p<128) { *text+=1; return *p; }
    extra=*p>=0xf0 && *p<=0xf4 ? 3 : (*p>=0xe0 && *p<0xf0 ? 2 : (*p>=0xc2 && *p<0xe0 ? 1 : -1));
    cp=extra==3 ? (*p&7) : (extra==2 ? (*p&15) : (*p&31));
    if (extra<0) { *text+=1; return 0xfffdUL; }
    for (k=1;k<=extra;k++) {
        if ((p[k]&0xc0)!=0x80) { *text+=1; return 0xfffdUL; }
        cp=(cp<<6)|(p[k]&63);
    }
    min=extra==3 ? 0x10000UL : (extra==2 ? 0x800UL : 0x80UL);
    if (cp<min || cp>0x10ffffUL || (cp>=0xd800UL && cp<=0xdfffUL)) { *text+=1; return 0xfffdUL; }
    *text+=extra+1; return cp;
}
size_t lc_utf8_encode(lc_codepoint cp,char out[5]) {
    size_t n;
    if (cp>0x10ffffUL || (cp>=0xd800UL && cp<=0xdfffUL)) cp=0xfffdUL;
    if (cp<0x80) { out[0]=(char)cp; n=1; }
    else if (cp<0x800) { out[0]=(char)(0xc0|(cp>>6)); out[1]=(char)(0x80|(cp&63)); n=2; }
    else if (cp<0x10000UL) { out[0]=(char)(0xe0|(cp>>12)); out[1]=(char)(0x80|((cp>>6)&63)); out[2]=(char)(0x80|(cp&63)); n=3; }
    else { out[0]=(char)(0xf0|(cp>>18)); out[1]=(char)(0x80|((cp>>12)&63)); out[2]=(char)(0x80|((cp>>6)&63)); out[3]=(char)(0x80|(cp&63)); n=4; }
    out[n]=0; return n;
}
lc_codepoint lc_cell_of(lc_codepoint c) {
    if (c<32 || (c>=127 && c<160)) return ' ';
    if ((c>=0x0300 && c<=0x036f)||(c>=0x200b && c<=0x200f)||(c>=0x202a && c<=0x202e)||(c>=0x2060 && c<=0x2069)||(c>=0xfe00 && c<=0xfe0f)||c==0xfeff) return 0;
    if ((c>=0x1100 && c<=0x115f)||(c>=0x2e80 && c<=0x303e)||(c>=0x3041 && c<=0x33ff)||(c>=0x3400 && c<=0x4dbf)||(c>=0x4e00 && c<=0x9fff)||(c>=0xa000 && c<=0xa4cf)||(c>=0xac00 && c<=0xd7a3)||(c>=0xf900 && c<=0xfaff)||(c>=0xfe30 && c<=0xfe4f)||(c>=0xff00 && c<=0xff60)||(c>=0xffe0 && c<=0xffe6)||(c>=0x1f300UL && c<=0x1f64fUL)||(c>=0x1f680UL && c<=0x1f6ffUL)||(c>=0x1f900UL && c<=0x1f9ffUL)||(c>=0x1fa70UL && c<=0x1faffUL)||(c>=0x20000UL && c<=0x3fffdUL)) return '?';
    return c;
}
size_t lc_text_width(const char *text) {
    size_t n=0; lc_codepoint cp;
    if (!text) return 0;
    while (*text) { cp=lc_utf8_next(&text); if (lc_cell_of(cp)) n++; }
    return n;
}
static char *scratch_copy(lc_context *ctx,const char *s) {
    size_t n=strlen(s); char *r=(char *)lc_scratch(ctx,n+1);
    if (r) memcpy(r,s,n+1);
    return r;
}
char *lc_text_truncate(lc_context *ctx,const char *text,size_t width) {
    size_t n,cap,used,count,keep; char *out,bytes[5]; lc_codepoint cp;
    if (!text) text="";
    n=lc_text_width(text);
    if (n<=width) return scratch_copy(ctx,text);
    if (!lc_size_mul(width,4,&cap) || cap==(size_t)-1) return NULL;
    out=(char *)lc_scratch(ctx,cap+1); if (!out) return NULL;
    used=0; count=0; keep=width>1 ? width-1 : width;
    while (*text && count<keep) {
        cp=lc_utf8_next(&text); if (!lc_cell_of(cp)) continue;
        n=lc_utf8_encode(cp,bytes); memcpy(out+used,bytes,n); used+=n; count++;
    }
    if (width>1) out[used++]='.';
    out[used]=0; return out;
}
/* sprintf is bounded here by the numeric domain and DBL_MAX_10_EXP, not by
 * trusting user text. Precision is limited to the documented 0..12 range. */
static char *commafy(lc_context *ctx,double v,int prec) {
    char raw[DBL_MAX_10_EXP+40]; char *out; const char *digits,*dot;
    size_t n,i,k,total;
    if (!lc_finite(v)) return scratch_copy(ctx,"n/a");
    if (prec<0) prec=0;
    if (prec>12) prec=12;
    sprintf(raw,"%.*f",prec,v);
    digits=raw+(raw[0]=='-'); dot=strchr(digits,'.'); n=dot ? (size_t)(dot-digits) : strlen(digits);
    total=strlen(raw)+n/3+2; out=(char *)lc_scratch(ctx,total); if (!out) return NULL;
    k=0; if (raw[0]=='-') out[k++]='-';
    for (i=0;i<n;i++) { if (i && (n-i)%3==0) out[k++]=','; out[k++]=digits[i]; }
    if (dot) { i=strlen(dot); memcpy(out+k,dot,i); k+=i; }
    out[k]=0; return out;
}
char *lc_fmt_val(lc_context *ctx,double v,int prec) {
    char *s; size_t n;
    if (!lc_finite(v)) return scratch_copy(ctx,"n/a");
    if (prec>=0) return commafy(ctx,v,prec);
    if (v==floor(v) && fabs(v)<1e15) return commafy(ctx,v,0);
    s=commafy(ctx,v,fabs(v)<1 ? 3 : 2); if (!s) return NULL;
    n=strlen(s); while (n && s[n-1]=='0') s[--n]=0;
    if (n && s[n-1]=='.') s[--n]=0;
    if (!n || !strcmp(s,"-")) return scratch_copy(ctx,"0");
    return s;
}
char *lc_fmt_raw(lc_context *ctx,double v) {
    char raw[DBL_MAX_10_EXP+40];
    if (!lc_finite(v)) return scratch_copy(ctx,"");
    if (v==floor(v) && fabs(v)<1e15) sprintf(raw,"%.0f",v);
    else sprintf(raw,"%.12g",v);
    return scratch_copy(ctx,raw);
}
char *lc_fmt_axis(lc_context *ctx,double v) {
    char raw[DBL_MAX_10_EXP+40],suffix; double a,div; size_t n;
    if (!lc_finite(v)) return scratch_copy(ctx,"n/a");
    a=fabs(v); div=1; suffix=0;
    if (a>=1e9) { suffix='B'; div=1e9; } else if (a>=1e6) { suffix='M'; div=1e6; } else if (a>=1e3) { suffix='K'; div=1e3; }
    if (!suffix) return lc_fmt_val(ctx,v,-1);
    sprintf(raw,"%.2f",v/div); n=strlen(raw);
    while (n && raw[n-1]=='0') n--;
    if (n && raw[n-1]=='.') n--;
    raw[n++]=suffix; raw[n]=0; return scratch_copy(ctx,raw);
}
double lc_nice_step(double raw) {
    double mag,n,m;
    if (!(raw>0) || !lc_finite(raw)) return 1;
    mag=pow(10.0,floor(log10(raw))); if (!(mag>0)) return raw;
    n=raw/mag; m=n<=1 ? 1 : (n<=2 ? 2 : (n<=2.5 ? 2.5 : (n<=5 ? 5 : 10)));
    return m*mag;
}

char **lc_text_wrap(lc_context *ctx,const char *text,size_t width,size_t *count) {
    char **lines,*line,*word,*copy,*start,*p; size_t bytes,n,used,nlines,word_bytes,word_width,line_width,take,capacity;
    const char *scan; lc_codepoint cp; char encoded[5];
    if (!count) return NULL;
    *count=0; if (!text) text=""; if (!width) width=1;
    n=strlen(text);
    /* Each nonempty line consumes at least one input byte. */
    if (n>(size_t)-1-2 || !lc_size_mul(n+2,sizeof *lines,&bytes)) return NULL;
    lines=(char **)lc_scratch(ctx,bytes); copy=scratch_copy(ctx,text);
    if (!lines || !copy) return NULL;
    if (!lc_size_mul(n,3,&capacity) || capacity>(size_t)-1-2) return NULL;
    capacity+=2; line=(char *)lc_scratch(ctx,capacity); word=(char *)lc_scratch(ctx,capacity);
    if (!line || !word) return NULL;
    line[0]=0; used=0; nlines=0; p=copy;
    for (;;) {
        while (*p==' ' || *p=='\t') p++;
        if (!*p || *p=='\n') {
            lines[nlines]=scratch_copy(ctx,line); if (!lines[nlines++]) return NULL;
            used=0; line[0]=0;
            if (!*p) break;
            p++; continue;
        }
        start=p; while (*p && *p!=' ' && *p!='\t' && *p!='\n') p++;
        word_bytes=(size_t)(p-start); memcpy(word,start,word_bytes); word[word_bytes]=0;
        word_width=lc_text_width(word); line_width=lc_text_width(line);
        if (used && (line_width>=width || word_width>width-line_width-1)) {
            lines[nlines]=scratch_copy(ctx,line); if (!lines[nlines++]) return NULL; used=0; line[0]=0;
        }
        if (used) line[used++]=' ';
        memcpy(line+used,word,word_bytes+1); used+=word_bytes;
        while (lc_text_width(line)>width) {
            lines[nlines]=lc_text_truncate(ctx,line,width); if (!lines[nlines++]) return NULL;
            scan=line; take=width>1 ? width-1 : 1;
            while (*scan && take) { cp=lc_utf8_next(&scan); if (lc_cell_of(cp)) take--; }
            used=0;
            while (*scan) {
                cp=lc_utf8_next(&scan); bytes=lc_utf8_encode(cp,encoded);
                memcpy(word+used,encoded,bytes); used+=bytes;
            }
            word[used]=0; memcpy(line,word,used+1);
        }
    }
    while (nlines>1 && !lines[nlines-1][0]) nlines--;
    *count=nlines; return lines;
}
