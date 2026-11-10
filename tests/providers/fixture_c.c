#include "mod_provider_abi.h"
#include "fixture_support.h"
#include <string.h>
#include <stdatomic.h>
#ifndef MODE
#define MODE 0
#endif
#ifndef BUILD
#define BUILD "c1"
#endif
#define ID "alpha.native"
#ifndef _WIN32
#include <pthread.h>
#endif
typedef struct {
    atomic_int stop,done;
#ifdef _WIN32
    HANDLE thread;
#else
    pthread_t thread;
#endif
} Instance;
static Instance *live;static uint64_t token=100;
#ifdef _WIN32
static DWORD WINAPI worker(void *data)
#else
static void *worker(void *data)
#endif
{
    Instance *p=(Instance *)data;while(!atomic_load(&p->stop))pause_ms(1);
    if(MODE==10)pause_ms(30);atomic_store(&p->done,1);
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}
static int start(Instance *p){
#ifdef _WIN32
    p->thread=CreateThread(NULL,0,worker,p,0,NULL);return p->thread!=NULL;
#else
    return pthread_create(&p->thread,NULL,worker,p)==0;
#endif
}
static void finish(Instance *p){
#ifdef _WIN32
    WaitForSingleObject(p->thread,INFINITE);CloseHandle(p->thread);
#else
    pthread_join(p->thread,NULL);
#endif
    free(p);
}
__attribute__((constructor)) static void loaded(void){marker(ID,"constructor");}
__attribute__((destructor)) static void unloaded(void){marker(ID,"destructor");}
static XmpResult XMP_CALL info(XmpInfo *p){if(p->size!=sizeof *p)return XMP_INVALID;memset(p,0,sizeof *p);p->size=sizeof *p;p->major=XMP_MAJOR;p->minor=XMP_MINOR;p->platform=XMP_PLATFORM;p->architecture=XMP_ARCH_X64;p->capabilities=MODE==4?0:1;strcpy(p->provider_id,MODE==3?"wrong.native":ID);strcpy(p->build_id,BUILD);if(MODE==5)p->reserved=1;if(MODE==6)p->padding[0]=1;if(MODE==13)p->platform=99;if(MODE==14)p->architecture=99;if(MODE==15)p->size--;marker(ID,"describe");return XMP_OK;}
static XmpResult XMP_CALL create(const XmpCreate *p,uint64_t *out){
    *out=0;if(p->size!=sizeof *p||p->major!=XMP_MAJOR||p->minor!=XMP_MINOR||p->reserved||!p->owner||!p->generation||strcmp(p->package_id,"alpha")||live)return XMP_INVALID;
    for(unsigned i=0;i<sizeof p->padding;++i)if(p->padding[i])return XMP_INVALID;
    if(p->required_capabilities&~UINT64_C(1))return XMP_INCOMPATIBLE;
    marker(ID,"create");if(MODE==8)return XMP_FAILED;
    Instance *s=(Instance *)calloc(1,sizeof *s);if(!s)return XMP_FAILED;
    if(!start(s)){free(s);return XMP_FAILED;}
    if(MODE==9){atomic_store(&s->stop,1);finish(s);marker(ID,"rollback");return XMP_FAILED;}
    live=s;*out=++token;return XMP_OK;
}
static XmpResult XMP_CALL stop(uint64_t value){if(!live||value!=token)return XMP_INVALID;atomic_store(&live->stop,1);marker(ID,"stop");return XMP_OK;}
static XmpResult XMP_CALL join(uint64_t value,uint32_t timeout){
    if(!live||value!=token)return XMP_INVALID;
    if(MODE==11){marker(ID,"hung");for(;;)pause_ms(100);}
    if(MODE==12)return XMP_BUSY;
    while(timeout--&&!atomic_load(&live->done))pause_ms(1);
    if(!atomic_load(&live->done))return XMP_BUSY;
    finish(live);live=NULL;marker(ID,"destroy");return XMP_OK;
}
#ifdef MISSING
#define xcx_provider_query_v1 missing_query
#endif
XMP_EXPORT XmpResult XMP_CALL xcx_provider_query_v1(uint32_t major,uint32_t minor,XmpApi *p){
    if(major!=XMP_MAJOR||minor!=XMP_MINOR||p->size!=sizeof *p)return XMP_INCOMPATIBLE;
    memset(p,0,sizeof *p);p->size=sizeof *p;p->major=MODE==1?9:XMP_MAJOR;p->minor=XMP_MINOR;p->describe=info;p->create_idle=create;p->request_stop=stop;p->join_destroy=join;
    if(MODE==2)p->size--;if(MODE==7)p->join_destroy=NULL;return XMP_OK;
}
