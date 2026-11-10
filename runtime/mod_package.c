#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
// Read and validate all source snapshots before Lua executes any package.
#include "mod_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif
static int identifier(const char *s) {
    size_t n=strlen(s);
    if (!n || n>64 || s[0]<'a' || s[0]>'z') return 0;
    for (size_t i=1;i<n;++i) if (!((s[i]>='a'&&s[i]<='z') || (s[i]>='0'&&s[i]<='9') || s[i]=='_' || s[i]=='-' || s[i]=='.')) return 0;
    return 1;
}
static void diagnostic(ModLog log,const char *id,const char *reason) {
    char line[256]; snprintf(line,sizeof line,"mods: %s: %s",id,reason); log(line);
}
// Root is an explicit trusted installation path. Children are fixed validated components.
static char *snapshot(const char *root,const char *id,const char *name,size_t limit,size_t *length) {
    char *data=NULL; *length=(size_t)-1;
#ifdef _WIN32
    char path[4096]; wchar_t wide[4096];
    if (snprintf(path,sizeof path,"%s",root)>=(int)sizeof path) return NULL;
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,4096)) return NULL;
    DWORD attrs=GetFileAttributesW(wide);
    if (attrs==INVALID_FILE_ATTRIBUTES || (attrs&FILE_ATTRIBUTE_REPARSE_POINT) || !(attrs&FILE_ATTRIBUTE_DIRECTORY)) { if (attrs==INVALID_FILE_ATTRIBUTES && (GetLastError()==ERROR_FILE_NOT_FOUND || GetLastError()==ERROR_PATH_NOT_FOUND)) *length=0; return NULL; }
    if (id) {
        if (snprintf(path,sizeof path,"%s/%s",root,id)>=(int)sizeof path) return NULL;
        if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,4096)) return NULL;
        attrs=GetFileAttributesW(wide);
        if (attrs==INVALID_FILE_ATTRIBUTES || (attrs&FILE_ATTRIBUTE_REPARSE_POINT) || !(attrs&FILE_ATTRIBUTE_DIRECTORY)) return NULL;
    }
    int written=id?snprintf(path,sizeof path,"%s/%s/%s",root,id,name):snprintf(path,sizeof path,"%s/%s",root,name);
    if (written<0 || written>=(int)sizeof path) return NULL;
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,4096)) return NULL;
    HANDLE file=CreateFileW(wide,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if (file==INVALID_HANDLE_VALUE) { if (GetLastError()==ERROR_FILE_NOT_FOUND || GetLastError()==ERROR_PATH_NOT_FOUND) *length=0; return NULL; }
    BY_HANDLE_FILE_INFORMATION info; LARGE_INTEGER size;
    if (GetFileInformationByHandle(file,&info) && !(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) && GetFileSizeEx(file,&size) && size.QuadPart>=0 && (unsigned long long)size.QuadPart<=limit) {
        size_t n=(size_t)size.QuadPart; data=malloc(n+1); DWORD got=0;
        if (data && ReadFile(file,data,(DWORD)n,&got,NULL) && got==n) { data[n]=0; *length=n; }
        else { free(data); data=NULL; }
    }
    CloseHandle(file);
#else
    int dir=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if (dir<0) { if (errno==ENOENT) *length=0; return NULL; }
    if (id) { int child=openat(dir,id,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC); close(dir); dir=child; }
    if (dir<0) return NULL;
    int file=openat(dir,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC); close(dir);
    if (file<0) { if (errno==ENOENT) *length=0; return NULL; }
    struct stat st;
    if (!fstat(file,&st) && S_ISREG(st.st_mode) && st.st_size>=0 && (unsigned long long)st.st_size<=limit) {
        size_t n=(size_t)st.st_size, at=0; data=malloc(n+1);
        if (data) {
            while (at<n) { ssize_t got=read(file,data+at,n-at); if (got<=0) break; at+=(size_t)got; }
            char extra; ssize_t tail=read(file,&extra,1);
            if (at==n && tail==0) { data[n]=0; *length=n; }
            else { free(data); data=NULL; }
        }
    }
    close(file);
#endif
    return data;
}
static int version(const char *s) {
    int dots=0, digits=0;
    if (!*s || strlen(s)>31) return 0;
    for (;*s;++s) { if (*s=='.') { if (!digits || ++dots>2) return 0; digits=0; } else if (*s>='0'&&*s<='9') ++digits; else return 0; }
    return dots==2 && digits;
}
static int manifest(ModPackage *m,char *s,size_t size) {
    if (memchr(s,0,size)) return 0;
    unsigned fields=0;
    for (char *line=s;line;) {
        char *next=strchr(line,'\n'); if (next) *next++=0;
        size_t n=strlen(line); if (n && line[n-1]=='\r') line[--n]=0;
        if (n && line[0]!='#') {
            char *value=strchr(line,'='); if (!value) return 0; *value++=0;
            unsigned field=0;
            if (!strcmp(line,"id")) { field=1; if (strcmp(value,m->id)) return 0; }
            else if (!strcmp(line,"api")) { field=2; if (strcmp(value,"1")) {strcpy(m->diagnostic,"Incompatible API: this host requires api=1.");return 0;} }
            else if (!strcmp(line,"language")) { field=4; if (strcmp(value,"lua55-basic-v1")) {strcpy(m->diagnostic,"Incompatible language: requires lua55-basic-v1.");return 0;} }
            else if (!strcmp(line,"version")) { field=8; if (!version(value)) {strcpy(m->diagnostic,"Invalid package version; expected three numeric components.");return 0;} strcpy(m->version,value); }
            else if (!strcmp(line,"name")) { field=32; if (!*value || strlen(value)>96) return 0; strcpy(m->name,value); }
            else if (!strcmp(line,"requires")) {
                field=16;
                while (*value) {
                    char *comma=strchr(value,','); if (comma) *comma=0;
                    if (!identifier(value) || m->require_count==MOD_LIMIT) return 0;
                    for (int j=0;j<m->require_count;++j) if (!strcmp(m->requires[j],value)) return 0;
                    strcpy(m->requires[m->require_count++],value);
                    if (!comma) break;
                    value=comma+1; if (!*value) return 0;
                }
            } else return 0;
            if (fields&field) return 0;
            fields|=field;
        }
        line=next;
    }
    return (fields&15)==15;
}
ModPlan *mod_packages_read(const char *root,ModLog log) {
    size_t length=0;
    char *enabled=snapshot(root,NULL,"enabled.txt",2048,&length);
    if (!enabled) { if (length) {diagnostic(log,"enabled.txt","unsafe, unreadable or oversized configuration; no packages loaded");return NULL;} enabled=calloc(1,1);length=0;if(!enabled)return NULL; }
    if (memchr(enabled,0,length)) { diagnostic(log,"enabled.txt","embedded NUL; no packages loaded"); free(enabled); return NULL; }
    ModPlan *plan=calloc(1,sizeof *plan); if (!plan) { free(enabled); return NULL; }
    if(strlen(root)>=sizeof plan->root){free(enabled);mod_packages_free(plan);return NULL;}
    strcpy(plan->root,root);
    for (char *line=enabled;line;) {
        char *next=strchr(line,'\n'); if (next) *next++=0;
        size_t n=strlen(line); if (n && line[n-1]=='\r') line[--n]=0;
        if (n && line[0]!='#') {
            if (!identifier(line)) diagnostic(log,"enabled.txt","invalid ID ignored");
            else {
                int found=-1;
                for (int i=0;i<plan->count;++i) if (!strcmp(plan->mods[i].id,line)) found=i;
                if (found>=0) { plan->mods[found].rejected=1; diagnostic(log,line,"duplicate enabled ID rejected"); }
                else if (plan->count==MOD_LIMIT) { diagnostic(log,"enabled.txt","more than 16 IDs; configuration rejected"); free(enabled); mod_packages_free(plan); return NULL; }
                else strcpy(plan->mods[plan->count++].id,line);
            }
        }
        line=next;
    }
    free(enabled);
    // Optional bounded inventory: absent IDs are displayed as disabled, never executed.
    char *catalog=snapshot(root,NULL,"catalog.txt",2048,&length);
    if (catalog && !memchr(catalog,0,length)) {
        for (char *line=catalog;line;) {
            char *next=strchr(line,'\n'); if(next)*next++=0;
            size_t n=strlen(line);if(n&&line[n-1]=='\r')line[--n]=0;
            if (identifier(line)) {
                int present=0;for(int i=0;i<plan->count;++i) if(!strcmp(plan->mods[i].id,line))present=1;
                if(!present && plan->count<MOD_LIMIT) {ModPackage *m=&plan->mods[plan->count++];strcpy(m->id,line);m->enabled=-1;}
            }
            line=next;
        }
    }
    free(catalog);
    for(int i=0;i<plan->count;++i) plan->mods[i].enabled=plan->mods[i].enabled!=-1;
    // Lexical tie-breaking is independent of directory enumeration and enabled-file order.
    for (int i=0;i<plan->count;++i) for (int j=i+1;j<plan->count;++j) if (strcmp(plan->mods[i].id,plan->mods[j].id)>0) { ModPackage temp=plan->mods[i]; plan->mods[i]=plan->mods[j]; plan->mods[j]=temp; }
    for (int i=0;i<plan->count;++i) {
        ModPackage *m=&plan->mods[i]; if (m->rejected) continue;
        char *text=snapshot(root,m->id,"mod.ini",4096,&length);
        if (!text || !manifest(m,text,length)) { m->rejected=1; if(!m->diagnostic[0])strcpy(m->diagnostic,"invalid manifest/API/language/version");diagnostic(log,m->id,m->diagnostic); }
        free(text);
        if (m->rejected || !m->enabled) {
            ModView view={0};strcpy(view.id,m->id);snprintf(view.name,sizeof view.name,"%s",m->name[0]?m->name:m->id);strcpy(view.version,m->version);
            view.status=m->enabled?MOD_INCOMPATIBLE:MOD_DISABLED;
            snprintf(view.diagnostic,sizeof view.diagnostic,"%s",m->enabled?"Manifest/API/language/version rejected":"Not enabled; restart after editing enabled.txt.");mod_ui_publish(&view);
            m->rejected=1;continue;
        }
        m->source=snapshot(root,m->id,"main.lua",MOD_SOURCE_LIMIT,&m->source_size);
        if (!m->source || (m->source_size && (unsigned char)m->source[0]==27)) { m->rejected=1; strcpy(m->diagnostic,"missing/unsafe/oversized source or bytecode");diagnostic(log,m->id,m->diagnostic); }
    }
    for (int pass=0;pass<plan->count;++pass) for (int i=0;i<plan->count;++i) {
        ModPackage *m=&plan->mods[i]; if (m->rejected) continue;
        for (int d=0;d<m->require_count;++d) {
            int found=-1;
            for (int j=0;j<plan->count;++j) if (!strcmp(m->requires[d],plan->mods[j].id) && !plan->mods[j].rejected) found=j;
            if (found<0) { m->rejected=1; strcpy(m->diagnostic,"required package absent/disabled/rejected");diagnostic(log,m->id,m->diagnostic); break; }
            m->dependencies|=1u<<found;
        }
    }
    // Preserve original indexes for dependency masks; core uses this topological order.
    unsigned ready=0;
    for (int pass=0;pass<plan->count;++pass) for (int i=0;i<plan->count;++i) if (!plan->mods[i].rejected && (plan->mods[i].dependencies&ready)==plan->mods[i].dependencies) ready|=1u<<i;
    for (int i=0;i<plan->count;++i) if (!plan->mods[i].rejected && !(ready&(1u<<i))) { plan->mods[i].rejected=1; strcpy(plan->mods[i].diagnostic,"dependency cycle or blocked by cycle");diagnostic(log,plan->mods[i].id,plan->mods[i].diagnostic); }
    for(int i=0;i<plan->count;++i) {
        ModPackage *m=&plan->mods[i];ModView view={0};strcpy(view.id,m->id);snprintf(view.name,sizeof view.name,"%s",m->name[0]?m->name:m->id);strcpy(view.version,m->version);
        view.status=!m->enabled?MOD_DISABLED:m->rejected?MOD_INCOMPATIBLE:MOD_STARTING;
        if(view.status==MOD_DISABLED) strcpy(view.diagnostic,"Not enabled. Changes require restart.");
        if(view.status==MOD_INCOMPATIBLE) snprintf(view.diagnostic,sizeof view.diagnostic,"%s",m->diagnostic[0]?m->diagnostic:"Package rejected; see log.");
        mod_ui_publish(&view);
    }
    if (!ready) { mod_packages_free(plan); return NULL; }
    return plan;
}
void mod_packages_free(ModPlan *plan) { if (plan) { for (int i=0;i<plan->count;++i) free(plan->mods[i].source); free(plan); } }
