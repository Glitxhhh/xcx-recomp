#pragma once
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
static inline void pause_ms(unsigned n){Sleep(n);}
#else
#include <time.h>
static inline void pause_ms(unsigned n){struct timespec t={n/1000,(long)(n%1000)*1000000};nanosleep(&t,NULL);}
#endif
static void marker(const char *id,const char *event){const char *p=getenv("XMP_MARKER");if(p){FILE *f=fopen(p,"a");if(f){fprintf(f,"%.64s %.64s\n",id,event);fclose(f);}}}
