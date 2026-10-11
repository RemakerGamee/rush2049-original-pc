#pragma once
// Included inside rush::input after the configuration persistence helpers.
struct NativeInputWindow {
    Config changed;Profile profile;SDL_Joystick* joystick=nullptr;HWND window=nullptr,deviceBox=nullptr,presetBox=nullptr,status=nullptr;
    std::array<HWND,16> values{};bool done=false,saved=false;int capture=-1;std::vector<int16_t> baseline;Uint64 deadline=0;
    static constexpr const wchar_t* labels[]={L"A",L"B",L"Start",L"Z",L"L",L"R",L"D-Up",L"D-Down",L"D-Left",L"D-Right",L"C-Up",L"C-Down",L"C-Left",L"C-Right",L"Stick X",L"Stick Y"};
    static constexpr const wchar_t* keyboard[]={L"Z",L"X",L"Enter",L"Space",L"Q",L"E",L"Unbound",L"Unbound",L"Unbound",L"Unbound",L"I",L"K",L"J",L"L",L"Left / Right",L"Up / Down"};
    HWND add(const wchar_t* kind,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id=0) {
        auto child=CreateWindowExW(wcscmp(kind,L"EDIT")==0?WS_EX_CLIENTEDGE:0,kind,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,window,reinterpret_cast<HMENU>(intptr_t(id)),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);return child;
    }
    void refresh() {
        for(int i=0;i<16;++i){std::string text;
            if(i<14){const auto& b=profile.bindings[i];text=b.empty()?"Unassigned":b[0].kind==BindKind::Button?"Button "+std::to_string(b[0].index):b[0].kind==BindKind::Hat?"D-pad "+std::to_string(b[0].index)+" / "+std::to_string(b[0].hat_mask):"Axis "+std::to_string(b[0].index)+(b[0].axis_sign<0?" -":" +");}
            else {const auto& a=i==14?profile.stick_x:profile.stick_y;text=a.set?"Axis "+std::to_string(a.axis)+(a.invert?" (inverted)":""):"Unassigned";}
            std::wstring wide(text.begin(),text.end());SetWindowTextW(values[i],wide.c_str());}
    }
    void select_device(int index) {
        capture=-1;if(joystick)SDL_JoystickClose(joystick);joystick=SDL_JoystickOpen(index);profile={};
        if(!joystick){SetWindowTextW(status,L"Connect a controller, then reopen F8. Keyboard remains available.");return;}
        char guid[33]{};SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joystick),guid,sizeof(guid));profile.id=std::string("pad-")+guid;profile.guid=guid;profile.deadzone=changed.deadzone;
        for(const auto& p:changed.profiles)if(!p.game_controller && p.guid==guid){profile=p;break;}
        int preset=profile.guid=="0300550d632500007505000000000000"?1:profile.guid=="03003d486f0e00001311000000000000"?2:SDL_IsGameController(index)?3:0;
        SendMessageW(presetBox,CB_SETCURSEL,preset,0);if(profile.bindings[0].empty() && preset)apply_preset(preset,index);refresh();
    }
    void apply_preset(int preset,int index) {
        if(!joystick)return;capture=-1;
        if(preset==1 || preset==2){profile.bindings={};profile.stick_x={true,0,false};profile.stick_y={true,1,true};const int buttons[]={2,1,preset==1?12:9,6,4,5,-1,-1,-1,-1,preset==1?9:11,3,0,preset==1?8:10};
            for(int i=0;i<14;++i)if(buttons[i]>=0)profile.bindings[i]={{BindKind::Button,buttons[i]}};
            const uint8_t hats[]={1,4,8,2};for(int i=0;i<4;++i)profile.bindings[6+i]={{BindKind::Hat,0,hats[i]}};
        }else if(preset==3){
            auto* controller=SDL_IsGameController(index)?SDL_GameControllerOpen(index):nullptr;
            if(!controller){SetWindowTextW(status,L"Xbox layout unavailable for this device. Click Change to map controls.");return;}
            const auto guid=profile.guid,id=profile.id;profile=default_game_controller_profile();profile.guid=guid;profile.id=id;profile.game_controller=false;
            for(auto& bindings:profile.bindings)for(auto& b:bindings){const auto raw=b.kind==BindKind::Button?SDL_GameControllerGetBindForButton(controller,SDL_GameControllerButton(b.index)):SDL_GameControllerGetBindForAxis(controller,SDL_GameControllerAxis(b.index));
                if(raw.bindType==SDL_CONTROLLER_BINDTYPE_BUTTON){b.kind=BindKind::Button;b.index=raw.value.button;}
                else if(raw.bindType==SDL_CONTROLLER_BINDTYPE_AXIS){b.kind=BindKind::Axis;b.index=raw.value.axis;}
                else if(raw.bindType==SDL_CONTROLLER_BINDTYPE_HAT){b.kind=BindKind::Hat;b.index=raw.value.hat.hat;b.hat_mask=raw.value.hat.hat_mask;}
                else b.index=-1;
            }
            for(auto axis:{&profile.stick_x,&profile.stick_y}){const auto raw=SDL_GameControllerGetBindForAxis(controller,SDL_GameControllerAxis(axis->axis));axis->set=raw.bindType==SDL_CONTROLLER_BINDTYPE_AXIS;if(axis->set)axis->axis=raw.value.axis;}
            SDL_GameControllerClose(controller);
        }
        SetWindowTextW(status,L"Defaults filled in. Click Change beside any control, then Save.");refresh();
    }
    void begin_capture(int target){if(!joystick)return;capture=target;baseline.resize(SDL_JoystickNumAxes(joystick));SDL_PumpEvents();for(size_t i=0;i<baseline.size();++i)baseline[i]=SDL_JoystickGetAxis(joystick,int(i));deadline=SDL_GetTicks64()+10000;
        SetWindowTextW(status,target==14?L"Move your stick RIGHT. Escape cancels.":target==15?L"Move your stick UP. Escape cancels.":L"Press your controller button, D-pad direction or trigger. Escape cancels.");}
    void capture_event(const SDL_Event& event){
        if(capture<0 || !joystick)return;if(SDL_GetTicks64()>deadline){capture=-1;SetWindowTextW(status,L"No input received. Previous binding kept.");return;}
        if(event.type==SDL_KEYDOWN && event.key.keysym.sym==SDLK_ESCAPE){capture=-1;return;}
        Binding binding;bool got=false;
        if(capture<14 && event.type==SDL_JOYBUTTONDOWN && event.jbutton.which==SDL_JoystickInstanceID(joystick)){binding={BindKind::Button,event.jbutton.button};got=true;}
        if(capture<14 && event.type==SDL_JOYHATMOTION && event.jhat.which==SDL_JoystickInstanceID(joystick) && (event.jhat.value==1||event.jhat.value==2||event.jhat.value==4||event.jhat.value==8)){binding={BindKind::Hat,event.jhat.hat,event.jhat.value};got=true;}
        if(event.type==SDL_JOYAXISMOTION && event.jaxis.which==SDL_JoystickInstanceID(joystick) && event.jaxis.axis<baseline.size() && std::abs(int(event.jaxis.value)-int(baseline[event.jaxis.axis]))>=16384){
            if(capture>=14){auto& a=capture==14?profile.stick_x:profile.stick_y;a={true,event.jaxis.axis,capture==14?event.jaxis.value<0:event.jaxis.value>0};got=true;}
            else {binding={BindKind::Axis,event.jaxis.axis,0,event.jaxis.value<0?-1:1,0.5f};got=true;}}
        if(got){if(capture<14)profile.bindings[capture]={binding};capture=-1;refresh();SetWindowTextW(status,L"Binding updated. Click Save to keep your changes.");}
    }
    void save(){if(!joystick)return;
        Capabilities caps{SDL_JoystickNumButtons(joystick),SDL_JoystickNumAxes(joystick),SDL_JoystickNumHats(joystick)};if(auto problem=validate_profile(profile,caps)){std::wstring text(problem->begin(),problem->end());SetWindowTextW(status,text.c_str());return;}
        auto it=std::find_if(changed.profiles.begin(),changed.profiles.end(),[&](const Profile& p){return !p.game_controller&&p.guid==profile.guid;});if(it==changed.profiles.end())changed.profiles.push_back(profile);else *it=profile;
        std::string error;if(!save_remap_config(changed,remap_config_path(),error)){std::wstring text(error.begin(),error.end());SetWindowTextW(status,text.c_str());return;}saved=true;done=true;
    }
    static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp){auto* self=reinterpret_cast<NativeInputWindow*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){self=static_cast<NativeInputWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(self){if(message==WM_CLOSE){self->done=true;return 0;}if(message==WM_COMMAND){const int id=LOWORD(wp);if(id==1)self->save();else if(id==2)self->done=true;else if(id>=100&&id<116)self->begin_capture(id-100);else if(id==20&&HIWORD(wp)==CBN_SELCHANGE)self->select_device(int(SendMessageW(self->deviceBox,CB_GETCURSEL,0,0)));else if(id==21&&HIWORD(wp)==CBN_SELCHANGE)self->apply_preset(int(SendMessageW(self->presetBox,CB_GETCURSEL,0,0)),int(SendMessageW(self->deviceBox,CB_GETCURSEL,0,0)));return 0;}}
        return DefWindowProcW(window,message,wp,lp);
    }
    std::optional<Config> run(const Config& original){changed=original;WNDCLASSW klass{};klass.hInstance=GetModuleHandleW(nullptr);klass.lpfnWndProc=proc;klass.lpszClassName=L"RushControllerBindings";klass.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));klass.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&klass);
        window=CreateWindowExW(WS_EX_APPWINDOW,klass.lpszClassName,L"Rush 2049 - Controller and Keyboard",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,680,670,nullptr,nullptr,klass.hInstance,this);if(!window)return {};
        add(L"STATIC",L"Connected device",0,18,16,140,20);deviceBox=add(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,164,12,474,220,20);
        for(int i=0;i<SDL_NumJoysticks();++i){char guid[33]{};SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i),guid,sizeof(guid));const char* raw=SDL_JoystickNameForIndex(i);std::string label=std::string(guid)=="0300550d632500007505000000000000"?"Wired N64 Controller":std::string(guid)=="03003d486f0e00001311000000000000"?"Wireless N64 / USB Dongle Controller":SDL_IsGameController(i)?"Xbox One / SDL Controller":raw?raw:"Custom Controller";label+=" #"+std::to_string(i+1);std::wstring text(label.begin(),label.end());SendMessageW(deviceBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}
        add(L"STATIC",L"Add device / preset",0,18,52,140,20);presetBox=add(L"COMBOBOX",L"",CBS_DROPDOWNLIST,164,48,474,180,21);
        for(const wchar_t* name:{L"Custom / current bindings",L"Wired N64 Controller",L"Wireless N64 / USB Dongle Controller",L"Xbox One Controller"})SendMessageW(presetBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
        add(L"STATIC",L"N64 control",0,18,92,135,20);add(L"STATIC",L"Keyboard",0,164,92,145,20);add(L"STATIC",L"Controller binding",0,335,92,170,20);
        for(int i=0;i<16;++i){const int y=120+i*25;add(L"STATIC",labels[i],0,18,y,130,21);add(L"STATIC",keyboard[i],0,164,y,155,21);values[i]=add(L"STATIC",L"Unassigned",0,335,y,178,21);add(L"BUTTON",L"Change",BS_PUSHBUTTON,548,y-2,90,24,100+i);}
        status=add(L"STATIC",L"Choose a preset or change individual controls. Keyboard stays available.",0,18,532,620,40);add(L"BUTTON",L"Save",BS_DEFPUSHBUTTON,436,588,95,28,1);add(L"BUTTON",L"Cancel",BS_PUSHBUTTON,543,588,95,28,2);
        if(SDL_NumJoysticks()){SendMessageW(deviceBox,CB_SETCURSEL,0,0);select_device(0);}else {SendMessageW(presetBox,CB_SETCURSEL,0,0);SetWindowTextW(status,L"Connect a controller and reopen F8. Keyboard controls are shown above.");}
        ShowWindow(window,SW_SHOW);while(!done){SDL_Event event{};if(SDL_WaitEventTimeout(&event,20)){if(event.type==SDL_QUIT){SDL_PushEvent(&event);done=true;}else capture_event(event);}else if(capture>=0&&SDL_GetTicks64()>deadline){capture=-1;SetWindowTextW(status,L"Capture timed out. Previous binding kept.");}}
        DestroyWindow(window);if(joystick)SDL_JoystickClose(joystick);joystick=nullptr;return saved?std::optional<Config>(changed):std::nullopt;
    }
};
