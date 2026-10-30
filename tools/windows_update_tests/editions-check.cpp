#define wWinMain updater_entry_not_called_by_tests
#include "../windows_native/port-update-agent.cpp"
#undef wWinMain
#include "../windows_native/rush-update-service.hpp"
#include <iostream>
#include <set>
using namespace rush::updates;
static void expect(bool b){if(!b)throw std::runtime_error("Edition updater regression failed");}
template<class F>static void refuses(F action){bool denied=false;try{action();}catch(const std::exception&){denied=true;}expect(denied);}
static void file(const fs::path& path,const std::string& value){std::ofstream(path,std::ios::binary)<<value;}
static void fixture_app(const fs::path& app,const fs::path& data,const fs::path& exe,const std::string& edition,const std::string& runtime,const std::string& v){
    fs::create_directories(app);fs::copy_file(exe,app/L"Rush2049Original.exe");file(app/L"RushWindowsRT64.exe","synthetic game placeholder");file(app/L"COPYING","test fixture notice");file(app/L"player-data-location.txt",text_path(data));
    file(app/L"release.json",Json{{"schema",1},{"port","rush2049"},{"platform","windows-x64"},{"channel","experimental"},{"runtime",runtime},{"edition",edition},{"version",v}}.dump());
}
static Json release_row(const std::string& edition,const std::string& runtime){
    auto name=asset_name(edition,"v0.3.4",runtime);return {{"draft",false},{"prerelease",true},{"tag_name","v0.3.4"},{"body","plain-text notes"},{"assets",Json::array({{{"name",name},{"state","uploaded"},{"size",3},{"digest","sha256:"+std::string(64,'a')},{"browser_download_url","https://github.com/"+std::string(repository)+"/releases/download/v0.3.4/"+name}}})}};
}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=2)throw std::runtime_error("Supply the synthetic fixture launcher");CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    auto exe=fs::absolute(argv[1]);auto root=fs::temp_directory_path()/fs::path("rush-editions-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));fs::create_directory(root);
    auto data=root/L"Player Data";fs::create_directory(data);file(data/L"controller.mpk","pak bytes");file(data/L"input.cfg","controller mappings");file(data/L"display.cfg","native display choices");file(data/L"options.json","LUS options stay byte identical");file(data/L"rom-location.txt",text_path(root/L"External ROM.z64"));
    auto before=player_hashes(data);auto rom_pointer=read(data/L"rom-location.txt",32768);std::set<std::string> shortcuts;Json document=Json::array();
    for(auto edition:{"original","remastered"})for(auto runtime:{"native","libultraship"})document.push_back(release_row(edition,runtime));
    for(auto edition:{"original","remastered"})for(auto runtime:{"native","libultraship"}){
        auto rows=releases(document,edition,runtime);expect(rows.size()==1&&rows[0].url.ends_with(asset_name(edition,"v0.3.4",runtime)));expect(shortcuts.insert(shortcut_name(edition,runtime)).second);
        for(auto e:{"original","remastered"})for(auto r:{"native","libultraship"})expect(releases(Json::array({release_row(e,r)}),edition,runtime).size()==size_t(std::string(e)==edition&&std::string(r)==runtime));
        auto bad=Json::array({release_row(edition,runtime)});bad[0]["assets"][0]["digest"]="";refuses([&]{releases(bad,edition,runtime);});bad[0]["assets"][0]["digest"]="sha256:"+std::string(64,'a');bad[0]["assets"][0]["browser_download_url"]="https://evil.example/installer.exe";refuses([&]{releases(bad,edition,runtime);});
        auto area=root/fs::u8path(std::string(edition)+"-"+runtime);fs::create_directory(area);auto old=area/L"Old App";fixture_app(old,data,exe,edition,runtime,"0.3.2");Context c{old,data,"0.3.2",edition,runtime};auto bytes=read(exe,512ull*1024*1024);Release release{"v0.3.4","",rush::rom::hash(bytes.data(),bytes.size(),BCRYPT_SHA256_ALGORITHM),"Synthetic package",bytes.size()};
        verify(release,bytes);auto corrupt=bytes;corrupt[0]^=1;refuses([&]{verify(release,corrupt);});corrupt.pop_back();refuses([&]{verify(release,corrupt);});
        const auto receipt=history_path(data,edition,runtime);fs::path active=old;int activations=0;
        Shortcut activate=[&](const fs::path& app,const std::string& e){expect(e==edition);active=app;++activations;};
        Installer install=[&](const fs::path&,const fs::path& app,const fs::path& rom,const fs::path& chosen){expect(chosen==data&&rom==root/L"External ROM.z64");fixture_app(app,data,exe,edition,runtime,"0.3.4");return DWORD(0);};
        auto unchanged=[&]{expect(before==player_hashes(data));expect(read(data/L"rom-location.txt",32768)==rom_pointer);expect(fs::exists(old));};
        auto invalid=release;invalid.hash=std::string(64,'0');refuses([&]{install_verified(c,invalid,exe,install,activate);});
        refuses([&]{install_verified(c,release,exe,[](auto&,auto&,auto&,auto&){return DWORD(ERROR_CANCELLED);},activate);});
        refuses([&]{install_verified(c,release,exe,[&](auto&,const fs::path& app,auto&,auto&){fixture_app(app,data,exe,edition,std::string(runtime)=="native"?"libultraship":"native","0.3.4");return DWORD(0);},activate);});
        refuses([&]{install_verified(c,release,exe,[&](auto&,const fs::path& app,auto&,auto&){fixture_app(app,data,exe,std::string(edition)=="original"?"remastered":"original",runtime,"0.3.4");return DWORD(0);},activate);});
        refuses([&]{install_verified(c,release,exe,[&](auto&,const fs::path& app,auto&,auto&){fixture_app(app,data,exe,edition,runtime,"0.3.4");file(app/L"fail-preflight","");return DWORD(0);},activate);});
        expect(activations==0&&!fs::exists(receipt));unchanged();
        // Successful install and offline recovery use the production transactions.
        auto current=install_verified(c,release,exe,install,activate);expect(active==current&&activations==1);unchanged();Context upgraded{current,data,"0.3.4",edition,runtime};
        expect(previous_version(data,"0.3.4",edition,current,runtime)=="0.3.2");
        auto receipt_bytes=read(receipt,16*1024*1024);auto damaged=receipt_bytes;damaged[damaged.size()/2]^=0x55;{std::ofstream out(receipt,std::ios::binary);out.write(reinterpret_cast<const char*>(damaged.data()),damaged.size());}refuses([&]{rollback_verified(upgraded,activate);});{std::ofstream out(receipt,std::ios::binary);out.write(reinterpret_cast<const char*>(receipt_bytes.data()),receipt_bytes.size());}
        file(old/L"COPYING","changed old application");refuses([&]{rollback_verified(upgraded,activate);});file(old/L"COPYING","test fixture notice");
        refuses([&]{rollback_verified(upgraded,[](auto&,auto&){throw std::runtime_error("Injected shortcut error");});});expect(history_load(data,edition,runtime).at("current").at("path")==text_path(current));
        // A broken current game must not prevent recovery of a verified old app.
        file(current/L"RushWindowsRT64.exe","damaged current game");fs::remove(current/L"release.json");auto restored=rollback_verified(upgraded,activate);expect(restored==old&&active==old&&fs::exists(current));unchanged();
        std::cout<<"PASS synthetic transactions: "<<edition<<" / "<<runtime<<"\n";
    }
    expect(shortcuts.size()==4);refuses([]{releases(Json::array(),"original","unknown");});refuses([]{asset_name("other","v0.3.4","native");});
    // Native cannot consume a legacy LUS receipt; legacy LUS reads remain supported.
    Json legacy{{"schema",1},{"edition","original"},{"current",{{"version","0.3.3"}}},{"previous",{{"version","0.3.2"}}}};auto legacyData=root/L"Legacy Data";fs::create_directory(legacyData);history_save(legacyData,legacy);fs::rename(history_path(legacyData,"original","libultraship"),legacyData/L"update-history.dat");expect(previous_version(legacyData,"0.3.3","original")=="0.3.2");expect(previous_version(legacyData,"0.3.3","original",{},"native").empty());refuses([&]{history_load(legacyData,"remastered","libultraship");});
    std::atomic_bool cancel=true;refuses([&]{fetch(api,1024,cancel);});
    fs::remove_all(root);CoUninitialize();std::cout<<"PASS: edition/runtime isolation, SHA-256, cancellation, failure recovery, offline rollback, player preservation and legacy receipts\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
