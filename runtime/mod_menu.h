// Copied menu registrations and requests. No guest/controller pointer enters the registry.
#pragma once
#include <stdint.h>
typedef struct {char owner[65],entry[65],page[65],label[33];uint64_t epoch;} ModMenuEntry;
void mod_menu_reset(void);
void mod_menu_support(int available);
const char *mod_menu_register(const ModMenuEntry *entry);
int mod_menu_snapshot(ModMenuEntry *entry);
int mod_menu_request(void);
int mod_menu_take(ModMenuEntry *entry);
uint64_t mod_menu_epoch(void);
void mod_menu_invalidate(void);
