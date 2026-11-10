// Serialized Lua host domain. Source and event data are copied; no engine ABI is exposed.
#include "mod_internal.h"
#include "mod_provider.h"
#include "mod_menu.h"
#include "mod_text.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CALL_BUDGET 20000
#define DISPATCH_BUDGET 25000
typedef union { size_t capacity; long double align; void *pointer; } Allocation;
typedef struct {
    struct ModRuntime *runtime;
    ModPackage *package;
    lua_State *L;
    size_t used, peak, attempts, denied;
    ModView view;
    int action_field;
    char action_id[65];ModServiceEvent service_event;
    int refs[6], failed, initialized, phase, instructions, logs;
} ModState;
struct ModRuntime {
    ModPlan *plan;
    ModProviders *providers;
    ModCancelled cancelled;int stopping;
    ModLog log;
    ModState states[MOD_LIMIT];
    int order[MOD_LIMIT], count, aggregate, event_cursor;unsigned service_cursor,lane;
};
static void sync_view(ModState *m);
static void retire_native(ModState *m) {
    ModProviderToken token;uint64_t native_owner=(uint64_t)(m->package-m->runtime->plan->mods)+1;
    if(mod_providers_token(m->runtime->providers,native_owner,&token)&&!mod_providers_stop_owner(m->runtime->providers,native_owner,token)) {
        m->runtime->log("providers: fatal owner cleanup failure; Lua state retained");abort();
    }
    sync_view(m);mod_ui_publish(&m->view);
}
static const char *phase_names[]={"state","setup","load","chunk","descriptor","init","event","shutdown","close","settings","action","provider"};
static ModState *owner(lua_State *L) { return *(ModState **)lua_getextraspace(L); }
static void message(ModState *m,const char *kind,const char *s,size_t n) {
    char line[512];
    int at=snprintf(line,sizeof line,"mods: %s %s %s: ",m->package->id,kind,phase_names[m->phase]);
    if (n>256) n=256;
    for (size_t i=0;i<n && at<(int)sizeof(line)-1;++i) { unsigned char c=(unsigned char)s[i]; line[at++]=c>=32 && c!=127 ? (char)c : '?'; }
    line[at]=0; m->runtime->log(line);
    if(!strcmp(kind,"error")) {m->view.status=MOD_FAULTED;snprintf(m->view.diagnostic,sizeof m->view.diagnostic,"%s",line);mod_ui_publish(&m->view);}
}
static void fail(ModState *m,const char *s) { message(m,"error",s,strlen(s)); m->failed=1; retire_native(m); }
static void *allocate(void *context,void *ptr,size_t old,size_t n) {
    ModState *m=context; Allocation *block=ptr?(Allocation *)ptr-1:NULL;
    size_t capacity=block?block->capacity:0; (void)old;
    if (!n) { free(block); m->used-=capacity; return NULL; }
    if (n<=capacity) return ptr; // Lua requires shrinking requests to succeed.
    ++m->attempts;
#ifdef XCX_MOD_TESTING
    const char *id=getenv("MOD_FAIL_ID"), *phase=getenv("MOD_FAIL_PHASE"), *after=getenv("MOD_FAIL_AFTER");
    if (id && phase && !strcmp(id,m->package->id) && !strcmp(phase,phase_names[m->phase]) && m->attempts>(after?strtoul(after,NULL,10):0)) { ++m->denied; return NULL; }
#endif
    if (n-capacity>MOD_MEMORY_LIMIT-m->used) { ++m->denied; return NULL; }
    Allocation *p=realloc(block,sizeof(*p)+n); if (!p) return NULL;
    p->capacity=n; m->used=m->used-capacity+n; if (m->used>m->peak) m->peak=m->used;
    return p+1;
}
static void hook(lua_State *L,lua_Debug *ar) {
    (void)ar; ModState *m=owner(L); m->instructions-=100;
    if (m->phase==6 || m->phase>=9) { m->runtime->aggregate-=100; if (m->runtime->aggregate<=0) luaL_error(L,"aggregate dispatch budget exceeded"); }
    if (m->instructions<=0) luaL_error(L,"instruction budget exceeded");
}
static int log_message(lua_State *L) {
    ModState *m=owner(L); size_t n; luaL_checktype(L,1,LUA_TSTRING);
    const char *s=lua_tolstring(L,1,&n); if (n>256) return luaL_error(L,"log exceeds 256 bytes");
    if (m->logs++<16) message(m,"log",s,n); return 0;
}
static void select_fields(lua_State *L,int original,const char *const *names) {
    original=lua_absindex(L,original); lua_newtable(L);
    for (;*names;++names) { lua_getfield(L,original,*names); lua_setfield(L,-2,*names); }
}
static int settings_register(lua_State *L);
static int settings_get(lua_State *L);
static int menu_register(lua_State *L);
static int provider_status(lua_State *L);
static int provider_submit(lua_State *L);
static int ui_status(lua_State *L);
static int ui_enable(lua_State *L);
static void sync_view(ModState *m) {
    ModServiceView v;mod_provider_view(m->runtime->providers,(uint64_t)(m->package-m->runtime->plan->mods)+1,&v);
    m->view.provider_available=v.available;m->view.provider_pending=v.pending;snprintf(m->view.provider_note,sizeof m->view.provider_note,"%s",v.text);
}
static void opaque(char *out,size_t cap,uint64_t generation,uint64_t owner,uint64_t request) {snprintf(out,cap,"%016llx:%016llx:%016llx",(unsigned long long)generation,(unsigned long long)owner,(unsigned long long)request);}
static int setup(lua_State *L) {
    static const char *const base[]={"assert","error","ipairs","next","pairs","select","tonumber","tostring","type","_VERSION",NULL};
    static const char *const strings[]={"byte","char","len","lower","reverse","sub","upper",NULL};
    static const char *const tables[]={"concat","insert","pack","remove","unpack",NULL};
    static const char *const maths[]={"abs","ceil","floor","max","min","sqrt","huge","maxinteger","mininteger","pi","tointeger","type",NULL};
    luaopen_base(L); select_fields(L,-1,base); lua_remove(L,-2);
    lua_pushvalue(L,-1); lua_setfield(L,-2,"_G"); lua_rawseti(L,LUA_REGISTRYINDEX,LUA_RIDX_GLOBALS);
    luaopen_string(L); select_fields(L,-1,strings); lua_remove(L,-2);
    // String method lookup must use the restricted table too.
    lua_pushliteral(L,""); lua_getmetatable(L,-1); lua_pushvalue(L,-3); lua_setfield(L,-2,"__index"); lua_pop(L,2); lua_setglobal(L,"string");
    luaopen_table(L); select_fields(L,-1,tables); lua_remove(L,-2); lua_setglobal(L,"table");
    luaopen_math(L); select_fields(L,-1,maths); lua_remove(L,-2); lua_setglobal(L,"math");
    lua_newtable(L); lua_pushcfunction(L,log_message); lua_setfield(L,-2,"log");
    lua_newtable(L);lua_pushcfunction(L,settings_register);lua_setfield(L,-2,"register");lua_pushcfunction(L,settings_get);lua_setfield(L,-2,"get");lua_setfield(L,-2,"settings");
    lua_newtable(L);lua_pushcfunction(L,provider_status);lua_setfield(L,-2,"status");lua_pushcfunction(L,provider_submit);lua_setfield(L,-2,"submit");lua_setfield(L,-2,"provider");
    lua_newtable(L);lua_pushcfunction(L,ui_status);lua_setfield(L,-2,"status");lua_pushcfunction(L,ui_enable);lua_setfield(L,-2,"enable");lua_setfield(L,-2,"ui");
    lua_newtable(L);lua_pushcfunction(L,menu_register);lua_setfield(L,-2,"register");lua_setfield(L,-2,"menu");
    lua_pushinteger(L,1); lua_setfield(L,-2,"api"); lua_pushstring(L,owner(L)->package->id); lua_setfield(L,-2,"id"); lua_setglobal(L,"host");
    return 0;
}
static int load(lua_State *L) {
    ModPackage *p=owner(L)->package;
    if (luaL_loadbufferx(L,p->source,p->source_size,p->id,"t")!=LUA_OK) return lua_error(L);
    return 1;
}
static int discover(lua_State *L) {
    ModState *m=owner(L); luaL_checktype(L,1,LUA_TTABLE);
    lua_pushliteral(L,"shutdown"); lua_rawget(L,1); luaL_checktype(L,-1,LUA_TFUNCTION); m->refs[2]=luaL_ref(L,LUA_REGISTRYINDEX);
    lua_pushliteral(L,"init"); lua_rawget(L,1); luaL_checktype(L,-1,LUA_TFUNCTION); m->refs[0]=luaL_ref(L,LUA_REGISTRYINDEX);
    lua_pushliteral(L,"events"); lua_rawget(L,1); luaL_checktype(L,-1,LUA_TTABLE);
    lua_pushnil(L);
    while (lua_next(L,-2)) {
        size_t size=0; const char *name=lua_type(L,-2)==LUA_TSTRING?lua_tolstring(L,-2,&size):NULL;
        if (!name || !((size==12 && !memcmp(name,"host.started",12)) || (size==16 && !memcmp(name,"settings.changed",16)) || (size==9 && !memcmp(name,"ui.action",9)) || (size==14 && !memcmp(name,"provider.event",14)))) return luaL_error(L,"unsupported event name");
        lua_pop(L,1);
    }
    lua_pushliteral(L,"host.started"); lua_rawget(L,-2); luaL_checktype(L,-1,LUA_TFUNCTION); m->refs[1]=luaL_ref(L,LUA_REGISTRYINDEX);
    lua_pushliteral(L,"settings.changed");lua_rawget(L,-2);
    if(!lua_isnil(L,-1)) {luaL_checktype(L,-1,LUA_TFUNCTION);m->refs[3]=luaL_ref(L,LUA_REGISTRYINDEX);} else lua_pop(L,1);
    const char *extra[]={"ui.action","provider.event"};
    for(int i=0;i<2;++i){lua_pushstring(L,extra[i]);lua_rawget(L,-2);if(!lua_isnil(L,-1)){luaL_checktype(L,-1,LUA_TFUNCTION);m->refs[4+i]=luaL_ref(L,LUA_REGISTRYINDEX);}else lua_pop(L,1);}
    return 0;
}
static int invoke(lua_State *L) {
    ModState *m=owner(L); int slot=m->phase==5?0:m->phase==6?1:m->phase==9?3:m->phase==10?4:m->phase==11?5:2;
    lua_rawgeti(L,LUA_REGISTRYINDEX,m->refs[slot]);
    if (slot==1) { lua_newtable(L); lua_newtable(L);lua_pushcfunction(L,menu_register);lua_setfield(L,-2,"register");lua_setfield(L,-2,"menu");
    lua_pushinteger(L,1); lua_setfield(L,-2,"api"); lua_pushliteral(L,"startup"); lua_setfield(L,-2,"phase"); }
    if(slot==3) {
        ModField *f=&m->view.fields[m->action_field];lua_newtable(L);
        lua_pushstring(L,m->view.page_id);lua_setfield(L,-2,"page");lua_pushstring(L,f->id);lua_setfield(L,-2,"field");
        if(f->type==MOD_BOOL)lua_pushboolean(L,f->value);else if(f->type==MOD_ENUM)lua_pushstring(L,f->options[f->value].id);else lua_pushinteger(L,f->value);
        lua_setfield(L,-2,"value");
    }
    if(slot==4){lua_newtable(L);lua_pushstring(L,m->action_id);lua_setfield(L,-2,"action");}
    if(slot==5){ModServiceEvent *e=&m->service_event;lua_newtable(L);char handle[64];opaque(handle,sizeof handle,e->terminal?e->result.generation:e->status.generation,e->terminal?e->result.owner:e->status.owner,0);lua_pushstring(L,handle);lua_setfield(L,-2,"handle");lua_pushstring(L,e->terminal?"result":"status");lua_setfield(L,-2,"kind");
        if(e->terminal){char request[64];opaque(request,sizeof request,e->result.generation,e->result.owner,e->result.request);lua_pushstring(L,request);lua_setfield(L,-2,"request");lua_pushstring(L,e->result.result==XMP_OK?"ok":e->result.result==XMP_INVALID?"rejected":"failed");lua_setfield(L,-2,"result");lua_pushstring(L,e->result.message);lua_setfield(L,-2,"message");}
        else{lua_pushstring(L,e->status.text);lua_setfield(L,-2,"message");}
    }
    lua_call(L,slot==1 || slot==3 || slot==4 || slot==5,0); return 0;
}
static int call(ModState *m,lua_CFunction fn,int args,int results,int phase) {
    m->phase=phase; m->instructions=CALL_BUDGET; m->attempts=m->denied=0;
    // Zero-upvalue C functions don't allocate. Arguments are prepared inside the trampoline.
    if (fn) { lua_pushcfunction(m->L,fn); if (args) lua_insert(m->L,-1-args); }
    lua_sethook(m->L,hook,LUA_MASKCOUNT,100);
    int status=lua_pcall(m->L,args,results,0); lua_sethook(m->L,NULL,0,0);
    if (status!=LUA_OK) {
        size_t n=0; const char *s=lua_type(m->L,-1)==LUA_TSTRING?lua_tolstring(m->L,-1,&n):NULL;
        message(m,"error",s?s:"non-string error",s?n:16); m->failed=1; lua_pop(m->L,1); retire_native(m);
    }
#ifdef XCX_MOD_TESTING
    char line[160]; snprintf(line,sizeof line,"alloc %s %s attempts=%zu denied=%zu",m->package->id,phase_names[phase],m->attempts,m->denied); m->runtime->log(line);
#endif
    return status==LUA_OK;
}
static int unavailable(ModRuntime *rt,ModState *m) {
    for (int i=0;i<rt->plan->count;++i) if ((m->package->dependencies&(1u<<i)) && (!rt->states[i].initialized || rt->states[i].failed)) return 1;
    return 0;
}
ModRuntime *mod_runtime_start(ModPlan *plan,ModLog log,ModCancelled cancelled) {
    ModRuntime *rt=calloc(1,sizeof *rt); if (!rt) { log("mods: runtime allocation failed"); mod_ui_start_failed("Native mod runtime allocation failed; restart to retry."); return NULL; }
    rt->plan=plan; rt->log=log;rt->cancelled=cancelled;
    rt->providers=mod_providers_start(plan,log);
    unsigned processed=0;
    for (int i=0;i<plan->count;++i) {
        ModState *m=&rt->states[i]; m->runtime=rt; m->package=&plan->mods[i]; m->failed=m->package->rejected;
        strcpy(m->view.id,m->package->id);strcpy(m->view.version,m->package->version);snprintf(m->view.name,sizeof m->view.name,"%s",m->package->name[0]?m->package->name:m->package->id);
        m->view.status=m->failed?MOD_INCOMPATIBLE:MOD_STARTING;
        for (int r=0;r<6;++r) m->refs[r]=LUA_NOREF;
        if (m->failed) processed|=1u<<i;
    }
    for (int step=0;step<plan->count;++step) {
        int chosen=-1;
        for (int i=0;i<plan->count;++i) if (!(processed&(1u<<i)) && (plan->mods[i].dependencies&processed)==plan->mods[i].dependencies) { chosen=i; break; }
        if (chosen<0 || (cancelled && cancelled())) break;
        processed|=1u<<chosen; rt->order[rt->count++]=chosen; ModState *m=&rt->states[chosen];
        if (unavailable(rt,m)) { fail(m,"dependency unavailable"); continue; }
        m->L=lua_newstate(allocate,m,0);
#ifdef XCX_MOD_TESTING
        char state_line[160]; snprintf(state_line,sizeof state_line,"alloc %s state attempts=%zu denied=%zu",m->package->id,m->attempts,m->denied); log(state_line);
#endif
        if (!m->L) { fail(m,"state allocation failed"); continue; }
        *(ModState **)lua_getextraspace(m->L)=m;
        if (call(m,setup,0,0,1) && call(m,load,0,1,2) && call(m,NULL,0,1,3) && call(m,discover,1,0,4) && call(m,invoke,0,0,5)) {
            m->initialized=1; m->view.status=MOD_LOADED;sync_view(m);mod_ui_publish(&m->view); message(m,"ready",m->package->version,strlen(m->package->version));
        }
    }
    return rt;
}
int mod_runtime_event(ModRuntime *rt,ModCancelled cancelled) {
    if (!rt) return 0;
    rt->aggregate=DISPATCH_BUDGET;
    while (rt->event_cursor<rt->count && rt->aggregate>=100 && !(cancelled && cancelled())) {
        ModState *m=&rt->states[rt->order[rt->event_cursor++]];
        if (m->failed || !m->initialized) continue;
        if (unavailable(rt,m)) { m->phase=6; fail(m,"dependency unavailable"); continue; }
        rt->aggregate-=100; call(m,invoke,0,0,6);
    }
    return rt->event_cursor<rt->count && !(cancelled && cancelled());
}
static int apply_action(ModRuntime *rt,const ModAction *a) {
    if(!rt)return 0;
    if(!mod_ui_action_current(a))return 0;
    for(int n=0;n<rt->count;++n) {
        ModState *m=&rt->states[rt->order[n]];
        if(m->failed || !m->initialized)continue;
        if(unavailable(rt,m)) {m->phase=9;fail(m,"dependency unavailable");continue;}
        if(strcmp(a->mod,m->view.id))continue;
        for(int f=0;f<m->view.field_count;++f) {
            ModField *field=&m->view.fields[f];
            if(strcmp(a->field,field->id) || field->type!=a->type || a->value<field->minimum || a->value>field->maximum)continue;
            if(field->value==a->value) {
                if(m->view.persistence_dirty){mod_settings_save(&m->view);mod_ui_publish(&m->view);}
                return 1; // Persistence retry is independent of value-change delivery.
            }
            field->value=a->value;m->action_field=f;
            mod_settings_save(&m->view);
            if(m->refs[3]!=LUA_NOREF) {rt->aggregate-=100;call(m,invoke,0,0,9);}
            mod_ui_publish(&m->view);
            for(int j=0;j<rt->count;++j) {ModState *dependent=&rt->states[rt->order[j]];if(!dependent->failed&&dependent->initialized&&unavailable(rt,dependent)){dependent->phase=9;fail(dependent,"dependency unavailable");}}
            return 1;
        }
    }
    return 0;
}
int mod_runtime_action(ModRuntime *rt,const ModAction *a) {
    if(!rt)return 0;rt->aggregate=DISPATCH_BUDGET;return apply_action(rt,a);
}
static int action_intent(ModRuntime *rt,const ModIntent *intent) {
    if(!mod_ui_intent_current(intent))return 0;
    for(int i=0;i<rt->count;++i){ModState *m=&rt->states[rt->order[i]];if(strcmp(m->view.id,intent->mod)||m->failed||!m->initialized)continue;
        if(unavailable(rt,m)){m->phase=10;fail(m,"dependency unavailable");return 0;}
        for(int j=0;j<m->view.button_count;++j){ModButton *b=&m->view.buttons[j];if(strcmp(b->id,intent->action)||!b->enabled||m->refs[4]==LUA_NOREF)continue;
            sync_view(m);if(b->provider&&(!m->view.provider_available||m->view.provider_pending))return 0;
            strcpy(m->action_id,b->id);rt->aggregate-=100;call(m,invoke,0,0,10);sync_view(m);mod_ui_publish(&m->view);return 1;
        }
    }return 0;
}
static int provider_delivery(ModRuntime *rt) {
    for(int n=0;n<rt->count;++n){int i=rt->service_cursor++%rt->count;ModState *m=&rt->states[rt->order[i]];
        if(m->failed||!m->initialized)continue;if(unavailable(rt,m)){m->phase=11;fail(m,"dependency unavailable");continue;}
        if(!mod_provider_take(rt->providers,(uint64_t)(m->package-rt->plan->mods)+1,&m->service_event))continue;
        if(m->refs[5]!=LUA_NOREF){rt->aggregate-=100;call(m,invoke,0,0,11);}sync_view(m);mod_ui_publish(&m->view);return 1;
    }return 0;
}
int mod_runtime_actions(ModRuntime *rt,ModCancelled cancelled) {
    if(!rt||rt->stopping||(cancelled&&cancelled()))return 0;rt->aggregate=DISPATCH_BUDGET;int processed=0;mod_provider_pump(rt->providers);
    for(int i=0;i<rt->count;++i){ModState *m=&rt->states[rt->order[i]];if(!m->failed&&m->initialized){if(unavailable(rt,m)){m->phase=11;fail(m,"dependency unavailable");}else{sync_view(m);mod_ui_publish(&m->view);}}}
    while(processed<8 && rt->aggregate>=100 && !(cancelled&&cancelled())) {
        int work=0;for(int try=0;try<3&&!work;++try){int lane=rt->lane++%3;
            if(lane==0){ModAction a;if(mod_ui_pop(&a)){apply_action(rt,&a);work=1;}}
            else if(lane==1){ModIntent a;if(mod_ui_intent_pop(&a)){action_intent(rt,&a);work=1;}}
            else work=provider_delivery(rt);
        }if(!work)break;++processed;
    }return processed;
}
void mod_runtime_stop(ModRuntime *rt) {
    if (!rt) return;
    rt->stopping=1;mod_menu_support(0);mod_menu_invalidate();
    if(!mod_providers_stop(rt->providers)){rt->log("providers: fatal cleanup failure; Lua states not closed");abort();}
    mod_providers_free(rt->providers);rt->providers=NULL;
    for (int n=rt->count-1;n>=0;--n) {
        ModState *m=&rt->states[rt->order[n]];
        m->initialized=0;
        if (m->L) {
            if (m->refs[2]!=LUA_NOREF) call(m,invoke,0,0,7);
            m->phase=8; m->attempts=m->denied=0; lua_close(m->L); m->L=NULL;
        }
        for (int r=0;r<6;++r) m->refs[r]=LUA_NOREF;
        char line[192]; snprintf(line,sizeof line,"mods: %s closed bytes=%zu callbacks=0",m->package->id,m->used); rt->log(line);
        if (m->used) rt->log("mods: allocator accounting violation");
    }
    free(rt);
}

