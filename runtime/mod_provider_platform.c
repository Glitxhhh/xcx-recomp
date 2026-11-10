#define _GNU_SOURCE
#include "mod_provider_platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <elf.h>
#include <dlfcn.h>

#endif
static int component(const char *s) {
    if(!*s || !strcmp(s,".") || !strcmp(s,"..") || strlen(s)>96)return 0;
    for(;*s;++s)if(!((*s>='a'&&*s<='z')||(*s>='0'&&*s<='9')||*s=='.'||*s=='_'||*s=='-'))return 0;
    return 1;
}
void mod_provider_file_close(ModProviderFile *f) {
#ifdef _WIN32
    if(f->file!=-1)CloseHandle((HANDLE)f->file);
#else
    if(f->file!=-1)close((int)f->file);
#endif
    free(f->data);f->data=NULL;f->file=-1;
}
int mod_provider_read(const char *root,const char *owner,const char *relative,size_t limit,ModProviderFile *out) {
    memset(out,0,sizeof *out);out->file=-1;
    if(!root || strlen(root)>3500 || (owner&&!component(owner)))return -1;
    char normalized[4096];snprintf(normalized,sizeof normalized,"%s",root);size_t root_len=strlen(normalized);
    while(root_len>1 && (normalized[root_len-1]=='/'||normalized[root_len-1]=='\\')) {
#ifdef _WIN32
        if(root_len==3 && normalized[1]==':')break;
#endif
        normalized[--root_len]=0;
    }
    root=normalized;
    char suffix[512];if(snprintf(suffix,sizeof suffix,"%s%s%s",owner?owner:"",owner?"/":"",relative)>=(int)sizeof suffix)return -1;
    char *parts[8];int count=0;char *s=suffix;
    while(s){if(count==8)return -1;char *next=strchr(s,'/');if(next)*next++=0;if(!component(s))return -1;parts[count++]=s;s=next;}
#ifdef _WIN32
    if(!(strlen(root)>3 && root[1]==':' && (root[2]=='/'||root[2]=='\\')))return -1;
    char path[4096];snprintf(path,sizeof path,"%s",root);HANDLE h=INVALID_HANDLE_VALUE;
    for(int i=-1;i<count;++i) {
        if(i>=0){size_t n=strlen(path);if(snprintf(path+n,sizeof(path)-n,"/%s",parts[i])>=(int)(sizeof(path)-n))return -1;}
        wchar_t wide[4096];if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,4096))return -1;
        for(wchar_t *w=wide;*w;++w)if(*w=='/')*w='\\';
        DWORD attrs=GetFileAttributesW(wide);
        if(attrs==INVALID_FILE_ATTRIBUTES){DWORD e=GetLastError();return e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND?0:-1;}
        int directory=i<count-1;
        if((attrs&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DEVICE)) || !!(attrs&FILE_ATTRIBUTE_DIRECTORY)!=directory)return -1;
        h=CreateFileW(wide,directory?FILE_READ_ATTRIBUTES:GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|(directory?FILE_FLAG_BACKUP_SEMANTICS:0),NULL);
        if(h==INVALID_HANDLE_VALUE)return -1;
        BY_HANDLE_FILE_INFORMATION info;int ok=GetFileType(h)==FILE_TYPE_DISK&&GetFileInformationByHandle(h,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&!!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)==directory;
        if(!ok){CloseHandle(h);return -1;}
        if(directory)CloseHandle(h);
    }
    out->file=(intptr_t)h;snprintf(out->path,sizeof out->path,"%s",path);LARGE_INTEGER n;
    if(!GetFileSizeEx(h,&n)||n.QuadPart<0||(uint64_t)n.QuadPart>limit)goto bad;
    out->size=(size_t)n.QuadPart;out->data=malloc(out->size+1);if(!out->data)goto bad;
    DWORD got=0,tail=0;char extra;
    if(!ReadFile(h,out->data,(DWORD)out->size,&got,NULL)||got!=out->size||!ReadFile(h,&extra,1,&tail,NULL)||tail)goto bad;
