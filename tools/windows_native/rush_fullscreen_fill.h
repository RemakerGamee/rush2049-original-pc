#pragma once
#include <atomic>
namespace rush::presentation {
inline std::atomic<bool> fullscreen{false},fill_fullscreen{true};
// Stretch the existing VI-visible area into the whole presentation surface.
// Preserve the VI's crop/sampling relationship and depth range; no scene/FOV edits.
template<class Viewport,class Rect>
bool fill_surface(Viewport& viewport,Rect& crop,int width,int height) {
    const int crop_width=crop.right-crop.left,crop_height=crop.bottom-crop.top;
    if(width<=0||height<=0||crop_width<=0||crop_height<=0)return false;
    const float sx=float(width)/crop_width,sy=float(height)/crop_height;
    viewport.x=(viewport.x-crop.left)*sx;
    viewport.y=(viewport.y-crop.top)*sy;
    viewport.width*=sx;viewport.height*=sy;
    crop.left=crop.top=0;crop.right=width;crop.bottom=height;
    return true;
}
}
