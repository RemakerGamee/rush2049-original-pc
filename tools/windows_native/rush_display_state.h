#pragma once
#include <atomic>
namespace rush::display {
struct Preset { int width,height; const wchar_t* label; };
inline constexpr Preset presets[]={{1280,720,L"720p (1280 x 720)"},{1920,1080,L"1080p (1920 x 1080)"},{2560,1440,L"1440p - 16:9 (2560 x 1440)"},{3440,1440,L"1440p - 21:9 ultrawide (3440 x 1440)"},{3840,2160,L"4K (3840 x 2160)"}};
inline std::atomic<int> selected{0};
inline std::atomic<bool> vsync{true};
inline std::atomic<bool> fullscreen{false};
inline std::atomic<unsigned> revision{1};
}
