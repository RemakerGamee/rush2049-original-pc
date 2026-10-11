#include <cstdlib>
#include <filesystem>
#include "hle/rt64_state.h"
#define XXH_INLINE_ALL
#include "xxhash.h"
#include "render/rt64_texture_cache.h"
#include "render/rt64_raster_shader_cache.h"
#include "render/rt64_raster_shader.h"
#include "common/rt64_tmem_hasher.h"
#include <chrono>
#include <cstdio>
#include "ultramodern/renderer_context.hpp"
#include "librecomp/game.hpp"
#include "hle/rt64_application.h"
#include "hle/rt64_workload_queue.h"
#include "hle/rt64_rsp.h"
#include "hle/rt64_rdp.h"
#include <array>
#include <stdexcept>
#include "render/rush_display_state.h"

namespace {
using namespace ultramodern::renderer;
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
        core.window = window.window;
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
        auto result = app->setup(window.thread_id);
        setup_result = static_cast<SetupResult>(result);
        ready = result == RT64::Application::SetupResult::Success;
        if (ready) {

            // Optional original-artwork remaster tooling. Unset means stock behavior.
            if (const char* capture = std::getenv("RUSH_REMASTER_CAPTURE")) {
                const auto path = std::filesystem::u8path(capture);
                if (!path.is_absolute() || !std::filesystem::is_directory(path))
                    throw std::runtime_error("Remaster capture requires an existing absolute directory");
                app->state->dumpingTexturesDirectory = path;
                std::puts("REMASTER_CAPTURE_ENABLED: original RT64 texture dumps");
            }
            if (const wchar_t* pack = _wgetenv(L"RUSH_REMASTER_TEXTURE_PACK")) {
                const auto path = std::filesystem::path(pack);
                if (!path.is_absolute() || !std::filesystem::exists(path))
                    throw std::runtime_error("Remaster pack requires an existing absolute path");
                if (!app->textureCache->loadReplacementDirectory(RT64::ReplacementDirectory(path)))
                    throw std::runtime_error("Remaster texture pack could not be loaded");
                std::puts("REMASTER_TEXTURE_PACK_LOADED");
            }
            // Warm the existing texture-decode pipeline with a valid synthetic
            // zero TMEM tile, before the guest submits its first scene. The real
            // hash and descriptor preserve texture-cache lookup semantics.
            const auto warm_start = std::chrono::steady_clock::now();
            std::array<uint8_t, 4096> zero_tmem{};
            RT64::LoadTile tile{};
            tile.fmt = 0; tile.siz = 2; tile.line = 2; tile.tmem = 0;
            const uint64_t hash = RT64::TMEMHasher::hash(zero_tmem.data(), tile, 8, 8, 0, RT64::TMEMHasher::CurrentHashVersion);
            app->textureCache->queueGPUUploadTMEM(hash, 0, zero_tmem.data(), int(zero_tmem.size()), 8, 8, 0, tile, true);
            app->textureCache->waitForGPUUploads();
            app->rasterShaderCache->shaderUber->waitForPipelineCreation();
            std::printf("NATIVE_GRAPHICS_WARMUP: texture decode and uber pipeline ready in %.3f ms\n", std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-warm_start).count());
        }
    }
    ~RushRenderer() override { shutdown(); }
    bool valid() override { return ready; }
    bool update_config(const GraphicsConfig&, const GraphicsConfig& cfg) override {
        if (!ready || (cfg.api_option != GraphicsApi::Auto && cfg.api_option != GraphicsApi::Vulkan)) return false;
        auto& user = app->userConfig;
        using U = RT64::UserConfiguration;
        user.developerMode = cfg.developer_mode;
        const auto preset=rush::display::presets[rush::display::selected.load()];
        user.resolution = U::Resolution::Manual;
        user.resolutionMultiplier = double(preset.height)/240.0;
        user.downsampleMultiplier = std::max(cfg.ds_option, 1);
        user.aspectRatio = U::AspectRatio::Manual;user.aspectTarget=double(preset.width)/preset.height;
        user.refreshRate = static_cast<U::RefreshRate>(cfg.rr_option);
        user.refreshRateTarget = std::max(cfg.rr_manual_value, 1);
        user.internalColorFormat = cfg.hpfb_option == HighPrecisionFramebuffer::On ? U::InternalColorFormat::High :
            cfg.hpfb_option == HighPrecisionFramebuffer::Off ? U::InternalColorFormat::Standard : U::InternalColorFormat::Automatic;
        auto oldMSAA = user.antialiasing;
        user.antialiasing = static_cast<U::Antialiasing>(cfg.msaa_option);
        user.validate();
        if (oldMSAA != user.antialiasing) app->updateMultisampling();
        app->updateUserConfig(false);
        app->setFullScreen(rush::display::fullscreen.load());
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
        static uint64_t tasks=0;
        auto start=std::chrono::steady_clock::now();
        if(tasks<2) std::printf("NATIVE_PHASE: task_%llu_start ns=%lld\n",(unsigned long long)(tasks+1),(long long)std::chrono::duration_cast<std::chrono::nanoseconds>(start.time_since_epoch()).count());
        app->processDisplayLists(ram, list, 0, true);
        double duration=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        ++tasks;
        if(tasks<=2) std::printf("NATIVE_PHASE: task_%llu_end ns=%lld\n",(unsigned long long)tasks,(long long)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        if(tasks%60==0 || duration>80) std::printf("NATIVE_GFX_TASK: count=%llu submit_ms=%.3f\n",(unsigned long long)tasks,duration);
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
    void update_screen() override {
        if(!ready)return;
        static unsigned applied=0;const unsigned revision=rush::display::revision.load();
        if(applied!=revision){const auto preset=rush::display::presets[rush::display::selected.load()];using U=RT64::UserConfiguration;
            app->userConfig.resolution=U::Resolution::Manual;app->userConfig.resolutionMultiplier=double(preset.height)/240.0;
            app->userConfig.aspectRatio=U::AspectRatio::Manual;app->userConfig.aspectTarget=double(preset.width)/preset.height;
            app->userConfig.validate();app->updateUserConfig(false);applied=revision;
            std::printf("DISPLAY_PRESET_APPLIED: %d x %d\n",preset.width,preset.height);}
        app->updateScreen();
    }
    void shutdown() override {
        if (app) { std::puts("NATIVE_RT64_SHUTDOWN_BEGIN"); app->end(); app.reset(); std::puts("NATIVE_RT64_SHUTDOWN_END"); }
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