static void schema_text(lua_State *L,int table,const char *key,char *out,size_t cap,int id) {
    table=lua_absindex(L,table);lua_pushstring(L,key);lua_rawget(L,table);luaL_checktype(L,-1,LUA_TSTRING);
    size_t n;const unsigned char *s=(const unsigned char *)lua_tolstring(L,-1,&n);
    if(!n || n>=cap || memchr(s,0,n))luaL_error(L,"invalid bounded settings label/ID");
    for(size_t i=0;i<n;) {
        unsigned c=s[i++];
        if(id) {if((i==1 && !(c>='a'&&c<='z')) || (!(c>='a'&&c<='z')&&!(c>='0'&&c<='9')&&c!='_'&&c!='-'&&c!='.'))luaL_error(L,"invalid settings ID");}
        else {
            if(c<32 || c==127)luaL_error(L,"control character in settings label");
            if(c>=128) {
                unsigned code;int more;
                if(c>=0xc2&&c<=0xdf){code=c&31;more=1;}else if(c>=0xe0&&c<=0xef){code=c&15;more=2;}else if(c>=0xf0&&c<=0xf4){code=c&7;more=3;}else {luaL_error(L,"invalid UTF-8 label");return;}
                int width=more;
                while(more--) {if(i>=n || (s[i]&0xc0)!=0x80)luaL_error(L,"invalid UTF-8 label");code=(code<<6)|(s[i++]&63);}
                if((width==2&&code<0x800)||(width==3&&code<0x10000)||code>0x10ffff||(code>=0xd800&&code<=0xdfff))luaL_error(L,"invalid UTF-8 label");
            }
        }
    }
    memcpy(out,s,n);out[n]=0;lua_pop(L,1);
}
static int schema_integer(lua_State *L,int table,const char *key) {
    table=lua_absindex(L,table);lua_pushstring(L,key);lua_rawget(L,table);
    if(!lua_isinteger(L,-1))luaL_error(L,"settings integer required");
    lua_Integer n=lua_tointeger(L,-1);if(n < -1000000 || n>1000000)luaL_error(L,"settings integer exceeds host limits");lua_pop(L,1);return(int)n;
}
static int settings_register(lua_State *L) {
    ModState *m=owner(L);
    if(m->phase!=5 || m->view.page_id[0])return luaL_error(L,"settings registration is init-only and once per mod");
    luaL_checktype(L,1,LUA_TTABLE);
    if(schema_integer(L,1,"version")!=1)return luaL_error(L,"unsupported settings schema version");
    ModView candidate=m->view;
    schema_text(L,1,"id",candidate.page_id,sizeof candidate.page_id,1);
    schema_text(L,1,"title",candidate.title,sizeof candidate.title,0);
    lua_pushliteral(L,"actions");lua_rawget(L,1);
    if(!lua_isnil(L,-1)){
        luaL_checktype(L,-1,LUA_TTABLE);size_t n=lua_rawlen(L,-1);if(!n||n>MOD_BUTTON_MAX||m->refs[4]==LUA_NOREF)return luaL_error(L,"invalid actions or missing ui.action handler");candidate.button_count=(int)n;int actions=lua_gettop(L);
        for(size_t i=0;i<n;++i){lua_rawgeti(L,actions,(lua_Integer)i+1);luaL_checktype(L,-1,LUA_TTABLE);ModButton *b=&candidate.buttons[i];schema_text(L,-1,"id",b->id,sizeof b->id,1);schema_text(L,-1,"label",b->label,sizeof b->label,0);b->enabled=1;
            lua_pushliteral(L,"provider");lua_rawget(L,-2);if(!lua_isnil(L,-1)){luaL_checktype(L,-1,LUA_TBOOLEAN);b->provider=lua_toboolean(L,-1);}lua_pop(L,1);
            for(size_t j=0;j<i;++j)if(!strcmp(b->id,candidate.buttons[j].id))return luaL_error(L,"duplicate action ID");lua_pop(L,1);
        }
    }lua_pop(L,1);
    lua_pushliteral(L,"fields");lua_rawget(L,1);luaL_checktype(L,-1,LUA_TTABLE);
    int fields=lua_gettop(L);size_t count=lua_rawlen(L,fields);
    if((!count&&!candidate.button_count) || count>MOD_FIELD_MAX)return luaL_error(L,"settings field count exceeds bounds");
    candidate.field_count=(int)count;
    for(size_t i=0;i<count;++i) {
        ModField *f=&candidate.fields[i];lua_rawgeti(L,fields,(lua_Integer)i+1);luaL_checktype(L,-1,LUA_TTABLE);int table=lua_gettop(L);
        schema_text(L,table,"id",f->id,sizeof f->id,1);schema_text(L,table,"label",f->label,sizeof f->label,0);
        for(size_t j=0;j<i;++j)if(!strcmp(f->id,candidate.fields[j].id))return luaL_error(L,"duplicate settings field ID");
        char type[16];schema_text(L,table,"type",type,sizeof type,1);
        if(!strcmp(type,"bool")) {
            f->type=MOD_BOOL;f->minimum=0;f->maximum=1;lua_pushliteral(L,"default");lua_rawget(L,table);luaL_checktype(L,-1,LUA_TBOOLEAN);f->value=lua_toboolean(L,-1);lua_pop(L,1);
        } else if(!strcmp(type,"integer")) {
            f->type=MOD_INTEGER;f->minimum=schema_integer(L,table,"min");f->maximum=schema_integer(L,table,"max");f->value=schema_integer(L,table,"default");
            if(f->minimum>f->maximum || f->value<f->minimum || f->value>f->maximum)return luaL_error(L,"invalid settings integer range/default");
        } else if(!strcmp(type,"enum")) {
            f->type=MOD_ENUM;lua_pushliteral(L,"options");lua_rawget(L,table);luaL_checktype(L,-1,LUA_TTABLE);int options=lua_gettop(L);size_t n=lua_rawlen(L,options);
            if(n<2 || n>MOD_ENUM_MAX)return luaL_error(L,"invalid settings enum size");f->option_count=(int)n;f->minimum=0;f->maximum=(int)n-1;
            for(size_t j=0;j<n;++j) {
                lua_rawgeti(L,options,(lua_Integer)j+1);luaL_checktype(L,-1,LUA_TTABLE);
                schema_text(L,-1,"id",f->options[j].id,sizeof f->options[j].id,1);schema_text(L,-1,"label",f->options[j].label,sizeof f->options[j].label,0);
                for(size_t k=0;k<j;++k)if(!strcmp(f->options[j].id,f->options[k].id))return luaL_error(L,"duplicate enum option ID");lua_pop(L,1);
            }
            lua_pop(L,1);char def[65];schema_text(L,table,"default",def,sizeof def,1);f->value=-1;
            for(int j=0;j<f->option_count;++j)if(!strcmp(def,f->options[j].id))f->value=j;
            if(f->value<0)return luaL_error(L,"invalid enum default");
        } else return luaL_error(L,"unsupported settings type");
        f->default_value=f->value;lua_pop(L,1);
    }
    mod_settings_load(&candidate);m->view=candidate;return 0;
}
static int settings_get(lua_State *L) {
    ModState *m=owner(L);luaL_checktype(L,1,LUA_TSTRING);size_t n;const char *id=lua_tolstring(L,1,&n);
    for(int i=0;i<m->view.field_count;++i) {
        ModField *f=&m->view.fields[i];if(strlen(f->id)!=n || memcmp(id,f->id,n))continue;
        if(f->type==MOD_BOOL)lua_pushboolean(L,f->value);else if(f->type==MOD_ENUM)lua_pushstring(L,f->options[f->value].id);else lua_pushinteger(L,f->value);return 1;
    }
    return luaL_error(L,"unknown settings field");
}

