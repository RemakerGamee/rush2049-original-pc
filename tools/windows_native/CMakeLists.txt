cmake_minimum_required(VERSION 3.20)
project(RushWindowsRT64 LANGUAGES C CXX)
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_definitions(NOMINMAX)
set(RT64_STATIC ON CACHE BOOL "" FORCE)
set(SDLROOT "${CMAKE_CURRENT_SOURCE_DIR}/rt64/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3")
add_subdirectory(N64ModernRuntime)
add_subdirectory(rt64)
file(GLOB GUEST RecompiledFuncs-probe/funcs_*.c)
add_library(rush_guest OBJECT ${GUEST})
target_include_directories(rush_guest PRIVATE N64Recomp/include RecompiledFuncs-probe compile-check)
target_compile_options(rush_guest PRIVATE /FI${CMAKE_CURRENT_SOURCE_DIR}/compile-check/external_declarations.h /fp:strict /O2)
file(GLOB COMPAT compat/*.cpp)
file(GLOB RSP RecompiledRsp/*.cpp)
add_executable(RushWindowsRT64 tools/runtime_boot_check.cpp host/renderer_bridge.cpp ${COMPAT} ${RSP} $<TARGET_OBJECTS:rush_guest>)
target_include_directories(RushWindowsRT64 PRIVATE N64Recomp/include RecompiledFuncs-probe RecompiledRsp rt64/src rt64/src/contrib rt64/src/contrib/xxHash ${SDLROOT}/include)
target_link_directories(RushWindowsRT64 PRIVATE ${SDLROOT}/lib/x64)
target_link_libraries(RushWindowsRT64 PRIVATE librecomp ultramodern rt64 SDL2)
target_compile_options(RushWindowsRT64 PRIVATE /O2)
add_custom_command(TARGET RushWindowsRT64 POST_BUILD
COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SDLROOT}/lib/x64/SDL2.dll" "$<TARGET_FILE_DIR:RushWindowsRT64>"
COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/rt64/src/contrib/dxc/bin/x64/dxcompiler.dll" "$<TARGET_FILE_DIR:RushWindowsRT64>"
COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/rt64/src/contrib/dxc/bin/x64/dxil.dll" "$<TARGET_FILE_DIR:RushWindowsRT64>")
target_include_directories(RushWindowsRT64 PRIVATE "$<TARGET_PROPERTY:rt64,INCLUDE_DIRECTORIES>")
target_compile_definitions(RushWindowsRT64 PRIVATE HLSL_CPU FFX_GCC IMGUI_IMPL_VULKAN_NO_PROTOTYPES __PRFCHWINTRIN_H)

# Same reviewed SDL backend as the game; discovery/mapping never starts Rush.
add_executable(RushControllerProbe tools/controller_input_probe.cpp)
target_include_directories(RushControllerProbe PRIVATE ${SDLROOT}/include)
target_link_directories(RushControllerProbe PRIVATE ${SDLROOT}/lib/x64)
target_link_libraries(RushControllerProbe PRIVATE SDL2)
target_compile_definitions(RushControllerProbe PRIVATE SDL_MAIN_HANDLED)
add_custom_command(TARGET RushControllerProbe POST_BUILD
COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SDLROOT}/lib/x64/SDL2.dll" "$<TARGET_FILE_DIR:RushControllerProbe>")

# Owned settings/lifecycle check links the same pinned SDL DLL as the game.
add_executable(RushControllerSettingsCheck tools/controller_settings_check.cpp)
target_include_directories(RushControllerSettingsCheck PRIVATE tools ${SDLROOT}/include)
target_link_directories(RushControllerSettingsCheck PRIVATE ${SDLROOT}/lib/x64)
target_link_libraries(RushControllerSettingsCheck PRIVATE SDL2)
target_compile_definitions(RushControllerSettingsCheck PRIVATE SDL_MAIN_HANDLED)

# Local-only linker map resolves native crash offsets; never publish the map.
target_link_options(RushWindowsRT64 PRIVATE /MAP)

target_include_directories(RushWindowsRT64 PRIVATE tools N64ModernRuntime/ultramodern/include)
