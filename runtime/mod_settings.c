#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "mod_settings.h"
#include "platform/platform.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
static atomic_flag gate=ATOMIC_FLAG_INIT;
static ModSnapshot snapshot;
static ModAction queue[MOD_ACTION_MAX];
static int queued,active,stopped;
static ModIntent intents[MOD_ACTION_MAX];static int intent_count;
static char directory[4096];
static void take(void) { while (atomic_flag_test_and_set_explicit(&gate,memory_order_acquire)) {} }
static void release(void) { atomic_flag_clear_explicit(&gate,memory_order_release); }
void mod_ui_reset(const char *root) {
    take(); uint64_t generation=snapshot.generation+1; memset(&snapshot,0,sizeof snapshot);snapshot.generation=generation;snapshot.session=1;
    queued=active=stopped=intent_count=0;snprintf(directory,sizeof directory,"%s",root?root:""); release();
}
void mod_ui_publish(const ModView *view) {
    take(); int slot=-1;
    for (int i=0;i<snapshot.count;++i) if (!strcmp(view->id,snapshot.mods[i].id)) slot=i;
    if (slot<0 && snapshot.count<MOD_UI_MAX) slot=snapshot.count++;
    if (slot>=0 && !stopped) snapshot.mods[slot]=*view;
    release();
}
void mod_ui_snapshot(ModSnapshot *out) { take();*out=snapshot;out->pending=queued;out->intent_pending=intent_count;release(); }
void mod_ui_interaction(int enabled) {
    take(); if (active!=enabled) {active=enabled;++snapshot.session;if (!active) queued=intent_count=0;} release();
}
int mod_ui_submit(const ModAction *a) {
    int ok=0;take();
    if (!memchr(a->mod,0,sizeof a->mod) || !memchr(a->field,0,sizeof a->field)) goto done;
    if (!active || stopped || a->generation!=snapshot.generation || a->session!=snapshot.session) goto done;
    int m=-1,f=-1;
    for (int i=0;i<snapshot.count;++i) if (!strcmp(a->mod,snapshot.mods[i].id)) m=i;
    if (m<0 || snapshot.mods[m].status!=MOD_LOADED) goto done;
    for (int i=0;i<snapshot.mods[m].field_count;++i) if (!strcmp(a->field,snapshot.mods[m].fields[i].id)) f=i;
    if (f<0) goto done;
    ModField *field=&snapshot.mods[m].fields[f];
    if (a->type!=field->type || a->value<field->minimum || a->value>field->maximum) goto done;
    for (int i=0;i<queued;++i) if (!strcmp(a->mod,queue[i].mod) && !strcmp(a->field,queue[i].field)) {queue[i]=*a;ok=1;goto done;}
    if (queued<MOD_ACTION_MAX) {queue[queued++]=*a;ok=1;snapshot.notice[0]=0;}
    else snprintf(snapshot.notice,sizeof snapshot.notice,"Settings queue is full; try again.");
 done: release();return ok;
}
int mod_ui_action_current(const ModAction *a) {take();int ok=!stopped&&active&&a->generation==snapshot.generation&&a->session==snapshot.session;release();return ok;}
int mod_ui_pop(ModAction *out) { take();int ok=queued>0&&!stopped;if(ok) {*out=queue[0];memmove(queue,queue+1,(size_t)(--queued)*sizeof *queue);} release();return ok; }
void mod_ui_shutdown(void) { take();stopped=1;active=0;queued=intent_count=0;++snapshot.session;release(); }
void mod_ui_start_failed(const char *reason) {
    take();
    if(!stopped)for(int i=0;i<snapshot.count;++i)if(snapshot.mods[i].status==MOD_STARTING) {
        snapshot.mods[i].status=MOD_FAULTED;
        snprintf(snapshot.mods[i].diagnostic,sizeof snapshot.mods[i].diagnostic,"%s",reason);
    }
    release();
}
static int setting_path(const char *id,char *out,size_t size) {
    if (!directory[0]) return 0;
    return snprintf(out,size,"%s/%s.ini",directory,id)<(int)size;
}
static int stored_value(ModField *f,const char *text,int *value) {
    if (f->type==MOD_ENUM) {
        if (strncmp(text,"e:",2)) return 0;
        for (int i=0;i<f->option_count;++i) if (!strcmp(text+2,f->options[i].id)) {*value=i;return 1;}
        return 0;
    }
    if (strlen(text)<2) return 0;
    if (text[0]!=(f->type==MOD_BOOL?'b':'i') || text[1]!=':') return 0;
    errno=0;char *end;long n=strtol(text+2,&end,10);
    if (errno || end==text+2 || *end || n<f->minimum || n>f->maximum) return 0;
    *value=(int)n;return 1;
}
// Settings use their own bounded snapshot reader; game-save helpers are unchanged.
// Return 0 only for an absent file/root, 1 for a complete regular file, -1 otherwise.
static int settings_read(const char *id,char *data,size_t *length) {
#ifdef _WIN32
    char path[4096];wchar_t root[4096],wide[4096];
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,directory,-1,root,4096))return -1;
    HANDLE dir=CreateFileW(root,FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,NULL);
    if(dir==INVALID_HANDLE_VALUE){DWORD e=GetLastError();return e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND?0:-1;}
    BY_HANDLE_FILE_INFORMATION info;
    int valid=GetFileType(dir)==FILE_TYPE_DISK && GetFileInformationByHandle(dir,&info) && (info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) && !(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT);
    CloseHandle(dir);if(!valid)return -1;
    if(!setting_path(id,path,sizeof path) || !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,4096))return -1;
    DWORD attrs=GetFileAttributesW(wide);
    if(attrs==INVALID_FILE_ATTRIBUTES){DWORD e=GetLastError();return e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND?0:-1;}
    if(attrs&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DEVICE))return -1;
    HANDLE file=CreateFileW(wide,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,NULL);
    if(file==INVALID_HANDLE_VALUE){DWORD e=GetLastError();return e==ERROR_FILE_NOT_FOUND||e==ERROR_PATH_NOT_FOUND?0:-1;}
    LARGE_INTEGER size;int result=-1;
    if(GetFileType(file)==FILE_TYPE_DISK && GetFileInformationByHandle(file,&info) && !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) && GetFileSizeEx(file,&size) && size.QuadPart>=0 && size.QuadPart<=4096) {
        DWORD got=0,tail=0;char extra;
        if(ReadFile(file,data,(DWORD)size.QuadPart,&got,NULL) && got==(DWORD)size.QuadPart && ReadFile(file,&extra,1,&tail,NULL) && !tail){*length=got;result=1;}
    }
    CloseHandle(file);return result;