static int provider_status(lua_State *L) {
    ModState *m=owner(L);ModServiceView v;mod_provider_view(m->runtime->providers,(uint64_t)(m->package-m->runtime->plan->mods)+1,&v);
    if(m->phase==7){v.available=0;v.pending=0;strcpy(v.text,"Provider stopped");}
    lua_newtable(L);lua_pushboolean(L,v.available);lua_setfield(L,-2,"available");lua_pushstring(L,v.text);lua_setfield(L,-2,"message");lua_pushstring(L,v.service);lua_setfield(L,-2,"service");lua_pushinteger(L,v.pending);lua_setfield(L,-2,"pending");
    char token[64];opaque(token,sizeof token,v.generation,v.owner,0);lua_pushstring(L,token);lua_setfield(L,-2,"handle");snprintf(token,sizeof token,"%016llx",(unsigned long long)v.capabilities);lua_pushstring(L,token);lua_setfield(L,-2,"capabilities");
    lua_newtable(L);for(int i=0;i<v.command_count;++i){const XmsCommandSpec *c=&v.commands[i];lua_newtable(L);lua_pushstring(L,c->id);lua_setfield(L,-2,"id");lua_pushstring(L,c->type==XMS_NONE?"none":c->type==XMS_BOOL?"bool":"integer");lua_setfield(L,-2,"type");lua_pushinteger(L,c->minimum);lua_setfield(L,-2,"min");lua_pushinteger(L,c->maximum);lua_setfield(L,-2,"max");lua_rawseti(L,-2,i+1);}lua_setfield(L,-2,"commands");return 1;
}
static int provider_submit(lua_State *L) {
    ModState *m=owner(L);if(!m->initialized || m->failed || m->runtime->stopping || (m->runtime->cancelled&&m->runtime->cancelled()) || m->phase==7 || m->refs[5]==LUA_NOREF){lua_pushnil(L);lua_pushliteral(L,"stopped or missing provider.event handler");return 2;}
    luaL_checktype(L,1,LUA_TSTRING);size_t n;const char *name=lua_tolstring(L,1,&n);if(!n||n>64||memchr(name,0,n))return luaL_error(L,"invalid command ID");
    ModServiceView v;uint64_t own=(uint64_t)(m->package-m->runtime->plan->mods)+1;mod_provider_view(m->runtime->providers,own,&v);
    if(!v.available){lua_pushnil(L);lua_pushstring(L,v.text);return 2;}
    const XmsCommandSpec *c=NULL;for(int i=0;i<v.command_count;++i)if(!strcmp(v.commands[i].id,name))c=&v.commands[i];if(!c)return luaL_error(L,"unknown provider command");
    int64_t value=0;if(c->type==XMS_NONE){if(!lua_isnoneornil(L,2))return luaL_error(L,"command takes no argument");}
    else if(c->type==XMS_BOOL){luaL_checktype(L,2,LUA_TBOOLEAN);value=lua_toboolean(L,2);}
    else{if(!lua_isinteger(L,2))return luaL_error(L,"command requires integer");value=lua_tointeger(L,2);if(value<c->minimum||value>c->maximum)return luaL_error(L,"command integer outside range");}
    uint64_t request=0;int result=mod_provider_submit(m->runtime->providers,own,name,(int)c->type,value,&request);
    if(result!=XMP_OK){lua_pushnil(L);lua_pushstring(L,result==XMP_BUSY?"busy":result==XMP_INVALID?"rejected":result==XMP_INCOMPATIBLE?"unavailable":"failed");return 2;}
    char token[64];opaque(token,sizeof token,v.generation,own,request);lua_pushstring(L,token);return 1;
}
static int ui_status(lua_State *L){ModState *m=owner(L);luaL_checktype(L,1,LUA_TSTRING);size_t n;const char *text=lua_tolstring(L,1,&n);if(n>256||!mod_text_valid(text,n))return luaL_error(L,"invalid bounded UI status");memcpy(m->view.ui_status,text,n);m->view.ui_status[n]=0;if(m->initialized)mod_ui_publish(&m->view);return 0;}
static int ui_enable(lua_State *L){ModState *m=owner(L);luaL_checktype(L,1,LUA_TSTRING);luaL_checktype(L,2,LUA_TBOOLEAN);size_t n;const char *id=lua_tolstring(L,1,&n);for(int i=0;i<m->view.button_count;++i)if(strlen(m->view.buttons[i].id)==n&&!memcmp(id,m->view.buttons[i].id,n)){m->view.buttons[i].enabled=lua_toboolean(L,2);if(m->initialized)mod_ui_publish(&m->view);return 0;}return luaL_error(L,"unknown action ID");}

