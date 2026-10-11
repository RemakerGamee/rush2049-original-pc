#pragma once
#include "lus-imgui-options.hpp"
#include "imgui/imgui.h"
#include "controller-presets.hpp"
#include "lus-imgui-store.hpp"
namespace rush::options {
using namespace rush::input;
inline Config input_draft;
inline bool input_loaded=false;
inline unsigned input_revision=0;
inline int selected_port=0;
inline std::string selected_guid;
inline int capture_target=-1;
inline Uint64 capture_deadline=0;
inline std::string capture_guid;
inline bool capture_standard=false;
inline int64_t capture_instance=-1;
inline std::string mapping_notice;
inline char unlisted_name[129]{};
inline int unlisted_family=-1;
inline std::string unlisted_guid;
inline bool open_unlisted=false;
inline Profile* selected_profile(){
    for(auto& p:input_draft.profiles)if(p.guid==selected_guid)return &p;
    return nullptr;
}
inline std::string binding_label(const Binding& b,bool standard){
    if(b.kind==BindKind::Button){
        if(standard){const char* name=SDL_GameControllerGetStringForButton(SDL_GameControllerButton(b.index));if(name)return name;}
        return "Button "+std::to_string(b.index+1);
    }
    if(b.kind==BindKind::Hat)return std::string("D-pad ")+(b.hat_mask==1?"Up":b.hat_mask==2?"Right":b.hat_mask==4?"Down":"Left");
    const char* name=standard?SDL_GameControllerGetStringForAxis(SDL_GameControllerAxis(b.index)):nullptr;
    return (name?std::string(name):"Axis "+std::to_string(b.index+1))+(b.axis_sign<0?" -":" +");
}
inline void cancel_capture(){capture_target=-1;capture_active.store(false);}
inline void begin_capture(int target,const DeviceSummary& device){
    capture_target=target;capture_guid=device.info.guid;capture_standard=device.info.game_controller;
    capture_instance=device.info.instance;capture_deadline=SDL_GetTicks64()+10000;capture_active.store(true);
    mapping_notice=target<14?std::string("Press a control for ")+button_name(Button(target)):target==14?"Move the stick RIGHT.":"Move the stick UP.";
}
inline void capture_binding(const SDL_Event& event){
    if(capture_target<0)return;
    if(event.type==SDL_KEYDOWN && event.key.keysym.scancode==SDL_SCANCODE_ESCAPE){cancel_capture();mapping_notice="Assignment cancelled.";return;}
    if(SDL_GetTicks64()>capture_deadline){cancel_capture();mapping_notice="No input captured. Previous mapping retained.";return;}
    auto* p=selected_profile();if(!p || p->guid!=capture_guid){cancel_capture();return;}
    Binding b;bool ready=false;int axis=-1,sign=1;
    if(capture_standard){
        if(event.type==SDL_CONTROLLERBUTTONDOWN && event.cbutton.which==capture_instance){b={BindKind::Button,int(event.cbutton.button)};ready=true;}
        if(event.type==SDL_CONTROLLERAXISMOTION && event.caxis.which==capture_instance && std::abs(int(event.caxis.value))>20000){axis=event.caxis.axis;sign=event.caxis.value<0?-1:1;}
    }else{
        if(event.type==SDL_JOYBUTTONDOWN && event.jbutton.which==capture_instance){b={BindKind::Button,int(event.jbutton.button)};ready=true;}
        if(event.type==SDL_JOYHATMOTION && event.jhat.which==capture_instance && (event.jhat.value==1 || event.jhat.value==2 || event.jhat.value==4 || event.jhat.value==8)){b={BindKind::Hat,int(event.jhat.hat),event.jhat.value};ready=true;}
        if(event.type==SDL_JOYAXISMOTION && event.jaxis.which==capture_instance && std::abs(int(event.jaxis.value))>20000){axis=event.jaxis.axis;sign=event.jaxis.value<0?-1:1;}
    }
    p->enabled=true;
    if(capture_target>=14){
        if(axis<0)return;
        auto& a=capture_target==14?p->stick_x:p->stick_y;a={true,axis,capture_target==14?sign<0:sign>0};
    }else{
        if(axis>=0){b={BindKind::Axis,axis,0,sign,0.5f};ready=true;}
        if(!ready)return;
        auto& list=p->bindings[size_t(capture_target)];
        if(std::none_of(list.begin(),list.end(),[&](const Binding& old){return old.kind==b.kind && old.index==b.index && old.hat_mask==b.hat_mask && old.axis_sign==b.axis_sign;}))list.push_back(b);
    }
    mapping_notice="Binding added. Apply and save to use it.";cancel_capture();
}
inline bool is_xbox(const DeviceSummary& d){
    std::string name=d.info.name;std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});
    return d.info.game_controller && (name.find("xbox")!=std::string::npos || name.find("xinput")!=std::string::npos);
}
inline std::string device_label(const DeviceSummary& d){
    for(const auto& p:input_draft.profiles)if(p.guid==d.info.guid && !p.display_name.empty())return p.display_name;
    if(const auto* name=controller_preset_name(d.info.guid))return name;
    if(is_xbox(d))return "Xbox One controller";
    return d.info.name;
}
inline void add_preset_device(const DeviceSummary& d,Profile preset){
    selected_guid=d.info.guid;cancel_capture();
    for(auto it=input_draft.assignments.begin();it!=input_draft.assignments.end();)if(it->second==selected_port && it->first!=selected_guid)it=input_draft.assignments.erase(it);else ++it;
    input_draft.assignments[selected_guid]=selected_port;
    // Keep existing custom mappings. Factory defaults fill a newly added device.
    if(!selected_profile())input_draft.profiles.push_back(std::move(preset));
    input_draft.keyboard_port=selected_port;
    mapping_notice=device_label(d)+" bindings added beside Keyboard. Apply and save to use them.";
}
inline void controller_page(Store& preferences){
    std::vector<DeviceSummary> devices;std::array<PortState,4> states;bool saving;std::string backend_error;
    {
        std::lock_guard<std::mutex> lock(input_ui.mutex);
        if(!input_ui.ready){ImGui::TextUnformatted("Waiting for controller discovery...");return;}
        if(!input_loaded || input_revision!=input_ui.revision){input_draft=input_ui.config;input_loaded=true;input_revision=input_ui.revision;}
        devices=input_ui.devices;states=input_ui.states;saving=input_ui.saving;backend_error=input_ui.error;
    }
    for(int p=0;p<4;++p){if(p)ImGui::SameLine();ImGui::PushID(p);if(ImGui::Selectable(("Port "+std::to_string(p+1)).c_str(),selected_port==p,0,ImVec2(72,0))){selected_port=p;selected_guid.clear();cancel_capture();}ImGui::PopID();}
    ImGui::Combo("Controller accessory",&preferences.pak_modes[selected_port],"Memory Pak\0Rumble Pak\0");
    if(preferences.pak_modes[selected_port]==1){
        ImGui::TextWrapped("Not all controllers support rumble. Check your controller manufacturer's specifications to confirm compatibility.");
        ImGui::Text("Detected rumble support: %s",rush::accessories::rumble_supported[selected_port].load()?"Available":"Not detected");
    }else{
        ImGui::TextWrapped(selected_port==0?(rush::accessories::memory_attached.load()?"Memory Pak: connected. Existing save storage is preserved.":"Memory Pak: no save image is configured for this session."):"Memory Pak save storage is currently available on Port 1.");
    }
    ImGui::TextDisabled("Pak changes take effect on the next launch after Apply and save.");
    bool keyboard=input_draft.keyboard_port==selected_port;
    if(ImGui::Checkbox("Keyboard (default input)",&keyboard))input_draft.keyboard_port=keyboard?selected_port:-1;
    ImGui::SameLine();if(ImGui::Button("+ Add device"))ImGui::OpenPopup("Add input device");
    if(ImGui::BeginPopup("Add input device")){
        const char* labels[]={"Xbox One controller","Wired N64 controller","Wireless / USB dongle N64 controller"};
        for(int kind=0;kind<3;++kind){
            bool found=false;
            for(const auto& d:devices){
                bool match=kind==0?is_xbox(d):d.info.guid==(kind==1?wired_n64_guid:wireless_n64_guid);
                if(!match)continue;
                found=true;ImGui::PushID(int(d.info.instance));
                if(ImGui::Selectable(labels[kind]))add_preset_device(d,kind==0?xbox_controller_preset(d.info.guid):n64_controller_preset(kind==2));
                ImGui::PopID();
            }
            if(!found){ImGui::BeginDisabled();ImGui::Selectable(labels[kind]);ImGui::EndDisabled();ImGui::SameLine();ImGui::TextDisabled("Not connected");}
        }
        for(const auto& d:devices){
            if(is_xbox(d) || controller_preset_name(d.info.guid))continue;
            if(ImGui::Selectable(d.info.name.c_str())){
                Profile p;if(d.info.game_controller)p=default_game_controller_profile();else p.enabled=false;
                p.id="device-"+d.info.guid;p.guid=d.info.guid;p.game_controller=d.info.game_controller;
                add_preset_device(d,std::move(p));
            }
        }
        ImGui::Separator();if(ImGui::Selectable("Unlisted..."))open_unlisted=true;
        ImGui::EndPopup();
    }
    if(open_unlisted){ImGui::OpenPopup("Unlisted controller");open_unlisted=false;}
    if(ImGui::BeginPopupModal("Unlisted controller",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::TextUnformatted("Name your controller and select its type.");
        ImGui::InputText("Controller name",unlisted_name,sizeof(unlisted_name));
        ImGui::Combo("Controller type",&unlisted_family,"PS3\0PS4\0Xbox 360\0Xbox One wired\0Xbox One wireless\0N64 wired\0N64 wireless / USB dongle\0");
        std::string selected_device="Select connected device";
        for(const auto& d:devices)if(d.info.guid==unlisted_guid)selected_device=d.info.name;
        if(ImGui::BeginCombo("Physical device",selected_device.c_str())){for(const auto& d:devices)if(ImGui::Selectable(d.info.name.c_str(),unlisted_guid==d.info.guid))unlisted_guid=d.info.guid;ImGui::EndCombo();}
        const DeviceSummary* found=nullptr;for(const auto& d:devices)if(d.info.guid==unlisted_guid)found=&d;
        ImGui::TextWrapped("Next, use the + buttons to assign your controller inputs alongside Keyboard.");
        ImGui::BeginDisabled(!found || unlisted_family<0 || !valid_controller_name(unlisted_name));
        if(ImGui::Button("Add and map inputs")){
            Profile p;if(found->info.game_controller)p=default_game_controller_profile();else p.enabled=false;
            p.guid=found->info.guid;p.id="unlisted-"+p.guid;p.game_controller=found->info.game_controller;
            add_preset_device(*found,std::move(p));auto* added=selected_profile();
            added->display_name=unlisted_name;added->controller_family=controller_families[unlisted_family];
            ImGui::CloseCurrentPopup();mapping_notice="Controller added. Map and test your inputs, then submit them for review.";
        }
        ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
    }
    const DeviceSummary* device=nullptr;
    for(const auto& d:devices){
        auto assignment=input_draft.assignments.find(d.info.guid);
        bool in_port=assignment!=input_draft.assignments.end()?assignment->second==selected_port:d.port==selected_port;
        if(!in_port)continue;
        if(selected_guid.empty())selected_guid=d.info.guid;
        ImGui::PushID(d.info.guid.c_str());
        if(ImGui::Selectable(device_label(d).c_str(),selected_guid==d.info.guid))selected_guid=d.info.guid;
        if(selected_guid==d.info.guid)device=&d;
        ImGui::PopID();
    }
    // A recognised SDL device has a real built-in profile even before it is pinned.
    if(device && !selected_profile()){
        if(device->info.game_controller){auto p=default_game_controller_profile();p.guid=device->info.guid;p.id="device-"+p.guid;input_draft.profiles.push_back(p);}
    }
    auto* profile=selected_profile();
    if(profile && device){
        if(ImGui::Button("Set defaults")){
            std::lock_guard<std::mutex> lock(input_ui.mutex);
            bool restored=false;
            for(const auto& p:input_ui.config.profiles)if(p.guid==selected_guid){*profile=p;restored=true;break;}
            if(const auto* preset=controller_preset_name(selected_guid)){*profile=n64_controller_preset(selected_guid==wireless_n64_guid);restored=true;}
            if(!restored && device->info.game_controller){*profile=default_game_controller_profile();profile->guid=selected_guid;profile->id="device-"+selected_guid;}
            mapping_notice=restored?"Saved device defaults restored.":device->info.game_controller?"Xbox / SDL defaults restored.":"No verified defaults for this device. Use + to map it.";
        }
        ImGui::SameLine();if(ImGui::Button("Clear controller bindings")){for(auto& row:profile->bindings)row.clear();profile->stick_x={};profile->stick_y={};profile->enabled=false;}
    }
    if(capture_target>=0){if(SDL_GetTicks64()>capture_deadline){cancel_capture();mapping_notice="Assignment timed out.";}else{ImGui::TextWrapped("%s Escape cancels. (%d seconds)",mapping_notice.c_str(),int((capture_deadline-SDL_GetTicks64())/1000));if(ImGui::Button("Cancel assignment"))cancel_capture();}}
    else if(!mapping_notice.empty())ImGui::TextWrapped("%s",mapping_notice.c_str());
    const char* keys[]={"Z","X","Enter","Space","Q","E","Unassigned","Unassigned","Unassigned","Unassigned","I","K","J","L"};
    if(ImGui::BeginTable("Bindings",3,ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_RowBg)){
        ImGui::TableSetupColumn("N64 control",ImGuiTableColumnFlags_WidthFixed,85);ImGui::TableSetupColumn("Keyboard",ImGuiTableColumnFlags_WidthFixed,90);ImGui::TableSetupColumn("Controller bindings");ImGui::TableHeadersRow();
        for(int row=0;row<14;++row){
            ImGui::PushID(row);ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::TextUnformatted(button_name(Button(row)));
            if(row==10 && ImGui::IsItemHovered())ImGui::SetTooltip("Abort / reset car (original Rush controls)");
            if(row==12 && ImGui::IsItemHovered())ImGui::SetTooltip("Reverse (original Rush controls)");
            ImGui::TableNextColumn();ImGui::TextUnformatted(keyboard?keys[row]:"Disabled");ImGui::TableNextColumn();
            if(profile){
                auto& bs=profile->bindings[row];
                for(size_t b=0;b<bs.size();){ImGui::PushID(int(b));ImGui::TextUnformatted(binding_label(bs[b],profile->game_controller).c_str());ImGui::SameLine();bool remove=ImGui::SmallButton("x");ImGui::PopID();if(remove)bs.erase(bs.begin()+b);else ++b;ImGui::SameLine();}
            }
            ImGui::BeginDisabled(!profile || !device || capture_target>=0);if(ImGui::SmallButton("+"))begin_capture(row,*device);ImGui::EndDisabled();ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if(ImGui::CollapsingHeader("Analog stick",ImGuiTreeNodeFlags_DefaultOpen)){
        ImGui::TextUnformatted(keyboard?"Keyboard: arrow keys steer / move the stick":"Keyboard disabled on this port");
        if(profile && device){
            auto stick_row=[&](const char* label,StickAxis& a,int target){ImGui::PushID(target);ImGui::Text("%s: %s",label,a.set?("Axis "+std::to_string(a.axis+1)+(a.invert?" inverted":"")).c_str():"Unassigned");ImGui::SameLine();ImGui::BeginDisabled(capture_target>=0);if(ImGui::SmallButton("+"))begin_capture(target,*device);ImGui::EndDisabled();ImGui::SameLine();if(ImGui::SmallButton("x"))a={};ImGui::PopID();};
            stick_row("Left / right",profile->stick_x,14);stick_row("Up / down",profile->stick_y,15);
            ImGui::SliderFloat("Sensitivity",&profile->sensitivity,0.1f,2.0f,"%.2fx");ImGui::SliderFloat("Deadzone",&profile->deadzone,0,0.9f,"%.2f");
            ImGui::Text("Live stick: X %.2f  Y %.2f",states[selected_port].x,states[selected_port].y);
        }
    }
    ImGui::Separator();
    ImGui::BeginDisabled(!profile || !device || !valid_controller_name(profile->display_name) || !valid_controller_family(profile->controller_family));
    if(ImGui::Button("Submit mapping for review"))ImGui::OpenPopup("Review controller submission");
    ImGui::EndDisabled();
    if(ImGui::BeginPopupModal("Review controller submission",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        if(profile && device){
            ImGui::Text("Name: %s",profile->display_name.c_str());
            for(int i=0;i<7;++i)if(profile->controller_family==controller_families[i])ImGui::Text("Type: %s",controller_family_labels[i]);
            ImGui::BeginChild("Submission bindings",ImVec2(420,180),true);
            for(int row=0;row<14;++row){ImGui::Text("%s:",button_name(Button(row)));for(const auto& b:profile->bindings[row]){ImGui::SameLine();ImGui::TextUnformatted(binding_label(b,profile->game_controller).c_str());}if(profile->bindings[row].empty()){ImGui::SameLine();ImGui::TextDisabled("Unassigned");}}
            ImGui::Text("Stick axes: X %d, Y %d",profile->stick_x.axis+1,profile->stick_y.axis+1);ImGui::EndChild();
            ImGui::TextWrapped("Your controller name, type, device identifier and mappings will go to the public Controller Submissions area. Finish submitting on GitHub. Saves, ROMs and device serial numbers are excluded.");
            if(ImGui::Button("Open GitHub submission form")){
                try{ReviewRequest request{*profile,device->info};(void)controller_submission(request);std::lock_guard<std::mutex> lock(input_ui.mutex);input_ui.review_request=std::move(request);input_ui.review_message="Preparing your review package...";ImGui::CloseCurrentPopup();}
                catch(const std::exception& e){mapping_notice=e.what();}
            }
            ImGui::TextWrapped("%s",mapping_notice.c_str());
        }
        ImGui::SameLine();if(ImGui::Button("Cancel submission"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
    }
    {std::lock_guard<std::mutex> lock(input_ui.mutex);if(!input_ui.review_message.empty())ImGui::TextWrapped("%s",input_ui.review_message.c_str());}
    if(saving)ImGui::TextUnformatted("Saving controller settings...");
    if(!backend_error.empty())ImGui::TextWrapped("%s",backend_error.c_str());
}
inline void save_controller_draft(){std::lock_guard<std::mutex> lock(input_ui.mutex);if(!input_ui.saving){input_ui.pending=input_draft;input_ui.saving=true;input_ui.error.clear();}}
}