#else
    int dir=open(directory,O_RDONLY|O_DIRECTORY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);
    if(dir<0)return errno==ENOENT?0:-1;
    char name[80];snprintf(name,sizeof name,"%s.ini",id);struct stat st;
    // Reject special files before open as well as checking the actual opened handle.
    if(fstatat(dir,name,&st,AT_SYMLINK_NOFOLLOW)) {int e=errno;close(dir);return e==ENOENT?0:-1;}
    if(!S_ISREG(st.st_mode)){close(dir);return -1;}
    int file=openat(dir,name,O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);close(dir);
    if(file<0)return -1;
    int result=-1;
    if(!fstat(file,&st) && S_ISREG(st.st_mode) && st.st_size>=0 && st.st_size<=4096) {
        size_t at=0,n=(size_t)st.st_size;
        while(at<n) {ssize_t got=read(file,data+at,n-at);if(got<0 && errno==EINTR)continue;if(got<=0)break;at+=(size_t)got;}
        char extra;ssize_t tail;
        do {tail=read(file,&extra,1);}while(tail<0 && errno==EINTR);
        if(at==n && tail==0){*length=n;result=1;}
    }
    close(file);return result;
#endif
}
void mod_settings_load(ModView *view) {
    char data[4097];if(!directory[0])return;
    size_t n=0;int result=settings_read(view->id,data,&n);
    if(!result)return;
    if(result<0){snprintf(view->save_note,sizeof view->save_note,"Stored settings unreadable or unsafe; using defaults.");return;}
    if(memchr(data,0,n)){snprintf(view->save_note,sizeof view->save_note,"Stored settings rejected; using defaults.");return;}
    data[n]=0;
    ModView candidate=*view;unsigned seen=0;int valid=1;
    char *line=data;int first=1;
    while (line) {
        char *next=strchr(line,'\n');if(next)*next++=0;size_t len=strlen(line);if(len&&line[len-1]=='\r')line[--len]=0;
        if (first) {valid=!strcmp(line,"version=1");first=0;}
        else if (*line) {
            char *value=strchr(line,'=');if(!value){valid=0;break;}*value++=0;
            for(int i=0;i<candidate.field_count;++i) if(!strcmp(line,candidate.fields[i].id)) {
                if ((seen&(1u<<i)) || !stored_value(&candidate.fields[i],value,&candidate.fields[i].value)) valid=0;
                seen|=1u<<i;
            }
        }
        line=next;
    }
    if (valid) *view=candidate;
    else snprintf(view->save_note,sizeof view->save_note,"Stored settings rejected; using defaults.");
}
int mod_settings_save(ModView *view) {
    char path[4096],data[4096];if(!setting_path(view->id,path,sizeof path)) {snprintf(view->save_note,sizeof view->save_note,"Persistence unavailable; session values only.");view->persistence_dirty=1;return 0;}
    int at=snprintf(data,sizeof data,"version=1\n");
    for(int i=0;i<view->field_count;++i) {
        ModField *f=&view->fields[i];
        if(f->type==MOD_ENUM) at+=snprintf(data+at,sizeof(data)-(size_t)at,"%s=e:%s\n",f->id,f->options[f->value].id);
        else at+=snprintf(data+at,sizeof(data)-(size_t)at,"%s=%c:%d\n",f->id,f->type==MOD_BOOL?'b':'i',f->value);
    }
    plat_file_mkdir_p(directory);
    int ok=plat_file_write_atomic(path,data,(size_t)at);
    view->persistence_dirty=!ok;
    snprintf(view->save_note,sizeof view->save_note,"%s",ok?"Saved":"Save failed; previous file preserved. Session value retained.");return ok;
}

