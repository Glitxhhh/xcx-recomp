// HTTPS downloads for the mod library (runtime/mod_library.c).
#pragma once
#include <stddef.h>
#include <stdint.h>

// Called while a download runs: bytes so far and the total (0 when the server sends no length).
typedef void (*FetchProgress)(uint64_t done, uint64_t total, void *ctx);

// Downloads url, which must start with https://, into the new file path (redirects to other https URLs are
// followed). At most max bytes. NULL on success, else an English reason; a failed download leaves no file at path.
const char *mod_fetch(const char *url, const char *path, uint64_t max, FetchProgress progress, void *ctx);
