// Local standalone persistence adapter; the game uses its existing atomic-write implementation.
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
void plat_file_mkdir_p(const char *path) {mkdir(path,0700);}
int plat_file_write_atomic(const char *path,const void *data,size_t n) {
    if(getenv("MOD_SAVE_FAIL"))return 0;
    char tmp[4096];if(snprintf(tmp,sizeof tmp,"%s.tmp.XXXXXX",path)>=(int)sizeof tmp)return 0;
    int fd=mkstemp(tmp);if(fd<0)return 0;
    FILE *f=fdopen(fd,"wb");if(!f){close(fd);unlink(tmp);return 0;}
    int ok=fwrite(data,1,n,f)==n && !fflush(f) && !fsync(fd);if(fclose(f))ok=0;
    if(ok)ok=!rename(tmp,path);if(!ok)unlink(tmp);return ok;
}
