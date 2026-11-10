#pragma once
#include "mod_internal.h"
#include "mod_provider_abi.h"
typedef struct ModProviders ModProviders;
typedef struct {uint64_t generation,owner;uint32_t slot,reserved;} ModProviderToken;
// One serialized control domain: mod worker (or standalone caller). Never UI/network threads.
ModProviders *mod_providers_start(const ModPlan *plan,ModLog log);
int mod_providers_token(ModProviders *p,uint64_t owner,ModProviderToken *out);
int mod_providers_stop_owner(ModProviders *p,uint64_t owner,ModProviderToken token);
int mod_providers_stop(ModProviders *p);
void mod_providers_free(ModProviders *p); // Only after stop succeeds; mapped modules stay resident.
#include "mod_provider_service_abi.h"
typedef struct {
    int available,pending,faulted,command_count;
    uint64_t generation,owner,capabilities;
    char service[65],text[257];XmsCommandSpec commands[XMS_COMMANDS];
} ModServiceView;
typedef struct {int terminal;XmsResult result;XmsStatus status;} ModServiceEvent;
void mod_provider_view(ModProviders *p,uint64_t owner,ModServiceView *out);
int mod_provider_submit(ModProviders *p,uint64_t owner,const char *command,int type,int64_t value,uint64_t *request);
void mod_provider_pump(ModProviders *p);
int mod_provider_take(ModProviders *p,uint64_t owner,ModServiceEvent *out);