#else
    if(root[0]!='/')return -1;
    int dir=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);if(dir<0)return errno==ENOENT?0:-1;
    int fd=-1;
    for(int i=0;i<count;++i) {
        struct stat st;if(fstatat(dir,parts[i],&st,AT_SYMLINK_NOFOLLOW)){int e=errno;close(dir);return e==ENOENT?0:-1;}
        int last=i==count-1;if(last?!S_ISREG(st.st_mode):!S_ISDIR(st.st_mode)){close(dir);return -1;}
        fd=openat(dir,parts[i],O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC|(last?0:O_DIRECTORY));close(dir);
        if(fd<0)return -1;if(!last)dir=fd;
    }
    out->file=fd;struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<0||(uint64_t)st.st_size>limit)goto bad;
    out->size=(size_t)st.st_size;out->data=malloc(out->size+1);if(!out->data)goto bad;
    size_t at=0;while(at<out->size){ssize_t n=read(fd,out->data+at,out->size-at);if(n<0&&errno==EINTR)continue;if(n<=0)goto bad;at+=(size_t)n;}
    char extra;ssize_t tail;do{tail=read(fd,&extra,1);}while(tail<0&&errno==EINTR);if(tail)goto bad;
    snprintf(out->path,sizeof out->path,"/proc/self/fd/%d",fd);
#endif
    out->data[out->size]=0;return 1;
