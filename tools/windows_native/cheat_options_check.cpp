// Native Win32 component regression. No game, ROM, GPU or input injection.
#include "cheat_visibility_window.hpp"
#include <iostream>
static HWND options=nullptr;
static BOOL CALLBACK find_owned(HWND window,LPARAM) {
 DWORD pid=0;GetWindowThreadProcessId(window,&pid);
 if(pid!=GetCurrentProcessId())return TRUE;
 wchar_t name[128]{};GetClassNameW(window,name,128);
 if(std::wstring(name)==L"RushCheatVisibility")options=window;
 return TRUE;
}
static void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv) {
 try {
  require(argc==2,"fresh test directory required");
  auto path=std::filesystem::path(argv[1])/"cheats.cfg";
  require(!std::filesystem::exists(path),"test settings already exist; preserve them");
  rush::cheats::Visibility preferences(path);
  {
   rush::cheats::VisibilityWindow window(preferences);
   EnumWindows(find_owned,0);require(options,"owned hidden settings window missing");
   require(!IsWindowVisible(options),"test window must remain hidden");
   // Refresh executes only on Open in production; initial controls must also
   // represent defaults before the first F5 refresh.
   auto master=GetDlgItem(options,1);require(master,"master control missing");
   require(SendMessageW(master,BM_GETCHECK,0,0)==BST_UNCHECKED,"master must default OFF");
   for(int i=0;i<21;++i) {
    auto control=GetDlgItem(options,100+i);require(control,"cheat control missing");
    require(!IsWindowEnabled(control),"master OFF must disable every cheat control");
    if(rush::cheats::settings_catalog()[i].value)
     require(SendMessageW(control,CB_GETCURSEL,0,0)==0,"value must default to normal/OFF");
    else require(SendMessageW(control,BM_GETCHECK,0,0)==BST_UNCHECKED,"modifier must default OFF");
   }
   SendMessageW(master,BM_SETCHECK,BST_CHECKED,0);
   SendMessageW(options,WM_COMMAND,MAKEWPARAM(1,BN_CLICKED),0);
   for(int i=0;i<21;++i)require(IsWindowEnabled(GetDlgItem(options,100+i)),"master ON must enable controls");
   SendMessageW(GetDlgItem(options,100),BM_SETCHECK,BST_CHECKED,0);
   // Exercise the actual close handler without opening/activating any window.
   SendMessageW(options,WM_CLOSE,0,0);
   require(!IsWindowVisible(options),"Close must hide settings");
   rush::cheats::Visibility reopened(path);auto saved=reopened.preferences();
   require(saved.show&&saved.enabled[0],"Close must save Enable Cheats and All Tracks");
   for(int i=1;i<21;++i)require(!saved.enabled[i],"Close must not select unrelated cheats");
   // Exercise every real control independently, then restore unchecked defaults.
   for(size_t selected=0;selected<21;++selected) {
    for(size_t i=0;i<21;++i) {
     auto control=GetDlgItem(options,int(100+i));
     if(rush::cheats::settings_catalog()[i].value)SendMessageW(control,CB_SETCURSEL,i==selected?1:0,0);
     else SendMessageW(control,BM_SETCHECK,i==selected?BST_CHECKED:BST_UNCHECKED,0);
    }
    SendMessageW(options,WM_CLOSE,0,0);
    auto one=rush::cheats::Visibility(path).preferences();
    require(one.show,"master must remain enabled across Close");
    for(size_t i=0;i<21;++i)require(one.enabled[i]==(i==selected),"independent control saved wrong cheat");
   }
   for(size_t i=0;i<21;++i) {
    auto control=GetDlgItem(options,int(100+i));
    if(rush::cheats::settings_catalog()[i].value)SendMessageW(control,CB_SETCURSEL,0,0);
    else SendMessageW(control,BM_SETCHECK,BST_UNCHECKED,0);
   }
   SendMessageW(options,WM_CLOSE,0,0);
   auto restored=rush::cheats::Visibility(path).preferences();
   for(bool enabled:restored.enabled)require(!enabled,"OFF restoration failed in options persistence");
   SendMessageW(master,BM_SETCHECK,BST_UNCHECKED,0);
   SendMessageW(options,WM_COMMAND,MAKEWPARAM(1,BN_CLICKED),0);
   SendMessageW(options,WM_COMMAND,MAKEWPARAM(2,BN_CLICKED),0);
   require(!rush::cheats::Visibility(path).desired(),"Done must save master OFF");
  }
  require(!IsWindow(options),"settings thread/window must stop on destruction");
  std::cout<<"PASS native hidden settings defaults, Enable Cheats, All Tracks-only, Close/Done persistence and window/thread cleanup; no gameplay claim.\n";
  return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
