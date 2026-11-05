/* Supvan wire packing and an original C89 LZMA1 literal encoder.
 * No compression library. Matches are deliberately omitted: the decoder's
 * state remains zero, and adaptive literals compress the sparse label raster.
 * Small source buffers guarantee a bounded fallback when a whole page does
 * not fit the device's 4096-byte compressed receive buffer.
 * Wire layout: docs/PRINTER.md. Arithmetic explicitly wraps at 32 bits on
 * both ILP32 and LP64; no C99 types, long long, or compiler extensions. */
#include "supvan.h"
#include <string.h>

#define U32MASK 0xffffffffUL

typedef struct range_encoder {
    unsigned long range,low,carry,pending;
    unsigned cache;
    unsigned char *out;
    size_t used,capacity;
    int overflow;
} range_encoder;

unsigned long sv_le32(const unsigned char *p)
{
    return (unsigned long)p[0]|((unsigned long)p[1]<<8)|
           ((unsigned long)p[2]<<16)|((unsigned long)p[3]<<24);
}
void sv_put32(unsigned char *p,unsigned long n)
{
    p[0]=(unsigned char)n;p[1]=(unsigned char)(n>>8);
    p[2]=(unsigned char)(n>>16);p[3]=(unsigned char)(n>>24);
}
static void byte_out(range_encoder *r,unsigned value)
{
    if(r->used<r->capacity)r->out[r->used++]=(unsigned char)value;
    else r->overflow=1;
}
static void shift_low(range_encoder *r)
{
    unsigned temp;
    if(r->low<0xff000000UL||r->carry){
        temp=r->cache;
        do{
            byte_out(r,temp+(unsigned)r->carry);temp=255;
        }while(--r->pending);
        r->cache=(unsigned)(r->low>>24);
    }
    r->pending++;
    r->low=(r->low<<8)&U32MASK;r->carry=0;
}
static void bit_out(range_encoder *r,unsigned short *prob,unsigned bit)
{
    unsigned long bound,old;
    bound=(r->range>>11)*(unsigned long)*prob;
    if(!bit){r->range=bound;*prob=(unsigned short)(*prob+((2048-*prob)>>5));}
    else{
        old=r->low;r->low=(old+bound)&U32MASK;
        if(r->low<old)r->carry++;
        r->range-=bound;*prob=(unsigned short)(*prob-(*prob>>5));
    }
    if(r->range<0x01000000UL){r->range=(r->range<<8)&U32MASK;shift_low(r);}
}
size_t sv_lzma(const unsigned char *input,size_t size,unsigned char *out,size_t capacity)
{
    range_encoder r;
    unsigned short literal[8][256],match[4];
    unsigned i,j,context,symbol,bit,previous=0;
    size_t pos;
    if(capacity<18||size>0xffffffffUL)return 0;
    memset(&r,0,sizeof r);r.range=U32MASK;r.pending=1;
    r.out=out;r.capacity=capacity;r.used=13;
    out[0]=0x5d;sv_put32(out+1,8192);sv_put32(out+5,(unsigned long)size);
    memset(out+9,0,4);
    for(i=0;i<8;i++)for(j=0;j<256;j++)literal[i][j]=1024;
    for(i=0;i<4;i++)match[i]=1024;
    for(pos=0;pos<size&&!r.overflow;pos++){
        bit_out(&r,&match[pos&3],0);
        context=previous>>5;symbol=1;
        for(i=0;i<8;i++){
            bit=(input[pos]>>(7-i))&1;
            bit_out(&r,&literal[context][symbol],bit);symbol=(symbol<<1)|bit;
        }
        previous=input[pos];
    }
    for(i=0;i<5;i++)shift_low(&r);
    return r.overflow?0:r.used;
}
void sv_buffer(unsigned char out[SV_BUFFER],const unsigned char *rows,
               unsigned count,int first,int last)
{
    unsigned i,sum=0,end;
    memset(out,0,SV_BUFFER);
    if(!count||count>SV_ROWS_PER_BUFFER)return;
    out[2]=(unsigned char)((first?2:0)|(last?12:0));
    out[3]=0x50; /* material label, density 4, one colour */
    out[4]=(unsigned char)count;out[5]=(unsigned char)(count>>8);
    out[6]=SV_BPL;out[8]=1;out[10]=1;out[12]=4;
    memcpy(out+14,rows,(size_t)count*SV_BPL);
    end=14+count*SV_BPL;
    for(i=2;i<14;i++)sum+=out[i];
    for(i=255;i<end;i+=256)sum+=out[i];
    out[0]=(unsigned char)sum;out[1]=(unsigned char)(sum>>8);
}
