#include "support.h"
#ifndef MODE
#define MODE 0
#endif
#ifndef BUILD
#define BUILD "work1"
#endif
typedef struct {Lock mutex;Thread thread;int stop,done;uint64_t owner,generation,last_request;XmsCommand queue[32];int count;XmsResult results[32];int ready,outstanding;XmsStatus status;XmsResult last;int duplicate;} Work;
static Work *live;static uint64_t token=900;
static void state(Work *p,const char *text){++p->status.revision;strcpy(p->status.text,text);}
static WORKER worker(void *data){Work *p=(Work *)data;while(!flag(&p->stop)){
    lock_take(&p->mutex);if(!p->count){lock_drop(&p->mutex);delay(1);continue;}XmsCommand c=p->queue[0];memmove(p->queue,p->queue+1,(size_t)(--p->count)*sizeof c);state(p,"Working");lock_drop(&p->mutex);
    if(c.opcode==2)for(int i=0;i<c.value&&!flag(&p->stop);++i)delay(1);
    if(flag(&p->stop))break;
    XmsResult r;memset(&r,0,sizeof r);r.size=sizeof r;r.owner=p->owner;r.generation=p->generation;r.request=c.request;r.result=c.opcode==4&&c.value?XMP_FAILED:XMP_OK;strcpy(r.message,r.result==XMP_OK?"Action completed":"Action failed as requested");
    if(MODE==1)++r.request;if(MODE==3)++r.owner;if(MODE==4)++r.generation;if(MODE==5)r.size--;
    lock_take(&p->mutex);p->results[p->ready++]=r;state(p,"Completed");lock_drop(&p->mutex);
}setflag(&p->done,1);RETURN_WORKER;}
static XmpResult XMP_CALL describe(XmsInfo *i){if(i->size!=sizeof *i)return XMP_INVALID;memset(i,0,sizeof *i);i->size=sizeof *i;i->major=2;i->platform=XMP_PLATFORM;i->architecture=XMP_ARCH_X64;i->capabilities=UINT64_C(0x8000000000000001);strcpy(i->provider_id,"alpha.native");strcpy(i->build_id,BUILD);strcpy(i->service_id,"inert.work.v1");i->command_count=4;spec(&i->commands[0],1,"success",XMS_NONE,0,0);spec(&i->commands[1],2,"delayed",XMS_INTEGER,10,10000);spec(&i->commands[2],3,"reject",XMS_NONE,0,0);spec(&i->commands[3],4,"fail",XMS_BOOL,0,1);return XMP_OK;}
static XmpResult XMP_CALL create(const XmpCreate *c,uint64_t *out){*out=0;if(live||c->size!=sizeof *c||c->major!=2||c->minor||c->reserved||!c->owner||!c->generation||(c->required_capabilities&~UINT64_C(0x8000000000000001)))return XMP_INVALID;Work *p=(Work *)calloc(1,sizeof *p);if(!p)return XMP_FAILED;lock_init(&p->mutex);p->owner=c->owner;p->generation=c->generation;p->status.size=sizeof p->status;p->status.owner=c->owner;p->status.generation=c->generation;p->status.revision=1;strcpy(p->status.text,"Ready");
#ifdef _WIN32
    p->thread=CreateThread(NULL,0,worker,p,0,NULL);int ok=p->thread!=NULL;
#else
    int ok=!pthread_create(&p->thread,NULL,worker,p);
#endif
    if(!ok){lock_free(&p->mutex);free(p);return XMP_FAILED;}live=p;*out=++token;return MODE==7?XMP_FAILED:XMP_OK;}
static XmpResult XMP_CALL stop(uint64_t t){if(!live||t!=token)return XMP_INVALID;setflag(&live->stop,1);return XMP_OK;}
static XmpResult XMP_CALL destroy(uint64_t t,uint32_t ms){if(!live||t!=token)return XMP_INVALID;while(ms--&&!flag(&live->done))delay(1);if(!flag(&live->done))return XMP_BUSY;join_thread(live->thread);lock_free(&live->mutex);free(live);live=NULL;return XMP_OK;}
static XmpResult XMP_CALL submit(uint64_t t,const XmsCommand *c){if(!live||t!=token||flag(&live->stop))return XMP_FAILED;if(c->size!=sizeof *c||c->reserved||c->owner!=live->owner||c->generation!=live->generation||!c->request||c->opcode<1||c->opcode>4)return XMP_INVALID;if((c->opcode==1||c->opcode==3)&&(c->type!=XMS_NONE||c->value))return XMP_INVALID;if(c->opcode==2&&(c->type!=XMS_INTEGER||c->value<10||c->value>10000))return XMP_INVALID;if(c->opcode==4&&(c->type!=XMS_BOOL||c->value<0||c->value>1))return XMP_INVALID;if(c->opcode==3)return XMP_INVALID;lock_take(&live->mutex);if(c->request<=live->last_request){lock_drop(&live->mutex);return XMP_INVALID;}if(live->outstanding==32){lock_drop(&live->mutex);return XMP_BUSY;}live->queue[live->count++]=*c;++live->outstanding;live->last_request=c->request;lock_drop(&live->mutex);return XMP_OK;}
static XmpResult XMP_CALL poll(uint64_t t,XmsResult *r){if(!live||t!=token||r->size!=sizeof *r)return XMP_INVALID;lock_take(&live->mutex);if(MODE==2&&live->duplicate){*r=live->last;live->duplicate=0;lock_drop(&live->mutex);return XMP_OK;}if(!live->ready){lock_drop(&live->mutex);return XMS_EMPTY;}*r=live->results[0];--live->outstanding;memmove(live->results,live->results+1,(size_t)(--live->ready)*sizeof *r);live->last=*r;if(MODE==2)live->duplicate=1;lock_drop(&live->mutex);return XMP_OK;}
static XmpResult XMP_CALL status(uint64_t t,XmsStatus *s){if(!live||t!=token||s->size!=sizeof *s)return XMP_INVALID;lock_take(&live->mutex);*s=live->status;lock_drop(&live->mutex);if(MODE==6)s->reserved=1;return XMP_OK;}
XMP_EXPORT XmpResult XMP_CALL xcx_provider_query_v2(uint32_t major,uint32_t minor,XmsApi *a){if(major!=2||minor||a->size!=sizeof *a)return XMP_INCOMPATIBLE;memset(a,0,sizeof *a);a->size=sizeof *a;a->major=2;a->describe=describe;a->create_idle=create;a->request_stop=stop;a->join_destroy=destroy;a->submit=submit;a->poll=poll;a->status=status;return XMP_OK;}
