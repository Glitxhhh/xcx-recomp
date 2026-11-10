#include "mod_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void report(const char *s){puts(s);}
static void dump(void) {
    ModSnapshot *s=malloc(sizeof *s);mod_ui_snapshot(s);
    printf("snapshot pending=%d\n",s->pending);
    for(int i=0;i<s->count;++i){ModView *m=&s->mods[i];printf("view %s %d %d\n",m->id,m->status,m->field_count);
        if(m->diagnostic[0])printf("diagnostic %s %s\n",m->id,m->diagnostic);
        printf("dirty %s %d\n",m->id,m->persistence_dirty);
        if(m->save_note[0])printf("save %s %s\n",m->id,m->save_note);
        for(int j=0;j<m->field_count;++j)printf("field %s %s %d %d\n",m->id,m->fields[j].id,m->fields[j].type,m->fields[j].value);
    }free(s);
}
int main(int argc,char **argv) {
    if(argc<3)return 2;mod_ui_reset(argv[2]);ModPlan *plan=mod_packages_read(argv[1],report);
    ModRuntime *rt=plan?mod_runtime_start(plan,report,NULL):NULL;
    while(mod_runtime_event(rt,NULL)){}
    mod_ui_interaction(1);dump();
    for(int i=3;i<argc;++i) {
        if(!strcmp(argv[i],"--save-fail")){setenv("MOD_SAVE_FAIL","1",1);continue;}
        if(!strcmp(argv[i],"--save-ok")){unsetenv("MOD_SAVE_FAIL");continue;}
        if(!strcmp(argv[i],"--dump")){dump();continue;}
        if(!strcmp(argv[i],"--close")){mod_ui_interaction(0);continue;}
        if(!strcmp(argv[i],"--drain")){while(mod_runtime_actions(rt,NULL)){}continue;}
        if(!strcmp(argv[i],"--shutdown")){mod_ui_shutdown();mod_runtime_stop(rt);rt=NULL;continue;}
        char id[65],field[65];int value;
        int queue=!strncmp(argv[i],"--queue=",8),stale=!strncmp(argv[i],"--stale=",8);
        const char *arg=queue||stale?argv[i]+8:!strncmp(argv[i],"--set=",6)?argv[i]+6:NULL;
        if(!arg || sscanf(arg,"%64[^:]:%64[^:]:%d",id,field,&value)!=3)return 2;
        ModSnapshot *s=malloc(sizeof *s);mod_ui_snapshot(s);ModAction a={0};a.generation=s->generation+stale;a.session=s->session;strcpy(a.mod,id);strcpy(a.field,field);a.value=value;
        for(int j=0;j<s->count;++j)if(!strcmp(id,s->mods[j].id))for(int k=0;k<s->mods[j].field_count;++k)if(!strcmp(field,s->mods[j].fields[k].id))a.type=s->mods[j].fields[k].type;
        free(s);printf("submitted %s %s %d\n",id,field,mod_ui_submit(&a));
        if(!queue)while(mod_runtime_actions(rt,NULL)){}
    }
    dump();mod_ui_shutdown();mod_runtime_stop(rt);mod_packages_free(plan);return 0;
}
