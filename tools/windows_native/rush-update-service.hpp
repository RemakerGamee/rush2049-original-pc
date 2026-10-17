#pragma once
#include "rush-update-history.hpp"
#include <thread>
#include <mutex>
#include <fstream>
namespace rush::updates {
inline void safe_cache_path(const std::filesystem::path& path){
    if(!path.is_absolute())throw std::runtime_error("An absolute update cache path is required");
    for(auto p=path;!p.empty();p=p.parent_path()){auto attrs=GetFileAttributesW(p.c_str());if(attrs!=INVALID_FILE_ATTRIBUTES&&(attrs&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Linked update cache folders are unsupported");if(p==p.parent_path())break;}
}
inline std::string environment(const char* key,const char* fallback=""){const char* p=std::getenv(key);return p?p:fallback;}
inline std::filesystem::path executable_folder(){wchar_t path[32768]{};auto n=GetModuleFileNameW(nullptr,path,32768);if(!n||n>=32768)throw std::runtime_error("Cannot locate application");return std::filesystem::path(path).parent_path();}
struct Snapshot{bool busy=false,ready=false;uint64_t received=0;Release release;std::string message="Check for updates when you are ready.";};
class Service {
    std::thread worker;mutable std::mutex mutex;Snapshot state;std::atomic_bool cancelled=false;
    mutable std::once_flag rollback_once;
    mutable std::string rollback_version,rollback_failure;
public:
    const std::string installed=environment("RUSH_RELEASE_VERSION",rush::release::version);
    const std::string edition=environment("RUSH_RELEASE_EDITION",environment("RUSH_REMASTER_TEXTURE_PACK").empty()?"original":"remastered");
    const std::filesystem::path data=std::filesystem::u8path(environment("RUSH_OPTIONS_CONFIG")).parent_path();
    ~Service(){cancel();if(worker.joinable())worker.join();}
    Snapshot snapshot()const{std::lock_guard lock(mutex);return state;}
    void cancel(){cancelled=true;}
    template<class F> void start(F action){
        if(snapshot().busy)return;if(worker.joinable())worker.join();cancelled=false;
        {std::lock_guard lock(mutex);state.busy=true;state.message="Connecting to the approved Rush release source...";}
        worker=std::thread([this,action]{try{action();}catch(const std::exception& e){std::lock_guard lock(mutex);state.message=e.what();state.ready=false;}std::lock_guard lock(mutex);state.busy=false;});
    }
    void check(const std::string& target={}){start([this,target]{auto release=select(online(edition,cancelled),installed,target);std::lock_guard lock(mutex);state.release=release;state.ready=false;state.received=0;state.message=release.tag.empty()?"No newer public updates are available for this edition.":"A verified release is available. Review the notes before downloading.";});}
    void download(){const auto release=snapshot().release;if(release.tag.empty())return;
        start([this,release]{auto bytes=fetch(release.url,release.bytes,cancelled,[this](uint64_t n){std::lock_guard lock(mutex);state.received=n;state.message="Downloading update...";});verify(release,bytes);if(cancelled)throw std::runtime_error("Update cancelled");
            // A cache is separate from versioned application files and player saves.
            if(data.empty()||!data.is_absolute())throw std::runtime_error("Install Rush with its launcher before downloading updates");
            auto folder=data/L"update-cache";safe_cache_path(folder);std::filesystem::create_directories(folder);auto file=folder/std::filesystem::u8path(release.hash+".exe");safe_cache_path(file);
            auto staging=folder/std::filesystem::u8path(release.hash+"-"+std::to_string(GetCurrentProcessId())+".new");safe_cache_path(staging);
            HANDLE output=CreateFileW(staging.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(output==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create the update staging file");
            DWORD written=0;bool good=WriteFile(output,bytes.data(),DWORD(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(output);CloseHandle(output);
            if(!good||cancelled||!MoveFileExW(staging.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(staging.c_str());throw std::runtime_error(cancelled?"Update cancelled":"Cannot save update download; retry when the cache is not in use");}
            std::lock_guard lock(mutex);state.ready=true;state.message="Download verified. Close the game to install when you are ready.";
        });
    }
    std::string previous()const{
        // The protected catalog can include thousands of remaster files. Read it once,
        // rather than decrypting/parsing it on every gameplay presentation frame.
        std::call_once(rollback_once,[this]{try{rollback_version=previous_version(data,installed,edition,executable_folder());}catch(const std::exception& e){rollback_failure=e.what();}});
        if(!rollback_failure.empty())throw std::runtime_error(rollback_failure);return rollback_version;
    }
    void request_install(){auto s=snapshot();if(s.busy||!s.ready)throw std::runtime_error("Download and verify the update first");write_ticket(Json{{"action","install"},{"tag",s.release.tag}});}
    void request_rollback(){if(snapshot().busy)throw std::runtime_error("Wait for the active update operation");if(previous_version(data,installed,edition,executable_folder()).empty())throw std::runtime_error("No verified previous installation is available");write_ticket(Json{{"action","rollback"}});}
    void write_ticket(const Json& ticket){
        const auto request=environment("RUSH_UPDATE_REQUEST");if(request.empty())throw std::runtime_error("Start Rush with the installed launcher to install updates");
        auto path=std::filesystem::u8path(request);if(path.parent_path()!=data)throw std::runtime_error("Invalid update request folder");
        auto bytes=ticket.dump();HANDLE output=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(output==INVALID_HANDLE_VALUE)throw std::runtime_error("An update request already exists");
        DWORD written=0;bool good=WriteFile(output,bytes.data(),DWORD(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(output);CloseHandle(output);if(!good){DeleteFileW(path.c_str());throw std::runtime_error("Cannot queue installation");}
    }
};
}
