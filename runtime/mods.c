// Ordinary-game lifecycle adapter. Lua executes only on this dedicated host thread.
#include "mods.h"
#include "mod_internal.h"
#include "mod_menu.h"
#include "platform/platform.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern void rt_log(const char *format,...);
static PlatMutex lock=PLAT_MUTEX_INIT;
static PlatCond condition=PLAT_COND_INIT;
static PlatThread *thread;
static ModPlan *plan;
static atomic_int stopping;
static uint32_t worker_id;
static int started, ready, closing;
static void log_message(const char *message) { rt_log("%s",message); }
static int cancelled(void) { return atomic_load_explicit(&stopping,memory_order_acquire); }
static void worker(void *unused) {
    (void)unused;
    plat_mutex_lock(&lock); worker_id=plat_thread_id(); plat_mutex_unlock(&lock);
    ModRuntime *runtime=mod_runtime_start(plan,log_message,cancelled);
    if(!runtime) {
        plat_mutex_lock(&lock);ready=1;worker_id=0;plat_cond_broadcast(&condition);plat_mutex_unlock(&lock);
        return; // Startup failure has no Lua states or work to dispatch.
    }
    // This event means host mod startup, not game readiness or a guest scheduler point.
    while (!cancelled() && mod_runtime_event(runtime,cancelled)) plat_sleep_ms(1);
    plat_mutex_lock(&lock); ready=1; plat_cond_broadcast(&condition);
    while (!cancelled()) {
        plat_mutex_unlock(&lock);
        int processed=mod_runtime_actions(runtime,cancelled);
        plat_mutex_lock(&lock);
        if(!cancelled()) plat_cond_wait_ms(&condition,&lock,processed?1:100);
    }
    plat_mutex_unlock(&lock);
    mod_runtime_stop(runtime);
}
void rt_mods_start(void) {
    if (started) return;
    started=1;
    char settings[4096]={0};
    if(plat_exe_path(settings,sizeof settings)) {
        char *s=strrchr(settings,'/'),*b=strrchr(settings,'\\');if(b&&(!s||b>s))s=b;
        if(s && (size_t)(s-settings)+14<sizeof settings)strcpy(s+1,"mod-settings");else settings[0]=0;
    }
    mod_ui_reset(settings);
    char root[4096]; const char *configured=getenv("XCX_MOD_DIR");
    if (configured) {
#ifdef _WIN32
        int absolute=(strlen(configured)>2 && configured[1]==':' && (configured[2]=='/' || configured[2]=='\\'));
#else
        int absolute=configured[0]=='/';
#endif
        if (!absolute || strlen(configured)>=sizeof root) { log_message("mods: XCX_MOD_DIR must be an absolute bounded path"); return; }
        strcpy(root,configured);
    } else {
        if (!plat_exe_path(root,sizeof root)) return;
        char *slash=strrchr(root,'/'), *back=strrchr(root,'\\'); if (back && (!slash || back>slash)) slash=back;
        if (!slash || (size_t)(slash-root)+6>=sizeof root) return;
        strcpy(slash+1,"mods");
    }
    mod_menu_reset();
    extern int rt_mod_menu_capable(void);mod_menu_support(rt_mod_menu_capable());
    plan=mod_packages_read(root,log_message);
    // The loader compares schema-2 requirements with actual linked game metadata.
    if(plan){extern const char rt_game_name[];snprintf(plan->game_id,sizeof plan->game_id,"%s",rt_game_name);}
    if (!plan) return; // No enabled valid package: no worker, Lua state or event queue.
    plat_mutex_lock(&lock);
    thread=plat_thread_new(worker,NULL,"xcx-mods");
    if (!thread) { plat_mutex_unlock(&lock); mod_packages_free(plan); plan=NULL; log_message("mods: worker creation failed"); mod_ui_start_failed("Mod worker creation failed; restart to retry."); return; }
    while (!ready) plat_cond_wait(&condition,&lock);
    plat_mutex_unlock(&lock);
}
void rt_mods_shutdown(void) {
    mod_menu_support(0);mod_menu_invalidate();
    plat_mutex_lock(&lock);
    mod_ui_shutdown();
    atomic_store_explicit(&stopping,1,memory_order_release); plat_cond_broadcast(&condition);
    // Script API cannot request exit; retain this guard for native exit callers on our thread.
    if (worker_id && worker_id==plat_thread_id()) { plat_mutex_unlock(&lock); return; }
    while (closing) plat_cond_wait(&condition,&lock);
    PlatThread *joining=thread;
    if (!joining) { plat_mutex_unlock(&lock); return; }
    closing=1; plat_mutex_unlock(&lock);
    plat_thread_join(joining); // No host lock is held while a callback finishes.
    mod_packages_free(plan);
    plat_mutex_lock(&lock); plan=NULL; thread=NULL; closing=0; plat_cond_broadcast(&condition); plat_mutex_unlock(&lock);
}

int rt_mods_ui_submit(const ModAction *action) {
    if(cancelled() || !mod_ui_submit(action))return 0;
    plat_mutex_lock(&lock);plat_cond_broadcast(&condition);plat_mutex_unlock(&lock);return 1;
}

int rt_mods_ui_intent(const ModIntent *intent) {
    if(cancelled()||!mod_ui_intent_submit(intent))return 0;
    plat_mutex_lock(&lock);plat_cond_broadcast(&condition);plat_mutex_unlock(&lock);return 1;
}
