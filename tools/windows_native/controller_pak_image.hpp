#pragma once
#include "pak_storage.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <Windows.h>
namespace rush_pak {
class Image : public Storage {
    std::array<uint8_t,32768> bytes{};
    std::filesystem::path path;
    HANDLE lock=INVALID_HANDLE_VALUE;
public:
    explicit Image(const std::string& filename) : path(std::filesystem::weakly_canonical(filename)) {
        auto lockpath=path.wstring()+L".lock";
        lock=CreateFileW(lockpath.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(lock==INVALID_HANDLE_VALUE) throw std::runtime_error("Controller Pak lock unavailable");
        try {
            if(!std::filesystem::is_regular_file(path)||std::filesystem::file_size(path)!=bytes.size())
                throw std::runtime_error("Controller Pak must be an existing 32768-byte regular file");
            std::ifstream in(path,std::ios::binary);
            in.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
            if(!in) throw std::runtime_error("Controller Pak read failed");
        } catch(...) { CloseHandle(lock); lock=INVALID_HANDLE_VALUE; throw; }
    }
    Image(const Image&)=delete;
    Image& operator=(const Image&)=delete;
    ~Image() { if(lock!=INVALID_HANDLE_VALUE) CloseHandle(lock); }
    bool read_block(uint16_t address,uint8_t* data) const override {
        if((address&31)||address>bytes.size()-32) return false;
        std::memcpy(data,bytes.data()+address,32); return true;
    }
    bool write_block(uint16_t address,const uint8_t* data) override {
        if((address&31)||address>bytes.size()-32) return false;
        auto next=bytes; std::memcpy(next.data()+address,data,32);
        auto temporary=path.wstring()+L".native-test.tmp";
        HANDLE out=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(out==INVALID_HANDLE_VALUE) return false;
        DWORD written=0;
        bool ok=WriteFile(out,next.data(),DWORD(next.size()),&written,nullptr)&&written==next.size()&&FlushFileBuffers(out);
        if(!CloseHandle(out)) ok=false;
        if(ok) ok=MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
        if(!ok) { DeleteFileW(temporary.c_str()); return false; }
        bytes=next; return true;
    }
};
}
