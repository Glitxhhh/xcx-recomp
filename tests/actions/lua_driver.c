#include "mod_internal.h"
#include "mod_provider_platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int stopping;static int canceled(void){return stopping;}
static void log_line(const char *s){puts(s);fflush(stdout);}
static void pump(ModRuntime *rt,unsigned ms){uint64_t end=mod_provider_clock_ms()+ms;do{mod_runtime_actions(rt,canceled);mod_provider_sleep();}while(mod_provider_clock_ms()<end);}
static void intent(const char *mod,const char *action){ModSnapshot *s=malloc(sizeof *s);mod_ui_snapshot(s);ModIntent a={0};a.generation=s->generation;a.session=s->session;snprintf(a.mod,sizeof a.mod,"%s",mod);snprintf(a.action,sizeof a.action,"%s",action);free(s);printf("intent %s %s %d\n",mod,action,mod_ui_intent_submit(&a));}
int main(int argc,char **argv){if(argc<3)return 2;mod_ui_reset(argv[2]);ModPlan *plan=mod_packages_read(argv[1],log_line);if(!plan)return 0;ModRuntime *rt=mod_runtime_start(plan,log_line,canceled);while(mod_runtime_event(rt,NULL)){}mod_ui_interaction(1);
 for(int i=3;i<argc;++i){unsigned ms;char name[65],action[65];if(!strcmp(argv[i],"cancel"))stopping=1;else if(!strcmp(argv[i],"close"))mod_ui_interaction(0);else if(!strcmp(argv[i],"open"))mod_ui_interaction(1);else if(sscanf(argv[i],"pump=%u",&ms)==1)pump(rt,ms);else if(sscanf(argv[i],"intent=%64[^:]:%64s",name,action)==2)intent(name,action);else return 2;}
 ModSnapshot *s=malloc(sizeof *s);mod_ui_snapshot(s);for(int i=0;i<s->count;++i)printf("page %s status=%d pending=%d available=%d text=%s native=%s\n",s->mods[i].id,s->mods[i].status,s->mods[i].provider_pending,s->mods[i].provider_available,s->mods[i].ui_status,s->mods[i].provider_note);free(s);mod_ui_shutdown();mod_runtime_stop(rt);mod_packages_free(plan);return 0;}
