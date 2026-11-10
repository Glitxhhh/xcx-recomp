#include "mods_page.h"
#include "imgui.h"
#include <cstdio>
#include <cstring>
extern "C" {
#include "mods.h"
#include "mod_settings.h"
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
void mods_page(float scale,float bottom,bool focus_first) {
#ifdef XCX_MOD_UI_TESTING
    mods_widget_count=0;mods_retry_visible=false;mods_button_count=0;
#endif
    mod_ui_interaction(1);
    static ModSnapshot snapshot;mod_ui_snapshot(&snapshot);
    static char selected[65];
    if(requested_owner[0]){for(int i=0;i<snapshot.count;++i)if(!strcmp(snapshot.mods[i].id,requested_owner)&&!strcmp(snapshot.mods[i].page_id,requested_page))snprintf(selected,sizeof selected,"%s",requested_owner);requested_owner[0]=0;}
    if(!snapshot.count) {ImGui::TextWrapped("No mods configured. Add packages and edit enabled.txt, then restart.");return;}
    int index=0;for(int i=0;i<snapshot.count;++i)if(!strcmp(selected,snapshot.mods[i].id))index=i;
    snprintf(selected,sizeof selected,"%s",snapshot.mods[index].id);
    if(ImGui::BeginChild("##mods_page",ImVec2(0,ImGui::GetContentRegionAvail().y-bottom),ImGuiChildFlags_NavFlattened)) {
        if(ImGui::BeginTable("##mods_columns",2,ImGuiTableFlags_SizingStretchProp)) {
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
