#include "rush-update-service.hpp"
#include "lus-imgui-store.hpp"
#include <iostream>
using namespace rush::updates;
static void expect(bool b){if(!b)throw std::runtime_error("Update regression check failed");}
template<class F>static void refuses(F f){bool denied=false;try{f();}catch(const std::exception&){denied=true;}expect(denied);}
static Json fixture(std::string edition="original",std::string tag="v0.3.4"){
    auto name=asset_name(edition,tag);return Json::array({{{"draft",false},{"prerelease",true},{"tag_name",tag},{"body","Release notes %s <script> must stay plain text"},{"assets",Json::array({{{"name",name},{"state","uploaded"},{"size",3},{"digest","sha256:"+std::string(64,'a')},{"browser_download_url","https://github.com/"+std::string(repository)+"/releases/download/"+tag+"/"+name}}})}}});
}
int main(int argc,char** argv){try{
    auto rows=releases(fixture(),"original");expect(select(rows,"0.3.3").tag=="v0.3.4");expect(select(rows,"0.3.4").tag.empty());expect(select(rows,"0.3.5").tag.empty());expect(select(rows,"0.3.5","v0.3.4").tag=="v0.3.4");
    expect(releases(fixture("remastered"),"original").empty());expect(releases(fixture(),"remastered").empty());
    for(auto bad:{"v0.3.4/../../evil","v0.3","0.3.4.exe","0.3.4-beta","0.3.999999"})refuses([&]{version(bad);});
    for(auto field:{"digest","browser_download_url","size"}){auto bad=fixture();if(std::string(field)=="size")bad[0]["assets"][0][field]=0;else bad[0]["assets"][0][field]="https://evil.example/installer.exe";refuses([&]{releases(bad,"original");});}
    refuses([&]{select(rows,"0.3.3","v0.3.2");});refuses([&]{releases(Json::object(),"original");});
    Release checksum{"v0.3.4","",rush::rom::hash(reinterpret_cast<const uint8_t*>("abc"),3,BCRYPT_SHA256_ALGORITHM),"",3};verify(checksum,{'a','b','c'});refuses([&]{verify(checksum,{'a','b'});});refuses([&]{verify(checksum,{'a','b','d'});});
    std::atomic_bool cancel=true;refuses([&]{fetch(api,1024,cancel);});cancel=false;
    for(auto url:{"http://github.com/a","https://evil.example/a","https://user:pass@github.com/a","https://github.com:444/a"})refuses([&]{fetch(url,1024,cancel);});
    auto mail=mailto("0.3.3","remastered libultraship");expect(mail.find(email)!=std::string::npos&&mail.find("%20")!=std::string::npos&&mail.find("attachment")==std::string::npos);
    auto folder=std::filesystem::temp_directory_path()/std::filesystem::path("rush-update-check-"+std::to_string(GetCurrentProcessId()));std::filesystem::create_directory(folder);
    auto file=folder/"options.json";rush::options::Store options(file);expect(!options.update_auto_check);options.update_auto_check=true;options.save();expect(rush::options::Store(file).update_auto_check);options.update_auto_check=false;options.save();expect(!rush::options::Store(file).update_auto_check);std::filesystem::remove(file);std::filesystem::remove(folder);
    if(argc>1){auto approved=online("original",cancel);expect(!approved.empty());auto remastered=online("remastered",cancel);expect(!remastered.empty());std::cout<<"Online metadata: "<<approved.size()<<" Original, "<<remastered.size()<<" Remastered releases; correct edition and published SHA-256 verified\n";
        if(std::string(argv[1])=="--download-check"){
            auto artifact=select(approved,"0.3.1");expect(!artifact.tag.empty());
            auto bytes=fetch(artifact.url,artifact.bytes,cancel);verify(artifact,bytes);
            std::cout<<"PASS: real release downloaded and SHA-256 verified in memory; no execution\n";
            refuses([&]{fetch(artifact.url,artifact.bytes,cancel,[&](uint64_t){cancel=true;});});cancel=false;
            refuses([&]{fetch(api,16,cancel);});
            std::cout<<"PASS: cancellation during a live download and oversized response refusal\n";
        }
        if(std::string(argv[1])=="--service-check"){
            auto temporary=std::filesystem::temp_directory_path()/std::filesystem::path("rush-service-"+std::to_string(GetCurrentProcessId()));std::filesystem::create_directory(temporary);
            auto config=temporary/"options.json",request=temporary/"update-request-test.json";
            _putenv_s("RUSH_OPTIONS_CONFIG",config.string().c_str());_putenv_s("RUSH_RELEASE_VERSION","0.3.1");_putenv_s("RUSH_RELEASE_EDITION","original");_putenv_s("RUSH_UPDATE_REQUEST",request.string().c_str());
            {Service service;auto wait=[&]{for(int n=0;n<3000&&service.snapshot().busy;++n)std::this_thread::sleep_for(std::chrono::milliseconds(10));auto s=service.snapshot();if(s.busy)throw std::runtime_error("Background updater timed out");return s;};
                service.check();auto found=wait();expect(found.release.tag=="v0.3.2");service.download();auto downloaded=wait();if(!downloaded.ready)throw std::runtime_error(downloaded.message);
                auto cache=temporary/"update-cache"/(downloaded.release.hash+".exe");{std::ofstream corrupt(cache,std::ios::binary|std::ios::trunc);corrupt<<"interrupted fixture";}
                service.download();auto repaired=wait();if(!repaired.ready)throw std::runtime_error(repaired.message);expect(std::filesystem::file_size(cache)==repaired.release.bytes);
                service.request_install();std::ifstream in(request);Json ticket;in>>ticket;expect(ticket.at("tag")=="v0.3.2");refuses([&]{service.request_install();});
            }
            std::filesystem::remove_all(temporary);
            std::cout<<"PASS: background check/download, corrupt cache recovery, verified install ticket and duplicate request refusal; no installer execution\n";
        }
    }
    std::cout<<"PASS: version/edition/channel selection, rollback selection, malformed metadata, trusted origins, checksums, interruption/cancellation, opt-in persistence and editable email\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
