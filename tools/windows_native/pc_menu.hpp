#pragma once
#include <Windows.h>
#include <SDL.h>
#include <filesystem>
#include <fstream>
#include "cheat_codes.hpp"
#include "render/rush_display_state.h"
namespace rush::pc {
inline SDL_Window* game=nullptr;
inline HWND native=nullptr;
inline HMENU bar=nullptr,cheatMenu=nullptr,displayMenu=nullptr;
inline WNDPROC previous=nullptr;
inline Uint32 action_event=0;
inline std::atomic<int> pending{0};
inline std::filesystem::path config;
inline bool fullscreen=false;
inline bool cheatsEnabled=false;
inline rush::cheats::Playback playback;
inline const auto codes=rush::cheats::catalog();
inline void checks() {
    CheckMenuRadioItem(displayMenu,200,204,200+rush::display::selected.load(),MF_BYCOMMAND);
    CheckMenuItem(displayMenu,205,MF_BYCOMMAND|(rush::display::vsync.load()?MF_CHECKED:MF_UNCHECKED));
    CheckMenuItem(displayMenu,206,MF_BYCOMMAND|(fullscreen?MF_CHECKED:MF_UNCHECKED));
    CheckMenuItem(cheatMenu,102,MF_BYCOMMAND|(cheatsEnabled?MF_CHECKED:MF_UNCHECKED));
    for(size_t i=0;i<codes.size();++i)EnableMenuItem(cheatMenu,110+i,MF_BYCOMMAND|(cheatsEnabled?MF_ENABLED:MF_GRAYED));
}
inline void save() {
    const auto temp=config.wstring()+L".tmp";
    std::ofstream file(std::filesystem::path(temp),std::ios::trunc);file<<"rush-display 1\n"<<rush::display::selected.load()<<' '<<rush::display::vsync.load()<<' '<<fullscreen<<'\n';file.close();
    if(!file || !MoveFileExW(temp.c_str(),config.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        std::fprintf(stderr,"DISPLAY_SETTINGS_SAVE_FAILED: current session settings still apply\n");
}
inline void resize() {
    rush::display::fullscreen.store(fullscreen);
    const auto preset=rush::display::presets[rush::display::selected.load()];
    SDL_SetWindowFullscreen(game,fullscreen?SDL_WINDOW_FULLSCREEN_DESKTOP:0);
    if(!fullscreen)SDL_SetWindowSize(game,preset.width,preset.height);
    checks();
}
inline void command(int id) {
    if(id>=200 && id<=204){rush::display::selected=id-200;rush::display::revision.fetch_add(1);resize();save();}
    else if(id==205){rush::display::vsync=!rush::display::vsync.load();checks();save();}
    else if(id==206){fullscreen=!fullscreen;resize();save();}
    else if(id==102){cheatsEnabled=!cheatsEnabled;playback.cancel();if(cheatsEnabled)playback.request(rush::cheats::menu_code());checks();std::printf("CHEATS_ENABLED: %d; enable at the game's main menu\n",cheatsEnabled);}
    else if(id==101)playback.cancel();
    else if(cheatsEnabled && id>=110 && id<110+int(codes.size())){playback.request(codes[id-110]);std::printf("CHEAT_SEQUENCE_REQUESTED: %s; select its matching entry in the game first\n",codes[id-110].label);}
    std::printf("PC_MENU_ACTION: %d\n",id);
}
inline LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_COMMAND && HIWORD(wp)==0){pending.store(LOWORD(wp));std::printf("PC_MENU_COMMAND_RECEIVED: %d\n",LOWORD(wp));return 0;}
    if(message==WM_KEYDOWN && !(lp&(1LL<<30))){int action=wp==VK_F1?401:wp==VK_F8?300:wp==VK_F9?409:wp==VK_F11?206:0;if(action){pending.store(action);return 0;}}
    return CallWindowProcW(previous,window,message,wp,lp);
}
inline void attach(SDL_Window* window,HWND hwnd,const std::filesystem::path& data) {
    game=window;native=hwnd;config=data/L"display.cfg";action_event=SDL_RegisterEvents(1);
    int selected=0,sync=1,full=0;std::string header;int version=0;std::ifstream file(config);
    if(file>>header>>version>>selected>>sync>>full && header=="rush-display" && version==1 && selected>=0 && selected<5 && (sync==0||sync==1) && (full==0||full==1)){rush::display::selected=selected;rush::display::vsync=sync;fullscreen=full;}
    bar=CreateMenu();cheatMenu=CreatePopupMenu();displayMenu=CreatePopupMenu();
    AppendMenuW(cheatMenu,MF_STRING,102,L"Enable Cheats (at game's main menu)");AppendMenuW(cheatMenu,MF_STRING,101,L"Cancel code playback");AppendMenuW(cheatMenu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(cheatMenu,MF_STRING|MF_DISABLED,0,L"Select matching cheat in game, then send code:");
    for(size_t i=0;i<codes.size();++i){std::wstring label;for(const char* p=codes[i].label;*p;++p)label+=wchar_t(*p);if(codes[i].disputed)label+=L" (sequence disputed)";AppendMenuW(cheatMenu,MF_STRING,110+i,label.c_str());}
    for(int i=0;i<5;++i)AppendMenuW(displayMenu,MF_STRING,200+i,rush::display::presets[i].label);
    AppendMenuW(displayMenu,MF_SEPARATOR,0,nullptr);AppendMenuW(displayMenu,MF_STRING,205,L"VSync");AppendMenuW(displayMenu,MF_STRING,206,L"Fullscreen\tF11");
    AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(cheatMenu),L"Cheats (F1)");AppendMenuW(bar,MF_STRING,300,L"Controller (F8)");AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(displayMenu),L"Display (F9)");
    SetMenu(native,bar);previous=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(native,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(window_proc)));resize();DrawMenuBar(native);
    std::puts("PC_MENU_READY: F1 cheats, F8 controller, F9 display, F11 fullscreen");
}
inline void popup(HMENU menu) {RECT rect{};GetWindowRect(native,&rect);const auto action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,rect.left+12,rect.top+55,0,native,nullptr);if(action)command(action);}
inline bool event(const SDL_Event& e) {
    if(e.type==action_event){if(e.user.code==300)return false;command(e.user.code);return true;}
    if(e.type!=SDL_KEYDOWN || e.key.repeat)return false;
    switch(e.key.keysym.sym){case SDLK_F1:popup(cheatMenu);return true;case SDLK_F9:popup(displayMenu);return true;case SDLK_F11:command(206);return true;default:return false;}
}
inline void detach(){if(native && previous)SetWindowLongPtrW(native,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(previous));if(native)SetMenu(native,nullptr);if(bar)DestroyMenu(bar);native=nullptr;previous=nullptr;bar=nullptr;}
}
