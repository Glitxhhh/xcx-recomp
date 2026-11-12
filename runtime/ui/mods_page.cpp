#include "mods_page.h"
#include "imgui.h"
#include <cstdio>
#include <cstring>
extern "C" {
#include "mods.h"
#include "mod_settings.h"
#include "mod_library.h"
#include "ui_text.h"
}
#ifdef XCX_MOD_UI_TESTING
#include "imgui_internal.h"
struct ModsWidget {ImGuiID id;ImGuiWindow *window;ImVec2 low,high;char mod[65],field[65];};
static ModsWidget mods_widgets[128];static int mods_widget_count;
static ImVec2 mods_row_low[MOD_UI_MAX],mods_row_high[MOD_UI_MAX];
static ImGuiID mods_button_id[4];static ImGuiWindow *mods_button_window[4];
static ImVec2 mods_button_low[4],mods_button_high[4];static int mods_button_count;
static ImVec2 mods_retry_low,mods_retry_high;static bool mods_retry_visible;
#endif
static const char *status_name(int status) {
    switch(status) {case MOD_LOADED:return "Loaded";case MOD_DISABLED:return "Disabled";case MOD_INCOMPATIBLE:return "Incompatible";case MOD_FAULTED:return "Faulted";default:return "Starting";}
}
static char requested_owner[65],requested_page[65];
void mods_page_select(const char *owner,const char *page){snprintf(requested_owner,sizeof requested_owner,"%s",owner);snprintf(requested_page,sizeof requested_page,"%s",page);}

// The mod library (runtime/mod_library.c): its mods and the installed ones on the left, the picked one's text and
// buttons on the right. Jobs run on the library's thread; this only reads its view and asks. Returns whether a row
// took the first focus.
static bool library(float scale,bool focus_first) {
    static ModLibView v;modlib_snapshot(&v);
    static char picked[65],confirm_remove[65];
    ImGui::SeparatorText(menu_text(UI_MODS_LIBRARY));
    bool idle=v.state==MODLIB_IDLE;
    if(v.state==MODLIB_LOADING)ImGui::TextDisabled("%s",menu_text(UI_MODS_LOADING));
    else if(v.state==MODLIB_WORKING&&v.total)ImGui::TextDisabled(menu_text(UI_MODS_DOWNLOADING),v.job,v.done/1048576.0,v.total/1048576.0);
    else if(v.state==MODLIB_WORKING)ImGui::TextDisabled(menu_text(UI_MODS_WORKING),v.job);
    if(v.message[0]){
        if(v.message_error)ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1.0f,0.55f,0.5f,1));
        ImGui::TextWrapped("%s",v.message);
        if(v.message_error)ImGui::PopStyleColor();
    }
    if(!v.count){
        ImGui::TextWrapped("%s",menu_text(UI_MODS_EMPTY));
        ImGui::BeginDisabled(!idle);if(ImGui::Button(menu_text(UI_MODS_REFRESH)))modlib_refresh();ImGui::EndDisabled();
        return false;
    }
    int index=0;for(int i=0;i<v.count;++i)if(!strcmp(picked,v.rows[i].id))index=i;
    snprintf(picked,sizeof picked,"%s",v.rows[index].id);
    bool focused=false;
    if(ImGui::BeginTable("##library",2,ImGuiTableFlags_SizingStretchProp)){
        ImGui::TableSetupColumn("##library_list",ImGuiTableColumnFlags_WidthFixed,300*scale);
        ImGui::TableSetupColumn("##library_detail",ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextColumn();
        for(int i=0;i<v.count;++i){
            const ModLibRow &r=v.rows[i];ImGui::PushID(r.id);
            if(focus_first&&i==index){ImGui::SetKeyboardFocusHere();focused=true;}
            if(ImGui::Selectable(r.name[0]?r.name:r.id,i==index)){snprintf(picked,sizeof picked,"%s",r.id);index=i;}
            UiTextId state=!r.installed?UI_MODS_STATE_AVAILABLE:r.update&&r.in_library?UI_MODS_STATE_UPDATE:r.enabled?UI_MODS_STATE_ON:UI_MODS_STATE_OFF;
            ImGui::TextDisabled("%s",menu_text(state));ImGui::Spacing();ImGui::PopID();
        }
        ImGui::BeginDisabled(!idle);if(ImGui::Button(menu_text(UI_MODS_REFRESH)))modlib_refresh();ImGui::EndDisabled();
        ImGui::TableNextColumn();
        const ModLibRow &r=v.rows[index];ImGui::PushID(r.id);
        ImGui::TextUnformatted(r.name[0]?r.name:r.id);
        if(r.author[0])ImGui::TextDisabled(menu_text(UI_MODS_BY),r.author);
        if(r.description[0])ImGui::TextWrapped("%s",r.description);
        if(r.notes[0]){ImGui::PushTextWrapPos(0);ImGui::TextDisabled("%s",r.notes);ImGui::PopTextWrapPos();}
        ImGui::PushTextWrapPos(0);ImGui::TextDisabled("%s",menu_text(strcmp(r.kind,"lua")?UI_MODS_KIND_ROMFS:UI_MODS_KIND_LUA));ImGui::PopTextWrapPos();
        if(r.in_library&&!strcmp(r.kind,"romfs")&&(!r.installed||r.update)&&r.homepage[0]){
            ImGui::PushTextWrapPos(0);ImGui::TextDisabled(menu_text(UI_MODS_SOURCE),r.size/1048576.0,r.homepage);ImGui::PopTextWrapPos();
        }
        ImGui::Spacing();
        ImGui::BeginDisabled(!idle);
        if(r.in_library&&(!r.installed||r.update)){
            if(ImGui::Button(menu_text(r.installed?UI_MODS_UPDATE:UI_MODS_INSTALL))){modlib_install(r.id);confirm_remove[0]=0;}
            ImGui::SameLine();
        }
        if(r.installed){
            bool on=r.enabled!=0;
            if(ImGui::Checkbox(menu_text(UI_MODS_ON),&on))modlib_set_enabled(r.id,on);
            ImGui::SameLine(0,24*scale);
            bool sure=!strcmp(confirm_remove,r.id);
            if(ImGui::Button(menu_text(sure?UI_MODS_REMOVE_CONFIRM:UI_MODS_REMOVE))){
                if(sure){modlib_remove(r.id);confirm_remove[0]=0;}else snprintf(confirm_remove,sizeof confirm_remove,"%s",r.id);
            }
        }
        ImGui::EndDisabled();
        if(strcmp(confirm_remove,r.id))confirm_remove[0]=0;   // another mod picked: the second press starts over
        ImGui::PopID();ImGui::EndTable();
    }
    return focused;
}

