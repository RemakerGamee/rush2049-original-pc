#include "controller_pak_image.hpp"
#include <iostream>
static void require(bool ok) { if(!ok) throw std::runtime_error("Windows Pak regression failed"); }
int main(int argc,char**argv) {
    require(argc==2);
    std::filesystem::path dir=argv[1]; require(!std::filesystem::exists(dir)); require(std::filesystem::create_directories(dir));
    auto file=dir/"synthetic-only.mpk";
    std::array<uint8_t,32768> initial{};
    {std::ofstream out(file,std::ios::binary); out.write(reinterpret_cast<char*>(initial.data()),initial.size());}
    uint8_t value[32],read[32]; std::memset(value,0x5a,32);
    {
        rush_pak::Image image(file.string());
        require(image.read_block(0,read)&&read[0]==0);
        require(!image.read_block(1,read)&&!image.write_block(32768,value));
        bool refused=false;
        try {rush_pak::Image second(file.string());} catch(...) {refused=true;}
        require(refused);
        auto temporary=file.wstring()+L".native-test.tmp";
        {std::ofstream block(temporary); block<<"synthetic failure fixture";}
        require(!image.write_block(0,value));
        require(image.read_block(0,read)&&read[0]==0);
        std::filesystem::remove(temporary);
        require(image.write_block(32736,value));
        require(image.read_block(32736,read)&&std::memcmp(read,value,32)==0);
    }
    {rush_pak::Image reopened(file.string()); require(reopened.read_block(32736,read)&&std::memcmp(read,value,32)==0);}
    auto bad=dir/"wrong-size.mpk";
    {std::ofstream out(bad);out<<"x";}
    bool rejected=false;try {rush_pak::Image image(bad.string());}catch(...) {rejected=true;}
    require(rejected);
    std::cout<<"PASS: native Windows Pak reopen, lock refusal, bounds, failed replacement preservation, persisted final block; synthetic files only\n";
}
