#include "../windows_native/native-updates-ui.hpp"
#include <iostream>
static void expect(bool b){if(!b)throw std::runtime_error("Native Updates UI check failed");}
int wmain(){try{
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=DefWindowProcW;cls.lpszClassName=L"RushUpdateTestOwner";RegisterClassW(&cls);
    auto window=CreateWindowW(cls.lpszClassName,L"Synthetic game",WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,cls.hInstance,nullptr);
    auto menu=CreateMenu();AppendMenuW(menu,MF_STRING,123,L"Existing controls");SetMenu(window,menu);
    rush::native_updates::attach(window);expect(GetMenu(window)==menu&&GetMenuItemCount(menu)==3);
    SendMessageW(window,WM_COMMAND,rush::native_updates::menu_id,0);
    expect(rush::native_updates::panel&&IsWindow(rush::native_updates::panel));
    SendMessageW(rush::native_updates::panel,WM_CLOSE,0,0);expect(!IsWindowVisible(rush::native_updates::panel));
    std::string opened,copied;
    rush::native_support::launch=[&](const std::string& url){opened=url;return false;};
    rush::native_support::copy=[&](const std::string& text){copied=text;return true;};
    for(auto edition:{"original","remastered"}){
        _putenv_s("RUSH_RELEASE_EDITION",edition);_putenv_s("RUSH_RELEASE_VERSION","0.3.4");
        SendMessageW(window,WM_COMMAND,rush::native_updates::support_menu_id,0);
        auto support=rush::native_support::panel;expect(support&&IsWindowVisible(support));
        SendMessageW(support,WM_COMMAND,MAKEWPARAM(1,BN_CLICKED),0);expect(opened==rush::updates::discord);
        SendMessageW(support,WM_COMMAND,MAKEWPARAM(2,BN_CLICKED),0);expect(copied==rush::updates::discord);
        SendMessageW(support,WM_COMMAND,MAKEWPARAM(3,BN_CLICKED),0);expect(opened==rush::updates::mailto("0.3.4",std::string(edition)+" native")&&opened.find("attachment")==std::string::npos);
        SendMessageW(support,WM_COMMAND,MAKEWPARAM(4,BN_CLICKED),0);expect(copied==rush::updates::email);
        SendMessageW(support,WM_CLOSE,0,0);expect(!IsWindowVisible(support));
    }
    rush::native_updates::detach();expect(GetMenu(window)==menu&&GetMenuItemCount(menu)==1&&GetMenuItemID(menu,0)==123);
    SetMenu(window,nullptr);DestroyMenu(menu);DestroyWindow(window);
    std::cout<<"PASS: additive native menu, modeless panel, cancellation on close, existing menu preserved\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
