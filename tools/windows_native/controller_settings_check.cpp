// Native Windows check of the actual System and saved configuration, using
// isolated files and SDL virtual devices. This does not exercise physical pads.
#include "controller_input.hpp"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <stdexcept>
using namespace rush::input;
static unsigned checks;
static void verify(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
static Config read(const char* path){std::ifstream f(path);verify(bool(f),"profile open");std::string s((std::istreambuf_iterator<char>(f)),{});auto c=parse_config(s);verify(c.ok(),"profile parse");return c.config;}
static void lifecycle(){
 SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
 verify(SDL_Init(SDL_INIT_VIDEO|sdl_init_flags)==0,"SDL init");
 SDL_VirtualJoystickDesc desc{};desc.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;desc.type=SDL_JOYSTICK_TYPE_UNKNOWN;desc.naxes=4;desc.nbuttons=13;desc.nhats=1;desc.vendor_id=0xf123;desc.product_id=1;desc.name="Rush isolated virtual one";
 int one=SDL_JoystickAttachVirtualEx(&desc);verify(one>=0,"attach one");char guid1[33]{};SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(one),guid1,33);
 desc.product_id=2;desc.name="Rush isolated virtual two";int two=SDL_JoystickAttachVirtualEx(&desc);verify(two>=0,"attach two");char guid2[33]{};SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(two),guid2,33);
 Config c;for(int port=0;port<2;++port){Profile p;p.id="virtual"+std::to_string(port);p.guid=port?guid2:guid1;p.stick_x={true,0,false};p.stick_y={true,1,true};p.bindings[size_t(Button::A)]={{BindKind::Button,2}};p.bindings[size_t(Button::B)]={{BindKind::Button,1}};c.profiles.push_back(p);c.assignments[p.guid]=port;}
 rush::keyboard::Input keyboard;System sys(keyboard,c,{},{});sys.manager().device_added(one);sys.manager().device_added(two);
 SDL_Joystick* j1=SDL_JoystickOpen(one);SDL_Joystick* j2=SDL_JoystickOpen(two);verify(j1&&j2,"setters open");
 auto change=[&](SDL_Joystick* j,int button,int value){verify(SDL_JoystickSetVirtualButton(j,button,Uint8(value))==0,"virtual button");SDL_JoystickUpdate();SDL_Event e{};e.type=SDL_JOYBUTTONDOWN;e.jbutton.which=SDL_JoystickInstanceID(j);sys.handle_event(e);};
 auto sample=[&](int port,uint16_t expected){uint16_t b=0;float x=0,y=0;verify(sys.get_input(port,&b,&x,&y),"port connected");verify(b==expected,"port buttons");};
 change(j1,2,1);change(j2,1,1);sample(0,0x8000);sample(1,0x4000);
 SDL_Event focus{};focus.type=SDL_WINDOWEVENT;focus.window.event=SDL_WINDOWEVENT_FOCUS_LOST;sys.handle_event(focus);sample(0,0);sample(1,0);
 change(j1,2,0);change(j2,1,0);sample(0,0);sample(1,0);focus.window.event=SDL_WINDOWEVENT_FOCUS_GAINED;sys.handle_event(focus);sample(0,0);sample(1,0);
 change(j1,2,1);sys.handle_event(SDL_Event{});focus.window.event=SDL_WINDOWEVENT_FOCUS_LOST;sys.handle_event(focus);sample(0,0);focus.window.event=SDL_WINDOWEVENT_FOCUS_GAINED;sys.handle_event(focus);sample(0,0x8000);
 auto id=SDL_JoystickInstanceID(j1);verify(SDL_JoystickDetachVirtual(one)==0,"detach held");SDL_JoystickClose(j1);SDL_Event removed{};removed.type=SDL_JOYDEVICEREMOVED;removed.jdevice.which=id;sys.handle_event(removed);sample(0,0);sample(1,0);
 desc.product_id=1;desc.name="Rush isolated virtual one";int again=SDL_JoystickAttachVirtualEx(&desc);verify(again>=0,"reattach");sys.manager().device_added(again);j1=SDL_JoystickOpen(again);verify(j1!=nullptr,"reopen");sample(0,0);sample(1,0);change(j1,2,1);sample(0,0x8000);sample(1,0);change(j1,2,0);sample(0,0);
 sys.manager().shutdown();SDL_JoystickClose(j1);SDL_JoystickClose(j2);SDL_JoystickDetachVirtual(again);
 // Device indices shift when the first device detaches; locate only our second.
 for(int i=SDL_NumJoysticks()-1;i>=0;--i){char guid[33]{};SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i),guid,33);if(std::string(guid)==guid2)SDL_JoystickDetachVirtual(i);}
 SDL_Quit();
}
int main(int argc,char**argv){try{
 verify(argc==4,"mode profile directory required");auto c=read(argv[2]);std::filesystem::path target=std::filesystem::path(argv[3])/"input.cfg";
 if(std::string(argv[1])=="write"){std::string error;verify(save_remap_config(c,target.string(),error),"initial save");c.keyboard_port=3;verify(save_remap_config(c,target.string(),error),"changed save");std::cout<<"Saved actual profiles with isolated keyboard-port3\n";}
 else if(std::string(argv[1])=="read" || std::string(argv[1])=="read-current"){verify(_putenv_s("RUSH_INPUT_CONFIG",target.string().c_str())==0,"environment set");std::vector<std::string> errors;auto loaded=load_config_from_environment(errors);verify(errors.empty(),"fresh-process load");int expected_keyboard=std::string(argv[1])=="read"?3:c.keyboard_port;verify(loaded.keyboard_port==expected_keyboard,"saved value survives");verify(loaded.assignments==c.assignments,"ports retained");verify(loaded.profiles.size()==c.profiles.size(),"both profiles retained");c.keyboard_port=expected_keyboard;verify(serialize_input_config(loaded)==serialize_input_config(c),"complete saved config survives");lifecycle();}
 else throw std::runtime_error("unknown mode");
 std::cout<<"PASS native controller settings: "<<checks<<" checks\n";return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
