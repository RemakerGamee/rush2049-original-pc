#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdint>

namespace rush::rom {
inline constexpr size_t size=0xC00000;
inline constexpr char sha1[]="3f99351d7bb61656614bdb2aa1a90cfe55d1922c";
inline std::string hash(const uint8_t* bytes,size_t count,const wchar_t* algorithm) {
    BCRYPT_ALG_HANDLE provider=nullptr;
    if(BCryptOpenAlgorithmProvider(&provider,algorithm,nullptr,0)<0)throw std::runtime_error("Windows checksum service is unavailable.");
    DWORD length=0,used=0;
    if(BCryptGetProperty(provider,BCRYPT_HASH_LENGTH,reinterpret_cast<PUCHAR>(&length),sizeof(length),&used,0)<0) {
        BCryptCloseAlgorithmProvider(provider,0);throw std::runtime_error("Cannot prepare checksum.");
    }
    std::vector<uint8_t> digest(length);
    const auto status=BCryptHash(provider,nullptr,0,const_cast<PUCHAR>(bytes),static_cast<ULONG>(count),digest.data(),length);
    BCryptCloseAlgorithmProvider(provider,0);
    if(status<0)throw std::runtime_error("Cannot calculate checksum.");
    std::string result;result.reserve(length*2);
    for(auto b:digest){result+="0123456789abcdef"[b>>4];result+="0123456789abcdef"[b&15];}
    return result;
}
inline void normalize(std::vector<uint8_t>& bytes) {
    if(bytes.size()!=size)throw std::runtime_error("Select the original US 12 MiB Rush 2049 N64 ROM.");
    if(bytes[0]==0x37 && bytes[1]==0x80 && bytes[2]==0x40 && bytes[3]==0x12) {
        for(size_t i=0;i<bytes.size();i+=2)std::swap(bytes[i],bytes[i+1]);
    } else if(bytes[0]==0x40 && bytes[1]==0x12 && bytes[2]==0x37 && bytes[3]==0x80) {
        for(size_t i=0;i<bytes.size();i+=4){std::swap(bytes[i],bytes[i+3]);std::swap(bytes[i+1],bytes[i+2]);}
    }
    if(hash(bytes.data(),bytes.size(),BCRYPT_SHA1_ALGORITHM)!=sha1)
        throw std::runtime_error("This ROM is unsupported or has been modified. Select the original US release.");
}
class File {
    HANDLE handle=INVALID_HANDLE_VALUE;
public:
    std::vector<uint8_t> bytes;
    explicit File(const std::filesystem::path& path) {
        // Hold a read-only sharing lock until launch ends. Never change the ROM.
        handle=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error("The saved ROM is missing or unreadable. Select it again.");
        try {
            LARGE_INTEGER length{};DWORD read=0;
            if(GetFileType(handle)!=FILE_TYPE_DISK || !GetFileSizeEx(handle,&length) || length.QuadPart!=size)
                throw std::runtime_error("Select the original US 12 MiB Rush 2049 N64 ROM.");
            bytes.resize(size);
            if(!ReadFile(handle,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr) || read!=size)
                throw std::runtime_error("The selected ROM could not be read completely.");
            normalize(bytes);
        } catch(...) {CloseHandle(handle);handle=INVALID_HANDLE_VALUE;throw;}
    }
    File(const File&)=delete;File& operator=(const File&)=delete;
    ~File(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
};
inline bool guest_image(const uint8_t* ram,size_t offset,size_t count,const char* digest) {
    std::vector<uint8_t> bytes(count);
    for(size_t i=0;i<count;++i)bytes[i]=ram[(offset+i)^3];
    return hash(bytes.data(),bytes.size(),BCRYPT_SHA256_ALGORITHM)==digest;
}
inline std::string utf8(const std::wstring& value) {
    if(value.empty())return {};
    const int count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
    if(!count)throw std::runtime_error("Invalid text in file path.");
    std::string result(count,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),count,nullptr,nullptr);return result;
}
inline std::wstring wide(const std::string& value) {
    if(value.empty())return {};
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
    if(!count)throw std::runtime_error("Invalid saved file path.");
    std::wstring result(count,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),count);return result;
}
}
