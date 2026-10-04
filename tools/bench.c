/* Host-only alternating before/after PNG benchmark. ISO C89 plus POSIX/BSD
 * process accounting; build: cc -O2 tools/bench.c -o /tmp/charts-bench
 * run: /tmp/charts-bench OLD_BINARY NEW_BINARY
 * Rows: warmup/iteration, implementation0or1, wall_ms, peak_RSS_KiB, CPU_ms.
 * First4iterations are warmup. Uses /tmp/libcharts-bench-png for output. */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/resource.h>
static double now(void) { struct timeval t;gettimeofday(&t,0);return t.tv_sec+t.tv_usec/1000000.0; }
int main(int argc,char **argv) {
 int round,k,child,status,fd;double start;struct rusage usage;
 if(argc!=3)return 2;
 for(round=0;round<24;round++)for(k=0;k<2;k++) {
  int choice=(round+k)%2;start=now();child=fork();
  if(child==0) {fd=open("/dev/null",O_WRONLY);dup2(fd,1);close(fd);execl(argv[choice+1],argv[choice+1],"--demo","--png-dir","/tmp/libcharts-bench-png",(char *)0);_exit(127);}
  if(child<0)return 1;
  if(wait4(child,&status,0,&usage)!=child||status)return 1;
  printf("%d %d %.3f %ld %.3f\n",round,choice,(now()-start)*1000,usage.ru_maxrss,usage.ru_utime.tv_sec*1000.0+usage.ru_utime.tv_usec/1000.0+usage.ru_stime.tv_sec*1000.0+usage.ru_stime.tv_usec/1000.0);
 }
 return 0;
}
