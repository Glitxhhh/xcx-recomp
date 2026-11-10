// Native provider lifecycle ABI v1.0. Little-endian x86-64, natural alignment.
#pragma once
#include <stdint.h>
#ifdef _WIN32
#define XMP_CALL __cdecl
#define XMP_EXPORT __declspec(dllexport)
#define XMP_PLATFORM 2u
#else
#define XMP_CALL
#define XMP_EXPORT __attribute__((visibility("default")))
#define XMP_PLATFORM 1u
#endif
#define XMP_MAJOR 1u
#define XMP_MINOR 0u
#define XMP_ARCH_X64 1u
typedef int32_t XmpResult;
enum { XMP_OK=0, XMP_BUSY=1, XMP_INVALID=-1, XMP_INCOMPATIBLE=-2, XMP_FAILED=-3 };
typedef struct {
    uint32_t size,major,minor,reserved,platform,architecture,reserved2[2];
    uint64_t capabilities;
    char provider_id[65],build_id[65];
    uint8_t padding[6];
} XmpInfo;
typedef struct {
    uint32_t size,major,minor,reserved;
    uint64_t owner,generation,required_capabilities;
    char package_id[65];
    uint8_t padding[7];
} XmpCreate;
typedef struct {
    uint32_t size,major,minor,reserved;
    XmpResult (XMP_CALL *describe)(XmpInfo *out);
    XmpResult (XMP_CALL *create_idle)(const XmpCreate *config,uint64_t *instance);
    XmpResult (XMP_CALL *request_stop)(uint64_t instance);
    XmpResult (XMP_CALL *join_destroy)(uint64_t instance,uint32_t timeout_ms);
} XmpApi;
typedef XmpResult (XMP_CALL *XmpQuery)(uint32_t major,uint32_t minor,XmpApi *out);
#ifdef __cplusplus
extern "C" {
#endif
XmpResult XMP_CALL xcx_provider_query_v1(uint32_t major,uint32_t minor,XmpApi *out);
#ifdef __cplusplus
}
#endif
// All inputs borrowed until return, outputs caller-owned. No callbacks or shared allocations.
// Failed create returns instance=0 after complete rollback. OK join destroys; BUSY retains
// all live resources. Stop is idempotent and nonblocking. No module unloading in v1.
