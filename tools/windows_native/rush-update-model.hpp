#pragma once
#include "nlohmann/json.hpp"
#include "rush-release-metadata.hpp"
#include <array>
#include <string>
#include <vector>
#include <stdexcept>
#include <cctype>
namespace rush::updates {
using Json=nlohmann::json;
inline constexpr char repository[]="RemakerGamee/rush2049-original-pc";
inline constexpr char api[]="https://api.github.com/repos/RemakerGamee/rush2049-original-pc/releases?per_page=100";
inline constexpr auto discord=rush::release::discord;
inline constexpr auto email=rush::release::email;
inline std::array<unsigned,3> version(std::string s){
    if(!s.empty()&&s[0]=='v')s.erase(0,1);
    std::array<unsigned,3> result{};size_t p=0;
    for(int i=0;i<3;++i){size_t start=p;while(p<s.size()&&std::isdigit(static_cast<unsigned char>(s[p])))++p;
        if(p==start||p-start>5)throw std::runtime_error("Unsupported release version");
        result[i]=unsigned(std::stoul(s.substr(start,p-start)));
        if(i<2){if(p>=s.size()||s[p++]!='.')throw std::runtime_error("Unsupported release version");}}
    if(p!=s.size())throw std::runtime_error("Unsupported release version");return result;
}
inline bool sha256(const std::string& s){return s.size()==64&&s.find_first_not_of("0123456789abcdef")==std::string::npos;}
inline std::string asset_name(const std::string& edition,const std::string& tag){
    version(tag);
    if(edition!="original"&&edition!="remastered")throw std::runtime_error("Unsupported visual edition");
    return "Rush2049-"+std::string(edition=="original"?"Original":"Remastered")+"-Libultraship-PC-"+tag+"-Setup.exe";
}
struct Release{std::string tag,url,hash,notes;uint64_t bytes=0;};
inline std::vector<Release> releases(const Json& document,const std::string& edition){
    if(!document.is_array()||document.size()>100)throw std::runtime_error("Unsupported release response");
    std::vector<Release> result;
    for(const auto& row:document){
        if(row.value("draft",true))continue;
        const auto tag=row.at("tag_name").get<std::string>();
        try{version(tag);}catch(...){continue;}
        // Experimental is the approved Rush channel. Never switch to another product or edition.
        if(!row.value("prerelease",false))continue;
        const auto expected=asset_name(edition,tag);
        for(const auto& a:row.at("assets"))if(a.value("name","")==expected){
            const auto url=a.at("browser_download_url").get<std::string>();
            const auto origin="https://github.com/"+std::string(repository)+"/releases/download/"+tag+"/"+expected;
            const auto digest=a.value("digest","");
            const auto bytes=a.at("size").get<int64_t>();
            if(url!=origin||digest.rfind("sha256:",0)!=0||!sha256(digest.substr(7))||bytes<=0||bytes>512ll*1024*1024)
                throw std::runtime_error("Release identity or published SHA-256 is invalid; installation refused");
            if(!a.value("state","").empty()&&a.value("state","")!="uploaded")throw std::runtime_error("Incomplete release artifact");
            auto notes=row.value("body",std::string{});if(notes.size()>32768)notes.resize(32768);
            result.push_back({tag,url,digest.substr(7),notes,uint64_t(bytes)});
        }
    }
    return result;
}
inline Release select(const std::vector<Release>& rows,const std::string& installed,const std::string& requested={}){
    version(installed);Release selected;
    for(const auto& row:rows){
        if(!requested.empty()){if(row.tag==requested)return row;}
        else if(version(row.tag)>version(installed)&&(selected.tag.empty()||version(row.tag)>version(selected.tag)))selected=row;
    }
    if(!requested.empty())throw std::runtime_error("The requested version has no approved compatible download");
    return selected;
}
inline std::string mailto(const std::string& installed,const std::string& edition){
    const std::string text="Rush 2049 "+edition+" "+installed+" support";std::string encoded;
    const char* hex="0123456789ABCDEF";
    for(unsigned char c:text){if(std::isalnum(c)||c=='-'||c=='.')encoded+=char(c);else{encoded+='%';encoded+=hex[c>>4];encoded+=hex[c&15];}}
    return std::string("mailto:")+email+"?subject="+encoded;
}
}