void mods_page(float scale,float bottom,bool focus_first) {
#ifdef XCX_MOD_UI_TESTING
    mods_widget_count=0;mods_retry_visible=false;mods_button_count=0;
#endif
    mod_ui_interaction(1);
    static ModSnapshot snapshot;mod_ui_snapshot(&snapshot);
    static char selected[65];
    if(requested_owner[0]){for(int i=0;i<snapshot.count;++i)if(!strcmp(snapshot.mods[i].id,requested_owner)&&!strcmp(snapshot.mods[i].page_id,requested_page))snprintf(selected,sizeof selected,"%s",requested_owner);requested_owner[0]=0;}
    if(ImGui::BeginChild("##mods_page",ImVec2(0,ImGui::GetContentRegionAvail().y-bottom),ImGuiChildFlags_NavFlattened)) {
        if(library(scale,focus_first))focus_first=false;
        ImGui::Spacing();ImGui::SeparatorText(menu_text(UI_MODS_LUA));
        if(!snapshot.count)ImGui::TextWrapped("%s",menu_text(UI_MODS_NO_LUA));
    }
    int index=0;for(int i=0;i<snapshot.count;++i)if(!strcmp(selected,snapshot.mods[i].id))index=i;
    if(snapshot.count)snprintf(selected,sizeof selected,"%s",snapshot.mods[index].id);
    {
        if(snapshot.count&&ImGui::BeginTable("##mods_columns",2,ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("##mod_list",ImGuiTableColumnFlags_WidthFixed,260*scale);
            ImGui::TableSetupColumn("##mod_settings",ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextColumn();
            for(int i=0;i<snapshot.count;++i) {
                const ModView &mod=snapshot.mods[i];ImGui::PushID(mod.id);
                if(focus_first && i==index)ImGui::SetKeyboardFocusHere();
                if(ImGui::Selectable(mod.name[0]?mod.name:mod.id,i==index)) {
                    if(index!=i){mod_ui_interaction(0);mod_ui_interaction(1);mod_ui_snapshot(&snapshot);}
                    index=i;snprintf(selected,sizeof selected,"%s",mod.id);
                }
#ifdef XCX_MOD_UI_TESTING
                mods_row_low[i]=ImGui::GetItemRectMin();mods_row_high[i]=ImGui::GetItemRectMax();
#endif
                ImGui::TextDisabled("%s",status_name(mod.status));ImGui::Spacing();ImGui::PopID();
            }
            ImGui::TableNextColumn();const ModView &mod=snapshot.mods[index];ImGui::PushID(mod.id);
            ImGui::TextUnformatted(mod.name[0]?mod.name:mod.id);ImGui::TextDisabled("%s  %s",mod.id,mod.version);
            ImGui::Text("Status: %s",status_name(mod.status));
            if(mod.diagnostic[0])ImGui::TextWrapped("%s",mod.diagnostic);
            if(mod.title[0]) {ImGui::Separator();ImGui::TextUnformatted(mod.title);}
            ImGui::BeginDisabled(mod.status!=MOD_LOADED);
            for(int i=0;i<mod.field_count;++i) {
                const ModField &f=mod.fields[i];ImGui::PushID(f.id);ImGui::TextUnformatted(f.label);
                bool changed=false;int value=f.value;
                ImGui::SetNextItemWidth(-1);
                if(f.type==MOD_BOOL) {bool checked=value!=0;changed=ImGui::Checkbox("##value",&checked);value=checked;}
                else if(f.type==MOD_INTEGER)changed=ImGui::SliderInt("##value",&value,f.minimum,f.maximum,"%d",ImGuiSliderFlags_AlwaysClamp|ImGuiSliderFlags_NoInput);
                else if(f.type==MOD_ENUM && ImGui::BeginCombo("##value",f.options[value].label)) {
                    for(int j=0;j<f.option_count;++j) {ImGui::PushID(f.options[j].id);if(ImGui::Selectable(f.options[j].label,j==value)){value=j;changed=true;}if(j==value)ImGui::SetItemDefaultFocus();ImGui::PopID();}
                    ImGui::EndCombo();
                }
#ifdef XCX_MOD_UI_TESTING
                if(mods_widget_count<128) {ModsWidget &w=mods_widgets[mods_widget_count++];w.id=GImGui->LastItemData.ID;w.window=ImGui::GetCurrentWindow();w.low=ImGui::GetItemRectMin();w.high=ImGui::GetItemRectMax();snprintf(w.mod,sizeof w.mod,"%s",mod.id);snprintf(w.field,sizeof w.field,"%s",f.id);}
#endif
                if(changed) {
                    ModAction a={};a.generation=snapshot.generation;a.session=snapshot.session;a.type=f.type;a.value=value;
                    snprintf(a.mod,sizeof a.mod,"%s",mod.id);snprintf(a.field,sizeof a.field,"%s",f.id);rt_mods_ui_submit(&a);
                }
                ImGui::PopID();
            }
            ImGui::EndDisabled();
            if(mod.save_note[0])ImGui::TextWrapped("%s",mod.save_note);
            if(mod.persistence_dirty && mod.field_count) {
                ImGui::BeginDisabled(mod.status!=MOD_LOADED);
                if(ImGui::Button("Retry save")) {
                    // A same-value action retries the complete page without repeating Lua events.
                    const ModField &f=mod.fields[0];ModAction a={};a.generation=snapshot.generation;a.session=snapshot.session;a.type=f.type;a.value=f.value;
                    snprintf(a.mod,sizeof a.mod,"%s",mod.id);snprintf(a.field,sizeof a.field,"%s",f.id);rt_mods_ui_submit(&a);
                }
#ifdef XCX_MOD_UI_TESTING
                mods_retry_visible=true;mods_retry_low=ImGui::GetItemRectMin();mods_retry_high=ImGui::GetItemRectMax();
#endif
                ImGui::EndDisabled();
            }
            if(mod.button_count){
                ImGui::Separator();ImGui::PushID("actions");bool provider_buttons=false;
                for(int i=0;i<mod.button_count;++i){const ModButton &button=mod.buttons[i];provider_buttons|=button.provider!=0;ImGui::PushID(button.id);
                    ImGui::BeginDisabled(mod.status!=MOD_LOADED||!button.enabled||(button.provider&&(!mod.provider_available||mod.provider_pending)));
                    if(ImGui::Button(button.label)){ModIntent a={};a.generation=snapshot.generation;a.session=snapshot.session;snprintf(a.mod,sizeof a.mod,"%s",mod.id);snprintf(a.action,sizeof a.action,"%s",button.id);rt_mods_ui_intent(&a);}
#ifdef XCX_MOD_UI_TESTING
                    if(mods_button_count<4){mods_button_id[mods_button_count]=GImGui->LastItemData.ID;mods_button_window[mods_button_count]=ImGui::GetCurrentWindow();mods_button_low[mods_button_count]=ImGui::GetItemRectMin();mods_button_high[mods_button_count++]=ImGui::GetItemRectMax();}
#endif
                    ImGui::EndDisabled();ImGui::PopID();
                }
                ImGui::PopID();
                if(mod.provider_pending)ImGui::Text("Pending work: %d",mod.provider_pending);
                if(provider_buttons&&mod.provider_note[0])ImGui::TextWrapped("%s",mod.provider_note);
            }
            if(mod.ui_status[0])ImGui::TextWrapped("%s",mod.ui_status);
            if(snapshot.pending)ImGui::TextDisabled("Applying settings...");
            if(snapshot.notice[0])ImGui::TextWrapped("%s",snapshot.notice);
            ImGui::Separator();ImGui::TextWrapped("Package and enable/disable changes take effect after restart.");
            ImGui::PopID();ImGui::EndTable();
        }
    }
    ImGui::EndChild();
}
