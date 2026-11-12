// The mod library in the F1 menu's Mods tab: the library's index (github.com/jmars125/xcx-recomp-mods) joined with
// the mods in the mods folder, and installs, updates, removals and on/off switches, the same operations as XCX
// Setup's Mods window (installer/mods.py), on a worker thread.
#pragma once
#include <stdint.h>

#define MODLIB_ROWS 64

typedef struct {
    char id[65];
    char kind[8];             // "romfs" (game files) or "lua"
    char name[96];
    char author[64];
    char description[400];
    char notes[600];
    char homepage[200];
    char version[32];         // the library's version; empty when the mod is not in the library
    uint64_t size;            // bytes of the download (a romfs mod's zip)
    int in_library, installed, enabled, update;
} ModLibRow;

enum { MODLIB_IDLE, MODLIB_LOADING, MODLIB_WORKING };

typedef struct {
    int count;
    ModLibRow rows[MODLIB_ROWS];
    int state;                // MODLIB_*
    char job[65];             // the mod the running job works on
    uint64_t done, total;     // download progress of the running job
    char message[400];        // the last job's outcome; English reasons
    int message_error;
    int index_ok;             // the library was read
    uint64_t generation;      // changes whenever the view does
} ModLibView;

// The current view. The first call starts reading the library.
void modlib_snapshot(ModLibView *v);
// Reads the library again (a no-op while a job runs).
void modlib_refresh(void);
// Installs or updates a library mod; a mod that was on stays on, a new one starts off.
void modlib_install(const char *id);
// Turns the mod off and deletes its folder.
void modlib_remove(const char *id);
// romfs mods switch while the game runs; Lua mods apply at the next start.
void modlib_set_enabled(const char *id, int on);
