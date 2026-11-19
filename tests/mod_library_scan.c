// The game's reading of the mods folder (runtime/mod_library.c: normalise and rescan) on its own, for
// tests/mods_manager_test.py, which compares it with XCX Setup's (installer/mods.py). Prints one line per note
// ("note <error> <text>") and per mod row ("row <id> <kind> <name>"). With an id and a game path, it also turns that
// mod on and asks runtime/romfs_mods.c for the path ("lookup <found> <host path>").
//
//   mod_library_scan.exe                      the mods folder is XCX_MOD_DIR
//   mod_library_scan.exe <id> rom:/<path>
#include <stdarg.h>
#include "../runtime/mod_library.c"

void rt_log(const char *fmt, ...) { (void)fmt; }
void rt_mods_shutdown(void) {}
const char *mod_fetch(const char *url, const char *path, uint64_t max, void (*progress)(uint64_t, uint64_t, void *),
                      void *ctx) {
    (void)url; (void)path; (void)max; (void)progress; (void)ctx;
    return "offline";
}
const char *mod_zip_unpack_romfs(const uint8_t *zip, size_t n, const char *dir) {
    (void)zip; (void)n; (void)dir;
    return "not in this test";
}

int main(int argc, char **argv) {
    normalise();
    rescan();
    for (int i = 0; i < view.nnotes; i++) printf("note %d %s\n", view.notes[i].error, view.notes[i].text);
    for (int i = 0; i < view.count; i++) printf("row %s %s %s\n", view.rows[i].id, view.rows[i].kind, view.rows[i].name);
    if (argc > 2) {
        char out[4200] = "";
        romfs_mods_set(argv[1], 1);
        int found = romfs_mod_file(argv[2], out, sizeof out);
        printf("lookup %d %s\n", found, out);
    }
    return 0;
}
