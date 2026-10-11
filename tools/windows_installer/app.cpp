#define NOMINMAX
#define UNICODE
#define _UNICODE
#include <Windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <fstream>
#include <memory>
#include <sstream>
#include <iomanip>
#include <cwctype>
#include "../windows_native/rom_identity.hpp"
#include "payload_table.hpp"

namespace fs=std::filesystem;
static constexpr wchar_t version[]=L"0.3.0";
static HINSTANCE instance;
static fs::path rootPath,dataPath,romPath;
static bool cli=false;
static int visualEdition=-1;
static bool choose_edition() {
    if(visualEdition>=0)return true;
    if(!payload::remastered){visualEdition=0;return true;}
    const TASKDIALOG_BUTTON buttons[]={{100,L"Original\nOriginal game textures"},{101,L"Remaster\nRemastered textures and menu lighting"}};
    TASKDIALOGCONFIG dialog{sizeof(dialog)};
    dialog.hInstance=instance;dialog.dwFlags=TDF_USE_COMMAND_LINKS|TDF_ALLOW_DIALOG_CANCELLATION;
    dialog.pszWindowTitle=L"Rush 2049";dialog.pszMainInstruction=L"Choose your version";
    dialog.pszContent=L"Both versions require your own supported N64 ROM and retain your saves and controller settings.";
    dialog.cButtons=2;dialog.pButtons=buttons;dialog.nDefaultButton=101;
    int selected=IDCANCEL;
    if(FAILED(TaskDialogIndirect(&dialog,&selected,nullptr,nullptr)))throw std::runtime_error("Cannot open version selection.");
    if(selected!=100&&selected!=101)return false;
    visualEdition=selected==101?1:0;return true;
}

