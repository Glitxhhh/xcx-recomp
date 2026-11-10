#include "mod_internal.h"
#include <stdio.h>
#include <string.h>
static void report(const char *s) { puts(s); }
static int cancel;
static int cancelled(void) { return cancel; }
int main(int argc,char **argv) {
    if (argc<2) return 2;
    ModPlan *plan=mod_packages_read(argv[1],report);
    if (!plan) { puts("no runtime"); return 0; }
    // Optional snapshot test: replace the file only after native validation has read it.
    if (argc==3) { FILE *f=fopen(argv[2],"wb"); if (!f) return 3; fputs("error('changed after validation')",f); fclose(f); }
    ModRuntime *runtime=mod_runtime_start(plan,report,cancelled);
    while (mod_runtime_event(runtime,cancelled)) puts("dispatch deferred");
    cancel=1;
    mod_runtime_stop(runtime); runtime=NULL;
    mod_runtime_event(runtime,cancelled); mod_runtime_stop(runtime);
    mod_packages_free(plan);
    return 0;
}
