#ifndef CHARTS_APP_SUPVAN_H
#define CHARTS_APP_SUPVAN_H
#include <stddef.h>

#define SV_HEAD 384
#define SV_BPL 48
#define SV_BUFFER 4096
#define SV_ROWS_PER_BUFFER 32
#define SV_MAX_ROWS 2040

/* Exact-size LZMA1-alone, literal-only range coder, no end marker.
   Returns 0 if capacity is insufficient; never emits a partial usable stream. */
size_t sv_lzma(const unsigned char *input,size_t size,unsigned char *out,size_t capacity);
/* Input is head-width, row-major, LSB-first: one bit per heated dot. */
void sv_buffer(unsigned char out[SV_BUFFER],const unsigned char *rows,
               unsigned count,int first,int last);
unsigned long sv_le32(const unsigned char *p);
void sv_put32(unsigned char *p,unsigned long n);
#endif
