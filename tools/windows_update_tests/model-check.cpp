#include "../windows_native/rush-update-model.hpp"
#include <fstream>
#include <iostream>
#include <set>
using namespace rush::updates;
static void expect(bool b){if(!b)throw std::runtime_error("Release identity regression failed");}
template<class F>static void refuses(F f){bool denied=false;try{f();}catch(const std::exception&){denied=true;}expect(denied);}
static Json fixture(const std::string& edition,const std::string& runtime){
    auto name=asset_name(edition,"v0.3.4",runtime);
    return Json::array({{{"draft",false},{"prerelease",true},{"tag_name","v0.3.4"},{"body","Plain text %s <script>"},{"assets",Json::array({{{"name",name},{"state","uploaded"},{"size",3},{"digest","sha256:"+std::string(64,'a')},{"browser_download_url","https://github.com/"+std::string(repository)+"/releases/download/v0.3.4/"+name}}})}}});
}
int main(int argc,char** argv){try{
    Json all=Json::array();std::set<std::string> names,shortcuts;
    for(auto e:{"original","remastered"})for(auto r:{"native","libultraship"}){
        auto document=fixture(e,r);all.push_back(document[0]);expect(names.insert(asset_name(e,"v0.3.4",r)).second);expect(shortcuts.insert(shortcut_name(e,r)).second);
        for(auto other:{"original","remastered"})for(auto interface:{"native","libultraship"})expect(releases(document,other,interface).size()==size_t(std::string(e)==other&&std::string(r)==interface));
        auto rows=releases(document,e,r);expect(select(rows,"0.3.3").tag=="v0.3.4");expect(select(rows,"0.3.4").tag.empty());expect(select(rows,"0.3.5").tag.empty());
        auto bad=document;bad[0]["draft"]=true;expect(releases(bad,e,r).empty());bad=document;bad[0]["prerelease"]=false;expect(releases(bad,e,r).empty());
        for(auto field:{"digest","browser_download_url","size","state"}){bad=document;if(std::string(field)=="size")bad[0]["assets"][0][field]=-1;else bad[0]["assets"][0][field]="invalid";refuses([&]{releases(bad,e,r);});}
        bad=document;bad[0]["assets"][0]["size"]=512ll*1024*1024+1;refuses([&]{releases(bad,e,r);});
        bad=document;bad[0]["assets"][0].erase("digest");refuses([&]{releases(bad,e,r);});
    }
    for(auto e:{"original","remastered"})for(auto r:{"native","libultraship"})expect(releases(all,e,r).size()==1);
    refuses([]{releases(Json::array(),"original","other");});refuses([]{asset_name("other","v0.3.4","native");});
    for(auto tag:{"v0.3.4/../../evil","v0.3","v0.3.4-beta","v0.3.999999"})refuses([&]{asset_name("original",tag,"native");});
    if(argc==2){std::ifstream in(argv[1]);Json live;in>>live;for(auto e:{"original","remastered"})for(auto r:{"native","libultraship"}){auto selected=select(releases(live,e,r),"0.0.0");std::cout<<e<<" / "<<r<<": "<<selected.tag<<'\n';}}
    std::cout<<"PASS: four independent release identities, 16 cross-edition/interface selections, version selection, digests, origin and channel refusal\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
