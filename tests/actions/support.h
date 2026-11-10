#pragma once
#include "mod_provider_service_abi.h"
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
typedef CRITICAL_SECTION Lock;typedef HANDLE Thread;
static void lock_init(Lock *m){InitializeCriticalSection(m);}static void lock_take(Lock *m){EnterCriticalSection(m);}static void lock_drop(Lock *m){LeaveCriticalSection(m);}static void lock_free(Lock *m){DeleteCriticalSection(m);}static void delay(unsigned ms){Sleep(ms);}
#define WORKER DWORD WINAPI
#define RETURN_WORKER return 0
static void join_thread(Thread t){WaitForSingleObject(t,INFINITE);CloseHandle(t);}
#else
#include <pthread.h>
#include <time.h>
typedef pthread_mutex_t Lock;typedef pthread_t Thread;
static void lock_init(Lock *m){pthread_mutex_init(m,NULL);}static void lock_take(Lock *m){pthread_mutex_lock(m);}static void lock_drop(Lock *m){pthread_mutex_unlock(m);}static void lock_free(Lock *m){pthread_mutex_destroy(m);}static void delay(unsigned ms){struct timespec t={ms/1000,(long)(ms%1000)*1000000};nanosleep(&t,NULL);}
#define WORKER void *
#define RETURN_WORKER return NULL
static void join_thread(Thread t){pthread_join(t,NULL);}
#endif
static int flag(int *p){return __atomic_load_n(p,__ATOMIC_ACQUIRE);}static void setflag(int *p,int v){__atomic_store_n(p,v,__ATOMIC_RELEASE);}
static void spec(XmsCommandSpec *c,unsigned op,const char *id,unsigned type,int min,int max){c->opcode=op;c->type=type;c->minimum=min;c->maximum=max;strcpy(c->id,id);}
