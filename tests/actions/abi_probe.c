#include "mod_provider_service_abi.h"
#include "mod_provider_platform.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"service ABI failed line %d\n",__LINE__);return 1;}}while(0)
int main(int argc,char **argv){if(argc!=2)return 2;ModProviderFile f;
#ifdef _WIN32
 const char *path="native/windows-x86_64/provider.dll";
#else
 const char *path="native/linux-x86_64/provider.so";
#endif
 CHECK(mod_provider_read(argv[1],"alpha",path,32u*1024*1024,&f)==1);CHECK(mod_provider_image(&f));void *module=mod_provider_map(&f);CHECK(module);mod_provider_file_close(&f);XmsQuery query=(XmsQuery)mod_provider_symbol(module,"xcx_provider_query_v2");CHECK(query);XmsApi api={0};api.size=sizeof api;CHECK(query(1,0,&api)==XMP_INCOMPATIBLE);api.size--;CHECK(query(2,0,&api)==XMP_INCOMPATIBLE);api.size=sizeof api;CHECK(query(2,1,&api)==XMP_INCOMPATIBLE);CHECK(query(2,0,&api)==XMP_OK);
 XmpCreate c={0};c.size=sizeof c;c.major=2;c.owner=9;c.generation=17;strcpy(c.package_id,"alpha");uint64_t token=0;CHECK(api.create_idle(&c,&token)==XMP_OK&&token);
 XmsCommand command={sizeof command,1,XMS_NONE,0,9,17,99,0};command.owner=10;CHECK(api.submit(token,&command)==XMP_INVALID);command.owner=9;command.generation=18;CHECK(api.submit(token,&command)==XMP_INVALID);command.generation=17;command.reserved=1;CHECK(api.submit(token,&command)==XMP_INVALID);command.reserved=0;command.type=XMS_BOOL;CHECK(api.submit(token,&command)==XMP_INVALID);command.type=XMS_NONE;CHECK(api.submit(token,&command)==XMP_OK);CHECK(api.submit(token,&command)==XMP_INVALID);
 uint64_t end=mod_provider_clock_ms()+1000;XmsResult result;int r;do{memset(&result,0,sizeof result);result.size=sizeof result;r=api.poll(token,&result);if(r!=XMS_EMPTY)break;mod_provider_sleep();}while(mod_provider_clock_ms()<end);CHECK(r==XMP_OK&&result.owner==9&&result.generation==17&&result.request==99);
 CHECK(api.request_stop(token)==XMP_OK);while((r=api.join_destroy(token,0))==XMP_BUSY&&mod_provider_clock_ms()<end)mod_provider_sleep();CHECK(r==XMP_OK);CHECK(api.submit(token,&command)==XMP_FAILED);puts("ABI 2.0 strict version, owner/generation, duplicate and type checks passed");return 0;}
