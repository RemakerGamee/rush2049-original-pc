#include "ultramodern/renderer_context.hpp"
#include "librecomp/game.hpp"
#include "hle/rt64_application.h"
#include "hle/rt64_workload_queue.h"
#include "hle/rt64_rsp.h"
#include "hle/rt64_rdp.h"
#include <array>
#include <stdexcept>

namespace {
using namespace ultramodern::renderer;
// The adapter converts these enums by value. Fail the build if either project reorders them.
using RT64Setup = RT64::Application::SetupResult;
using RT64Config = RT64::UserConfiguration;
#define RUSH_ENUM_MATCH(Type, Other, Name) \
    static_assert(static_cast<int>(Type::Name) == static_cast<int>(Other::Name), #Type "::" #Name " differs from RT64")
RUSH_ENUM_MATCH(SetupResult, RT64Setup, Success);
RUSH_ENUM_MATCH(SetupResult, RT64Setup, DynamicLibrariesNotFound);
RUSH_ENUM_MATCH(SetupResult, RT64Setup, InvalidGraphicsAPI);
RUSH_ENUM_MATCH(SetupResult, RT64Setup, GraphicsAPINotFound);
RUSH_ENUM_MATCH(SetupResult, RT64Setup, GraphicsDeviceNotFound);
RUSH_ENUM_MATCH(AspectRatio, RT64Config::AspectRatio, Original);
RUSH_ENUM_MATCH(AspectRatio, RT64Config::AspectRatio, Expand);
RUSH_ENUM_MATCH(AspectRatio, RT64Config::AspectRatio, Manual);
RUSH_ENUM_MATCH(RefreshRate, RT64Config::RefreshRate, Original);
RUSH_ENUM_MATCH(RefreshRate, RT64Config::RefreshRate, Display);
RUSH_ENUM_MATCH(RefreshRate, RT64Config::RefreshRate, Manual);
RUSH_ENUM_MATCH(Antialiasing, RT64Config::Antialiasing, None);
RUSH_ENUM_MATCH(Antialiasing, RT64Config::Antialiasing, MSAA2X);
RUSH_ENUM_MATCH(Antialiasing, RT64Config::Antialiasing, MSAA4X);
RUSH_ENUM_MATCH(Antialiasing, RT64Config::Antialiasing, MSAA8X);
#undef RUSH_ENUM_MATCH
thread_local uint32_t* renderer_interrupt_bits = nullptr;
void consume_renderer_interrupts() {
    // ultramodern's graphics thread sends SP/DP messages around send_dl.
    // Clear RT64's local bits; it must not issue duplicate OS messages.
    if (renderer_interrupt_bits) *renderer_interrupt_bits = 0;
}
class RushRenderer final : public RendererContext {
    uint8_t* ram;
    std::array<uint8_t, 64> header{};
    std::array<uint8_t, 4096> dmem{}, imem{};
    std::array<uint32_t, 9> regs{};
    std::unique_ptr<RT64::Application> app;
    bool ready = false;
public:
    RushRenderer(uint8_t* rdram, WindowHandle window, bool developer) : ram(rdram) {
        chosen_api = GraphicsApi::Vulkan;
        setup_result = SetupResult::GraphicsAPINotFound;
        RT64::Application::Core core{};
        core.window = window;
        core.RDRAM = ram;
        core.HEADER = header.data(); core.DMEM = dmem.data(); core.IMEM = imem.data();
        auto rom = recomp::get_rom();
        if (rom.size() < header.size()) throw std::runtime_error("Renderer requires the original ROM header");
        for (size_t i = 0; i < header.size(); i++) header[i ^ 3] = rom[i];
        core.MI_INTR_REG = &regs[0];
        core.DPC_START_REG = &regs[1]; core.DPC_END_REG = &regs[2];
        core.DPC_CURRENT_REG = &regs[3]; core.DPC_STATUS_REG = &regs[4];
        core.DPC_CLOCK_REG = &regs[5]; core.DPC_BUFBUSY_REG = &regs[6];
        core.DPC_PIPEBUSY_REG = &regs[7]; core.DPC_TMEM_REG = &regs[8];
        auto vi = get_vi_regs();
        core.VI_STATUS_REG = &vi->VI_STATUS_REG;
        core.VI_ORIGIN_REG = &vi->VI_ORIGIN_REG;
        core.VI_WIDTH_REG = &vi->VI_WIDTH_REG;
        core.VI_INTR_REG = &vi->VI_INTR_REG;
        core.VI_V_CURRENT_LINE_REG = &vi->VI_V_CURRENT_LINE_REG;
        core.VI_TIMING_REG = &vi->VI_TIMING_REG;
        core.VI_V_SYNC_REG = &vi->VI_V_SYNC_REG;
        core.VI_H_SYNC_REG = &vi->VI_H_SYNC_REG;
        core.VI_LEAP_REG = &vi->VI_LEAP_REG;
        core.VI_H_START_REG = &vi->VI_H_START_REG;
        core.VI_V_START_REG = &vi->VI_V_START_REG;
        core.VI_V_BURST_REG = &vi->VI_V_BURST_REG;
        core.VI_X_SCALE_REG = &vi->VI_X_SCALE_REG;
        core.VI_Y_SCALE_REG = &vi->VI_Y_SCALE_REG;
        renderer_interrupt_bits = &regs[0];
        core.checkInterrupts = consume_renderer_interrupts;
        RT64::ApplicationConfiguration config;
        config.detectDataPath = false; config.useConfigurationFile = false;
        app = std::make_unique<RT64::Application>(core, config);
        app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Vulkan;
        app->userConfig.developerMode = developer;
        app->emulatorConfig.framebuffer.renderToRAM = true;
        auto result = app->setup(0);
        setup_result = static_cast<SetupResult>(result);
        ready = result == RT64::Application::SetupResult::Success;
    }
    ~RushRenderer() override { shutdown(); }
    bool valid() override { return ready; }
    bool update_config(const GraphicsConfig&, const GraphicsConfig& cfg) override {
        if (!ready || (cfg.api_option != GraphicsApi::Auto && cfg.api_option != GraphicsApi::Vulkan)) return false;
        auto& user = app->userConfig;
        using U = RT64::UserConfiguration;
        user.developerMode = cfg.developer_mode;
        user.resolution = cfg.res_option == Resolution::Auto ? U::Resolution::WindowIntegerScale : U::Resolution::Manual;
        user.resolutionMultiplier = cfg.res_option == Resolution::Original2x ? 2.0 : 1.0;
        user.downsampleMultiplier = std::max(cfg.ds_option, 1);
        user.aspectRatio = static_cast<U::AspectRatio>(cfg.ar_option);
        user.refreshRate = static_cast<U::RefreshRate>(cfg.rr_option);
        user.refreshRateTarget = std::max(cfg.rr_manual_value, 1);
        user.internalColorFormat = cfg.hpfb_option == HighPrecisionFramebuffer::On ? U::InternalColorFormat::High :
            cfg.hpfb_option == HighPrecisionFramebuffer::Off ? U::InternalColorFormat::Standard : U::InternalColorFormat::Automatic;
        auto oldMSAA = user.antialiasing;
        user.antialiasing = static_cast<U::Antialiasing>(cfg.msaa_option);
        user.validate();
        if (oldMSAA != user.antialiasing) app->updateMultisampling();
        app->updateUserConfig(false);
        app->setFullScreen(cfg.wm_option == WindowMode::Fullscreen);
        return true;
    }
    void enable_instant_present() override {
        if (!ready) return;
        app->enhancementConfig.presentation.mode = RT64::EnhancementConfiguration::Presentation::Mode::PresentEarly;
        app->updateEnhancementConfig();
    }
    void send_dl(const OSTask* task) override {
        if (!ready || !task || task->t.type != 1) throw std::runtime_error("Invalid graphics task");
        uint32_t text = task->t.ucode & 0x1FFFFFFF;
        uint32_t data = task->t.ucode_data & 0x1FFFFFFF;
        uint32_t list = task->t.data_ptr & 0x1FFFFFFF;
        if (text > 0x800000 - 0x2000 || data > 0x800000 - 0x1000 ||
            list >= 0x800000 || (list & 7) || task->t.data_size > 0x800000 - list)
            throw std::runtime_error("Graphics task outside N64 RDRAM");
        app->interpreter->loadUCodeGBI(text, data, true);
        if (!app->interpreter->hleGBI) throw std::runtime_error("Unsupported graphics microcode");
        app->processDisplayLists(ram, list, 0, true);
    }
    void send_dummy_workload(uint32_t address) override {
        if (!ready) return;
        auto& queue = *app->workloadQueue;
        auto& work = queue.workloads[queue.writeCursor];
        work.reset();
        auto vi = get_vi_regs();
        work.addFramebufferPair(address & 0x1FFFFFFF, G_IM_FMT_RGBA,
            (vi->VI_STATUS_REG & 3) == 3 ? G_IM_SIZ_32b : G_IM_SIZ_16b,
            vi->VI_WIDTH_REG & 0xFFF, 0);
        app->state->advanceWorkload(work, false);
        queue.advanceToNextWorkload();
    }
    void update_screen() override { if (ready) app->updateScreen(); }
    void shutdown() override {
        if (app) { app->end(); app.reset(); }
        ready = false;
        renderer_interrupt_bits = nullptr;
    }
    uint32_t get_display_framerate() const override {
        return app ? app->appWindow->getRefreshRate() : 0;
    }
    float get_resolution_scale() const override {
        return app ? float(app->userConfig.resolutionMultiplier) : 1.0f;
    }
};
std::unique_ptr<RendererContext> create_renderer(uint8_t* ram, WindowHandle window, bool developer) {
    return std::make_unique<RushRenderer>(ram, window, developer);
}
}
ultramodern::renderer::callbacks_t rush_renderer_callbacks() { return {create_renderer}; }
