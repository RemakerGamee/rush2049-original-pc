#include "../windows_native/rush-update-network.hpp"
#include <iostream>
using namespace rush::updates;
static void expect(bool b){if(!b)throw std::runtime_error("Live update transport check failed");}
template<class F>static void refuses(F f){bool denied=false;try{f();}catch(const std::exception&){denied=true;}expect(denied);}
int main(){try{
    std::atomic_bool cancel=false;auto document_bytes=fetch(api,4*1024*1024,cancel);auto document=Json::parse(document_bytes.begin(),document_bytes.end());
    for(auto e:{"original","remastered"})for(auto r:{"native","libultraship"}){
        auto release=select(releases(document,e,r),"0.0.0");expect(!release.tag.empty());std::cout<<"Verified metadata: "<<e<<" / "<<r<<" "<<release.tag<<'\n';
    }
    // One representative native package; verified in memory, never saved or run.
    auto release=select(releases(document,"original","native"),"0.0.0");auto bytes=fetch(release.url,release.bytes,cancel);verify(release,bytes);bytes[bytes.size()/2]^=1;refuses([&]{verify(release,bytes);});
    uint64_t partial=0;refuses([&]{fetch(release.url,release.bytes,cancel,[&](uint64_t n){partial=n;cancel=true;});});expect(partial>0&&cancel);cancel=false;
    refuses([&]{fetch(api,16,cancel);});
    std::cout<<"PASS: live SHA-256 download, tampering, in-transfer cancellation and response-size refusal; no execution\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
