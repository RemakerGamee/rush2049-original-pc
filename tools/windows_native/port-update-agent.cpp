// Runs only after the launcher and game have exited. Never restarts the game.
#include "rush-update-history.hpp"
#include <functional>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <wintrust.h>
#include <softpub.h>
namespace fs=std::filesystem;
using rush::updates::Json;
static void safe(const fs::path& path){
    if(!path.is_absolute())throw std::runtime_error("An absolute installation/data path is required");
    for(auto p=path;!p.empty();p=p.parent_path()){
        auto attrs=GetFileAttributesW(p.c_str());if(attrs!=INVALID_FILE_ATTRIBUTES&&(attrs&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Linked update/data folders are unsupported");
        if(p==p.parent_path())break;
    }
}
static std::vector<uint8_t> read(const fs::path& file,uint64_t maximum){std::ifstream in(file,std::ios::binary|std::ios::ate);if(!in||in.tellg()<0||uint64_t(in.tellg())>maximum)throw std::runtime_error("Cannot read update file");std::vector<uint8_t> bytes(size_t(in.tellg()));in.seekg(0);if(!in.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("Cannot read update file");return bytes;}
static std::wstring quote(const std::wstring& text){std::wstring result=L"\"";size_t slashes=0;for(wchar_t c:text){if(c==L'\\'){++slashes;continue;}result.append(c==L'\"'?slashes*2+1:slashes,L'\\');slashes=0;result+=c;}result.append(slashes*2,L'\\');return result+L'\"';}
static DWORD run(const fs::path& exe,const std::wstring& args){auto command=quote(exe.wstring())+L" "+args;STARTUPINFOW start{sizeof(start)};PROCESS_INFORMATION child{};if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,exe.parent_path().c_str(),&start,&child))throw std::runtime_error("Cannot start the verified installer");CloseHandle(child.hThread);WaitForSingleObject(child.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(child.hProcess,&code);CloseHandle(child.hProcess);return code;}
static void publisher_signature(const fs::path& exe){
    WINTRUST_FILE_INFO file{sizeof(file)};file.pcwszFilePath=exe.c_str();
    WINTRUST_DATA trust{sizeof(trust)};trust.dwUIChoice=WTD_UI_NONE;trust.fdwRevocationChecks=WTD_REVOKE_NONE;trust.dwUnionChoice=WTD_CHOICE_FILE;trust.pFile=&file;trust.dwStateAction=WTD_STATEACTION_VERIFY;trust.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID policy=WINTRUST_ACTION_GENERIC_VERIFY_V2;LONG status=WinVerifyTrust(nullptr,&policy,&trust);trust.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(nullptr,&policy,&trust);
    if(status!=ERROR_SUCCESS&&status!=TRUST_E_NOSIGNATURE)throw std::runtime_error("Installer publisher signature verification failed");
}
struct Lock{HANDLE handle=nullptr;explicit Lock(HANDLE h):handle(h){}~Lock(){if(handle&&handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}};
struct Session{
    Lock mutex{CreateMutexW(nullptr,FALSE,L"Local\\Rush2049-Native-Session")};Lock file{nullptr};bool acquired=false;
    explicit Session(const fs::path& data){auto wait=mutex.handle?WaitForSingleObject(mutex.handle,0):WAIT_FAILED;if(wait!=WAIT_OBJECT_0&&wait!=WAIT_ABANDONED)throw std::runtime_error("Rush is still running. Close it before installing");acquired=true;
        file.handle=CreateFileW((data/L"session.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(file.handle==INVALID_HANDLE_VALUE){ReleaseMutex(mutex.handle);acquired=false;throw std::runtime_error("Player data is in use. Close the other Rush session first");}
        Lock snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));if(snapshot.handle==INVALID_HANDLE_VALUE){ReleaseMutex(mutex.handle);acquired=false;throw std::runtime_error("Cannot check running games");}
        PROCESSENTRY32W e{sizeof(e)};if(Process32FirstW(snapshot.handle,&e))do{if(e.th32ProcessID!=GetCurrentProcessId()&&_wcsnicmp(e.szExeFile,L"Rush",4)==0){ReleaseMutex(mutex.handle);acquired=false;throw std::runtime_error("Another Rush game, launcher or installer is running. Installation refused");}}while(Process32NextW(snapshot.handle,&e));
    }
    ~Session(){if(acquired)ReleaseMutex(mutex.handle);}
};
static std::map<fs::path,std::string> player_hashes(const fs::path& data){std::map<fs::path,std::string> result;for(auto& f:fs::recursive_directory_iterator(data)){
    auto relative=f.path().lexically_relative(data);if(relative.begin()->wstring()==L"update-cache"){if(f.is_symlink())throw std::runtime_error("Linked update cache is unsupported");continue;}
    if(f.is_symlink())throw std::runtime_error("Linked player files are unsupported");if(!f.is_regular_file())continue;
    // Setup may deliberately repair the user's moved ROM pointer; it still verifies ROM identity.
    auto name=relative.filename().wstring();if(name==L"session.lock"||name==L"rom-location.txt"||name==L"update-history.json"||name==L"update-history.dat"||name.starts_with(L"update-request-"))continue;
    auto bytes=read(f.path(),512ull*1024*1024);result[relative]=rush::rom::hash(bytes.data(),bytes.size(),BCRYPT_SHA256_ALGORITHM);
}return result;}

struct Context {fs::path app,data;std::string installed,edition;};
using Installer=std::function<DWORD(const fs::path&,const fs::path&,const fs::path&,const fs::path&)>;
using Shortcut=std::function<void(const fs::path&,const std::string&)>;
static std::string text_path(const fs::path& p){return rush::rom::utf8(p.wstring());}
static Json catalog(const fs::path& app,const std::string& installed){
    safe(app);Json files=Json::object();
    for(auto& entry:fs::recursive_directory_iterator(app)){
        safe(entry.path());if(!entry.is_regular_file())continue;auto raw=read(entry.path(),512ull*1024*1024);
        files[entry.path().lexically_relative(app).generic_string()]=rush::rom::hash(raw.data(),raw.size(),BCRYPT_SHA256_ALGORITHM);
    }
    if(!files.contains("Rush2049Original.exe")||!files.contains("RushWindowsRT64.exe"))throw std::runtime_error("Previous application is incomplete");
    return {{"path",text_path(app)},{"version",installed},{"files",files}};
}
static fs::path verify_catalog(const Json& record,const Context& c){
    auto app=fs::u8path(record.at("path").get<std::string>()).lexically_normal();safe(app);
    if(app.parent_path()!=c.app.parent_path().lexically_normal())throw std::runtime_error("Rollback application is outside the installation area");
    rush::updates::version(record.at("version").get<std::string>());
    const auto& files=record.at("files");if(!files.is_object()||files.size()>20000||!files.contains("Rush2049Original.exe")||!files.contains("RushWindowsRT64.exe"))throw std::runtime_error("Invalid rollback file catalog");
    for(auto it=files.begin();it!=files.end();++it){auto relative=fs::u8path(it.key());if(relative.is_absolute()||relative.has_root_name()||it.key().find(':')!=std::string::npos)throw std::runtime_error("Unsafe rollback file path");for(auto& part:relative)if(part==".."||part==".")throw std::runtime_error("Unsafe rollback file path");
        auto path=app/relative;safe(path);auto bytes=read(path,512ull*1024*1024);if(!rush::updates::sha256(it.value().get<std::string>())||rush::rom::hash(bytes.data(),bytes.size(),BCRYPT_SHA256_ALGORITHM)!=it.value().get<std::string>())throw std::runtime_error("The retained application has changed; rollback refused");}
    auto pointer=read(app/L"player-data-location.txt",32768);if(fs::u8path(std::string(pointer.begin(),pointer.end())).lexically_normal()!=c.data.lexically_normal())throw std::runtime_error("Rollback uses a different player-data folder");
    return app;
}
static void preflight_files(const fs::path& app){if(run(app/L"Rush2049Original.exe",L"--verify-application")!=0)throw std::runtime_error("Application files failed verification; previous application retained");}
static void preflight(const fs::path& app,const std::string& edition){if(run(app/L"Rush2049Original.exe",L"--preflight --edition "+std::wstring(edition=="original"?L"original":L"remaster"))!=0)throw std::runtime_error("Application verification failed. The previous application is retained");}
static fs::path desktop_folder(){PWSTR value=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_Desktop,0,nullptr,&value)))throw std::runtime_error("Cannot locate Desktop");fs::path result(value);CoTaskMemFree(value);return result;}
static void shortcut_at(const fs::path& app,const std::string& edition,const fs::path& folder){
    auto target=folder/fs::u8path("Rush 2049 "+std::string(edition=="original"?"Original":"Remastered")+" libultraship.lnk");
    IShellLinkW* link=nullptr;if(FAILED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link))))throw std::runtime_error("Cannot create the game shortcut");
    IPersistFile* file=nullptr;HRESULT status=link->QueryInterface(IID_PPV_ARGS(&file));
    if(SUCCEEDED(status)){
        if(fs::exists(target)){status=file->Load(target.c_str(),STGM_READ);wchar_t old[32768]{};if(FAILED(status)||FAILED(link->GetPath(old,32768,nullptr,SLGP_RAWPATH))||fs::path(old).filename()!=L"Rush2049Original.exe"){file->Release();link->Release();throw std::runtime_error("An unrelated shortcut already uses the Rush shortcut name");}}
        link->SetPath((app/L"Rush2049Original.exe").c_str());link->SetArguments(L"");link->SetWorkingDirectory(app.c_str());link->SetIconLocation((app/L"Rush2049Original.exe").c_str(),0);
        auto staging=folder/(L".rush-shortcut-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".lnk");
        if(fs::exists(staging))status=E_FAIL;else{status=file->Save(staging.c_str(),TRUE);if(SUCCEEDED(status)&&!MoveFileExW(staging.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))status=E_FAIL;}
        if(FAILED(status))DeleteFileW(staging.c_str());file->Release();
    }link->Release();if(FAILED(status))throw std::runtime_error("Could not update the game shortcut; the installation folders are retained");
}
static void preserve(const std::map<fs::path,std::string>& before,const fs::path& data){auto after=player_hashes(data);for(auto& [path,hash]:before)if(!after.contains(path)||after.at(path)!=hash)throw std::runtime_error("Player-data preservation check failed; the previous application is retained");}
static void activate_with_history(const fs::path& data,const Json& history,const fs::path& app,const std::string& edition,const Shortcut& shortcut){
    const bool existed=fs::exists(data/L"update-history.dat");Json old;if(existed)old=rush::updates::history_load(data);
    rush::updates::history_save(data,history);
    try{shortcut(app,edition);}catch(...){if(existed)rush::updates::history_save(data,old);else if(!DeleteFileW((data/L"update-history.dat").c_str()))throw std::runtime_error("Shortcut activation and rollback-history recovery failed; previous application retained");throw;}
}
static fs::path install_verified(const Context& c,const rush::updates::Release& release,const fs::path& cache,const Installer& installer,const Shortcut& shortcut){
    safe(c.app);safe(c.data);safe(cache);rush::updates::version(c.installed);rush::updates::version(release.tag);rush::updates::asset_name(c.edition,release.tag);
    Lock artifact(CreateFileW(cache.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));if(artifact.handle==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot lock the installer for verification");
    rush::updates::verify(release,read(cache,release.bytes));publisher_signature(cache);preflight_files(c.app);
    auto old=catalog(c.app,c.installed);auto before=player_hashes(c.data);
    auto destination=c.app.parent_path()/fs::u8path("Rush2049-"+c.edition+"-LUS-"+release.tag+"-"+std::to_string(GetTickCount64()));safe(destination);if(fs::exists(destination))throw std::runtime_error("Update destination already exists");
    auto raw=read(c.data/L"rom-location.txt",32768);auto rom=fs::path(rush::rom::wide(std::string(raw.begin(),raw.end())));
    if(installer(cache,destination,rom,c.data)!=0||!fs::exists(destination/L"Rush2049Original.exe"))throw std::runtime_error("Installation cancelled or incomplete; the previous application is retained");
    auto metadataRaw=read(destination/L"release.json",32768);auto metadata=Json::parse(metadataRaw.begin(),metadataRaw.end());
    if(metadata.at("schema")!=1||metadata.at("port")!="rush2049"||metadata.at("platform")!="windows-x64"||metadata.at("runtime")!="libultraship"||metadata.at("channel")!="experimental"||metadata.at("edition")!=c.edition||rush::updates::version(metadata.at("version").get<std::string>())!=rush::updates::version(release.tag))throw std::runtime_error("Installed release identity does not match the selected update; previous application retained");
    preflight(destination,c.edition);preserve(before,c.data);verify_catalog(old,c);
    auto current=catalog(destination,release.tag);
    activate_with_history(c.data,{{"schema",1},{"edition",c.edition},{"current",current},{"previous",old}},destination,c.edition,shortcut);return destination;
}
static fs::path rollback_verified(const Context& c,const Shortcut& shortcut){
    safe(c.app);safe(c.data);auto history=rush::updates::history_load(c.data);
    if(history.at("edition")!=c.edition||fs::u8path(history.at("current").at("path").get<std::string>()).lexically_normal()!=c.app.lexically_normal()||rush::updates::version(history.at("current").at("version").get<std::string>())!=rush::updates::version(c.installed))throw std::runtime_error("Rollback does not match this installed edition/version");
    auto before=player_hashes(c.data);auto previous=verify_catalog(history.at("previous"),c);if(previous==c.app)throw std::runtime_error("Rollback target is the current application");
    preflight_files(previous);preserve(before,c.data);
    auto retained=history.at("current");history["current"]=history.at("previous");history["previous"]=retained;activate_with_history(c.data,history,previous,c.edition,shortcut);
    return previous;
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try{
        int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);if(count!=7){if(args)LocalFree(args);throw std::runtime_error("Start updates from Rush's Updates tab");}
        Context c{args[1],args[2],rush::rom::utf8(args[3]),rush::rom::utf8(args[4])};fs::path request=args[5];DWORD parent=std::stoul(args[6]);LocalFree(args);
        safe(c.app);safe(c.data);safe(request);if(request.parent_path()!=c.data||!request.filename().wstring().starts_with(L"update-request-"))throw std::runtime_error("Invalid update request");
        Lock launcher(OpenProcess(SYNCHRONIZE,FALSE,parent));if(launcher.handle&&WaitForSingleObject(launcher.handle,120000)!=WAIT_OBJECT_0)throw std::runtime_error("The launcher is still running; installation cancelled");
        Session session(c.data);const auto raw=read(request,4096);auto j=Json::parse(raw.begin(),raw.end());fs::remove(request);
        auto action=j.value("action",std::string("install"));if(action!="install"&&action!="rollback")throw std::runtime_error("Unsupported update action");
        auto shortcut=[](const fs::path& app,const std::string& edition){shortcut_at(app,edition,desktop_folder());};
        fs::path destination;
        if(action=="rollback"){
            if(MessageBoxW(nullptr,L"Restore the previous verified application? Your saves and preferences stay in place. The game will not restart automatically.",L"Rush 2049 rollback",MB_OKCANCEL|MB_ICONQUESTION)!=IDOK)return 0;
            destination=rollback_verified(c,shortcut);
        }else{
            auto target=j.at("tag").get<std::string>();rush::updates::version(target);
            auto message=L"Install Rush 2049 "+rush::rom::wide(c.edition)+L" "+rush::rom::wide(target)+L"?\n\nThe previous application is retained. Your ROM and player data stay in place. Rush will not restart automatically.";
            if(MessageBoxW(nullptr,message.c_str(),L"Rush 2049 update",MB_OKCANCEL|MB_ICONQUESTION)!=IDOK)return 0;
            std::atomic_bool cancel=false;auto release=rush::updates::select(rush::updates::online(c.edition,cancel),c.installed,target);
            safe(c.data/L"update-cache");fs::create_directories(c.data/L"update-cache");auto cache=c.data/L"update-cache"/fs::u8path(release.hash+".exe");safe(cache);
            if(!fs::exists(cache)){auto bytes=rush::updates::fetch(release.url,release.bytes,cancel);rush::updates::verify(release,bytes);std::ofstream out(cache,std::ios::binary);if(!out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size()))throw std::runtime_error("Cannot save verified installer");}
            destination=install_verified(c,release,cache,[](const fs::path& exe,const fs::path& app,const fs::path& rom,const fs::path& data){return run(exe,L"--directory "+quote(app.wstring())+L" --data-dir "+quote(data.wstring())+L" --rom "+quote(rom.wstring())+L" --no-shortcut");},shortcut);
        }
        MessageBoxW(nullptr,(L"Application verified. Player data preserved.\n\nApplication folder:\n"+destination.wstring()+L"\n\nUse the Rush shortcut to play. The other application folder is retained.").c_str(),L"Rush 2049 complete",MB_OK|MB_ICONINFORMATION);CoUninitialize();return 0;
    }catch(const std::exception& e){MessageBoxW(nullptr,rush::rom::wide(e.what()).c_str(),L"Rush 2049 update � action required",MB_OK|MB_ICONERROR);CoUninitialize();return 1;}
}