static int menu_register(lua_State *L){
 ModState *m=owner(L);if(m->phase!=5){lua_pushnil(L);lua_pushliteral(L,"unavailable");return 2;}
 luaL_checktype(L,1,LUA_TTABLE);lua_getfield(L,1,"version");lua_Integer version=lua_isinteger(L,-1)?lua_tointeger(L,-1):0;lua_pop(L,1);
 lua_getfield(L,1,"location");size_t n;const char *location=lua_tolstring(L,-1,&n);int supported=version==1&&location&&n==6&&!memcmp(location,"online",6);lua_pop(L,1);
 if(!supported){lua_pushnil(L);lua_pushliteral(L,"unsupported");return 2;}
 ModMenuEntry e={0};snprintf(e.owner,sizeof e.owner,"%s",m->package->id);
 const char *keys[]={"id","page","label"};char *dest[]={e.entry,e.page,e.label};size_t caps[]={sizeof e.entry,sizeof e.page,sizeof e.label};
 for(int i=0;i<3;++i){lua_getfield(L,1,keys[i]);luaL_checktype(L,-1,LUA_TSTRING);const char *v=lua_tolstring(L,-1,&n);if(!n||n>=caps[i]||memchr(v,0,n))return luaL_error(L,"invalid menu string");for(size_t j=0;j<n;++j)if(i==2?!((v[j]>='a'&&v[j]<='z')||(v[j]>='A'&&v[j]<='Z')||(v[j]>='0'&&v[j]<='9')||v[j]==' '||v[j]=='-'||v[j]=='_'||v[j]=='.'||v[j]=='('||v[j]==')'):!((v[j]>='a'&&v[j]<='z')||(j&&((v[j]>='0'&&v[j]<='9')||v[j]=='.'||v[j]=='_'||v[j]=='-'))))return luaL_error(L,"invalid menu label or ID");memcpy(dest[i],v,n);lua_pop(L,1);}
 if(!m->view.page_id[0]||strcmp(e.page,m->view.page_id)){lua_pushnil(L);lua_pushliteral(L,"unavailable");return 2;}
 const char *error=mod_menu_register(&e);if(error){lua_pushnil(L);lua_pushstring(L,error);return 2;}lua_pushboolean(L,1);return 1;
}
