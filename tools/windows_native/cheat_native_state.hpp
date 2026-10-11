#pragma once
#include "cheat_visibility.hpp"
#include <array>
#include <cstdint>
namespace rush::cheats {
// US retail overlay A: 8039C910 initializes display values and its jump table
// at 803B95BC selects the gameplay setters. Code records at 803B3460 prove
// label-to-row identity. This order is NOT the visible carousel order.
struct NativeBinding {uint8_t row;uint32_t byte_address;uint8_t off,on;};
inline constexpr std::array<NativeBinding,21> native_bindings{{
 {15,0,0,1}, {14,0,0,1}, {16,0,0,1},
 {17,0x801407B0,0,1}, {18,0x801613AB,0,1}, {19,0x801407D0,0,1},
 // Fog NORMAL=0; BLACK uses palette index 1 encoded as 18+1 by CF64/CF68.
 {20,0x801427A0,0,19}, {0,0x8017A4D8,1,0}, {1,0x8013FECC,0,1},
 {2,0x8013FECD,0,1}, {3,0,0,1}, {4,0x801403D0,0,1},
 {5,0x801427A1,0,1}, {6,0x80142B00,0,1}, {7,0x80140418,0,1},
 {8,0x8014061A,0,1}, {9,0x8013F2FC,1,0}, {10,0x80142DB0,0,2},
 {11,0x80143F1C,0,1}, {12,0x8017A634,0,1}, {13,0,0,1}
}};
inline constexpr uint32_t native_menu_flag=0x801164A1;
inline constexpr uint32_t native_unlocks=0x8011648C;
inline constexpr uint32_t native_values=0x801164A4;
// Called exclusively at retail main-menu entry on the guest thread, before
// visibility/input is evaluated. UI threads publish snapshots, never write RAM.
inline uint16_t fog_menu_value(uint8_t raw) {
 return raw>=19&&raw<=26?uint16_t(raw-18):0;
}
class NativeState {
 uint64_t revision_=0;
 bool owned_=false;
public:
 void invalidate(){revision_=0;}
 template<class Memory> bool apply(Memory& m,const Preferences& p,uint64_t revision) {
  if(revision==revision_)return false;
  revision_=revision;
  if(!p.show&&!owned_)return false; // untouched retail startup when opted out
  for(size_t i=0;i<native_bindings.size();++i) {
   const auto b=native_bindings[i];const bool enabled=p.show&&p.enabled[i];
   const uint8_t raw=enabled?b.on:b.off;
   m.byte(native_unlocks+b.row,enabled?1:0);
   if(b.byte_address)m.byte(b.byte_address,raw);
   // Menu representations of super tires and fog differ from gameplay bytes.
   const uint16_t display=i==17?(raw==2):i==6?(enabled?1:0):raw;
   m.half(native_values+2*b.row,display);
  }
  m.byte(native_menu_flag,p.show?1:0);owned_=p.show;
  return true;
 }
};
}
