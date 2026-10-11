#pragma once
#include "cheat_visibility.hpp"
#include <thread>
#include <condition_variable>
namespace rush::cheats {
class VisibilityWindow {
 Visibility& preference_;
 std::thread thread_;
 std::mutex ready_mutex_;
 std::condition_variable ready_;
 DWORD thread_id_=0;
 HWND window_=nullptr,checkbox_=nullptr;
 std::atomic<HWND> owner_{nullptr};
 void hide() {
  const bool focused=GetForegroundWindow()==window_;
  ShowWindow(window_,SW_HIDE);
  const auto owner=owner_.load();DWORD pid=0;
  if(owner&&IsWindow(owner))GetWindowThreadProcessId(owner,&pid);
  // Never activate an unrelated application after the user switches away.
  if(focused&&pid==GetCurrentProcessId())SetForegroundWindow(owner);
 }
 std::array<HWND,21> controls_{};
 bool save() {
  auto p=preference_.preferences();p.show=SendMessageW(checkbox_,BM_GETCHECK,0,0)==BST_CHECKED;
  for(size_t i=0;i<21;++i)p.enabled[i]=settings_catalog()[i].value?SendMessageW(controls_[i],CB_GETCURSEL,0,0)==1:SendMessageW(controls_[i],BM_GETCHECK,0,0)==BST_CHECKED;
  std::string error;
  if(!preference_.set(p,error)){MessageBoxA(window_,error.c_str(),"Settings not saved",MB_OK|MB_ICONERROR);return false;}
  return true;
 }
 void enable_controls() {
  const bool enabled=SendMessageW(checkbox_,BM_GETCHECK,0,0)==BST_CHECKED;
  for(auto control:controls_)EnableWindow(control,enabled);
 }
 static LRESULT CALLBACK proc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
  auto* self=reinterpret_cast<VisibilityWindow*>(GetWindowLongPtrW(w,GWLP_USERDATA));
  if(msg==WM_NCCREATE){self=static_cast<VisibilityWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
  if(self&&msg==WM_COMMAND&&LOWORD(wp)==1&&HIWORD(wp)==BN_CLICKED) {self->enable_controls();return 0;}
  if(self&&msg==WM_COMMAND&&LOWORD(wp)==2) {if(self->save())self->hide();return 0;}
  if(self&&msg==WM_CLOSE){if(self->save())self->hide();return 0;}
  return DefWindowProcW(w,msg,wp,lp);
 }
 void refresh() {
  const auto p=preference_.preferences();SendMessageW(checkbox_,BM_SETCHECK,p.show?BST_CHECKED:BST_UNCHECKED,0);
  for(size_t i=0;i<21;++i)SendMessageW(controls_[i],settings_catalog()[i].value?CB_SETCURSEL:BM_SETCHECK,p.enabled[i]?1:0,0);
  enable_controls();
 }
 void run() {
  WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"RushCheatVisibility";
  cls.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);RegisterClassW(&cls);
  window_=CreateWindowExW(WS_EX_APPWINDOW,cls.lpszClassName,L"Rush 2049 port options - Cheats",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,730,670,nullptr,nullptr,cls.hInstance,this);
  std::printf("CHEAT_OPTIONS_WINDOW_CREATED: success=%d error=%lu\n",window_!=nullptr,(unsigned long)GetLastError());
  checkbox_=CreateWindowW(L"BUTTON",L"Enable cheats and show the in-game cheat menu",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,18,16,660,28,window_,reinterpret_cast<HMENU>(1),cls.hInstance,nullptr);
  CreateWindowW(L"STATIC",L"Choose your cheats, then close this panel. Choices apply after PRESS START.\nNo code entry or automatic button presses.",WS_CHILD|WS_VISIBLE,18,48,680,34,window_,nullptr,cls.hInstance,nullptr);
  for(size_t i=0;i<21;++i) {
   int col=int(i/11),row=int(i%11),x=18+col*350,y=92+row*38;
   const auto& setting=settings_catalog()[i];
   if(!setting.value)controls_[i]=CreateWindowA("BUTTON",i==7?"DISABLE CAR COLLISIONS":i==16?"DISABLE BRAKES":setting.label,WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,x,y,330,28,window_,reinterpret_cast<HMENU>(100+i),cls.hInstance,nullptr);
   else {
    CreateWindowA("STATIC",setting.label,WS_CHILD|WS_VISIBLE,x,y+5,178,25,window_,nullptr,cls.hInstance,nullptr);
    controls_[i]=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST,x+178,y,152,150,window_,reinterpret_cast<HMENU>(100+i),cls.hInstance,nullptr);
    SendMessageA(controls_[i],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(i==4?"0 (OFF)":"NORMAL (OFF)"));
    SendMessageA(controls_[i],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(setting.value));
   }
  }
  CreateWindowW(L"STATIC",L"Unchecked choices preserve normal gameplay. Only selected modifiers are applied.\nClose or click Done to save. Changes apply at the main menu; return there if already racing.",WS_CHILD|WS_VISIBLE,18,520,675,45,window_,nullptr,cls.hInstance,nullptr);
  CreateWindowW(L"BUTTON",L"Done",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,592,575,100,30,window_,reinterpret_cast<HMENU>(2),cls.hInstance,nullptr);
  CreateWindowW(L"STATIC",L"F5 opens these options. F8 opens controller setup.",WS_CHILD|WS_VISIBLE,18,575,560,42,window_,reinterpret_cast<HMENU>(3),cls.hInstance,nullptr);
  refresh();
  {std::lock_guard<std::mutex> lock(ready_mutex_);thread_id_=GetCurrentThreadId();}ready_.notify_one();
  MSG msg{};
  while(GetMessageW(&msg,nullptr,0,0)>0) {
   if(msg.message==WM_APP){std::puts("CHEAT_OPTIONS_OPENED");refresh();ShowWindow(window_,SW_SHOW);SetForegroundWindow(window_);continue;}
   TranslateMessage(&msg);DispatchMessageW(&msg);
  }
  if(window_)DestroyWindow(window_);
 }
public:
 explicit VisibilityWindow(Visibility& value):preference_(value){thread_=std::thread([this]{run();});std::unique_lock<std::mutex> lock(ready_mutex_);ready_.wait(lock,[this]{return thread_id_!=0;});}
 ~VisibilityWindow(){PostThreadMessageW(thread_id_,WM_QUIT,0,0);if(thread_.joinable())thread_.join();}
 void open(HWND owner){owner_.store(owner);if(!PostThreadMessageW(thread_id_,WM_APP,0,0))std::fprintf(stderr,"CHEAT_OPTIONS_OPEN_FAILED: %lu\n",(unsigned long)GetLastError());}
};
}
