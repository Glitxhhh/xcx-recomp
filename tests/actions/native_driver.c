#include "mod_provider.h"
#include "mod_provider_platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"check failed %d\n",__LINE__);return 3;}}while(0)
static void log_line(const char *s){puts(s);}
int main(int argc,char **argv){if(argc!=3)return 2;mod_ui_reset("");ModPlan *plan=mod_packages_read(argv[1],log_line);CHECK(plan);ModProviders *p=mod_providers_start(plan,log_line);uint64_t owner=0;for(int i=0;i<plan->count;++i)if(!strcmp(plan->mods[i].id,"alpha"))owner=(uint64_t)i+1;CHECK(owner);uint64_t request;
 if(!strcmp(argv[2],"credits")){
  CHECK(mod_provider_submit(p,owner,"delayed",XMS_INTEGER,10001,&request)==XMP_INVALID);CHECK(mod_provider_submit(p,owner+100,"success",XMS_NONE,0,&request)==XMP_INCOMPATIBLE);
  for(int i=0;i<32;++i){CHECK(mod_provider_submit(p,owner,"success",XMS_NONE,0,&request)==XMP_OK);CHECK(request==(uint64_t)i+1);}CHECK(mod_provider_submit(p,owner,"success",XMS_NONE,0,&request)==XMP_BUSY);
  uint64_t end=mod_provider_clock_ms()+500;while(mod_provider_clock_ms()<end){mod_provider_pump(p);mod_provider_sleep();}ModServiceView v;mod_provider_view(p,owner,&v);CHECK(v.pending==32);CHECK(mod_provider_submit(p,owner,"success",XMS_NONE,0,&request)==XMP_BUSY);
  int count=0;ModServiceEvent e;while(mod_provider_take(p,owner,&e)){if(e.terminal){CHECK(e.result.request==(uint64_t)++count);CHECK(e.result.result==XMP_OK);}}CHECK(count==32);mod_provider_view(p,owner,&v);CHECK(v.pending==0);puts("32 reserved credits, FIFO terminal delivery and slow-consumer rejection verified");
 }else if(!strcmp(argv[2],"fault")){
  CHECK(mod_provider_submit(p,owner,"success",XMS_NONE,0,&request)==XMP_OK);CHECK(mod_provider_submit(p,owner,"delayed",XMS_INTEGER,20,&request)==XMP_OK);
  uint64_t end=mod_provider_clock_ms()+1000;ModServiceView v;
  do{mod_provider_pump(p);mod_provider_view(p,owner,&v);if(v.faulted)break;mod_provider_sleep();}while(mod_provider_clock_ms()<end);CHECK(v.faulted&&!v.available);int count=0;ModServiceEvent e;while(mod_provider_take(p,owner,&e))count+=e.terminal;CHECK(count==2);puts("protocol fault retired provider and resolved every reserved terminal credit");
 }
 CHECK(mod_providers_stop(p));mod_providers_free(p);mod_packages_free(plan);return 0;}
