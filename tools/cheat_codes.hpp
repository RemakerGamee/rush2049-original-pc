#pragma once
#include <array>
#include <cstdint>
#include <mutex>
#include <vector>

namespace rush::cheats {
constexpr uint16_t z=0x2000, l=0x20, r=0x10, cu=8, cd=4, cl=2, cr=1;
struct Code { const char* label; std::vector<uint16_t> steps; bool disputed=false; };
struct Builder {
    std::vector<uint16_t> steps{0};
    void tap(uint16_t press,uint16_t hold=0) {
        if(hold) steps.push_back(hold);
        steps.push_back(hold|press); steps.push_back(hold);
    }
    void taps(std::initializer_list<uint16_t> keys,uint16_t hold=0) {
        for(auto key:keys) tap(key,hold);
    }
    Code finish(const char* label,bool disputed=false) { steps.push_back(0);return {label,steps,disputed}; }
};
inline std::vector<Code> catalog() {
    std::vector<Code> out;
    auto add=[&](const char* label,auto build,bool disputed=false) {
        Builder b;build(b);out.push_back(b.finish(label,disputed));
    };
    add("ALL TRACKS",[](Builder& b){b.taps({uint16_t(z|cu|cr),uint16_t(z|cd|cl),uint16_t(z|cl|cu),uint16_t(z|cd|cr)},l|r);});
    add("ALL CARS",[](Builder& b){b.taps({cl,cl,cl,cu,cu,cu,cr,cr,cr,cd,cd,cd});b.tap(z,l|r|cu|cd|cl|cr);});
    add("ALL PARTS",[](Builder& b){b.tap(z,l|r);b.taps({cd,cu,cl,cr});b.tap(z,l|r);});
    add("INVINCIBLE",[](Builder& b){b.taps({cr,l,r,r,l});b.tap(z,cl|cd);});
    add("TRACK ORIENTATION",[](Builder& b){b.taps({cr,cl,cu,cd,z},l|r);});
    add("INVISIBLE TRACK",[](Builder& b){b.taps({cr,cr});b.tap(cl,l|r);b.taps({cl,cl});b.tap(cr,l|r);});
    add("INVISIBLE CAR",[](Builder& b){b.taps({cu,cd,cl,cr,l,r,z});});
    add("MASS",[](Builder& b){b.taps({uint16_t(r|cd),uint16_t(l|cu),uint16_t(r|cl),uint16_t(l|cr)});});
    add("CAR COLLISIONS",[](Builder& b){b.tap(z,l);b.tap(z,r);b.tap(z,l);b.tap(z,r);});
    // Source conflict: two guides end in C-Up x3, another says C-Down x3.
    add("SUPER SPEED",[](Builder& b){b.tap(z);b.tap(z,l|r);b.tap(cd);b.tap(cd,l|r);b.taps({cu,cu,cu});},true);
    add("SUPER TIRES",[](Builder& b){b.taps({z,z,z,l,r,cu,cu,cl,cr,cd});});
    add("TIRE SCALING",[](Builder& b){b.taps({uint16_t(z|r|cd|cl),uint16_t(z|l|cr|cu),z,z,z});});
    add("FRAME SCALE",[](Builder& b){b.taps({cl,cl});b.steps.push_back(l|r|cr);});
    add("BRAKES",[](Builder& b){b.taps({cd,cd});b.tap(cu,l|r);b.taps({cu,cu});b.tap(cd,l|r);});
    add("CAR MINES",[](Builder& b){b.tap(z,l|r);b.tap(z,cl|cd);b.tap(z,cl|cu);b.tap(z,cu|cr);});
    add("CONE MINES",[](Builder& b){b.taps({uint16_t(z|cd),uint16_t(l|cl),uint16_t(r|cu),uint16_t(z|cr)});});
    add("AUTO ABORT",[](Builder& b){b.taps({cl,cu,cr,cd,z,l,r,z,z});});
    add("RESURRECT IN PLACE",[](Builder& b){b.taps({uint16_t(z|cd),uint16_t(z|cr),uint16_t(z|cu),uint16_t(z|cl),uint16_t(z|r),uint16_t(z|l)});});
    add("SUICIDE MODE",[](Builder& b){b.taps({cr,cu,cl,cd},r);b.taps({cd,cl,cu,cr},l);});
    add("BATTLE PAINT SHOP",[](Builder& b){b.taps({z,z,z,cd,cd,cd,cl,cl,cl,cr,cu,cl,cd});});
    add("FOG COLOR",[](Builder& b){b.tap(l,cu|cr);b.tap(r,cd|cl);b.taps({cr,cl,cr,cl});});
    return out;
}
inline Code menu_code() {Builder b;b.tap(z,l|r|cu|cr);return b.finish("NATIVE MENU");}
// Advance after both a minimum hold time and actual backend samples. Wall-time
// alone must not skip a press if the game briefly stops polling input.
class Playback {
    std::mutex mutex;
    std::vector<uint16_t> steps;
    size_t index=0;
    unsigned samples=0;
    uint64_t started=0;
public:
    bool request(const Code& code) {
        std::lock_guard<std::mutex> lock(mutex);
        if(!steps.empty() || code.steps.empty() || code.steps.size()>256) return false;
        steps=code.steps;index=0;samples=0;started=0;return true;
    }
    bool sample(uint64_t now,uint16_t& buttons) {
        std::lock_guard<std::mutex> lock(mutex);
        if(steps.empty()) return false;
        if(!started) started=now;
        if(samples>=2 && now-started>=60) {
            if(++index==steps.size()) {steps.clear();return false;}
            samples=0;started=now;
        }
        buttons=steps[index];++samples;return true;
    }
    void cancel() {std::lock_guard<std::mutex> lock(mutex);steps.clear();}
};
}
