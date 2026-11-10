#include "mod_provider_abi.h"
#include "mod_provider_platform.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"ABI probe failed at line %d\n",__LINE__);return 1;}}while(0)
int main(int argc,char **argv){
    if(argc!=2)return 2;ModProviderFile f;
#ifdef _WIN32
    const char *binary="native/windows-x86_64/provider.dll";
#else
    const char *binary="native/linux-x86_64/provider.so";
#endif
    CHECK(mod_provider_read(argv[1],"alpha",binary,32u*1024*1024,&f)==1);CHECK(mod_provider_image(&f));void *module=mod_provider_map(&f);CHECK(module);mod_provider_file_close(&f);
#ifndef _WIN32
    CHECK(!mod_provider_symbol(NULL,"xcx_provider_query_v1")); // RTLD_LOCAL does not export the provider globally.
#endif
    XmpQuery query=(XmpQuery)mod_provider_symbol(module,"xcx_provider_query_v1");CHECK(query);XmpApi api={0};api.size=sizeof api;CHECK(query(9,0,&api)==XMP_INCOMPATIBLE);CHECK(query(1,9,&api)==XMP_INCOMPATIBLE);CHECK(query(1,0,&api)==XMP_OK);
    XmpInfo info={0};info.size=1;CHECK(api.describe(&info)==XMP_INVALID);info.size=sizeof info;CHECK(api.describe(&info)==XMP_OK);
    XmpCreate c={0};c.size=sizeof c;c.major=1;c.owner=1;c.generation=1;strcpy(c.package_id,"alpha");uint64_t t=0;c.reserved=1;CHECK(api.create_idle(&c,&t)==XMP_INVALID&&t==0);c.reserved=0;c.padding[0]=1;CHECK(api.create_idle(&c,&t)==XMP_INVALID&&t==0);c.padding[0]=0;CHECK(api.create_idle(&c,&t)==XMP_OK&&t);
    CHECK(api.request_stop(t+1)==XMP_INVALID);CHECK(api.join_destroy(t,0)==XMP_BUSY);CHECK(api.request_stop(t)==XMP_OK);CHECK(api.request_stop(t)==XMP_OK);
    uint64_t deadline=mod_provider_clock_ms()+1000;XmpResult result;
    while((result=api.join_destroy(t,0))==XMP_BUSY&&mod_provider_clock_ms()<deadline)mod_provider_sleep();CHECK(result==XMP_OK);CHECK(api.request_stop(t)==XMP_INVALID);CHECK(api.join_destroy(t,0)==XMP_INVALID);puts("ABI probe passed");return 0;
}
