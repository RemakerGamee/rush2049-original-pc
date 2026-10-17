#pragma once
#include "rush-update-network.hpp"
#include <wincrypt.h>
#include <filesystem>
#include <fstream>
namespace rush::updates {
namespace fs=std::filesystem;
inline std::vector<uint8_t> history_bytes(const fs::path& path,size_t maximum=16*1024*1024){std::ifstream in(path,std::ios::binary|std::ios::ate);if(!in||in.tellg()<0||uint64_t(in.tellg())>maximum)throw std::runtime_error("Cannot read rollback information");std::vector<uint8_t> bytes(size_t(in.tellg()));in.seekg(0);if(!in.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("Cannot read rollback information");return bytes;}
inline void history_save(const fs::path& data,const Json& value){
    auto text=value.dump();DATA_BLOB plain{DWORD(text.size()),reinterpret_cast<BYTE*>(text.data())},encrypted{};
    if(!CryptProtectData(&plain,L"Rush 2049 verified rollback",nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&encrypted))throw std::runtime_error("Cannot protect rollback information");
    auto staging=data/(L"update-history-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".new");
    HANDLE file=CreateFileW(staging.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);DWORD written=0;
    bool good=file!=INVALID_HANDLE_VALUE&&WriteFile(file,encrypted.pbData,encrypted.cbData,&written,nullptr)&&written==encrypted.cbData&&FlushFileBuffers(file);if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);LocalFree(encrypted.pbData);
    if(!good||!MoveFileExW(staging.c_str(),(data/L"update-history.dat").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(staging.c_str());throw std::runtime_error("Cannot save verified rollback information");}
}
inline Json history_load(const fs::path& data){
    auto bytes=history_bytes(data/L"update-history.dat");DATA_BLOB encrypted{DWORD(bytes.size()),bytes.data()},plain{};
    if(!CryptUnprotectData(&encrypted,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&plain))throw std::runtime_error("Rollback information is damaged or belongs to another Windows user");
    std::vector<uint8_t> clear(plain.pbData,plain.pbData+plain.cbData);LocalFree(plain.pbData);auto value=Json::parse(clear.begin(),clear.end());if(value.at("schema")!=1)throw std::runtime_error("Unsupported rollback information");return value;
}
inline std::string previous_version(const fs::path& data,const std::string& installed,const std::string& edition,const fs::path& app={}){
    if(!fs::exists(data/L"update-history.dat"))return {};
    auto history=history_load(data);if(history.at("edition")!=edition||version(history.at("current").at("version").get<std::string>())!=version(installed))throw std::runtime_error("Rollback history does not match this installed edition/version");
    if(!app.empty()&&fs::u8path(history.at("current").at("path").get<std::string>()).lexically_normal()!=app.lexically_normal())throw std::runtime_error("Rollback belongs to another installation folder");
    auto previous=history.at("previous").at("version").get<std::string>();version(previous);return previous;
}
}
