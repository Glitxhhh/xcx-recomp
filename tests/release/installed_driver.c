// Private entry for the packaged runtime: reserve guest memory, exercise real mod startup, never boot or draw.
#include "host.h"
#include "mods.h"
#include "mod_settings.h"
#include "mod_menu.h"
#include "platform/platform.h"
#include <stdio.h>
#include <stdlib.h>
extern int rt_mod_menu_capable(void);
extern int __real_main(int argc, char **argv);
static ModSnapshot state;
static int find(const char *id) {
    mod_ui_snapshot(&state);
    for (int i=0;i<state.count;++i) if (!strcmp(state.mods[i].id,id)) return i;
    return -1;
}
static void show(void) {
    mod_ui_snapshot(&state);
    printf("SNAPSHOT %d capability=%d\n",state.count,rt_mod_menu_capable());
    for (int i=0;i<state.count;++i) {
        ModView *v=&state.mods[i];
        printf("PACKAGE %s status=%d fields=%d value=%d dirty=%d save=%s text=%s\n",v->id,v->status,v->field_count,v->field_count?v->fields[0].value:-1,v->persistence_dirty,v->save_note,v->ui_status);
    }
    fflush(stdout);
}
int __wrap_main(int argc,char **argv) {
    if (argc==2 && !strcmp(argv[1],"--selftest")) return __real_main(argc,argv);
    if (argc<2 || (strcmp(argv[1],"status")&&strcmp(argv[1],"hold")&&strcmp(argv[1],"set")&&strcmp(argv[1],"action"))) return 2;
    mem_init(); rt_mods_start(); show(); int result=0;
    if (!strcmp(argv[1],"hold")) {
        char line[32]; puts("READY");fflush(stdout);
        while(fgets(line,sizeof line,stdin)) {if(!strncmp(line,"quit",4))break;show();}
    } else if (!strcmp(argv[1],"set") || !strcmp(argv[1],"action")) {
        if(argc!=4){result=2;goto done;}
        int n=find(argv[2]);if(n<0){result=3;goto done;}mod_ui_interaction(1);mod_ui_snapshot(&state);
        if(!strcmp(argv[1],"set")) {
            ModField *f=&state.mods[n].fields[0];ModAction a={0};a.generation=state.generation;a.session=state.session;a.type=f->type;a.value=atoi(argv[3]);snprintf(a.mod,sizeof a.mod,"%s",argv[2]);snprintf(a.field,sizeof a.field,"%s",f->id);
            if(!rt_mods_ui_submit(&a)){result=4;goto done;}
        } else {
            ModIntent a={0};a.generation=state.generation;a.session=state.session;snprintf(a.mod,sizeof a.mod,"%s",argv[2]);snprintf(a.action,sizeof a.action,"%s",argv[3]);if(!rt_mods_ui_intent(&a)){result=4;goto done;}
        }
        int ready=0;
        for(int tries=0;tries<400&&!ready;++tries) {
            plat_sleep_ms(5);n=find(argv[2]);
            if(n>=0)ready=!strcmp(argv[1],"set")?(!strcmp(state.mods[n].save_note,"Saved")&&state.mods[n].fields[0].value==atoi(argv[3])):strstr(state.mods[n].ui_status,"Clicks this run: 1")!=NULL||strstr(state.mods[n].ui_status,"ok: Action completed")!=NULL;
        }
        if(!ready)result=5;show();
    }
 done:
    rt_mods_shutdown();rt_mods_shutdown();puts("SHUTDOWN clean");return result;
}
