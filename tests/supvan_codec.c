#include "supvan.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc,char **argv)
{
    unsigned char *input,*out;
    size_t count,n,capacity;
    unsigned i,sum;
    unsigned char rows[SV_ROWS_PER_BUFFER*SV_BPL],buffer[SV_BUFFER];
    if(argc==2&&!strcmp(argv[1],"encode")){
        input=(unsigned char *)malloc(262144);
        out=(unsigned char *)malloc(524288);
        if(!input||!out){free(input);free(out);return 1;}
        count=fread(input,1,262144,stdin);
        if(ferror(stdin)||(!feof(stdin)&&getchar()!=EOF)){free(input);free(out);return 1;}
        n=sv_lzma(input,count,out,524288);
        if(!n||fwrite(out,1,n,stdout)!=n){free(input);free(out);return 1;}
        free(input);free(out);return 0;
    }
    memset(rows,0xa5,sizeof rows);
    sv_buffer(buffer,rows,32,1,1);
    if(buffer[2]!=14||buffer[3]!=0x50||buffer[4]!=32||buffer[6]!=48)return 1;
    sum=0;for(i=2;i<14;i++)sum+=buffer[i];
    for(i=255;i<14+sizeof rows;i+=256)sum+=buffer[i];
    if(buffer[0]!=(sum&255)||buffer[1]!=(sum>>8))return 1;
    if(memcmp(buffer+14,rows,sizeof rows))return 1;
    for(i=14+sizeof rows;i<SV_BUFFER;i++)if(buffer[i])return 1;
    for(capacity=0;capacity<18;capacity++)
        if(sv_lzma(rows,sizeof rows,buffer,capacity))return 1;
    puts("Supvan buffer flags, checksum, padding and capacity checks passed");
    return 0;
}
