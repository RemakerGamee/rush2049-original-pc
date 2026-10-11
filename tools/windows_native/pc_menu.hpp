#pragma once
#include <Windows.h>
#include <SDL.h>
#include <filesystem>
#include <fstream>
#include "cheat_visibility.hpp"
#include <memory>
#include "render/rush_display_state.h"
namespace rush::pc {
inline SDL_Window* game=nullptr;
inline HWND native=nullptr;
inline HWND cheatWindow=nullptr;
inline HMENU bar=nullptr,cheatMenu=nullptr,displayMenu=nullptr;
inline WNDPROC previous=nullptr;
inline Uint32 action_event=0;
inline std::atomic<int> pending{0};
inline std::filesystem::path config;
inline bool fullscreen=false;
inline std::unique_ptr<rush::cheats::Visibility> cheatSettings;
inline const auto codes=rush::cheats::settings_catalog();
inline void checks() {
    CheckMenuRadioItem(displayMenu,200,204,200+rush::display::selected.load(),MF_BYCOMMAND);
    CheckMenuItem(displayMenu,205,MF_BYCOMMAND|(rush::display::vsync.load()?MF_CHECKED:MF_UNCHECKED));
    CheckMenuItem(displayMenu,206,MF_BYCOMMAND|(fullscreen?MF_CHECKED:MF_UNCHECKED));
    const auto preferences=cheatSettings->preferences();
    const bool cheatsEnabled=preferences.show;
    CheckMenuItem(cheatMenu,102,MF_BYCOMMAND|(cheatsEnabled?MF_CHECKED:MF_UNCHECKED));
    const bool all=std::all_of(preferences.enabled.begin(),preferences.enabled.end(),[](bool v){return v;});
    if(cheatWindow){SendMessageW(GetDlgItem(cheatWindow,102),BM_SETCHECK,cheatsEnabled?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(GetDlgItem(cheatWindow,103),BM_SETCHECK,all?BST_CHECKED:BST_UNCHECKED,0);EnableWindow(GetDlgItem(cheatWindow,103),cheatsEnabled);}
    for(size_t i=0;i<codes.size();++i){EnableMenuItem(cheatMenu,110+i,MF_BYCOMMAND|(cheatsEnabled?MF_ENABLED:MF_GRAYED));CheckMenuItem(cheatMenu,110+i,MF_BYCOMMAND|(preferences.enabled[i]?MF_CHECKED:MF_UNCHECKED));}
    if(cheatWindow)for(size_t i=0;i<codes.size();++i){const auto control=GetDlgItem(cheatWindow,110+int(i));EnableWindow(control,cheatsEnabled);SendMessageW(control,BM_SETCHECK,preferences.enabled[i]?BST_CHECKED:BST_UNCHECKED,0);}
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
    else if(id==102 || id==103 || (id>=110 && id<110+int(codes.size()))) {
        auto preferences=cheatSettings->preferences();
        if(id==102)preferences.show=!preferences.show;
        else if(id==103 && preferences.show)preferences.enabled.fill(!std::all_of(preferences.enabled.begin(),preferences.enabled.end(),[](bool v){return v;}));
        else if(preferences.show)preferences.enabled[id-110]=!preferences.enabled[id-110];
        std::string error;
        if(!cheatSettings->set(preferences,error))MessageBoxA(native,error.c_str(),"Could not save cheat settings",MB_OK|MB_ICONERROR);
        checks();
    }
    std::printf("PC_MENU_ACTION: %d\n",id);
}
inline LRESULT CALLBACK cheat_window_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_CLOSE){ShowWindow(window,SW_HIDE);return 0;}
    if(message==WM_ACTIVATE&&LOWORD(wp)==WA_INACTIVE){ShowWindow(window,SW_HIDE);return 0;}
    if(message==WM_KEYDOWN&&wp==VK_ESCAPE){ShowWindow(window,SW_HIDE);return 0;}
    if(message==WM_COMMAND&&HIWORD(wp)==BN_CLICKED){if(LOWORD(wp)==2)ShowWindow(window,SW_HIDE);else command(LOWORD(wp));return 0;}
    return DefWindowProcW(window,message,wp,lp);
}
inline void open_cheats(bool show=true) {
    if(!cheatWindow){
        WNDCLASSW cls{};cls.lpfnWndProc=cheat_window_proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"RushOriginalCheatSettings";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);RegisterClassW(&cls);
        RECT position{};GetWindowRect(native,&position);
        cheatWindow=CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,L"Cheats (F1)",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,position.left+35,position.top+60,510,780,native,nullptr,cls.hInstance,nullptr);
        auto control=[&](const wchar_t* type,const wchar_t* label,DWORD style,int y,int id){auto child=CreateWindowExW(0,type,label,WS_CHILD|WS_VISIBLE|style,18,y,465,25,cheatWindow,reinterpret_cast<HMENU>(INT_PTR(id)),cls.hInstance,nullptr);SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);};
        control(L"BUTTON",L"Enable Cheats (before or after Start)",BS_AUTOCHECKBOX,12,102);
        control(L"STATIC",L"Choose cheats to activate (checked = on):",0,46,0);
        control(L"BUTTON",L"All Cheats",BS_AUTOCHECKBOX,74,103);
        for(size_t i=0;i<codes.size();++i){std::wstring label;for(const char* p=codes[i].label;*p;++p)label+=wchar_t(*p);if(codes[i].value){label+=L" (";for(const char* p=codes[i].value;*p;++p)label+=wchar_t(*p);label+=L")";}else if(codes[i].normal_on)label+=L" (disable)";control(L"BUTTON",label.c_str(),BS_AUTOCHECKBOX,110+int(i)*27,110+int(i));}
        control(L"BUTTON",L"Done",BS_PUSHBUTTON,693,2);
    }
    checks();if(show){ShowWindow(cheatWindow,SW_SHOWNORMAL);SetForegroundWindow(cheatWindow);}
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
    cheatSettings=std::make_unique<rush::cheats::Visibility>(data/L"cheats.cfg");
    bar=CreateMenu();cheatMenu=CreatePopupMenu();displayMenu=CreatePopupMenu();
    AppendMenuW(cheatMenu,MF_STRING,102,L"Enable Cheats (before or after Start)");AppendMenuW(cheatMenu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(cheatMenu,MF_STRING|MF_DISABLED,0,L"Choose cheats to activate (checked = on):");
    for(size_t i=0;i<codes.size();++i){std::wstring label;for(const char* p=codes[i].label;*p;++p)label+=wchar_t(*p);if(codes[i].value){label+=L" (";for(const char* p=codes[i].value;*p;++p)label+=wchar_t(*p);label+=L")";}else if(codes[i].normal_on)label+=L" (disable)";AppendMenuW(cheatMenu,MF_STRING,110+i,label.c_str());}
    for(int i=0;i<5;++i)AppendMenuW(displayMenu,MF_STRING,200+i,rush::display::presets[i].label);
    AppendMenuW(displayMenu,MF_SEPARATOR,0,nullptr);AppendMenuW(displayMenu,MF_STRING,205,L"VSync");AppendMenuW(displayMenu,MF_STRING,206,L"Fullscreen\tF11");
    AppendMenuW(bar,MF_STRING,401,L"Cheats (F1)");AppendMenuW(bar,MF_STRING,300,L"Controller (F8)");AppendMenuW(bar,MF_POPUP,reinterpret_cast<UINT_PTR>(displayMenu),L"Display (F9)");
    SetMenu(native,bar);previous=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(native,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(window_proc)));resize();DrawMenuBar(native);
    std::puts("PC_MENU_READY: F1 cheats, F8 controller, F9 display, F11 fullscreen");
}
inline void popup(HMENU menu) {if(menu==cheatMenu){open_cheats();return;}RECT rect{};GetWindowRect(native,&rect);const auto action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,rect.left+12,rect.top+55,0,native,nullptr);if(action)command(action);}
inline bool event(const SDL_Event& e) {
    if(e.type==action_event){if(e.user.code==300)return false;command(e.user.code);return true;}
    if(e.type!=SDL_KEYDOWN || e.key.repeat)return false;
    switch(e.key.keysym.sym){case SDLK_F1:popup(cheatMenu);return true;case SDLK_F9:popup(displayMenu);return true;case SDLK_F11:command(206);return true;default:return false;}
}
inline void detach(){if(cheatWindow)DestroyWindow(cheatWindow);cheatWindow=nullptr;if(native && previous)SetWindowLongPtrW(native,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(previous));if(native)SetMenu(native,nullptr);if(bar)DestroyMenu(bar);native=nullptr;previous=nullptr;bar=nullptr;}
}
