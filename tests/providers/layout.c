#include "mod_provider_abi.h"
#include <stddef.h>
#ifdef __cplusplus
#define CHECK static_assert
#else
#define CHECK _Static_assert
#endif
CHECK(sizeof(void *)==8,"x64 only");
CHECK(sizeof(XmpInfo)==176,"info layout");
CHECK(offsetof(XmpInfo,capabilities)==32,"capabilities offset");
CHECK(sizeof(XmpCreate)==112,"create layout");
CHECK(offsetof(XmpCreate,package_id)==40,"owner identity offset");
CHECK(sizeof(XmpApi)==48,"table layout");