static fs::path known(REFKNOWNFOLDERID id) {
    PWSTR path=nullptr;if(FAILED(SHGetKnownFolderPath(id,0,nullptr,&path)))throw std::runtime_error("Cannot locate your Windows user folder.");
    fs::path result(path);CoTaskMemFree(path);return result;
}
static fs::path self() {std::vector<wchar_t> path(32768);const DWORD n=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));if(!n || n>=path.size())throw std::runtime_error("Cannot locate this application.");return fs::path(std::wstring(path.data(),n));}
static void safe(const fs::path& path) {
    if(!path.is_absolute())throw std::runtime_error("Choose an absolute installation or data folder.");
    for(auto p=path;!p.empty();p=p.parent_path()) {
        const DWORD attrs=GetFileAttributesW(p.c_str());
        if(attrs!=INVALID_FILE_ATTRIBUTES && (attrs&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Linked installation or save folders are unsupported.");
        if(p==p.parent_path())break;
    }
}
static std::vector<uint8_t> read(const fs::path& path,size_t maximum=128*1024*1024) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file)throw std::runtime_error("A required file is missing or unreadable.");
    const auto n=file.tellg();if(n<0 || static_cast<uint64_t>(n)>maximum)throw std::runtime_error("Invalid file size.");
    std::vector<uint8_t> result(static_cast<size_t>(n));file.seekg(0);file.read(reinterpret_cast<char*>(result.data()),result.size());
    if(!file)throw std::runtime_error("A required file could not be read completely.");return result;
}
static void write_new(const fs::path& path,const uint8_t* bytes,size_t count) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create a new installation file. Existing files are preserved.");
    DWORD written=0;const bool good=WriteFile(file,bytes,static_cast<DWORD>(count),&written,nullptr) && written==count && FlushFileBuffers(file);CloseHandle(file);
    if(!good)throw std::runtime_error("Cannot finish writing installation files.");
}
static void save_rom(const fs::path& data,const fs::path& rom) {
    const auto text=rush::rom::utf8(fs::absolute(rom).lexically_normal().wstring());
    const auto temp=data/(L"rom-location-"+std::to_wstring(GetCurrentProcessId())+L".tmp");
    write_new(temp,reinterpret_cast<const uint8_t*>(text.data()),text.size());
    if(!MoveFileExW(temp.c_str(),(data/L"rom-location.txt").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        fs::remove(temp);throw std::runtime_error("Cannot save the selected ROM location.");
    }
}
static fs::path load_rom(const fs::path& data) {
    const auto bytes=read(data/L"rom-location.txt",32768);
    const fs::path path(rush::rom::wide(std::string(bytes.begin(),bytes.end())));
    if(!path.is_absolute())throw std::runtime_error("The saved ROM location is invalid. Select your ROM again.");return path;
}
static bool contained(const fs::path& child,const fs::path& parent) {
    auto a=fs::absolute(child).lexically_normal().wstring(),b=fs::absolute(parent).lexically_normal().wstring();
    std::transform(a.begin(),a.end(),a.begin(),towlower);std::transform(b.begin(),b.end(),b.begin(),towlower);
    if(a==b)return true;if(!b.empty() && b.back()!=L'\\')b+=L'\\';return a.starts_with(b);
}
static void validate_data(const fs::path& data) {
    safe(data);
    if(fs::exists(data/L"controller.mpk") && read(data/L"controller.mpk",32768).size()!=32768)throw std::runtime_error("Existing Controller Pak size differs. Your save was preserved.");
    if(fs::exists(data/L"input.cfg")) {
        const auto raw=read(data/L"input.cfg",1024*1024);std::istringstream lines(std::string(raw.begin(),raw.end()));std::string line;bool accepted=false;
        while(std::getline(lines,line)) {line=line.substr(0,line.find('#'));std::istringstream words(line);std::string first,second,extra;if(!(words>>first))continue;
            accepted=first=="rush-input" && (words>>second) && second=="1" && !(words>>extra);break;}
        if(!accepted)throw std::runtime_error("Existing controller settings are unsupported. They were preserved.");
    }
}
static void initialize_data(const fs::path& data,const fs::path& app) {
    validate_data(data);fs::create_directories(data);
    if(!fs::exists(data/L"controller.mpk")) {
        std::vector<uint8_t> pak(32768,0);uint8_t identity[32]{};
        if(BCryptGenRandom(nullptr,identity,24,BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("Cannot initialize a Controller Pak.");
        identity[25]=identity[26]=identity[27]=1;uint16_t sum=0,inverse=0;
        for(int i=0;i<28;i+=2){const uint16_t word=(identity[i]<<8)|identity[i+1];sum+=word;inverse+=static_cast<uint16_t>(65535-word);}
        identity[28]=sum>>8;identity[29]=sum&255;identity[30]=inverse>>8;identity[31]=inverse&255;
        for(size_t offset:{0x20,0x60,0x80,0xc0})std::copy(identity,identity+32,pak.begin()+offset);
        std::vector<uint8_t> inode(256,0);for(int page=5;page<128;++page)inode[page*2+1]=3;
        for(size_t i=10;i<256;++i)inode[1]+=inode[i];std::copy(inode.begin(),inode.end(),pak.begin()+256);std::copy(inode.begin(),inode.end(),pak.begin()+512);
        write_new(data/L"controller.mpk",pak.data(),pak.size());
    }
    if(!fs::exists(data/L"input.cfg")){const auto bytes=read(app/L"defaults/input.cfg",1024*1024);write_new(data/L"input.cfg",bytes.data(),bytes.size());}
    fs::create_directories(data/L"profile");
}
static fs::path pick_rom(HWND owner) {
    std::vector<wchar_t> file(32768);OPENFILENAMEW dialog{sizeof(dialog)};dialog.hwndOwner=owner;
    dialog.lpstrFilter=L"Nintendo 64 ROM (*.z64;*.v64;*.n64)\0*.z64;*.v64;*.n64\0All files\0*.*\0";
    dialog.lpstrFile=file.data();dialog.nMaxFile=static_cast<DWORD>(file.size());dialog.lpstrTitle=L"Select your original US Rush 2049 ROM";
    dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_DONTADDTORECENT;
    if(!GetOpenFileNameW(&dialog))return {};return fs::absolute(file.data());
}
static std::wstring quote(const std::wstring& text) {
    std::wstring out=L"\"";size_t backslashes=0;
    for(wchar_t c:text){if(c==L'\\'){++backslashes;continue;}if(c==L'\"')out.append(backslashes*2+1,L'\\');else out.append(backslashes,L'\\');backslashes=0;out+=c;}
    out.append(backslashes*2,L'\\');out+=L'\"';return out;
}
static void verify_payload(const fs::path& app) {
    for(const auto& item:payload::files){const auto path=app/fs::path(item.path);safe(path);const auto bytes=read(path);if(rush::rom::hash(bytes.data(),bytes.size(),BCRYPT_SHA256_ALGORITHM)!=item.sha256)throw std::runtime_error("Installed files changed or are missing. Install a fresh version.");}
}
static void shortcut(const fs::path& app) {
    IShellLinkW* link=nullptr;if(FAILED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link))))return;
    link->SetPath((app/L"Rush2049Original.exe").c_str());link->SetWorkingDirectory(app.c_str());link->SetDescription(payload::edition);
    link->SetIconLocation((app/L"Rush2049Original.exe").c_str(),0);
    IPersistFile* file=nullptr;if(SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&file)))){
        const auto path=known(FOLDERID_Desktop)/(payload::remastered?L"Rush 2049 Remastered v0.3.lnk":L"Rush 2049 Original v0.3.lnk");
        if(!fs::exists(path))file->Save(path.c_str(),TRUE);
        else if(SUCCEEDED(file->Load(path.c_str(),STGM_READWRITE))){
            wchar_t existing[MAX_PATH]{};
            if(SUCCEEDED(link->GetPath(existing,MAX_PATH,nullptr,SLGP_RAWPATH)) && fs::path(existing).filename()==L"Rush2049Original.exe"){
                // Refresh our existing shortcut's icon while retaining its target and arguments.
                link->SetIconLocation((app/L"Rush2049Original.exe").c_str(),0);file->Save(path.c_str(),TRUE);
            }
        }
        file->Release();
    }link->Release();
}
static void install_app(const fs::path& destination,const fs::path& rom,const fs::path& data,bool desktop) {
    // ROM gate precedes every installation/data-directory write.
    rush::rom::File validated(rom);
    safe(destination);safe(data);
    if(fs::exists(destination))throw std::runtime_error("Choose a NEW installation folder. Your existing version is preserved.");
    if(contained(data,destination)||contained(destination,data))throw std::runtime_error("Keep saves separate from the installed game folder.");
    if(contained(rom,destination))throw std::runtime_error("Keep your ROM outside the installation folder.");
    validate_data(data);fs::create_directories(destination.parent_path());
    const auto stage=destination.parent_path()/(L".rush-install-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    if(!fs::create_directory(stage))throw std::runtime_error("Cannot create a new installation staging folder.");
    bool owned=true;
    try {
        for(const auto& item:payload::files) {
            const auto relative=fs::path(item.path);if(relative.is_absolute() || relative.wstring().find(L"..")!=std::wstring::npos)throw std::runtime_error("Invalid installer resource path.");
            const auto resource=FindResourceW(instance,MAKEINTRESOURCEW(item.id),RT_RCDATA);
            const auto loaded=resource?LoadResource(instance,resource):nullptr;
            const auto size=resource?SizeofResource(instance,resource):0;
            const auto* bytes=loaded?static_cast<const uint8_t*>(LockResource(loaded)):nullptr;
            if(!bytes || !size || rush::rom::hash(bytes,size,BCRYPT_SHA256_ALGORITHM)!=item.sha256)throw std::runtime_error("Installer files are damaged. Download the installer again.");
            const auto target=stage/relative;fs::create_directories(target.parent_path());write_new(target,bytes,size);
        }
        initialize_data(data,stage);
        const auto dataText=rush::rom::utf8(data.wstring());write_new(stage/L"player-data-location.txt",reinterpret_cast<const uint8_t*>(dataText.data()),dataText.size());
        if(!MoveFileW(stage.c_str(),destination.c_str()))throw std::runtime_error("Installation could not finish; existing folders were preserved.");
        owned=false;save_rom(data,rom);
        if(desktop)shortcut(destination);
    } catch(...) {if(owned && stage.parent_path()==destination.parent_path() && stage.filename().wstring().starts_with(L".rush-install-"))fs::remove_all(stage);throw;}
}
class Session {
    HANDLE mutex=nullptr,file=INVALID_HANDLE_VALUE;bool acquired=false;
public:
    explicit Session(const fs::path& data) {
        mutex=CreateMutexW(nullptr,FALSE,L"Local\\Rush2049-Native-Session");
        const auto wait=mutex?WaitForSingleObject(mutex,0):WAIT_FAILED;
        if(wait!=WAIT_OBJECT_0 && wait!=WAIT_ABANDONED){if(mutex)CloseHandle(mutex);throw std::runtime_error("Another Rush session is running. Close it first.");}
        acquired=true;file=CreateFileW((data/L"session.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE){ReleaseMutex(mutex);CloseHandle(mutex);throw std::runtime_error("Your save folder is in use. Close the other session first.");}
    }
    ~Session(){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);if(mutex){if(acquired)ReleaseMutex(mutex);CloseHandle(mutex);}}
};
static void reject_other_game() {
    const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot check active game sessions.");
    PROCESSENTRY32W entry{sizeof(entry)};bool found=false;
    if(Process32FirstW(snapshot,&entry))do{if(entry.th32ProcessID!=GetCurrentProcessId() && _wcsnicmp(entry.szExeFile,L"Rush",4)==0){found=true;break;}}while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot);if(found)throw std::runtime_error("Another Rush game or installer is running. Close it first.");
}
static int launch(const fs::path& app,const fs::path& data,bool preflight) {
    verify_payload(app);validate_data(data);
    if(!fs::exists(data/L"controller.mpk") || !fs::exists(data/L"input.cfg"))throw std::runtime_error("Player data is missing. Run the installer again.");
    std::unique_ptr<rush::rom::File> validated;
    try {romPath=load_rom(data);validated=std::make_unique<rush::rom::File>(romPath);}
    catch(const std::exception& error) {
        if(preflight || cli)throw;
        const auto message=rush::rom::wide(error.what())+L"\n\nSelect your ROM at its current location to continue.";
        MessageBoxW(nullptr,message.c_str(),L"Rush 2049 — ROM required",MB_OK|MB_ICONINFORMATION);
        romPath=pick_rom(nullptr);if(romPath.empty())return 0;
        validated=std::make_unique<rush::rom::File>(romPath);save_rom(data,romPath);
    }
    if(visualEdition==1&&!payload::remastered)throw std::runtime_error("Remaster textures are not included in this installation.");
    if(preflight)return 0;
    if(!choose_edition())return 0;
    Session session(data);reject_other_game();
    // Remove inherited diagnostic overrides. The chosen external ROM is the only input.
    LPWCH environment=GetEnvironmentStringsW();std::vector<std::wstring> remove;
    if(environment){for(auto p=environment;*p;p+=wcslen(p)+1){std::wstring row(p);auto eq=row.find(L'=');if(eq!=std::wstring::npos){const auto name=row.substr(0,eq);if(name.starts_with(L"RUSH_")||name==L"VK_DRIVER_FILES"||name==L"VK_ICD_FILENAMES")remove.push_back(name);}}FreeEnvironmentStringsW(environment);}
    for(const auto& name:remove)SetEnvironmentVariableW(name.c_str(),nullptr);
    if(visualEdition==1){SetEnvironmentVariableW(L"RUSH_REMASTER_TEXTURE_PACK",(app/L"remaster-pack").c_str());SetEnvironmentVariableW(L"RUSH_REMASTER_CIRCUIT_GLOW",L"1");}
    SetEnvironmentVariableW(L"RUSH_LUS_IMGUI",L"1");
    SetEnvironmentVariableW(L"RUSH_OPTIONS_CONFIG",(data/L"options.json").c_str());
    SetEnvironmentVariableW(L"RUSH_CONTROLLER_PAK",(data/L"controller.mpk").c_str());SetEnvironmentVariableW(L"RUSH_INPUT_CONFIG",(data/L"input.cfg").c_str());
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    HANDLE log=CreateFileW((data/L"last-session.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    HANDLE input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr);
    if(log==INVALID_HANDLE_VALUE || input==INVALID_HANDLE_VALUE){if(log!=INVALID_HANDLE_VALUE)CloseHandle(log);if(input!=INVALID_HANDLE_VALUE)CloseHandle(input);throw std::runtime_error("Cannot open the game session log.");}
    STARTUPINFOW start{sizeof(start)};start.dwFlags=STARTF_USESTDHANDLES;start.hStdOutput=start.hStdError=log;start.hStdInput=input;
    PROCESS_INFORMATION child{};const auto exe=app/L"RushWindowsRT64.exe";
    auto command=quote(exe.wstring())+L" "+quote(romPath.wstring())+L" "+quote((data/L"profile").wstring())+L" play";
    const bool created=CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,app.c_str(),&start,&child);
    CloseHandle(log);CloseHandle(input);
    if(!created)throw std::runtime_error("The game could not start. Reinstall this version and check Vulkan support.");
    WaitForSingleObject(child.hProcess,INFINITE);DWORD status=1;GetExitCodeProcess(child.hProcess,&status);CloseHandle(child.hThread);CloseHandle(child.hProcess);
    if(status!=0 && !cli)MessageBoxW(nullptr,L"The game exited with an error. Your saves are retained. Details are in last-session.log in your player-data folder.",L"Rush 2049",MB_OK|MB_ICONWARNING);
    return static_cast<int>(status);
}

#ifdef RUSH_SETUP
static HWND romEdit,directoryEdit,statusLabel,installButton,desktopCheck;
static HFONT font;
static HWND control(HWND parent,const wchar_t* type,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id=0) {
    HWND result=CreateWindowExW(type==std::wstring(L"EDIT")?WS_EX_CLIENTEDGE:0,type,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,parent,reinterpret_cast<HMENU>(static_cast<intptr_t>(id)),instance,nullptr);
    SendMessageW(result,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return result;
}
static std::wstring text(HWND field){int n=GetWindowTextLengthW(field);std::wstring value(n+1,L'\0');GetWindowTextW(field,value.data(),n+1);value.resize(n);return value;}
static LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_CREATE){
        font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        control(window,L"STATIC",payload::edition,0,24,16,550,28);
        control(window,L"STATIC",L"Select your original US N64 ROM before installing. It stays in your folder.",0,24,50,565,22);
        control(window,L"STATIC",L"Your ROM",0,24,84,565,20);
        romEdit=control(window,L"EDIT",L"",ES_AUTOHSCROLL|ES_READONLY,24,108,456,27,101);
        control(window,L"BUTTON",L"Browse...",WS_TABSTOP,490,108,100,27,102);
        control(window,L"STATIC",L"New installation folder",0,24,149,565,20);
        directoryEdit=control(window,L"EDIT",rootPath.c_str(),ES_AUTOHSCROLL|WS_TABSTOP,24,173,566,27,103);
        desktopCheck=control(window,L"BUTTON",L"Create a desktop shortcut",BS_AUTOCHECKBOX|WS_TABSTOP,24,217,330,25,104);SendMessageW(desktopCheck,BM_SETCHECK,BST_CHECKED,0);
        statusLabel=control(window,L"STATIC",L"ROM required. No compiler tools or Python needed.",0,24,255,565,44,105);
        installButton=control(window,L"BUTTON",L"Install",BS_DEFPUSHBUTTON|WS_TABSTOP,378,308,100,30,106);EnableWindow(installButton,FALSE);
        control(window,L"BUTTON",L"Cancel",WS_TABSTOP,490,308,100,30,107);return 0;
    }
    if(message==WM_COMMAND){
        if(LOWORD(wp)==102){auto selected=pick_rom(window);if(!selected.empty()){try{rush::rom::File verified(selected);romPath=selected;SetWindowTextW(romEdit,romPath.c_str());SetWindowTextW(statusLabel,L"Supported US ROM verified. It will be checked before every launch.");EnableWindow(installButton,TRUE);}catch(const std::exception& error){EnableWindow(installButton,FALSE);SetWindowTextW(statusLabel,rush::rom::wide(error.what()).c_str());}}return 0;}
        if(LOWORD(wp)==106){EnableWindow(installButton,FALSE);try{rootPath=fs::absolute(text(directoryEdit)).lexically_normal();SetWindowTextW(statusLabel,L"Installing...");UpdateWindow(window);install_app(rootPath,romPath,dataPath,SendMessageW(desktopCheck,BM_GETCHECK,0,0)==BST_CHECKED);MessageBoxW(window,L"Installation complete. Open Rush2049Original.exe or use your desktop shortcut to play. Your ROM will be checked every time.",L"Rush 2049",MB_OK|MB_ICONINFORMATION);DestroyWindow(window);}catch(const std::exception& error){SetWindowTextW(statusLabel,rush::rom::wide(error.what()).c_str());EnableWindow(installButton,TRUE);}return 0;}
        if(LOWORD(wp)==107){DestroyWindow(window);return 0;}
    }
    if(message==WM_DESTROY){if(font)DeleteObject(font);PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,message,wp,lp);
}
#endif

int WINAPI wWinMain(HINSTANCE app,HINSTANCE,LPWSTR,int show) {
    instance=app;CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);bool preflight=false,installMode=false,noShortcut=false;
    try {
        dataPath=known(FOLDERID_LocalAppData)/L"Rush2049Original/PlayerData";rootPath=self().parent_path();
#ifdef RUSH_SETUP
        rootPath=known(FOLDERID_LocalAppData)/(payload::remastered?L"Rush2049Original/App-Remastered-v0.3.0":L"Rush2049Original/App-v0.3.0");
#endif
        bool explicitData=false;
        for(int i=1;i<argc;++i){std::wstring a=argv[i];if(a==L"--edition" && i+1<argc){std::wstring edition=argv[++i]; if(edition==L"original")visualEdition=0;else if(edition==L"remaster")visualEdition=1;else throw std::runtime_error("Choose original or remaster.");}else if(a==L"--install"){installMode=true;cli=true;}else if(a==L"--preflight"){preflight=true;cli=true;}else if(a==L"--no-shortcut")noShortcut=true;else if((a==L"--rom"||a==L"--directory"||a==L"--data-dir") && i+1<argc){const fs::path value=fs::absolute(argv[++i]).lexically_normal();if(a==L"--rom")romPath=value;else if(a==L"--directory")rootPath=value;else {dataPath=value;explicitData=true;}}else throw std::runtime_error("Unknown launch option.");}
        LocalFree(argv);argv=nullptr;
#ifdef RUSH_SETUP
        if(installMode){if(romPath.empty())throw std::runtime_error("A supported original US ROM is required BEFORE installation.");install_app(rootPath,romPath,dataPath,!noShortcut);CoUninitialize();return 0;}
        WNDCLASSW klass{};klass.hInstance=instance;klass.lpfnWndProc=window_proc;klass.lpszClassName=L"Rush2049OriginalInstaller";klass.hCursor=LoadCursorW(nullptr,IDC_ARROW);klass.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));klass.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&klass);
        auto window=CreateWindowExW(WS_EX_APPWINDOW,klass.lpszClassName,payload::edition,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,632,390,nullptr,nullptr,instance,nullptr);
        if(!window)throw std::runtime_error("Cannot open the installer window.");ShowWindow(window,show);MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}CoUninitialize();return 0;
#else
        if(!explicitData){const auto raw=read(rootPath/L"player-data-location.txt",32768);dataPath=fs::path(rush::rom::wide(std::string(raw.begin(),raw.end())));}
        const auto result=launch(rootPath,dataPath,preflight);CoUninitialize();return result;
#endif
    } catch(const std::exception& error) {
        if(argv)LocalFree(argv);
        if(cli){auto message=rush::rom::utf8(rush::rom::wide(error.what()));HANDLE output=GetStdHandle(STD_ERROR_HANDLE);DWORD n=0;if(output && output!=INVALID_HANDLE_VALUE)WriteFile(output,message.data(),static_cast<DWORD>(message.size()),&n,nullptr);}
        else MessageBoxW(nullptr,rush::rom::wide(error.what()).c_str(),L"Rush 2049 — action required",MB_OK|MB_ICONERROR);
        CoUninitialize();return 65;
    }
}
