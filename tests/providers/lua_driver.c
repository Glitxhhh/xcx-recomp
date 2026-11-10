#include "mod_internal.h"
#include <stdio.h>
static void log_line(const char *s){puts(s);fflush(stdout);}
int main(int argc,char **argv){
    if(argc!=3)return 2;mod_ui_reset(argv[2]);ModPlan *plan=mod_packages_read(argv[1],log_line);
    if(!plan)return 0;ModRuntime *rt=mod_runtime_start(plan,log_line,NULL);
    while(mod_runtime_event(rt,NULL)){}
    mod_ui_shutdown();mod_runtime_stop(rt);mod_packages_free(plan);return 0;
}
