// Native lifecycle only. No guest, renderer or input access.
#pragma once
void rt_mods_start(void);
void rt_mods_shutdown(void);
typedef struct ModAction ModAction;
int rt_mods_ui_submit(const ModAction *action);

struct ModIntent;
int rt_mods_ui_intent(const struct ModIntent *intent);
