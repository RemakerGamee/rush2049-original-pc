#pragma once
#include "lus-imgui-options.hpp"
#include "lus-imgui-store.hpp"
#include "gui/rt64_inspector.h"
#include "rhi/rt64_render_hooks.h"
#include "imgui/imgui.h"
#include "cheat_visibility.hpp"
#include "lus-controller-ui.hpp"
namespace rush::options {
inline rush::cheats::Preferences cheat_draft;
inline std::unique_ptr<RT64::Inspector> overlay;
inline std::unique_ptr<Store> store;
inline RT64::Application* app=nullptr;
inline std::string error;
inline int page=0;
class ListenerProxy final : public RT64::ApplicationWindow::Listener {
public:
    RT64::ApplicationWindow::Listener* previous=nullptr;
    bool usesWindowMessageFilter()override{return previous->usesWindowMessageFilter();}
    bool sdlEventFilter(SDL_Event* event)override{
        // RT64 reserves F1 even with developer mode disabled. Route it before
        // its filter so the same key cannot activate a second ImGui context.
        if(event->type==SDL_KEYDOWN && (event->key.keysym.scancode==SDL_SCANCODE_F1 || event->key.keysym.scancode==SDL_SCANCODE_F2))return route(*event);
        return previous->sdlEventFilter(event);
    }
    bool windowMessageFilter(unsigned int message,WPARAM w,LPARAM l)override{return previous->windowMessageFilter(message,w,l);}
};
inline ListenerProxy listener;
inline void draw(RenderCommandList* commands,RenderFramebuffer*){
    if(!overlay)return;
    auto queued=drain();
    int requested_page=page_request.exchange(-1);if(requested_page>=0)page=requested_page;
    for(auto& event:queued){capture_binding(event);overlay->handleSdlEvent(&event);}
    int sync=vsync_request.exchange(-1);if(sync>=0){app->swapChain->setVsyncEnabled(sync!=0);std::printf("LUS_VSYNC_REQUEST: %d\n",sync);}
    actual_vsync.store(app->swapChain->isVsyncEnabled());
    overlay->newFrame(app->presentGraphicsWorker.get());
    ImGui::GetIO().FontGlobalScale=store->scale;
    store->fullscreen=is_fullscreen.load();
    if(ImGui::BeginMainMenuBar()) {
        ImGui::TextUnformatted("RUSH 2049");ImGui::Separator();
        if(ImGui::Button("F1: Interface")){page=0;requested_page=0;visible.store(true);}
        if(ImGui::Button("F2: Display")){page=1;requested_page=1;visible.store(true);}
        if(ImGui::Button("F5: Cheats")){page=4;requested_page=4;visible.store(true);}
        if(ImGui::Button("F8: Controllers")){page=2;requested_page=2;visible.store(true);}
        if(ImGui::Button("F9: Diagnostics")){page=3;requested_page=3;visible.store(true);}
        if(ImGui::Button("F11: Fullscreen"))fullscreen_request.store(is_fullscreen.load()?0:1);
        bar_height.store(int(ImGui::GetWindowHeight())+1);
        ImGui::EndMainMenuBar();
    }
    if(visible.load()){
    ImGui::SetNextWindowPos(ImVec2(24,54),ImGuiCond_FirstUseEver);
    const auto display=ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(ImVec2(std::min(1000.0f,display.x-48.0f),std::min(780.0f,display.y-80.0f)),ImGuiCond_Always);
    bool open=true;
    if(ImGui::Begin("Rush settings",&open)) {
        if(ImGui::BeginTabBar("Rush categories")){
            const char* names[]={"Interface","Display","Controllers","Diagnostics","Cheats"};
            for(int i=0;i<5;++i){
                auto flags=requested_page==i?ImGuiTabItemFlags_SetSelected:ImGuiTabItemFlags_None;
                if(ImGui::BeginTabItem(names[i],nullptr,flags)){page=i;ImGui::EndTabItem();}
            }
            ImGui::EndTabBar();
        }
        ImGui::BeginChild("Settings content",ImVec2(0,-55));
        if(page==0){ImGui::TextWrapped("Escape returns to the game.");ImGui::SliderFloat("Interface scale",&store->scale,0.75f,2.0f,"%.2fx");}
        if(page==1){
            ImGui::TextUnformatted("Renderer: RT64 / Vulkan");
            ImGui::SetNextItemWidth(std::max(100.0f,ImGui::GetContentRegionAvail().x-150.0f));
            ImGui::Combo("Render resolution",&store->resolution,"Original\0Original 2x\0Window scale\0");
            ImGui::SetNextItemWidth(std::max(100.0f,ImGui::GetContentRegionAvail().x-150.0f));
            ImGui::Combo("Window resolution",&store->window_size,"Standard (640 x 480)\0 720p (1280 x 720)\0 1080p (1920 x 1080)\0 1440p 16:9 (2560 x 1440)\0 1440p 21:9 (3440 x 1440)\0 4K (3840 x 2160)\0Custom\0");
            if(store->window_size==6){
                ImGui::InputInt("Window width",&store->window_width,10,100);
                ImGui::InputInt("Window height",&store->window_height,10,100);
            }
            const auto extent=actual_window_extent.load();
            ImGui::Text("Current window: %u x %u",unsigned(extent>>32),unsigned(uint32_t(extent)));
            ImGui::BeginDisabled(is_fullscreen.load() || !extent);
            if(ImGui::Button("Use current window size")){store->window_size=6;store->window_width=int(extent>>32);store->window_height=int(uint32_t(extent));}
            ImGui::EndDisabled();
            ImGui::TextWrapped("Drag the window edges to resize. Use current window size, then Apply and save, to remember it. Window sizes fit the usable desktop area.");
            const int rates[]={60,120,144,165,180,200,240};int selected=0;
            for(int i=0;i<7;++i)if(store->refresh==rates[i])selected=i;
            if(ImGui::Combo("Refresh rate",&selected,"60 Hz\0 120 Hz\0 144 Hz\0 165 Hz\0 180 Hz\0 200 Hz\0 240 Hz\0"))store->refresh=rates[selected];
            ImGui::Checkbox("Enable VSync",&store->vsync);
            ImGui::Text("Active VSync: %s",actual_vsync.load()?"On":"Off");
            ImGui::TextWrapped("Refresh rate sets RT64's presentation target. The monitor and desktop refresh rate limit what can be displayed.");
            store->fullscreen=is_fullscreen.load();
            if(ImGui::Checkbox("Fullscreen",&store->fullscreen))fullscreen_request.store(store->fullscreen?1:0);
            ImGui::TextWrapped("Window resolution applies in windowed mode. Fullscreen uses the desktop size. Window scale adjusts RT64 rendering to the game window.");
        }
        if(page==2)controller_page(*store);
        if(page==3){ImGui::Checkbox("Show frame timing",&store->diagnostics);ImGui::Text("UI frame rate: %.1f fps",ImGui::GetIO().Framerate);ImGui::TextWrapped("Dear ImGui uses RT64's presentation hook. libultraship provides the options configuration component. Game runtime, audio and input retain their existing backends.");}
        if(page==4 && cheat_preferences){
            ImGui::Checkbox("Enable cheat menu",&cheat_draft.show);
            if(ImGui::Button("Enable all cheats"))rush::cheats::enable_all(cheat_draft);
            ImGui::TextWrapped("Apply and save works from the title screen, before Start, and during the game. Enable all cheats selects every modifier, including invisible track/car and disabled brakes/collisions.");
            ImGui::BeginChild("Native cheats",ImVec2(0,180),true);
            ImGui::BeginDisabled(!cheat_draft.show);
            for(size_t i=0;i<cheat_draft.enabled.size();++i){
                ImGui::PushID(int(i));const auto& row=rush::cheats::settings_catalog()[i];
                const std::string label=row.normal_on?std::string("Disable ")+row.label:row.label;
                ImGui::Checkbox(label.c_str(),&cheat_draft.enabled[i]);
                if(row.value){ImGui::SameLine();ImGui::TextDisabled("(%s)",row.value);}ImGui::PopID();
            }
            ImGui::EndDisabled();ImGui::EndChild();
        }
        ImGui::EndChild();
        ImGui::Separator();
        if(ImGui::Button("Apply and save")){
            try{if(page==2){store->save();save_controller_draft();}else if(page==4 && cheat_preferences){if(!cheat_preferences->set(cheat_draft,error))throw std::runtime_error(error);}else{store->save();resolution_request.store(store->resolution);fullscreen_request.store(store->fullscreen?1:0);window_size_request.store(store->window_size);auto size=store->window_dimensions();window_extent_request.store(pack_extent(size[0],size[1]));refresh_request.store(store->refresh);vsync_request.store(store->vsync?1:0);}error.clear();std::puts("LUS_OPTIONS_SAVED");}
            catch(const std::exception& e){error=e.what();}
        }
        ImGui::SameLine();if(ImGui::Button("Close"))visible.store(false);
        if(!error.empty())ImGui::TextWrapped("%s",error.c_str());
    }
    ImGui::End();if(!open)visible.store(false);
    }
    if(store->diagnostics){ImGui::SetNextWindowBgAlpha(0.65f);if(ImGui::Begin("Frame timing")){ImGui::Text("%.2f ms (UI presentation)",1000.0f/ImGui::GetIO().Framerate);}ImGui::End();}
    overlay->endFrame();overlay->draw(commands);
    static bool logged=false;
    if(!logged){auto* data=ImGui::GetDrawData();std::printf("LUS_IMGUI_DRAW: vertices=%d lists=%d\n",data->TotalVtxCount,data->CmdListsCount);logged=true;}
}
inline void deinit(){available.store(false);visible.store(false);if(app && listener.previous)app->appWindow->listener=listener.previous;listener.previous=nullptr;overlay.reset();store.reset();app=nullptr;drain();RT64::SetRenderHooks(nullptr,nullptr,nullptr);}
inline void init(RT64::Application* application){
    if(!std::getenv("RUSH_LUS_IMGUI"))return;
    if(!window || application->userConfig.developerMode || RT64::GetRenderHookDraw() || RT64::GetRenderHookInit() || RT64::GetRenderHookDeinit())throw std::runtime_error("Options cannot replace existing renderer hooks");
    const char* file=std::getenv("RUSH_OPTIONS_CONFIG");
    store=std::make_unique<Store>(file?std::filesystem::u8path(file):std::filesystem::path{});
    if(cheat_preferences)cheat_draft=cheat_preferences->preferences();
    app=application;
    overlay=std::make_unique<RT64::Inspector>(app->device.get(),app->swapChain.get(),app->chosenGraphicsAPI,window);
    ImGui::GetIO().IniFilename=nullptr;
    ImGui::GetIO().ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    auto& style=ImGui::GetStyle();style.WindowRounding=6;style.FrameRounding=4;style.Colors[ImGuiCol_WindowBg]=ImVec4(0.035f,0.05f,0.08f,0.93f);style.Colors[ImGuiCol_Button]=ImVec4(0.04f,0.30f,0.52f,1);
    RT64::SetRenderHooks(nullptr,draw,deinit);available.store(true);
    listener.previous=app->appWindow->listener;app->appWindow->listener=&listener;
    resolution_request.store(store->resolution);fullscreen_request.store(store->fullscreen?1:0);window_size_request.store(store->window_size);auto size=store->window_dimensions();window_extent_request.store(pack_extent(size[0],size[1]));refresh_request.store(store->refresh);vsync_request.store(store->vsync?1:0);
    visible.store(std::getenv("RUSH_OPTIONS_OPEN")!=nullptr);
    std::puts("LUS_IMGUI_READY: F1 settings; RT64 renderer retained");
}
}
