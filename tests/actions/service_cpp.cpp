#include "support.h"
// An independent C++ service, using only its own fixed arrays and an OS worker.
class Counter {
public:
 Lock mutex;Thread thread;int stopping,finished;uint64_t owner,generation,last_request;XmsCommand work[32];unsigned used;XmsResult output[32];unsigned ready,outstanding;XmsStatus latest;
};
static Counter *counter;static uint64_t serial=1200;
static WORKER count_worker(void *arg){Counter *p=static_cast<Counter *>(arg);while(!flag(&p->stopping)){
 lock_take(&p->mutex);if(!p->used){lock_drop(&p->mutex);delay(1);continue;}XmsCommand c=p->work[0];--p->used;memmove(p->work,p->work+1,p->used*sizeof c);++p->latest.revision;strcpy(p->latest.text,"Counting");lock_drop(&p->mutex);
 unsigned wait=c.opcode==12?(unsigned)c.value:0;while(wait--&&!flag(&p->stopping))delay(1);if(flag(&p->stopping))break;
 XmsResult r={};r.size=sizeof r;r.owner=p->owner;r.generation=p->generation;r.request=c.request;r.result=c.opcode==14&&c.value?XMP_FAILED:XMP_OK;strcpy(r.message,r.result==XMP_OK?"Counter completed":"Counter failed as requested");
 lock_take(&p->mutex);p->output[p->ready++]=r;++p->latest.revision;strcpy(p->latest.text,"Counter ready");lock_drop(&p->mutex);
 }setflag(&p->finished,1);RETURN_WORKER;}
static XmpResult XMP_CALL describe(XmsInfo *i){if(i->size!=sizeof *i)return XMP_INVALID;memset(i,0,sizeof *i);i->size=sizeof *i;i->major=2;i->platform=XMP_PLATFORM;i->architecture=XMP_ARCH_X64;i->capabilities=1;strcpy(i->provider_id,"beta.native");strcpy(i->build_id,"counter1");strcpy(i->service_id,"inert.counter.v1");i->command_count=4;spec(&i->commands[0],11,"success",XMS_NONE,0,0);spec(&i->commands[1],12,"delayed",XMS_INTEGER,10,10000);spec(&i->commands[2],13,"reject",XMS_NONE,0,0);spec(&i->commands[3],14,"fail",XMS_BOOL,0,1);return XMP_OK;}
static XmpResult XMP_CALL create(const XmpCreate *c,uint64_t *out){*out=0;if(counter||c->size!=sizeof *c||c->major!=2||c->minor||c->reserved||!c->owner||!c->generation)return XMP_INVALID;Counter *p=static_cast<Counter *>(calloc(1,sizeof *p));if(!p)return XMP_FAILED;lock_init(&p->mutex);p->owner=c->owner;p->generation=c->generation;p->latest.size=sizeof p->latest;p->latest.owner=c->owner;p->latest.generation=c->generation;p->latest.revision=1;strcpy(p->latest.text,"Ready");
#ifdef _WIN32
 p->thread=CreateThread(NULL,0,count_worker,p,0,NULL);int ok=p->thread!=NULL;
#else
 int ok=!pthread_create(&p->thread,NULL,count_worker,p);
#endif
 if(!ok){lock_free(&p->mutex);free(p);return XMP_FAILED;}counter=p;*out=++serial;return XMP_OK;}
static XmpResult XMP_CALL stop(uint64_t t){if(!counter||t!=serial)return XMP_INVALID;setflag(&counter->stopping,1);return XMP_OK;}
static XmpResult XMP_CALL destroy(uint64_t t,uint32_t ms){if(!counter||t!=serial)return XMP_INVALID;while(ms--&&!flag(&counter->finished))delay(1);if(!flag(&counter->finished))return XMP_BUSY;join_thread(counter->thread);lock_free(&counter->mutex);free(counter);counter=nullptr;return XMP_OK;}
static XmpResult XMP_CALL submit(uint64_t t,const XmsCommand *c){if(!counter||t!=serial||flag(&counter->stopping))return XMP_FAILED;if(c->size!=sizeof *c||c->reserved||c->owner!=counter->owner||c->generation!=counter->generation||!c->request||c->opcode<11||c->opcode>14)return XMP_INVALID;if((c->opcode==11||c->opcode==13)&&(c->type!=XMS_NONE||c->value))return XMP_INVALID;if(c->opcode==12&&(c->type!=XMS_INTEGER||c->value<10||c->value>10000))return XMP_INVALID;if(c->opcode==14&&(c->type!=XMS_BOOL||c->value<0||c->value>1))return XMP_INVALID;if(c->opcode==13)return XMP_INVALID;lock_take(&counter->mutex);if(c->request<=counter->last_request){lock_drop(&counter->mutex);return XMP_INVALID;}if(counter->outstanding==32){lock_drop(&counter->mutex);return XMP_BUSY;}counter->work[counter->used++]=*c;++counter->outstanding;counter->last_request=c->request;lock_drop(&counter->mutex);return XMP_OK;}
static XmpResult XMP_CALL poll(uint64_t t,XmsResult *r){if(!counter||t!=serial||r->size!=sizeof *r)return XMP_INVALID;lock_take(&counter->mutex);if(!counter->ready){lock_drop(&counter->mutex);return XMS_EMPTY;}*r=counter->output[0];--counter->outstanding;--counter->ready;memmove(counter->output,counter->output+1,counter->ready*sizeof *r);lock_drop(&counter->mutex);return XMP_OK;}
static XmpResult XMP_CALL status(uint64_t t,XmsStatus *s){if(!counter||t!=serial||s->size!=sizeof *s)return XMP_INVALID;lock_take(&counter->mutex);*s=counter->latest;lock_drop(&counter->mutex);return XMP_OK;}
extern "C" XMP_EXPORT XmpResult XMP_CALL xcx_provider_query_v2(uint32_t major,uint32_t minor,XmsApi *a){if(major!=2||minor||a->size!=sizeof *a)return XMP_INCOMPATIBLE;memset(a,0,sizeof *a);a->size=sizeof *a;a->major=2;a->describe=describe;a->create_idle=create;a->request_stop=stop;a->join_destroy=destroy;a->submit=submit;a->poll=poll;a->status=status;return XMP_OK;}
