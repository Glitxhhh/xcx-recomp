// Optional native libraries. Every call is serialized by the mod worker, with no host lock held.
#include "mod_provider.h"
#include "mod_provider_platform.h"
#include "sha256.h"
#include "mod_text.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#define PROVIDERS 4
#ifndef XMP_STOP_TIMEOUT_MS
#define XMP_STOP_TIMEOUT_MS 5000u
#endif
typedef struct {
    char owner_id[65],id[65],hash[65],build[65];uint64_t owner,required,instance;
    XmpApi api;int valid,stopping,stopped,major,faulted;
    XmsApi service_api;XmsInfo info;XmsStatus status;
    uint64_t next_request,last_poll;int status_dirty,prefer_status;
    struct {uint64_t request;int completed;} credits[XMS_CREDITS];int pending;
    XmsResult results[XMS_CREDITS];int result_count;
    ModProviderFile file;
} Provider;
struct ModProviders {uint64_t generation;ModLog log;int count;Provider entries[PROVIDERS];};
// Process-lifetime mapping ownership, including ABI-rejected modules. Never dlclose/FreeLibrary.
static struct {void *module;intptr_t file;char hash[65],id[65];} mapped[PROVIDERS];
static int mapped_count;
static uint64_t generation;
static int id(const char *s) {
    size_t n=strlen(s);if(!n||n>64||*s<'a'||*s>'z')return 0;
    for(;*s;++s)if(!((*s>='a'&&*s<='z')||(*s>='0'&&*s<='9')||*s=='.'||*s=='_'||*s=='-'))return 0;
    return 1;
}
static void report(ModProviders *p,const char *owner,const char *reason) {
    char line[384];snprintf(line,sizeof line,"providers: %.64s: %.256s",owner,reason);p->log(line);
}
static int zero(const void *data,size_t n){const unsigned char *b=data;for(size_t i=0;i<n;++i)if(b[i])return 0;return 1;}
static int hash_ok(const char *s){if(strlen(s)!=64)return 0;for(;*s;++s)if(!((*s>='0'&&*s<='9')||(*s>='a'&&*s<='f')))return 0;return 1;}
static char *next_line(char *line) {char *n=strchr(line,'\n');if(n)*n++=0;size_t k=strlen(line);if(k&&line[k-1]=='\r')line[k-1]=0;return n;}
static int metadata(Provider *p,char *text,const char *game) {
    unsigned fields=0;int schema=0;char owner[65]={0},provider[65]={0},required_host[65]={0},required_game[65]={0};
    for(char *line=text;line;) {
        char *next=next_line(line);
        if(*line&&*line!='#') {
            char *v=strchr(line,'=');if(!v)return 0;*v++=0;unsigned bit=0;
            if(!strcmp(line,"schema")){bit=1;if(!strcmp(v,"1"))schema=1;else if(!strcmp(v,"2"))schema=2;else return 0;}
            else if(!strcmp(line,"id")){bit=2;if(!id(v))return 0;strcpy(provider,v);}
            else if(!strcmp(line,"owner")){bit=4;if(!id(v))return 0;strcpy(owner,v);}
            else if(!strcmp(line,"abi")){bit=8;if(!strcmp(v,"1.0"))p->major=1;else if(!strcmp(v,"2.0"))p->major=2;else return 0;}
            else if(!strcmp(line,"required")){bit=16;errno=0;char *end;if(!*v||*v<'0'||*v>'9')return 0;p->required=strtoull(v,&end,10);if(errno||*end)return 0;}
            else if(!strcmp(line,"host")){bit=128;if(strlen(v)>64)return 0;strcpy(required_host,v);}
            else if(!strcmp(line,"game")){bit=256;if(strlen(v)>64)return 0;strcpy(required_game,v);}
            else if(!strcmp(line,"linux-sha256")||!strcmp(line,"windows-sha256")) {
                bit=!strcmp(line,"linux-sha256")?32:64;if(!hash_ok(v))return 0;
#ifdef _WIN32
                if(bit==64)strcpy(p->hash,v);
#else
                if(bit==32)strcpy(p->hash,v);
#endif
            }else return 0;
            if(fields&bit)return 0;fields|=bit;
        }line=next;
    }
    if(schema==1&&(fields&(128|256)))return 0;
    if(schema==2&&((fields&(128|256))!=(128|256)||strcmp(required_host,"xcx-mods-service-2.0")||!game||!*game||strcmp(required_game,game)))return 0;
    return (fields&31)==31&&p->hash[0]&&!strcmp(owner,p->owner_id)&&!strcmp(provider,p->id);
}
static int preflight(ModProviders *ctx,Provider *p,const char *root,const char *game) {
    ModProviderFile meta;int result=mod_provider_read(root,p->owner_id,"provider.ini",4096,&meta);
    if(result!=1)return 0;
    int ok=!memchr(meta.data,0,meta.size)&&metadata(p,(char *)meta.data,game);mod_provider_file_close(&meta);if(!ok)return 0;
#ifdef _WIN32
    const char *binary="native/windows-x86_64/provider.dll";
#else
    const char *binary="native/linux-x86_64/provider.so";
#endif
    if(mod_provider_read(root,p->owner_id,binary,32u*1024*1024,&p->file)!=1)return 0;
    unsigned char digest[32];char hex[65];sha256(p->file.data,p->file.size,digest);
    for(int i=0;i<32;++i)snprintf(hex+2*i,3,"%02x",digest[i]);
    if(strcmp(hex,p->hash)||!mod_provider_image(&p->file)){mod_provider_file_close(&p->file);return 0;}
    (void)ctx;return 1;
}
static void *map(Provider *p) {
    for(int i=0;i<mapped_count;++i)if(!strcmp(mapped[i].id,p->id)){mod_provider_file_close(&p->file);return !strcmp(mapped[i].hash,p->hash)?mapped[i].module:NULL;}
    if(mapped_count==PROVIDERS){mod_provider_file_close(&p->file);return NULL;}
    void *module=mod_provider_map(&p->file);
    if(module){mapped[mapped_count].module=module;mapped[mapped_count].file=p->file.file;strcpy(mapped[mapped_count].id,p->id);strcpy(mapped[mapped_count++].hash,p->hash);p->file.file=-1;}
    mod_provider_file_close(&p->file);return module;
}
static int bounded_text(const char *s,size_t cap) {const char *end=memchr(s,0,cap);return end&&mod_text_valid(s,(size_t)(end-s));}
static int describe_service(Provider *p,void *module) {
    XmsQuery query=(XmsQuery)mod_provider_symbol(module,"xcx_provider_query_v2");if(!query)return 0;
    XmsApi *a=&p->service_api;a->size=sizeof *a;
    if(query(2,0,a)!=XMP_OK||a->size!=sizeof *a||a->major!=2||a->minor||a->reserved||!a->describe||!a->create_idle||!a->request_stop||!a->join_destroy||!a->submit||!a->poll||!a->status)return 0;
    XmsInfo *i=&p->info;i->size=sizeof *i;
    if(a->describe(i)!=XMP_OK||i->size!=sizeof *i||i->major!=2||i->minor||i->reserved||i->reserved2||!zero(i->padding,sizeof i->padding)||i->platform!=XMP_PLATFORM||i->architecture!=XMP_ARCH_X64||!bounded_text(i->provider_id,sizeof i->provider_id)||strcmp(i->provider_id,p->id)||!bounded_text(i->build_id,sizeof i->build_id)||!id(i->build_id)||!bounded_text(i->service_id,sizeof i->service_id)||!id(i->service_id)||(i->capabilities&p->required)!=p->required||!i->command_count||i->command_count>XMS_COMMANDS)return 0;
    for(unsigned n=0;n<i->command_count;++n){XmsCommandSpec *c=&i->commands[n];
        if(!c->opcode||!zero(c->padding,sizeof c->padding)||!bounded_text(c->id,sizeof c->id)||!id(c->id)||c->type>XMS_INTEGER||c->minimum>c->maximum||c->minimum < -1000000||c->maximum>1000000||(c->type==XMS_NONE&&(c->minimum||c->maximum))||(c->type==XMS_BOOL&&(c->minimum!=0||c->maximum!=1)))return 0;
        for(unsigned j=0;j<n;++j)if(!strcmp(i->commands[j].id,c->id)||i->commands[j].opcode==c->opcode)return 0;
    }
    p->api.create_idle=a->create_idle;p->api.request_stop=a->request_stop;p->api.join_destroy=a->join_destroy;strcpy(p->build,i->build_id);return 1;
}
static int describe(Provider *p,void *module) {
    if(p->major==2)return describe_service(p,module);
    XmpQuery query=(XmpQuery)mod_provider_symbol(module,"xcx_provider_query_v1");
    if(!query)return 0;
    memset(&p->api,0,sizeof p->api);p->api.size=sizeof p->api;
    if(query(XMP_MAJOR,XMP_MINOR,&p->api)!=XMP_OK||p->api.size!=sizeof p->api||p->api.major!=XMP_MAJOR||p->api.minor!=XMP_MINOR||p->api.reserved||!p->api.describe||!p->api.create_idle||!p->api.request_stop||!p->api.join_destroy)return 0;
    XmpInfo info={0};info.size=sizeof info;
    if(p->api.describe(&info)!=XMP_OK||info.size!=sizeof info||info.major!=XMP_MAJOR||info.minor!=XMP_MINOR||info.reserved||!zero(info.reserved2,sizeof info.reserved2)||!zero(info.padding,sizeof info.padding)||info.platform!=XMP_PLATFORM||info.architecture!=XMP_ARCH_X64||!memchr(info.provider_id,0,sizeof info.provider_id)||!memchr(info.build_id,0,sizeof info.build_id)||strcmp(info.provider_id,p->id)||!id(info.build_id)||(info.capabilities&p->required)!=p->required)return 0;
    strcpy(p->build,info.build_id);return 1;
}
ModProviders *mod_providers_start(const ModPlan *plan,ModLog log) {
    ModProviders *ctx=calloc(1,sizeof *ctx);if(!ctx){log("providers: native loader allocation failed; no providers loaded");return NULL;}
    ctx->log=log;if(generation==UINT64_MAX){free(ctx);return NULL;}ctx->generation=++generation;
    ModProviderFile activation;int r=mod_provider_read(plan->root,NULL,"providers-enabled.txt",2048,&activation);
    if(r!=1){if(r<0)report(ctx,"activation","unsafe or unreadable list; no providers loaded");return ctx;}
    int invalid=memchr(activation.data,0,activation.size)!=NULL;char owners[16][65],ids[16][65];int count=0;
    for(char *line=(char *)activation.data;line&&!invalid;) {
        char *next=next_line(line);
        if(*line&&*line!='#') {
            char *sep=strchr(line,':');if(!sep||count==16){invalid=1;break;}*sep++=0;
            if(!id(line)||!id(sep)){invalid=1;break;}
            for(int i=0;i<count;++i)if(!strcmp(owners[i],line)||!strcmp(ids[i],sep))invalid=1;
            strcpy(owners[count],line);strcpy(ids[count++],sep);
        }line=next;
    }
    mod_provider_file_close(&activation);
    if(invalid){report(ctx,"activation","invalid or duplicate association; no providers loaded");return ctx;}
    for(int n=0;n<count;++n) {
        int owner=-1;for(int i=0;i<plan->count;++i)if(!strcmp(plan->mods[i].id,owners[n])&&plan->mods[i].enabled&&!plan->mods[i].rejected)owner=i;
        if(owner<0){report(ctx,owners[n],"owner absent/disabled/rejected; not loaded");continue;}
        if(ctx->count==PROVIDERS){report(ctx,"activation","more than four eligible providers; no providers loaded");for(int i=0;i<ctx->count;++i)mod_provider_file_close(&ctx->entries[i].file);ctx->count=0;return ctx;}
        Provider *p=&ctx->entries[ctx->count++];p->file.file=-1;p->owner=(uint64_t)owner+1;strcpy(p->owner_id,owners[n]);strcpy(p->id,ids[n]);
        p->valid=preflight(ctx,p,plan->root,plan->game_id);if(!p->valid)report(ctx,p->owner_id,"metadata, path, hash, image or dependency preflight rejected");
    }
    if(ctx->count && !mod_provider_environment()) {
        report(ctx,"activation","unsupported native dependency environment; no providers loaded");
        for(int i=0;i<ctx->count;++i){mod_provider_file_close(&ctx->entries[i].file);ctx->entries[i].valid=0;}
        return ctx;
    }
    // Complete all package preflight before mapping any library.
    for(int i=0;i<ctx->count;++i) {
        Provider *p=&ctx->entries[i];if(!p->valid)continue;p->valid=0;
        void *module=map(p);if(!module||!describe(p,module)){report(ctx,p->owner_id,"mapping or ABI rejected; mapped library retained");continue;}
        XmpCreate config={0};config.size=sizeof config;config.major=(uint32_t)p->major;config.minor=XMP_MINOR;config.owner=p->owner;config.generation=ctx->generation;config.required_capabilities=p->required;strcpy(config.package_id,p->owner_id);
        XmpResult result=p->api.create_idle(&config,&p->instance);
        if(result!=XMP_OK||!p->instance){report(ctx,p->owner_id,"idle create failed (requires complete provider rollback)");if(p->instance){p->valid=1;p->faulted=1;report(ctx,p->owner_id,"provider violated failed-create token contract; retained for stop");}continue;}
        p->valid=1;p->status.size=sizeof p->status;p->status.owner=p->owner;p->status.generation=ctx->generation;p->status.revision=p->major==2?0:1;strcpy(p->status.text,"Ready");p->status_dirty=1;char ready[128];snprintf(ready,sizeof ready,"idle provider ready build=%s",p->build);report(ctx,p->owner_id,ready);
    }
    return ctx;
}
int mod_providers_token(ModProviders *p,uint64_t owner,ModProviderToken *out) {
    if(!p||!out)return 0;
    for(int i=0;i<p->count;++i)if(p->entries[i].owner==owner&&p->entries[i].valid&&!p->entries[i].stopped){*out=(ModProviderToken){p->generation,owner,(uint32_t)i,0};return 1;}
    return 0;
}
static int stop_one(ModProviders *p,Provider *s,uint64_t deadline) {
    if(!s->valid||s->stopped)return 1;
    if(!s->stopping){s->stopping=1;if(s->api.request_stop(s->instance)!=XMP_OK){report(p,s->owner_id,"stop request failed; instance retained");return 0;}}
    do {
        XmpResult r=s->api.join_destroy(s->instance,0);
        if(r==XMP_OK){s->stopped=1;s->instance=0;report(p,s->owner_id,"provider destroyed before Lua teardown");return 1;}
        if(r!=XMP_BUSY){report(p,s->owner_id,"join failed; instance retained");return 0;}
        if(mod_provider_clock_ms()>=deadline)break;mod_provider_sleep();
    }while(1);
    report(p,s->owner_id,"cleanup timeout; live instance retained");return 0;
}
int mod_providers_stop_owner(ModProviders *p,uint64_t owner,ModProviderToken token) {
    if(!p||token.reserved||token.generation!=p->generation||token.owner!=owner||token.slot>=(uint32_t)p->count||p->entries[token.slot].owner!=owner)return 0;
    Provider *s=&p->entries[token.slot];s->pending=s->result_count=s->status_dirty=0;memset(s->credits,0,sizeof s->credits);
    return stop_one(p,&p->entries[token.slot],mod_provider_clock_ms()+XMP_STOP_TIMEOUT_MS);
}
int mod_providers_stop(ModProviders *p) {
    if(!p)return 1;for(int i=0;i<p->count;++i){p->entries[i].pending=p->entries[i].result_count=p->entries[i].status_dirty=0;memset(p->entries[i].credits,0,sizeof p->entries[i].credits);}
    int ok=1;uint64_t deadline=mod_provider_clock_ms()+XMP_STOP_TIMEOUT_MS;
    for(int i=p->count-1;i>=0;--i){Provider *s=&p->entries[i];if(s->valid&&!s->stopped&&!s->stopping){s->stopping=1;if(s->api.request_stop(s->instance)!=XMP_OK)ok=0;}}
    for(int i=p->count-1;i>=0;--i)if(!stop_one(p,&p->entries[i],deadline))ok=0;
    return ok;
}
void mod_providers_free(ModProviders *p) {
    if(!p)return;for(int i=0;i<p->count;++i)if(p->entries[i].valid&&!p->entries[i].stopped){report(p,p->entries[i].owner_id,"refused free of live provider context");return;}free(p);
}

