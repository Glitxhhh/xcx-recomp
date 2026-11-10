#define _POSIX_C_SOURCE 200809L
#include "mods.h"
#include "mod_settings.h"
#include "platform/platform.h"
#include <stdatomic.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
extern int test_threads_created,test_threads_joined;
extern atomic_int test_threads_finished;
void rt_log(const char *fmt,...) {va_list ap;va_start(ap,fmt);vprintf(fmt,ap);va_end(ap);puts("");}
int main(int argc,char **argv) {
    if(argc!=2 || setenv("XCX_MOD_DIR",argv[1],1))return 2;
    rt_mods_start();
    if(getenv("MOD_RUNTIME_FAIL"))for(int i=0;i<100 && !atomic_load(&test_threads_finished);++i)plat_sleep_ms(1);
    printf("finished before shutdown %d\n",atomic_load(&test_threads_finished));
    ModSnapshot *s=malloc(sizeof *s);mod_ui_snapshot(s);
    for(int i=0;i<s->count;++i)printf("startup %s %d %s\n",s->mods[i].id,s->mods[i].status,s->mods[i].diagnostic);
    free(s);rt_mods_shutdown();rt_mods_shutdown();rt_mods_start();
    printf("threads %d %d\n",test_threads_created,test_threads_joined);return 0;
}
