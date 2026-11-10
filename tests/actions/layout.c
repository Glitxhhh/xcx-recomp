#include "mod_provider_service_abi.h"
#include <stddef.h>
#ifdef __cplusplus
#define CHECK static_assert
#else
#define CHECK _Static_assert
#endif
CHECK(sizeof(XmsCommandSpec)==96,"command spec");CHECK(sizeof(XmsInfo)==624,"service info");CHECK(sizeof(XmsCommand)==48,"command");CHECK(sizeof(XmsResult)==296,"result");CHECK(sizeof(XmsStatus)==296,"status");CHECK(sizeof(XmsApi)==72,"service api");CHECK(offsetof(XmsCommand,request)==32,"request identity");