static Provider *service_owner(ModProviders *p,uint64_t owner){if(p)for(int i=0;i<p->count;++i)if(p->entries[i].owner==owner)return &p->entries[i];return NULL;}
void mod_provider_view(ModProviders *p,uint64_t owner,ModServiceView *out) {
    memset(out,0,sizeof *out);Provider *s=service_owner(p,owner);out->owner=owner;out->generation=p?p->generation:0;
    strcpy(out->text,"Provider absent or disabled");if(!s)return;
    out->pending=s->pending;out->faulted=s->faulted;
    if(!s->valid){strcpy(out->text,"Provider unavailable or incompatible");return;}
    if(s->major!=2){strcpy(out->text,"Provider lifecycle ABI 1.0 has no command service");return;}
    out->available=!s->stopped&&!s->stopping&&!s->faulted;out->capabilities=s->info.capabilities;out->command_count=(int)s->info.command_count;strcpy(out->service,s->info.service_id);memcpy(out->commands,s->info.commands,sizeof out->commands);
    snprintf(out->text,sizeof out->text,"%s",s->faulted?"Provider protocol fault":s->stopped?"Provider stopped":s->status.text);
}
int mod_provider_submit(ModProviders *p,uint64_t owner,const char *command,int type,int64_t value,uint64_t *request) {
    Provider *s=service_owner(p,owner);if(!s||!s->valid||s->major!=2||s->stopped||s->stopping||s->faulted)return XMP_INCOMPATIBLE;
    const XmsCommandSpec *spec=NULL;for(unsigned i=0;i<s->info.command_count;++i)if(!strcmp(command,s->info.commands[i].id))spec=&s->info.commands[i];
    if(!spec||type!=(int)spec->type||value<spec->minimum||value>spec->maximum)return XMP_INVALID;
    if(s->pending==XMS_CREDITS||s->next_request==UINT64_MAX)return XMP_BUSY;
    int slot=-1;for(int i=0;i<XMS_CREDITS;++i)if(!s->credits[i].request){slot=i;break;}if(slot<0)return XMP_BUSY;
    uint64_t seq=++s->next_request;s->credits[slot].request=seq;s->credits[slot].completed=0;++s->pending;
    XmsCommand c={sizeof c,spec->opcode,spec->type,0,owner,p->generation,seq,value};
    int result=s->service_api.submit(s->instance,&c);
    if(result!=XMP_OK){s->credits[slot].request=0;--s->pending;return result==XMP_BUSY?XMP_BUSY:result==XMP_INVALID?XMP_INVALID:XMP_FAILED;}
    *request=seq;return XMP_OK;
}
static void service_fault(ModProviders *p,Provider *s) {
    s->faulted=1;report(p,s->owner_id,"provider protocol fault; outstanding work failed");
    if(!stop_one(p,s,mod_provider_clock_ms()+XMP_STOP_TIMEOUT_MS)){report(p,s->owner_id,"fatal service retirement failure");abort();}
    for(int i=0;i<XMS_CREDITS;++i)if(s->credits[i].request&&!s->credits[i].completed){
        XmsResult r={0};r.size=sizeof r;r.result=XMP_FAILED;r.owner=s->owner;r.generation=p->generation;r.request=s->credits[i].request;strcpy(r.message,"Provider protocol fault; request canceled");s->results[s->result_count++]=r;s->credits[i].completed=1;
    }
    strcpy(s->status.text,"Provider protocol fault");++s->status.revision;s->status_dirty=1;
}
void mod_provider_pump(ModProviders *p) {
    if(!p)return;
    for(int n=0;n<p->count;++n){Provider *s=&p->entries[n];if(!s->valid||s->major!=2||s->stopped||s->faulted)continue;
        for(int k=0;k<4;++k){XmsResult r={0};r.size=sizeof r;int result=s->service_api.poll(s->instance,&r);if(result==XMS_EMPTY)break;
            int slot=-1;for(int i=0;i<XMS_CREDITS;++i)if(s->credits[i].request==r.request)slot=i;
            if(result!=XMP_OK||r.size!=sizeof r||r.owner!=s->owner||r.generation!=p->generation||!r.request||slot<0||s->credits[slot].completed||!zero(r.padding,sizeof r.padding)||!bounded_text(r.message,sizeof r.message)||(r.result!=XMP_OK&&r.result!=XMP_FAILED&&r.result!=XMP_INVALID)||s->result_count==XMS_CREDITS){service_fault(p,s);break;}
            s->credits[slot].completed=1;s->results[s->result_count++]=r;
        }
        if(s->faulted)continue;uint64_t now=mod_provider_clock_ms();if(now-s->last_poll<100)continue;s->last_poll=now;
        XmsStatus status={0};status.size=sizeof status;
        if(s->service_api.status(s->instance,&status)!=XMP_OK||status.size!=sizeof status||status.reserved||status.owner!=s->owner||status.generation!=p->generation||!status.revision||status.revision==UINT64_MAX||!zero(status.padding,sizeof status.padding)||!bounded_text(status.text,sizeof status.text)||status.revision<s->status.revision||(status.revision==s->status.revision&&strcmp(status.text,s->status.text))){service_fault(p,s);continue;}
        if(status.revision!=s->status.revision){s->status=status;s->status_dirty=1;}
    }
}
int mod_provider_take(ModProviders *p,uint64_t owner,ModServiceEvent *out) {
    Provider *s=service_owner(p,owner);if(!s||s->major!=2)return 0;memset(out,0,sizeof *out);
    if(s->status_dirty&&(!s->result_count||s->prefer_status)){out->status=s->status;s->status_dirty=0;s->prefer_status=0;return 1;}
    if(!s->result_count)return 0;out->terminal=1;out->result=s->results[0];memmove(s->results,s->results+1,(size_t)(--s->result_count)*sizeof s->results[0]);
    for(int i=0;i<XMS_CREDITS;++i)if(s->credits[i].request==out->result.request){s->credits[i].request=0;s->credits[i].completed=0;--s->pending;break;}s->prefer_status=1;return 1;
}