bad:mod_provider_file_close(out);return -1;
}
static int range(size_t size,uint64_t at,uint64_t n){return at<=size && n<=size-at;}
#ifndef _WIN32
static int elf_image(const ModProviderFile *f) {
    const unsigned char *b=f->data;size_t n=f->size;Elf64_Ehdr e;
    if(n<sizeof e)return 0;memcpy(&e,b,sizeof e);
    if(memcmp(e.e_ident,ELFMAG,SELFMAG)||e.e_ident[EI_CLASS]!=ELFCLASS64||e.e_ident[EI_DATA]!=ELFDATA2LSB||e.e_machine!=EM_X86_64||e.e_type!=ET_DYN||e.e_phentsize!=sizeof(Elf64_Phdr)||!e.e_phnum||e.e_phnum>128||!range(n,e.e_phoff,(uint64_t)e.e_phnum*sizeof(Elf64_Phdr)))return 0;
    Elf64_Phdr ph[128];memcpy(ph,b+e.e_phoff,e.e_phnum*sizeof *ph);uint64_t dynamic=0,bytes=0,str=0,strsize=0;int found=0;
    for(int i=0;i<e.e_phnum;++i)if((ph[i].p_type==PT_LOAD||ph[i].p_type==PT_DYNAMIC)&&!range(n,ph[i].p_offset,ph[i].p_filesz))return 0;
    for(int i=0;i<e.e_phnum;++i)if(ph[i].p_type==PT_DYNAMIC){if(found++)return 0;dynamic=ph[i].p_offset;bytes=ph[i].p_filesz;}
    if(!found||!range(n,dynamic,bytes)||bytes%sizeof(Elf64_Dyn))return 0;
    int end=0;
    for(uint64_t at=0;at<bytes;at+=sizeof(Elf64_Dyn)) {
        Elf64_Dyn d;memcpy(&d,b+dynamic+at,sizeof d);if(d.d_tag==DT_NULL){end=1;break;}
        if(d.d_tag==DT_RPATH||d.d_tag==DT_RUNPATH||d.d_tag==DT_FILTER||d.d_tag==DT_AUXILIARY||d.d_tag==DT_AUDIT||d.d_tag==DT_DEPAUDIT)return 0;
        if(d.d_tag==DT_STRTAB)str=d.d_un.d_ptr;if(d.d_tag==DT_STRSZ)strsize=d.d_un.d_val;
    }
    if(!end||!strsize)return 0;uint64_t strings=0;found=0;
    for(int i=0;i<e.e_phnum;++i)if(ph[i].p_type==PT_LOAD&&str>=ph[i].p_vaddr&&str-ph[i].p_vaddr<=ph[i].p_filesz&&strsize<=ph[i].p_filesz-(str-ph[i].p_vaddr)) {
        strings=ph[i].p_offset+(str-ph[i].p_vaddr);found=1;
    }
    if(!found||!range(n,strings,strsize))return 0;
    for(uint64_t at=0;at<bytes;at+=sizeof(Elf64_Dyn)) {
        Elf64_Dyn d;memcpy(&d,b+dynamic+at,sizeof d);if(d.d_tag==DT_NULL)break;
        if(d.d_tag==DT_NEEDED) {
            uint64_t k=d.d_un.d_val;if(k>=strsize||!memchr(b+strings+k,0,(size_t)(strsize-k))||strcmp((const char *)b+strings+k,"libc.so.6"))return 0;
        }
    }
    return 1;
}
#else
static uint32_t pe_offset(const unsigned char *b,size_t n,uint32_t rva,const IMAGE_SECTION_HEADER *s,unsigned count) {
    for(unsigned i=0;i<count;++i)if(rva>=s[i].VirtualAddress && rva-s[i].VirtualAddress<s[i].SizeOfRawData) {
        uint64_t p=(uint64_t)s[i].PointerToRawData+rva-s[i].VirtualAddress;if(range(n,p,1))return (uint32_t)p;
    }
    (void)b;return 0;
}
static int pe_image(const ModProviderFile *f) {
    const unsigned char *b=f->data;size_t n=f->size;IMAGE_DOS_HEADER dos;
    if(n<sizeof dos)return 0;memcpy(&dos,b,sizeof dos);if(dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0||!range(n,(uint32_t)dos.e_lfanew,sizeof(IMAGE_NT_HEADERS64)))return 0;
    IMAGE_NT_HEADERS64 h;memcpy(&h,b+dos.e_lfanew,sizeof h);
    if(h.Signature!=IMAGE_NT_SIGNATURE||h.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||!(h.FileHeader.Characteristics&IMAGE_FILE_DLL)||h.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC||h.FileHeader.SizeOfOptionalHeader!=sizeof(IMAGE_OPTIONAL_HEADER64)||h.FileHeader.NumberOfSections>96||h.OptionalHeader.NumberOfRvaAndSizes!=IMAGE_NUMBEROF_DIRECTORY_ENTRIES)return 0;
    if(h.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].Size)return 0;
    uint64_t off=(uint32_t)dos.e_lfanew+sizeof h;unsigned count=h.FileHeader.NumberOfSections;if(!range(n,off,count*sizeof(IMAGE_SECTION_HEADER)))return 0;
    IMAGE_SECTION_HEADER sections[96];memcpy(sections,b+off,count*sizeof *sections);
    IMAGE_DATA_DIRECTORY im=h.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!im.Size)return 1;uint32_t p=pe_offset(b,n,im.VirtualAddress,sections,count);if(!p||!range(n,p,im.Size))return 0;
    for(uint32_t at=0;at+sizeof(IMAGE_IMPORT_DESCRIPTOR)<=im.Size;at+=sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        IMAGE_IMPORT_DESCRIPTOR d;memcpy(&d,b+p+at,sizeof d);if(!d.Name)return !d.FirstThunk&&!d.OriginalFirstThunk&&!d.TimeDateStamp&&!d.ForwarderChain;
        uint32_t text=pe_offset(b,n,d.Name,sections,count);if(!text)return 0;const char *s=(const char *)b+text;
        size_t bound=n-text;if(bound>96)bound=96;if(!memchr(s,0,bound))return 0;
        if(_stricmp(s,"ws2_32.dll")&&_stricmp(s,"kernel32.dll")&&_stricmp(s,"ucrtbase.dll")&&_stricmp(s,"msvcrt.dll")&&_strnicmp(s,"api-ms-win-crt-",15))return 0;
        for(const char *c=s;*c;++c)if(!((*c>='a'&&*c<='z')||(*c>='A'&&*c<='Z')||(*c>='0'&&*c<='9')||*c=='-'||*c=='.'))return 0;
    }
    return 0;
}
#endif
int mod_provider_image(const ModProviderFile *f) {
#ifdef _WIN32
    return pe_image(f);
#else
    return elf_image(f);
#endif
}
int mod_provider_environment(void) {
#ifdef _WIN32
    return 1; // Dependency lookup is restricted by LoadLibraryEx flags, independent of PATH.
#else
    if(getenv("LD_LIBRARY_PATH")||getenv("LD_PRELOAD")||getenv("LD_AUDIT"))return 0;
    // Only the already resident system glibc and its OS loader are permitted dependencies.
    Dl_info info;char resolved[4096];
    if(!dladdr(dlsym(RTLD_DEFAULT,"gnu_get_libc_version"),&info)||!info.dli_fname||!realpath(info.dli_fname,resolved))return 0;
    return !strncmp(resolved,"/usr/lib/",9)||!strncmp(resolved,"/usr/lib64/",11)||!strncmp(resolved,"/lib/",5)||!strncmp(resolved,"/lib64/",7);
#endif
}
void *mod_provider_map(ModProviderFile *f) {
#ifdef _WIN32
    wchar_t wide[4096];if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,f->path,-1,wide,4096))return NULL;
    for(wchar_t *w=wide;*w;++w)if(*w=='/')*w='\\';
    return (void *)LoadLibraryExW(wide,NULL,LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
    if(!mod_provider_environment())return NULL;
    return dlopen(f->path,RTLD_NOW|RTLD_LOCAL);
#endif
}
void *mod_provider_symbol(void *module,const char *name) {
#ifdef _WIN32
    return (void *)GetProcAddress((HMODULE)module,name);
#else
    return dlsym(module,name);
#endif
}
uint64_t mod_provider_clock_ms(void) {
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+(unsigned long)t.tv_nsec/1000000;
#endif
}
void mod_provider_sleep(void) {
#ifdef _WIN32
    Sleep(1);
#else
    struct timespec t={0,1000000};nanosleep(&t,NULL);
#endif
}
