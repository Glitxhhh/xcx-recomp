// Unpacks the game files of a mod zip (runtime/mod_library.c), by the rules of installer/mods.py, unpack_romfs: the
// files below the last "romfs" folder of each path, without a leading "mod/" (the XCXDE mod loader's layout), go to
// <folder>/romfs/<path>. Entries with "..", absolute paths, drive letters or symbolic links refuse the whole zip, as
// do two files whose paths differ only in case.
#pragma once
#include <stddef.h>
#include <stdint.h>

#define MOD_ZIP_LIMIT ((uint64_t)16 << 30)   // bytes a mod may unpack to

// zip: the whole archive in memory. folder: an existing, empty directory. NULL on success, else an English reason
// (the folder may then hold part of the files; the caller deletes it).
const char *mod_zip_unpack_romfs(const uint8_t *zip, size_t size, const char *folder);

// Raw deflate (RFC 1951) of src into dst, which holds exactly the expected size; 1 when the stream filled dst exactly.
int mod_inflate(const uint8_t *src, size_t n, uint8_t *dst, size_t want);
// Raw deflate of src into dst, which holds cap bytes: one block with the fixed Huffman codes. The compressed size, or
// 0 when it would not fit in cap (the caller stores the data instead). For the bug report (runtime/bug_report.c).
size_t mod_deflate(const uint8_t *src, size_t n, uint8_t *dst, size_t cap);
uint32_t mod_crc32(const uint8_t *p, size_t n);