int mod_ui_intent_submit(const ModIntent *a) {
    int ok=0;take();
    if(!memchr(a->mod,0,sizeof a->mod)||!memchr(a->action,0,sizeof a->action)||!active||stopped||a->generation!=snapshot.generation||a->session!=snapshot.session)goto done;
    for(int i=0;i<snapshot.count;++i){ModView *m=&snapshot.mods[i];if(strcmp(m->id,a->mod)||m->status!=MOD_LOADED)continue;
        for(int j=0;j<m->button_count;++j){ModButton *b=&m->buttons[j];if(strcmp(b->id,a->action)||!b->enabled||(b->provider&&(!m->provider_available||m->provider_pending)))continue;
            if(intent_count<MOD_ACTION_MAX){intents[intent_count++]=*a;ok=1;snapshot.notice[0]=0;}else snprintf(snapshot.notice,sizeof snapshot.notice,"Action queue is full; try again.");
        }
    }
 done:release();return ok;
}
int mod_ui_intent_pop(ModIntent *a){take();int ok=intent_count>0&&!stopped;if(ok){*a=intents[0];memmove(intents,intents+1,(size_t)(--intent_count)*sizeof *intents);}release();return ok;}
int mod_ui_intent_current(const ModIntent *a){take();int ok=active&&!stopped&&a->generation==snapshot.generation&&a->session==snapshot.session;release();return ok;}
