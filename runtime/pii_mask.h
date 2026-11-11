// Masking of personal information in text (runtime/pii_mask.c), for the copies of the logs and reports the bug report
// zip carries (runtime/bug_report.c). The files on the disk are not touched.
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct PiiMask PiiMask;

// A masker with the player's names from the OS (plat_identity): login name, account display name, computer name, home
// folder and its 8.3 form. NULL when out of memory.
PiiMask *pii_mask_new(void);
void pii_mask_free(PiiMask *m);
// A folder whose paths keep the part below it, behind tag ("<game>", "<profile>", "<mods>", "<logs>"). Longest wins;
// a folder given twice keeps its first tag.
void pii_mask_root(PiiMask *m, const char *path, const char *tag);
// One more name to replace with "<user>" (the probe's test names).
void pii_mask_name(PiiMask *m, const char *name);

// The masked copy of text (malloc'd; its length in *out_n), NULL when out of memory:
// - every absolute path (C:\..., c:/..., \\server\share\..., \\?\ and \\.\ paths, file:// URLs, %VAR%\..., ~/...,
//   $HOME/..., /home/... and any other rooted Unix path of two or more parts) becomes the tag of the root folder it is
//   in plus the part below it, else "<path>" plus its last part (the file name), so no folder of it shows;
// - the login name, the account's display name and its words, the computer and host names, the home folder and their
//   8.3 and URL-encoded forms, as whole words in any case (Unicode lowercase by plat_char_lower), become <user>,
//   <computer> and <home>;
// - e-mail addresses become <email>, IPv4 and IPv6 addresses (not loopback or unspecified) <ip>, MAC addresses <mac>,
//   Windows SIDs <sid>.
uint8_t *pii_mask_text(const PiiMask *m, const uint8_t *text, size_t n, size_t *out_n);
