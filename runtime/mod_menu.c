#include "mod_menu.h"
#include "mod_settings.h"
#include <stdatomic.h>
#include <string.h>
static atomic_flag guard=ATOMIC_FLAG_INIT;
static ModMenuEntry registered;static int supported,pending;static uint64_t epoch=1;
static void lock(void){while(atomic_flag_test_and_set_explicit(&guard,memory_order_acquire)){};}
static void unlock(void){atomic_flag_clear_explicit(&guard,memory_order_release);}
void mod_menu_reset(void){lock();registered=(ModMenuEntry){0};pending=0;supported=0;if(epoch!=UINT64_MAX)++epoch;unlock();}
void mod_menu_support(int yes){lock();supported=epoch==UINT64_MAX?-1:yes;if(supported<=0)pending=0;unlock();}
const char *mod_menu_register(const ModMenuEntry *e){const char *error=NULL;lock();if(!supported)error="unsupported";else if(supported<0)error="unavailable";else if(registered.owner[0])error="full";else{registered=*e;registered.epoch=epoch;}unlock();return error;}
static int healthy(const ModMenuEntry *e){ModSnapshot s;mod_ui_snapshot(&s);for(int i=0;i<s.count;++i)if(!strcmp(e->owner,s.mods[i].id)&&s.mods[i].status==MOD_LOADED&&!strcmp(e->page,s.mods[i].page_id))return 1;return 0;}
int mod_menu_snapshot(ModMenuEntry *e){lock();*e=registered;int ok=supported>0&&e->owner[0];e->epoch=epoch;unlock();return ok&&healthy(e);}
int mod_menu_request(void){ModMenuEntry e;if(!mod_menu_snapshot(&e))return 0;lock();int ok=epoch==e.epoch&&!pending;if(ok)pending=1;unlock();return ok;}
int mod_menu_take(ModMenuEntry *e){lock();int ok=pending&&supported>0;pending=0;*e=registered;e->epoch=epoch;unlock();return ok&&healthy(e);}
uint64_t mod_menu_epoch(void){lock();uint64_t n=epoch;unlock();return n;}
void mod_menu_invalidate(void){lock();pending=0;if(epoch!=UINT64_MAX)++epoch;if(epoch==UINT64_MAX)supported=-1;unlock();}
