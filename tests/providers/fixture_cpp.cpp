#include "mod_provider_abi.h"
#include "fixture_support.h"
#include <cstring>
#define ID "beta.native"
struct Idle {uint64_t owner,generation;bool stopped;};
static Idle *live;static uint64_t token=200;
__attribute__((constructor)) static void loaded(){marker(ID,"constructor");}
__attribute__((destructor)) static void unloaded(){marker(ID,"destructor");}
static XmpResult XMP_CALL info(XmpInfo *p){if(p->size!=sizeof *p)return XMP_INVALID;std::memset(p,0,sizeof *p);p->size=sizeof *p;p->major=XMP_MAJOR;p->minor=XMP_MINOR;p->platform=XMP_PLATFORM;p->architecture=XMP_ARCH_X64;std::strcpy(p->provider_id,ID);std::strcpy(p->build_id,"cpp1");return XMP_OK;}
static XmpResult XMP_CALL create(const XmpCreate *c,uint64_t *out){*out=0;if(c->size!=sizeof *c||c->reserved||c->major!=XMP_MAJOR||c->minor!=XMP_MINOR||!c->owner||!c->generation||std::strcmp(c->package_id,"beta")||live)return XMP_INVALID;for(unsigned i=0;i<sizeof c->padding;++i)if(c->padding[i])return XMP_INVALID;if(c->required_capabilities)return XMP_INCOMPATIBLE;live=static_cast<Idle *>(std::calloc(1,sizeof *live));if(!live)return XMP_FAILED;live->owner=c->owner;live->generation=c->generation;*out=++token;marker(ID,"create");return XMP_OK;}
static XmpResult XMP_CALL stop(uint64_t t){if(!live||t!=token)return XMP_INVALID;live->stopped=true;marker(ID,"stop");return XMP_OK;}
static XmpResult XMP_CALL join(uint64_t t,uint32_t){if(!live||t!=token)return XMP_INVALID;if(!live->stopped)return XMP_BUSY;std::free(live);live=nullptr;marker(ID,"destroy");return XMP_OK;}
extern "C" XMP_EXPORT XmpResult XMP_CALL xcx_provider_query_v1(uint32_t major,uint32_t minor,XmpApi *p){if(major!=XMP_MAJOR||minor!=XMP_MINOR||p->size!=sizeof *p)return XMP_INCOMPATIBLE;std::memset(p,0,sizeof *p);p->size=sizeof *p;p->major=XMP_MAJOR;p->minor=XMP_MINOR;p->describe=info;p->create_idle=create;p->request_stop=stop;p->join_destroy=join;return XMP_OK;}
