/* Run on DOS as well as the host: replacing an existing file must preserve it
 * until the complete new contents exist, and temporary names must fit8.3. */
#include "../app/platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int check(const char *path,const char *want,app_error *error)
{
    char *data;
    int ok;
    data=app_read_file(path,NULL,error);ok=data && !strcmp(data,want);free(data);return ok;
}
int app_file_sink(void *user,const unsigned char *p,size_t n)
{ return fwrite(p,1,n,(FILE *)user)==n ? 0 : -1; }
int main(void)
{
    app_error error;
    const char *path;
    memset(&error,0,sizeof error);
    path="CH000001.TMP";remove(path);
    if (!app_write_replace(path,"old\n",4,&error) || !check(path,"old\n",&error) ||
        !app_write_replace(path,"new\n",4,&error) || !check(path,"new\n",&error)) {
        fprintf(stderr,"replacement failed: %s\n",error.message);return 1;
    }
    remove(path);
    puts("SAVE_REPLACE_PASS");return 0;
}
