#pragma once
#include <stddef.h>
#include <stdint.h>
typedef struct {unsigned char *data;size_t size;intptr_t file;char path[4096];} ModProviderFile;
// 0 missing, -1 unsafe/error, 1 bounded snapshot; trusted absolute installation root.
int mod_provider_read(const char *root,const char *owner,const char *relative,size_t limit,ModProviderFile *out);
void mod_provider_file_close(ModProviderFile *file);
int mod_provider_image(const ModProviderFile *file);
int mod_provider_environment(void);
void *mod_provider_map(ModProviderFile *file);
void *mod_provider_symbol(void *module,const char *name);
uint64_t mod_provider_clock_ms(void);
void mod_provider_sleep(void);
