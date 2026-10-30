#include "../windows_native/native-updates-ui.hpp"
#include <iostream>
static void expect(bool b){if(!b)throw std::runtime_error("Native Updates UI check failed");}
int wmain(){try{
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=DefWindowProcW;cls.lpszClassName=L"RushUpdateTestOwner";RegisterClassW(&cls);
    auto window=CreateWindowW(cls.lpszClassName,L"Synthetic game",WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,cls.hInstance,nullptr);
    auto menu=CreateMenu();AppendMenuW(menu,MF_STRING,123,L"Existing controls");SetMenu(window,menu);
    rush::native_updates::attach(window);expect(GetMenu(window)==menu&&GetMenuItemCount(menu)==2);
    SendMessageW(window,WM_COMMAND,rush::native_updates::menu_id,0);
    expect(rush::native_updates::panel&&IsWindow(rush::native_updates::panel));
    SendMessageW(rush::native_updates::panel,WM_CLOSE,0,0);expect(!IsWindowVisible(rush::native_updates::panel));
    rush::native_updates::detach();expect(GetMenu(window)==menu&&GetMenuItemCount(menu)==1&&GetMenuItemID(menu,0)==123);
    SetMenu(window,nullptr);DestroyMenu(menu);DestroyWindow(window);
    std::cout<<"PASS: additive native menu, modeless panel, cancellation on close, existing menu preserved\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
