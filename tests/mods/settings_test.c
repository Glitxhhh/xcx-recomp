#define _GNU_SOURCE
// Private read-error adapter around the unchanged native settings implementation.
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
static int test_settings_openat(int dir,const char *name,int flags) {
    if(getenv("MOD_OPEN_ERROR")){errno=EACCES;return -1;}
    return openat(dir,name,flags);
}
static int test_settings_stat(int dir,const char *name,struct stat *st,int flags) {
    int r=fstatat(dir,name,st,flags);
    if(!r && getenv("MOD_STAT_SPECIAL"))st->st_mode=S_IFCHR;
    return r;
}
static ssize_t test_settings_read(int fd,void *data,size_t size) {
    if(getenv("MOD_READ_ERROR")){errno=EIO;return -1;}
    if(getenv("MOD_READ_SHORT"))return 0;
    return read(fd,data,size);
}
#define read test_settings_read
#define openat test_settings_openat
#define fstatat test_settings_stat
#include "../../runtime/mod_settings.c"
