#include "sources.h"
#include "platform.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int suffix(const char *s,const char *end)
{
  size_t n=strlen(s),m=strlen(end),i;
  if(n<m)return 0;
  for(i=0;i<m;i++)if(tolower((unsigned char)s[n-m+i])!=end[i])return 0;
  return 1;
}
static int push(char ***out,size_t *count,size_t *cap,const char *path,app_error *err)
{
  char *copy=app_strdup(path,err);
  if(!copy)return 0;
  if(!app_reserve((void **)out,cap,*count+1,sizeof **out,err)){free(copy);return 0;}
  (*out)[(*count)++]=copy;return 1;
}
int app_sources_expand(const char *const *args,size_t count,char ***out,size_t *out_count,app_error *err)
{
  size_t i,j,n,cap=0;
  char **files=NULL,**here;
  *out=NULL;*out_count=0;
  for(i=0;i<count;i++){
    if(!strcmp(args[i],"-")||!app_path_is_dir(args[i])){
      if(!push(out,out_count,&cap,args[i],err))goto fail;
      continue;
    }
    here=NULL;n=0;
    if(!app_paths_in_dir(args[i],&here,&n,err)){
      if(err&&err->code)goto fail;
      if(!push(out,out_count,&cap,args[i],err))goto fail;
      continue;
    }
    for(j=0;j<n;j++){
      const char *name=strrchr(here[j],'/');
      name=name?name+1:here[j];
      if(*name!='.'&&(suffix(name,".csv")||suffix(name,".tsv")||suffix(name,".json")) &&
         !app_is_deck_file(here[j]))
        if(!push(out,out_count,&cap,here[j],err)){app_paths_free(here,n);goto fail;}
    }
    app_paths_free(here,n);
  }
  return 1;
fail:
  files=*out;n=*out_count;
  for(j=0;j<n;j++)free(files[j]);
  free(files);
  *out=NULL;*out_count=0;return 0;
}
