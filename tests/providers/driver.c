#include "mod_provider.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void log_line(const char *s){puts(s);fflush(stdout);}
int main(int argc,char **argv){
    if(argc<2)return 2;mod_ui_reset("");ModPlan *plan=mod_packages_read(argv[1],log_line);if(!plan){puts("no plan");return 0;}const char *game=getenv("XMP_TEST_GAME");if(game)snprintf(plan->game_id,sizeof plan->game_id,"%s",game);
    ModProviders *p=mod_providers_start(plan,log_line);int tokens=0;ModProviderToken old={0};
    for(int i=0;i<plan->count;++i){ModProviderToken t;if(mod_providers_token(p,(uint64_t)i+1,&t)){
        old=t;++tokens;printf("token %s\n",plan->mods[i].id);
        ModProviderToken stale=t;stale.generation++;if(mod_providers_stop_owner(p,t.owner,stale)||mod_providers_stop_owner(p,t.owner+100,t))return 3;
        stale=t;stale.reserved=1;if(mod_providers_stop_owner(p,t.owner,stale))return 4;
        if(argc>2&&!strcmp(argv[2],"tokens")){if(!mod_providers_stop_owner(p,t.owner,t)||!mod_providers_stop_owner(p,t.owner,t))return 5;}
    }}
    int stopped=mod_providers_stop(p);printf("tokens %d stopped %d\n",tokens,stopped);fflush(stdout);
    if(!stopped){mod_providers_free(p);fflush(stdout);_Exit(70);}
    if(!mod_providers_stop(p))return 6;mod_providers_free(p);
    if(argc>2&&!strcmp(argv[2],"hold")){puts("ready for restart attempt");fflush(stdout);if(getchar()==EOF)return 9;p=mod_providers_start(plan,log_line);ModProviderToken t;int active=mod_providers_token(p,old.owner,&t);printf("replacement active %d\n",active);if(!mod_providers_stop(p))return 10;mod_providers_free(p);}
    if(argc>2&&!strcmp(argv[2],"generations")){p=mod_providers_start(plan,log_line);if(mod_providers_stop_owner(p,old.owner,old))return 7;if(!mod_providers_stop(p))return 8;mod_providers_free(p);puts("old generation rejected");}
    const char *path=getenv("XMP_MARKER");if(path){FILE *mark=fopen(path,"a");if(mark){fputs("host before-exit\n",mark);fclose(mark);}}
    mod_packages_free(plan);puts("host finished; mappings still retained");return 0;
}
