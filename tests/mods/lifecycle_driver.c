#define _POSIX_C_SOURCE 200809L
#include "mods.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void rt_log(const char *fmt,...) {
    va_list ap; va_start(ap,fmt); char line[1024]; vsnprintf(line,sizeof line,fmt,ap); va_end(ap);
    puts(line);
    if (strstr(line,"self-exit-probe")) rt_mods_shutdown();
}
static void *stop(void *p) { (void)p; rt_mods_shutdown(); return NULL; }
int main(int argc,char **argv) {
    if (argc!=2 || setenv("XCX_MOD_DIR",argv[1],1)) return 2;
    rt_mods_start();
    pthread_t a,b; pthread_create(&a,NULL,stop,NULL); pthread_create(&b,NULL,stop,NULL);
    pthread_join(a,NULL); pthread_join(b,NULL);
    rt_mods_shutdown(); rt_mods_start();
    puts("lifecycle stopped idempotently"); return 0;
}
