#pragma once
#include "rush-update-model.hpp"
#include "rom_identity.hpp"
#include <winhttp.h>
#include <atomic>
#include <functional>
namespace rush::updates {
struct Internet{HINTERNET h=nullptr;explicit Internet(HINTERNET value):h(value){if(!h)throw std::runtime_error("Update connection failed");}~Internet(){if(h)WinHttpCloseHandle(h);}Internet(const Internet&)=delete;};
inline std::vector<uint8_t> fetch(std::string url,uint64_t limit,std::atomic_bool& cancel,const std::function<void(uint64_t)>& progress={}){
    Internet session(WinHttpOpen(L"Rush2049-Updates/1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    WinHttpSetTimeouts(session.h,5000,5000,5000,5000);
    for(int hop=0;hop<6;++hop){
        if(cancel)throw std::runtime_error("Update cancelled");
        auto wide=rush::rom::wide(url);URL_COMPONENTS parts{};parts.dwStructSize=sizeof(parts);parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=parts.dwUserNameLength=parts.dwPasswordLength=DWORD(-1);
        if(!WinHttpCrackUrl(wide.c_str(),0,0,&parts)||parts.nScheme!=INTERNET_SCHEME_HTTPS||parts.nPort!=443||parts.dwUserNameLength||parts.dwPasswordLength)throw std::runtime_error("Untrusted update URL");
        std::wstring host(parts.lpszHostName,parts.dwHostNameLength);
        if(host!=L"api.github.com"&&host!=L"github.com"&&host!=L"release-assets.githubusercontent.com"&&host!=L"objects.githubusercontent.com")throw std::runtime_error("Untrusted update origin");
        std::wstring path(parts.lpszUrlPath,parts.dwUrlPathLength);if(parts.dwExtraInfoLength)path.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);
        Internet connection(WinHttpConnect(session.h,host.c_str(),443,0));
        Internet request(WinHttpOpenRequest(connection.h,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE));
        DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;WinHttpSetOption(request.h,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy));
        if(!WinHttpSendRequest(request.h,L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n",DWORD(-1),WINHTTP_NO_REQUEST_DATA,0,0,0)||!WinHttpReceiveResponse(request.h,nullptr))throw std::runtime_error("Cannot reach GitHub. Check your connection and try again");
        DWORD code=0,size=sizeof(code);if(!WinHttpQueryHeaders(request.h,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&code,&size,WINHTTP_NO_HEADER_INDEX))throw std::runtime_error("Invalid update response");
        if(code==301||code==302||code==303||code==307||code==308){
            DWORD count=0;WinHttpQueryHeaders(request.h,WINHTTP_QUERY_LOCATION,WINHTTP_HEADER_NAME_BY_INDEX,nullptr,&count,WINHTTP_NO_HEADER_INDEX);
            if(count<2||count>32768)throw std::runtime_error("Invalid download redirect");
            std::wstring next(count/sizeof(wchar_t),L'\0');if(!WinHttpQueryHeaders(request.h,WINHTTP_QUERY_LOCATION,WINHTTP_HEADER_NAME_BY_INDEX,next.data(),&count,WINHTTP_NO_HEADER_INDEX))throw std::runtime_error("Invalid download redirect");
            next.resize(wcslen(next.c_str()));url=rush::rom::utf8(next);continue;
        }
        if(code!=200)throw std::runtime_error("GitHub update check returned HTTP "+std::to_string(code)+". Try again later");
        std::vector<uint8_t> bytes;uint8_t buffer[65536];DWORD read=0;
        while(true){if(cancel)throw std::runtime_error("Update cancelled");if(!WinHttpReadData(request.h,buffer,sizeof(buffer),&read))throw std::runtime_error("Update download interrupted");if(!read)break;
            if(bytes.size()+read>limit)throw std::runtime_error("Update download exceeds its declared size");bytes.insert(bytes.end(),buffer,buffer+read);if(progress)progress(bytes.size());}
        return bytes;
    }
    throw std::runtime_error("Too many download redirects");
}
inline std::vector<Release> online(const std::string& edition,std::atomic_bool& cancel){const auto data=fetch(api,4*1024*1024,cancel);return releases(Json::parse(data.begin(),data.end()),edition);}
inline void verify(const Release& release,const std::vector<uint8_t>& bytes){if(bytes.size()!=release.bytes||rush::rom::hash(bytes.data(),bytes.size(),BCRYPT_SHA256_ALGORITHM)!=release.hash)throw std::runtime_error("Update checksum mismatch or incomplete download; installation refused");}
}
