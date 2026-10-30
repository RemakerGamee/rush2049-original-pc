// Synthetic application used only by updater transaction checks. Contains no game.
#include <filesystem>
#include <string>
int wmain(int argc,wchar_t** argv){
    auto app=std::filesystem::absolute(argv[0]).parent_path();
    for(int i=1;i<argc;++i)if(std::wstring(argv[i])==L"--preflight"&&std::filesystem::exists(app/L"fail-preflight"))return 65;
    return 0;
}
