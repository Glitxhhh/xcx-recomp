// Copied host descriptions only. No Lua/engine/ImGui objects cross this interface.
#pragma once
#include <stdint.h>
#define MOD_UI_MAX 16
#define MOD_FIELD_MAX 8
#define MOD_ENUM_MAX 8
#define MOD_ACTION_MAX 32
#define MOD_BUTTON_MAX 4
typedef struct {char id[65],label[97];int enabled,provider;} ModButton;
enum { MOD_LOADED=1, MOD_DISABLED, MOD_INCOMPATIBLE, MOD_FAULTED, MOD_STARTING };
enum { MOD_BOOL=1, MOD_INTEGER, MOD_ENUM };
typedef struct { char id[65],label[97]; } ModOption;
typedef struct {
    char id[65],label[97];
    int type,minimum,maximum,value,default_value,option_count;
    ModOption options[MOD_ENUM_MAX];
} ModField;
typedef struct {
    char id[65],name[97],version[32],diagnostic[257],page_id[65],title[97],save_note[129];
    int status,field_count,persistence_dirty;
    ModButton buttons[MOD_BUTTON_MAX];int button_count,provider_available,provider_pending;
    char ui_status[257],provider_note[257];
    ModField fields[MOD_FIELD_MAX];
} ModView;
typedef struct {
    uint64_t generation,session;
    int count,pending,intent_pending;
    char notice[129];
    ModView mods[MOD_UI_MAX];
} ModSnapshot;
typedef struct ModAction { uint64_t generation,session; char mod[65],field[65]; int type,value; } ModAction;
void mod_ui_reset(const char *settings_root);
void mod_ui_publish(const ModView *view);
void mod_ui_snapshot(ModSnapshot *out);
void mod_ui_interaction(int active);
int mod_ui_submit(const ModAction *action);
int mod_ui_action_current(const ModAction *action);
int mod_ui_pop(ModAction *out);
void mod_ui_shutdown(void);
void mod_ui_start_failed(const char *reason);
void mod_settings_load(ModView *view);
int mod_settings_save(ModView *view);

typedef struct ModIntent {uint64_t generation,session;char mod[65],action[65];} ModIntent;
int mod_ui_intent_submit(const ModIntent *intent);
int mod_ui_intent_pop(ModIntent *intent);
int mod_ui_intent_current(const ModIntent *intent);
