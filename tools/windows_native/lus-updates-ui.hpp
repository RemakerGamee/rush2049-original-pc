#pragma once
#include "rush-update-service.hpp"
namespace rush::options {
extern std::string error;
inline std::unique_ptr<rush::updates::Service> updater;
inline std::string support_error;
inline void updates_page(Store& config){
    auto s=updater->snapshot();
    ImGui::Text("Installed version: %s",updater->installed.c_str());
    ImGui::Text("Edition: %s libultraship | Windows x64 | Experimental",updater->edition.c_str());
    ImGui::TextWrapped("Release source: github.com/RemakerGamee/rush2049-original-pc");
    bool automatic=config.update_auto_check;
    if(ImGui::Checkbox("Check for updates when Rush starts",&automatic)){
        const bool previous=config.update_auto_check;config.update_auto_check=automatic;
        try{config.save();if(automatic)updater->check();error.clear();}catch(const std::exception& e){config.update_auto_check=previous;error=e.what();}
    }
    ImGui::TextWrapped("Optional checks only notify you. Downloads, installation and restarting always remain your choice.");
    ImGui::BeginDisabled(s.busy);
    if(ImGui::Button("Check for updates"))updater->check();
    std::string previous;try{previous=updater->previous();}catch(const std::exception& e){ImGui::TextWrapped("Rollback unavailable: %s",e.what());}
    if(!previous.empty()){ImGui::SameLine();if(ImGui::Button("Roll back to previous version"))ImGui::OpenPopup("Restore previous Rush version?");}
    ImGui::EndDisabled();
    ImGui::TextWrapped("%s",s.message.c_str());
    if(s.busy){if(s.release.bytes)ImGui::ProgressBar(float(double(s.received)/s.release.bytes));if(ImGui::Button("Cancel"))updater->cancel();}
    if(!s.release.tag.empty()){
        ImGui::Text("Available version: %s",s.release.tag.c_str());
        ImGui::TextWrapped("Release notes:");ImGui::BeginChild("Release notes",ImVec2(0,140),true);ImGui::TextWrapped("%s",s.release.notes.c_str());ImGui::EndChild();
        ImGui::BeginDisabled(s.busy);
        if(!s.ready){if(ImGui::Button("Download update"))updater->download();}
        else if(ImGui::Button("Close game and install..."))ImGui::OpenPopup("Install Rush update?");
        ImGui::EndDisabled();
    }
    if(ImGui::BeginPopupModal("Install Rush update?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::TextWrapped("Close Rush and install the selected version? Your existing installation is retained.\nYour ROM, saves, Memory Pak and preferences stay in the same player-data folder.\nThe game will not restart automatically.");
        if(ImGui::Button("Close game and continue")){try{updater->request_install();SDL_Event quit{};quit.type=SDL_QUIT;if(SDL_PushEvent(&quit)<0)throw std::runtime_error("Close the game normally to finish the queued update");ImGui::CloseCurrentPopup();}catch(const std::exception& e){error=e.what();}}
        ImGui::SameLine();if(ImGui::Button("Cancel installation"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
    }
    if(ImGui::BeginPopupModal("Restore previous Rush version?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::TextWrapped("Close Rush and restore the retained, verified previous application?\nYour ROM, saves, Memory Pak and preferences stay in place.\nNo internet connection is required. The game will not restart automatically.");
        if(ImGui::Button("Close game and restore")){try{updater->request_rollback();SDL_Event quit{};quit.type=SDL_QUIT;if(SDL_PushEvent(&quit)<0)throw std::runtime_error("Close the game normally to finish rollback");ImGui::CloseCurrentPopup();}catch(const std::exception& e){error=e.what();}}
        ImGui::SameLine();if(ImGui::Button("Cancel rollback"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
    }
    ImGui::TextWrapped("Rollback verifies and restores the retained previous application offline. Both application folders are kept; your player-data folder stays the same.");
}
inline void support_page(){
    ImGui::TextUnformatted("Rush 2049 Support");
    ImGui::TextWrapped("Get help from Retro Remakes. Email opens a draft you can review and send. No files or diagnostics are attached automatically.");
    if(ImGui::Button("Discord")){if(SDL_OpenURL(rush::updates::discord)!=0)support_error="Could not open your browser. Copy the invite below.";else support_error.clear();}
    ImGui::SameLine();if(ImGui::Button("Copy Discord invite")){support_error=SDL_SetClipboardText(rush::updates::discord)==0?"Invite copied.":"Could not access the clipboard.";}
    ImGui::TextWrapped("%s",rush::updates::discord);
    if(ImGui::Button("Email support")){auto url=rush::updates::mailto(updater->installed,updater->edition+" libultraship");if(SDL_OpenURL(url.c_str())!=0)support_error="Could not open your mail application. Use Copy email address.";else support_error="Review the draft in your mail application. If no draft appeared, use Copy email address.";}
    ImGui::SameLine();if(ImGui::Button("Copy email address"))support_error=SDL_SetClipboardText(rush::updates::email)==0?"Email address copied.":"Could not access the clipboard. The address is shown below.";
    ImGui::TextWrapped("%s",rush::updates::email);
    if(!support_error.empty())ImGui::TextWrapped("%s",support_error.c_str());
}
}
