// The in-game bug report (runtime/bug_report.c): one zip with what a report needs, for the menu's "Make a bug report".
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Writes bug-reports/xcx-bug-report-<local time>.zip beside xcx.exe and its path into out. The zip holds
//   logs/xcx.log, logs/xcx.1.log   this run's log and the previous run's (or the file stderr goes to)
//   crash.txt, crash.1.txt, ...    the crash reports kept (rt_crash_report_nth), newest first
//   xcx.ini                        the settings file beside xcx.exe
//   system-info.txt                the version line and sysinfo, the text given (sysinfo_text on the caller's thread)
//   mods/*.txt, mods/folders.txt   the mods folder's list files and its entries
//   save/...                       with_save only: the committed save under <profile>/save
// Every entry but the save is masked (pii_mask.h, bug_report_masker): paths keep only the part below the game's own
// folders or their file name, and names and addresses become placeholders. Runs on any thread and takes long for a large log, so the menu calls it from a thread of its own. NULL on success,
// else an English reason (logged too).
const char *bug_report_write(int with_save, const char *sysinfo, char *out, size_t cap);

// The masker the zip's text entries go through: pii_mask_new with the game's folders as roots (<logs>, <mods>,
// <profile>, <game>). NULL when out of memory. Free with pii_mask_free.
struct PiiMask *bug_report_masker(void);

// "XCX Recomp 0.1.19, game 2.0.0" plus the build date: the first line of system-info.txt and of a crash report.
void bug_report_version_line(char *out, size_t cap);

#ifdef __cplusplus
}
#endif
