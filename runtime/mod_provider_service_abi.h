// Explicit service ABI 2.0. Lifecycle ABI 1.0 remains unchanged and separately queried.
#pragma once
#include "mod_provider_abi.h"
#define XMS_MAJOR 2u
#define XMS_MINOR 0u
#define XMS_COMMANDS 4u
#define XMS_CREDITS 32u
enum {XMS_EMPTY=2,XMS_NONE=0,XMS_BOOL=1,XMS_INTEGER=2};
typedef struct {
    uint32_t opcode,type;int64_t minimum,maximum;
    char id[65];uint8_t padding[7];
} XmsCommandSpec;
typedef struct {
    uint32_t size,major,minor,reserved,platform,architecture,command_count,reserved2;
    uint64_t capabilities;char provider_id[65],build_id[65],service_id[65];uint8_t padding[5];
    XmsCommandSpec commands[XMS_COMMANDS];
} XmsInfo;
typedef struct {
    uint32_t size,opcode,type,reserved;
    uint64_t owner,generation,request;int64_t value;
} XmsCommand;
typedef struct {
    uint32_t size;int32_t result;
    uint64_t owner,generation,request;
    char message[257];uint8_t padding[7];
} XmsResult;
typedef struct {
    uint32_t size,reserved;uint64_t owner,generation,revision;
    char text[257];uint8_t padding[7];
} XmsStatus;
typedef struct {
    uint32_t size,major,minor,reserved;
    XmpResult (XMP_CALL *describe)(XmsInfo *out);
    XmpResult (XMP_CALL *create_idle)(const XmpCreate *config,uint64_t *instance);
    XmpResult (XMP_CALL *request_stop)(uint64_t instance);
    XmpResult (XMP_CALL *join_destroy)(uint64_t instance,uint32_t timeout_ms);
    XmpResult (XMP_CALL *submit)(uint64_t instance,const XmsCommand *command);
    XmpResult (XMP_CALL *poll)(uint64_t instance,XmsResult *out);
    XmpResult (XMP_CALL *status)(uint64_t instance,XmsStatus *out);
} XmsApi;
typedef XmpResult (XMP_CALL *XmsQuery)(uint32_t major,uint32_t minor,XmsApi *out);
#ifdef __cplusplus
extern "C" {
#endif
XmpResult XMP_CALL xcx_provider_query_v2(uint32_t major,uint32_t minor,XmsApi *out);
#ifdef __cplusplus
}
#endif
// Submit OK copies input and owes one terminal record. BUSY/rejection accepts nothing.
// Poll EMPTY has no record. Status coalesces independently. All calls serialized, prompt,
// borrowed buffers only; no callbacks. Stop joins workers before successful destruction.
