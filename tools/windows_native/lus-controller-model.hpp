#pragma once
#include "controller_manager.hpp"
#include "controller-submission.hpp"
#include <mutex>
namespace rush::options {
struct InputUi {
    std::mutex mutex;
    rush::input::Config config;
    std::vector<rush::input::DeviceSummary> devices;
    std::array<rush::input::PortState,4> states{};
    std::optional<rush::input::Config> pending;
    std::string error,review_message;
    std::optional<ReviewRequest> review_request;
    unsigned revision=0;
    bool ready=false,saving=false;
};
inline InputUi input_ui;
}
