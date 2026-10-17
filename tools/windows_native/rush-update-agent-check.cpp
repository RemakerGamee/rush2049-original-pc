#define wWinMain updater_entry_not_called_by_tests
#include "port-update-agent.cpp"
#undef wWinMain
#include <thread>
#include <future>
#include <iostream>
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>static void refuses(const char* name,F action){bool failed=false;try{action();}catch(const std::exception& e){failed=true;std::cout<<"Refused "<<name<<": "<<e.what()<<'\n';}require(failed,name);}
static fs::path shortcut_target(const fs::path& folder,const std::string& edition){auto path=folder/fs::u8path("Rush 2049 "+std::string(edition=="original"?"Original":"Remastered")+" libultraship.lnk");IShellLinkW* link=nullptr;IPersistFile* file=nullptr;if(FAILED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link)))||FAILED(link->QueryInterface(IID_PPV_ARGS(&file))))throw std::runtime_error("Cannot read test shortcut");auto status=file->Load(path.c_str(),STGM_READ);wchar_t target[32768]{};if(SUCCEEDED(status))status=link->GetPath(target,32768,nullptr,SLGP_RAWPATH);file->Release();link->Release();if(FAILED(status))throw std::runtime_error("Cannot read test shortcut target");return target;}
static void cycle(const fs::path& initialSetup,const fs::path& futureSetup,const fs::path& root,const std::string& edition,const fs::path& sourceRom){
    require(root.is_absolute()&&!fs::exists(root),"Cycle test requires a new absolute test folder");fs::create_directories(root);auto data=root/L"Player Data",old=root/L"App-0.3.3",external=root/L"External ROMs";fs::create_directory(external);auto rom=external/L"My Rush.z64";fs::copy_file(sourceRom,rom);
    auto cli=[](const fs::path& exe,const fs::path& app,const fs::path& rom,const fs::path& data){return run(exe,L"--install --directory "+quote(app.wstring())+L" --data-dir "+quote(data.wstring())+L" --rom "+quote(rom.wstring())+L" --no-shortcut");};
    require(cli(initialSetup,old,rom,data)==0,"Initial installation failed");
    {std::ofstream(data/L"options.json")<<R"({"schema":5,"updateAutoCheck":true,"windowWidth":1280,"windowHeight":720,"windowSize":6,"fillFullscreen":true})";std::ofstream(data/L"user-save.fixture",std::ios::binary)<<"SCOTT save bytes: \x00 \x01 preserved";}
    auto originalRom=read(rom,20*1024*1024),pak=read(data/L"controller.mpk",32768),options=read(data/L"options.json",32768),inputs=read(data/L"input.cfg",1024*1024),save=read(data/L"user-save.fixture",32768);
    auto unchanged=[&]{require(read(data/L"controller.mpk",32768)==pak&&read(data/L"options.json",32768)==options&&read(data/L"input.cfg",1024*1024)==inputs&&read(data/L"user-save.fixture",32768)==save,"Player bytes changed");};
    Context c{old,data,"0.3.3",edition};auto bytes=read(futureSetup,512ull*1024*1024);rush::updates::Release release{"v0.3.4","",rush::rom::hash(bytes.data(),bytes.size(),BCRYPT_SHA256_ALGORITHM),"Private test fixture",uint64_t(bytes.size())};auto cache=root/L"verified-setup.exe";fs::copy_file(futureSetup,cache);
    fs::create_directory(root/L"Shortcuts");int activations=0;Shortcut activate=[&](const fs::path& app,const std::string& e){shortcut_at(app,e,root/L"Shortcuts");++activations;};
    {Session stopped(data);
        auto bad=release;bad.hash=std::string(64,'0');refuses("corrupt checksum",[&]{install_verified(c,bad,cache,cli,activate);});unchanged();require(activations==0&&!fs::exists(data/L"update-history.dat"),"Failed download activated an application");
        refuses("cancelled installation",[&]{install_verified(c,release,cache,[](auto&,auto&,auto&,auto&){return DWORD(ERROR_CANCELLED);},activate);});unchanged();require(activations==0&&!fs::exists(data/L"update-history.dat"),"Cancelled install wrote rollback state");
        refuses("mismatched installed edition",[&]{install_verified(c,release,cache,[&](const fs::path& exe,const fs::path& app,const fs::path& selectedRom,const fs::path& selectedData){auto code=cli(exe,app,selectedRom,selectedData);if(!code){auto raw=read(app/L"release.json",32768);auto metadata=Json::parse(raw.begin(),raw.end());metadata["edition"]=edition=="original"?"remastered":"original";std::ofstream(app/L"release.json")<<metadata.dump();}return code;},activate);});unchanged();require(activations==0&&!fs::exists(data/L"update-history.dat"),"Mismatched edition activated an application");
        // Move the external ROM: setup may explicitly repair its pointer, never the ROM bytes.
        auto moved=external/L"Moved Rush.z64";fs::rename(rom,moved);
        auto installed=install_verified(c,release,cache,[&](const fs::path& exe,const fs::path& app,const fs::path&,const fs::path& selectedData){return cli(exe,app,moved,selectedData);},activate);
        unchanged();require(read(moved,20*1024*1024)==originalRom,"ROM was changed");require(shortcut_target(root/L"Shortcuts",edition).parent_path()==installed,"Update shortcut did not target the verified new version");
        auto history=rush::updates::history_load(data);require(history.at("current").at("version")=="v0.3.4"&&history.at("previous").at("version")=="0.3.3","Incorrect update history");
        Context upgraded{installed,data,"0.3.4",edition};auto protectedBytes=read(data/L"update-history.dat",16*1024*1024);auto tampered=protectedBytes;tampered[tampered.size()/2]^=0x55;{std::ofstream out(data/L"update-history.dat",std::ios::binary);out.write(reinterpret_cast<const char*>(tampered.data()),tampered.size());}
        refuses("tampered rollback receipt",[&]{rollback_verified(upgraded,activate);});{std::ofstream out(data/L"update-history.dat",std::ios::binary);out.write(reinterpret_cast<const char*>(protectedBytes.data()),protectedBytes.size());}unchanged();
        auto originalLicense=read(old/L"COPYING",1024*1024);{std::ofstream(old/L"COPYING",std::ios::binary|std::ios::app)<<"tamper";}
        refuses("modified retained application",[&]{rollback_verified(upgraded,activate);});{std::ofstream out(old/L"COPYING",std::ios::binary);out.write(reinterpret_cast<const char*>(originalLicense.data()),originalLicense.size());}unchanged();
        refuses("rollback shortcut failure",[&]{rollback_verified(upgraded,[](auto&,auto&){throw std::runtime_error("Injected shortcut failure");});});
        require(rush::updates::history_load(data).at("current").at("version")=="v0.3.4"&&shortcut_target(root/L"Shortcuts",edition).parent_path()==installed,"Failed activation changed rollback state");unchanged();
        require(run(installed/L"Rush2049Original.exe",L"--verify-rollback-helper")==0,"Installed recovery helper is unavailable");
        auto runtimeBytes=read(installed/L"RushWindowsRT64.exe",128*1024*1024);{std::ofstream(installed/L"RushWindowsRT64.exe",std::ios::binary|std::ios::app)<<"broken-update-fixture";}
        require(run(installed/L"Rush2049Original.exe",L"--preflight")==65,"Damaged new application was allowed to launch");
        require(run(installed/L"Rush2049Original.exe",L"--verify-rollback-helper")==0,"Damaged game prevented safe recovery-helper access");
        auto rolledBack=rollback_verified(upgraded,activate);
        {std::ofstream out(installed/L"RushWindowsRT64.exe",std::ios::binary);out.write(reinterpret_cast<const char*>(runtimeBytes.data()),runtimeBytes.size());}require(rolledBack==old&&shortcut_target(root/L"Shortcuts",edition).parent_path()==old,"Offline rollback did not restore old application and shortcut");unchanged();require(fs::is_directory(installed)&&fs::is_directory(old),"An application version was removed");
        require(rush::updates::previous_version(data,"0.3.3",edition)=="v0.3.4","Rollback history was not swapped");
        preflight(old,edition);preflight(installed,edition);
        Json report{{"edition",edition},{"upgrade","0.3.3 -> 0.3.4"},{"rollback","0.3.4 -> 0.3.3"},{"old_app",text_path(old)},{"new_app",text_path(installed)},{"data",text_path(data)},{"real_installers",true},{"same_production_transaction",true},{"offline_rollback",true},{"broken_new_app_recovered",true},{"shortcut_failure_preserved_history",true},{"verified_recovery_helper_available",true},{"corrupt_download_refused",true},{"cancellation_preserved_state",true},{"mismatched_edition_refused",true},{"tampered_history_refused",true},{"modified_previous_app_refused",true},{"moved_rom_repaired",true},{"player_files_identical",true},{"both_versions_retained",true},{"shortcut_switches_verified",true},{"game_auto_restarted",false}};
        std::ofstream(root/L"cycle-result.json")<<report.dump(2)<<'\n';std::cout<<report.dump()<<'\n';
    }
}
int wmain(int argc,wchar_t** argv){CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);try{
    auto data=fs::temp_directory_path()/fs::path("rush-agent-check-"+std::to_string(GetCurrentProcessId()));fs::create_directory(data);
    {std::ofstream(data/L"controller.mpk",std::ios::binary)<<"private Pak fixture";std::ofstream(data/L"options.json")<<"preserve options";}
    auto before=player_hashes(data);if(before.size()!=2)throw std::runtime_error("Player snapshot incomplete");
    std::promise<void> held,release;auto done=release.get_future();
    std::thread owner([&]{HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\Rush2049-Native-Session");auto status=WaitForSingleObject(mutex,0);held.set_value();done.wait();if(status==WAIT_OBJECT_0||status==WAIT_ABANDONED)ReleaseMutex(mutex);CloseHandle(mutex);});
    held.get_future().wait();bool denied=false;try{Session session(data);}catch(...){denied=true;}release.set_value();owner.join();if(!denied)throw std::runtime_error("Running-game refusal failed");
    if(before!=player_hashes(data))throw std::runtime_error("Refused install modified player data");
    {Session session(data);if(before!=player_hashes(data))throw std::runtime_error("Lock modified player data");}
    for(auto p:{data/L"controller.mpk",data/L"options.json",data/L"session.lock"})fs::remove(p);fs::remove(data);
    std::cout<<"PASS: stopped-game locks, running-game refusal and byte-for-byte player data preservation\n";
    if(argc==7&&std::wstring(argv[1])==L"--cycle")cycle(fs::path(argv[2]),fs::path(argv[3]),fs::path(argv[4]),rush::rom::utf8(argv[5]),fs::path(argv[6]));CoUninitialize();return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
