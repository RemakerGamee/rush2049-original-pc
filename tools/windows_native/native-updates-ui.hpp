#pragma once
#include "rush-update-service.hpp"
#include <commctrl.h>
#include <memory>

// An additive Win32 panel; no renderer, controller or existing menu ownership changes.
namespace rush::native_updates {
inline constexpr UINT menu_id=0x7e01;
inline constexpr UINT_PTR subclass_id=0x52555348;
inline HWND panel=nullptr,owner=nullptr;
inline HMENU menu=nullptr;
inline bool owns_menu=false;
inline std::wstring shown_notes;
inline std::unique_ptr<rush::updates::Service> updater;
inline HWND control(const wchar_t* kind,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id){
    auto child=CreateWindowExW(kind==std::wstring(L"EDIT")?WS_EX_CLIENTEDGE:0,kind,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,panel,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return child;
}
inline void refresh(){
    if(!panel||!updater)return;
    auto s=updater->snapshot();
    std::string status=updater->edition+" / native | Installed: "+updater->installed+"\r\n"+s.message;
    if(s.release.bytes)status+="\r\nDownloaded: "+std::to_string(s.received)+" / "+std::to_string(s.release.bytes)+" bytes";
    SetWindowTextW(GetDlgItem(panel,10),rush::rom::wide(status).c_str());
    auto notes=s.release.tag.empty()?std::string("No release selected."):"Available: "+s.release.tag+"\r\n\r\n"+s.release.notes;
    auto wide=rush::rom::wide(notes);auto edit=GetDlgItem(panel,11);
    if(wide!=shown_notes){SetWindowTextW(edit,wide.c_str());shown_notes=wide;}
    EnableWindow(GetDlgItem(panel,1),!s.busy);EnableWindow(GetDlgItem(panel,2),!s.busy&&!s.release.tag.empty());
    SetWindowTextW(GetDlgItem(panel,2),s.ready?L"Close game and install...":L"Download update");
    EnableWindow(GetDlgItem(panel,3),s.busy);
    try{EnableWindow(GetDlgItem(panel,4),!s.busy&&!updater->previous().empty());}
    catch(const std::exception& e){EnableWindow(GetDlgItem(panel,4),FALSE);SetWindowTextW(GetDlgItem(panel,12),rush::rom::wide(std::string("Rollback unavailable: ")+e.what()).c_str());}
}
inline LRESULT CALLBACK panel_proc(HWND window,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_CLOSE){updater->cancel();ShowWindow(window,SW_HIDE);return 0;}
    if(msg==WM_TIMER){refresh();return 0;}
    if(msg==WM_COMMAND&&HIWORD(wp)==BN_CLICKED){try{
        switch(LOWORD(wp)){
        case 1:updater->check();break;
        case 2:
            if(!updater->snapshot().ready){updater->download();break;}
            if(MessageBoxW(window,L"Close Rush and install this verified update? Your previous application, ROM, Memory Pak and controller settings are retained. The game will not restart automatically.",L"Rush update",MB_OKCANCEL|MB_ICONQUESTION)!=IDOK)break;
            updater->request_install();PostMessageW(owner,WM_CLOSE,0,0);break;
        case 3:updater->cancel();break;
        case 4:
            if(MessageBoxW(window,L"Close Rush and restore the retained verified application offline? Saves and preferences stay in place. The game will not restart automatically.",L"Rush rollback",MB_OKCANCEL|MB_ICONQUESTION)!=IDOK)break;
            updater->request_rollback();PostMessageW(owner,WM_CLOSE,0,0);break;
        }
    }catch(const std::exception& e){MessageBoxW(window,rush::rom::wide(e.what()).c_str(),L"Rush Updates",MB_OK|MB_ICONERROR);}refresh();return 0;}
    return DefWindowProcW(window,msg,wp,lp);
}
inline void open(){
    if(!updater)updater=std::make_unique<rush::updates::Service>();
    if(!panel){
        WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=panel_proc;cls.lpszClassName=L"RushNativeUpdates";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);RegisterClassW(&cls);
        panel=CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,L"Rush 2049 Updates",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU, CW_USEDEFAULT,CW_USEDEFAULT,640,510,owner,nullptr,cls.hInstance,nullptr);
        if(!panel)throw std::runtime_error("Cannot open Updates");
        control(L"STATIC",L"",0,16,12,600,80,10);
        control(L"BUTTON",L"Check for updates",BS_PUSHBUTTON|WS_TABSTOP,16,100,165,30,1);
        control(L"BUTTON",L"Download update",BS_PUSHBUTTON|WS_TABSTOP,190,100,230,30,2);
        control(L"BUTTON",L"Cancel download",BS_PUSHBUTTON|WS_TABSTOP,430,100,175,30,3);
        control(L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,16,142,590,205,11);
        control(L"BUTTON",L"Roll back to previous version",BS_PUSHBUTTON|WS_TABSTOP,16,360,280,30,4);
        control(L"STATIC",L"Manual checks only. Installation always needs your approval.\nBoth application folders are kept. Rollback works offline.",0,16,400,590,55,12);
        SetTimer(panel,1,200,nullptr);
    }
    refresh();ShowWindow(panel,SW_SHOWNORMAL);SetForegroundWindow(panel);
}
inline LRESULT CALLBACK game_proc(HWND window,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(msg==WM_COMMAND&&LOWORD(wp)==menu_id){try{open();}catch(const std::exception& e){MessageBoxW(window,rush::rom::wide(e.what()).c_str(),L"Rush Updates",MB_OK|MB_ICONERROR);}return 0;}
    return DefSubclassProc(window,msg,wp,lp);
}
inline void attach(HWND window){
    owner=window;menu=GetMenu(window);owns_menu=!menu;if(!menu)menu=CreateMenu();
    if(!menu||!AppendMenuW(menu,MF_STRING,menu_id,L"Updates")||!SetWindowSubclass(window,game_proc,subclass_id,0))throw std::runtime_error("Cannot attach Updates menu");
    if(owns_menu)SetMenu(window,menu);DrawMenuBar(window);
}
inline void detach(){
    if(panel)DestroyWindow(panel);panel=nullptr;updater.reset();
    if(owner){RemoveWindowSubclass(owner,game_proc,subclass_id);if(menu)DeleteMenu(menu,menu_id,MF_BYCOMMAND);if(owns_menu)SetMenu(owner,nullptr);DrawMenuBar(owner);}
    if(owns_menu&&menu)DestroyMenu(menu);menu=nullptr;owner=nullptr;owns_menu=false;
}
}
