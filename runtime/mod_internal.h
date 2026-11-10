#pragma once
#include <stddef.h>
#include "mod_settings.h"
#define MOD_LIMIT 16
#define MOD_SOURCE_LIMIT 65536
#define MOD_MEMORY_LIMIT (1024u * 1024u)
typedef void (*ModLog)(const char *message);
typedef struct {
    char id[65], version[32], name[97], diagnostic[257];
    char requires[MOD_LIMIT][65];
    int require_count, rejected, enabled;
    unsigned dependencies;
    char *source;
    size_t source_size;
} ModPackage;
typedef struct { ModPackage mods[MOD_LIMIT]; int count; char root[4096],game_id[65]; } ModPlan;
typedef struct ModRuntime ModRuntime;
typedef int (*ModCancelled)(void);
ModPlan *mod_packages_read(const char *root, ModLog log);
void mod_packages_free(ModPlan *plan);
ModRuntime *mod_runtime_start(ModPlan *plan, ModLog log, ModCancelled cancelled);
int mod_runtime_event(ModRuntime *runtime, ModCancelled cancelled);
int mod_runtime_actions(ModRuntime *runtime,ModCancelled cancelled);
int mod_runtime_action(ModRuntime *runtime,const ModAction *action);
void mod_runtime_stop(ModRuntime *runtime);
