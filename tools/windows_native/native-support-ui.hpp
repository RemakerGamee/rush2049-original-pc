#pragma once
#include "rush-update-model.hpp"
#include "rom_identity.hpp"
#include <shellapi.h>
#include <functional>
#include <cstring>

namespace rush::native_support {
inline HWND panel=nullptr;
inline std::string installed,edition;
inline std::function<bool(const std::string&)> launch=[](const std::string& url){return reinterpret_cast<INT_PTR>(ShellExecuteW(panel,L"open",rush::rom::wide(url).c_str(),nullptr,nullptr,SW_SHOWNORMAL))>32;};
inline bool clipboard_text(const std::string& text){
    auto wide=rush::rom::wide(text);auto memory=GlobalAlloc(GMEM_MOVEABLE,(wide.size()+1)*sizeof(wchar_t));if(!memory)return false;
    auto bytes=GlobalLock(memory);if(!bytes){GlobalFree(memory);return false;}memcpy(bytes,wide.c_str(),(wide.size()+1)*sizeof(wchar_t));GlobalUnlock(memory);
    if(!OpenClipboard(panel)){GlobalFree(memory);return false;}bool good=EmptyClipboard()&&SetClipboardData(CF_UNICODETEXT,memory);CloseClipboard();if(!good)GlobalFree(memory);return good;
}
inline std::function<bool(const std::string&)> copy=clipboard_text;
inline void status(const wchar_t* text){SetWindowTextW(GetDlgItem(panel,20),text);}
inline LRESULT CALLBACK window_proc(HWND window,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_CLOSE){ShowWindow(window,SW_HIDE);return 0;}
    if(msg==WM_COMMAND&&HIWORD(wp)==BN_CLICKED){try{
        switch(LOWORD(wp)){
        case 1:status(launch(rush::updates::discord)?L"Discord opened in your browser.":L"Could not open your browser. Use Copy Discord invite.");break;
        case 2:status(copy(rush::updates::discord)?L"Discord invite copied.":L"Could not copy. The invite is displayed above.");break;
        case 3:status(launch(rush::updates::mailto(installed,edition))?L"Review the editable draft in your mail application. If no draft appeared, copy the address.":L"Could not open your mail application. Use Copy email address.");break;
        case 4:status(copy(rush::updates::email)?L"Email address copied.":L"Could not copy. The email address is displayed above.");break;
        }
    }catch(const std::exception&){status(L"Could not open or copy the support contact. Use the displayed contact details.");}return 0;}
    return DefWindowProcW(window,msg,wp,lp);
}
inline void open(HWND owner,const std::string& version,const std::string& selected){
    installed=version;edition=selected;
    if(!panel){
        WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=window_proc;cls.lpszClassName=L"RushNativeSupport";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);RegisterClassW(&cls);
        panel=CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,L"Rush 2049 Support",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,630,385,owner,nullptr,cls.hInstance,nullptr);
        if(!panel)throw std::runtime_error("Cannot open Support");
        auto control=[&](const wchar_t* kind,const std::wstring& label,DWORD style,int x,int y,int w,int h,int id){auto child=CreateWindowExW(0,kind,label.c_str(),WS_CHILD|WS_VISIBLE|style,x,y,w,h,panel,reinterpret_cast<HMENU>(INT_PTR(id)),cls.hInstance,nullptr);SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);};
        control(L"STATIC",L"Get help from RetroRemakes. Email opens a draft for you to review.\nNo messages, files or diagnostics are sent automatically.",0,16,15,580,45,10);
        control(L"STATIC",rush::rom::wide(rush::updates::discord),SS_LEFT,16,75,580,25,11);
        control(L"BUTTON",L"Open Discord",BS_PUSHBUTTON|WS_TABSTOP,16,108,190,30,1);
        control(L"BUTTON",L"Copy Discord invite",BS_PUSHBUTTON|WS_TABSTOP,218,108,210,30,2);
        control(L"STATIC",rush::rom::wide(rush::updates::email),SS_LEFT,16,160,580,25,12);
        control(L"BUTTON",L"Email support",BS_PUSHBUTTON|WS_TABSTOP,16,193,190,30,3);
        control(L"BUTTON",L"Copy email address",BS_PUSHBUTTON|WS_TABSTOP,218,193,210,30,4);
        control(L"STATIC",L"",SS_LEFT,16,245,580,65,20);
    }
    status(rush::rom::wide("Rush 2049 "+edition+" | "+installed).c_str());ShowWindow(panel,SW_SHOWNORMAL);SetForegroundWindow(panel);
}
inline void detach(){if(panel)DestroyWindow(panel);panel=nullptr;}
}
