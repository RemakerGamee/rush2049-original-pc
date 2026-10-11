#include <algorithm>
#include "rom_identity.hpp"
#include "recomp_overlays.inl"
#include "librecomp/game.hpp"
#include "librecomp/overlays.hpp"
#include "ultramodern/ultramodern.hpp"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_syswm.h>
#include <Windows.h>
#define XXH_INLINE_ALL
#include "xxhash.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <filesystem>
#include <atomic>
#include <chrono>
#include <mutex>
#include <deque>
#include <string_view>
#include <csignal>




class RushWindowsAudioClient {
public:
void start(uint32_t) { throw std::runtime_error("WSL audio helper unavailable in native Windows; use SDL audio"); }
void frequency(uint32_t) { start(0); }
void queue(uint32_t,const void*,uint32_t) { start(0); }
uint32_t queued_bytes() const { return 0; }
uint64_t completed_bytes() const { return 0; }
};
#include "keyboard_input.hpp"
#include "controller_input.hpp"
#include "pc_menu.hpp"
#include <memory>

extern ultramodern::renderer::callbacks_t rush_renderer_callbacks();
extern recomp::rsp::callbacks_t rush_rsp_callbacks();
static bool check_game_init = false;
static bool check_decompress = false;
static bool check_game_code = false;
static bool check_render_loop = false;
static bool check_graphics_task = false;
static bool check_framebuffer = false;
static bool check_startup_run = false;
static bool check_menu_test = false;
static bool check_track_test = false;
static bool check_play = false;
static rush::keyboard::Input keyboard_input;
static std::unique_ptr<rush::input::System> controller_input;
static std::atomic<uint16_t> native_script_buttons{0};
static std::atomic<float> native_script_x{0},native_script_y{0};
static std::atomic<int64_t> native_script_deadline{0};
static std::atomic<int64_t> first_visible_ms{0};
static SDL_AudioDeviceID audio_device = 0;
static std::mutex audio_mutex;
static uint64_t audio_samples_queued = 0, audio_nonzero_samples = 0;
static uint64_t audio_underruns = 0, audio_clipped_samples = 0;
static uint32_t audio_actual_frequency = 48000;
static uint32_t audio_host_frequency = 48000;
static bool audio_windows_output = false;
static RushWindowsAudioClient audio_windows_host;
static std::deque<uint64_t> audio_windows_buffer_ends;
static uint64_t audio_windows_submitted = 0, audio_bootstrap_next_ns = 0;
static uint64_t audio_last_buffer_duration_ns = 0;
static uint32_t audio_host_queued_bytes() {
    return audio_windows_output ? audio_windows_host.queued_bytes() : audio_device ? SDL_GetQueuedAudioSize(audio_device) : 0;
}
static uint64_t audio_interval_samples = 0, audio_interval_empty = 0;
static uint32_t audio_interval_peak_bytes = 0;
static ultramodern::runtime_clock::time_point audio_interval_start{};
static bool audio_dma_timing = false;
static bool audio_stream_started = false;
static uint32_t audio_event_queue = 0, audio_event_message = 0;
static uint64_t audio_dma_end_ns = 0, audio_completion_events = 0;
static uint64_t audio_sdl_submitted = 0;
static constexpr uint32_t native_audio_target_ms = 60;
static std::deque<uint64_t> audio_buffer_ends;
static uint64_t audio_clock_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(ultramodern::runtime_clock::now().time_since_epoch()).count();
}
// Optional source-buffer trace. It records the native ring without changing it.
extern "C" void rush_audio_ring_trace(uint8_t* rdram, uint32_t kind, uint32_t address) {
    const char* path = std::getenv("RUSH_AUDIO_RING_TRACE");
    if (!path) return;
    static std::mutex trace_mutex;
    std::lock_guard<std::mutex> guard(trace_mutex);
    static FILE* trace = std::fopen(path, "w");
    if (!trace) return;
    std::fprintf(trace, "%llu,%u,%08X,%u,%u,%u",
        (unsigned long long)audio_clock_ns(), kind, address,
        unsigned(MEM_BU(0, int32_t(0x80038288))), unsigned(MEM_HU(0, int32_t(0x80038360))),
        unsigned(MEM_HU(0, int32_t(0x800382CE))));
    for (unsigned i = 0; i < 8; ++i) {
        const uint32_t buffer = uint32_t(MEM_W(int32_t(i * 4), int32_t(0x80038228))) & 0x1FFFFFFF;
        const uint64_t hash = buffer && buffer < 0x7FFD00 ? XXH64(rdram + buffer, 768, 0) : 0;
        std::fprintf(trace, ",%016llX", (unsigned long long)hash);
    }
    std::fputc('\n', trace);
}
extern "C" void rush_real_osAiSetNextBuffer_recomp(uint8_t*, recomp_context*);
extern "C" void osAiSetNextBuffer_recomp(uint8_t* rdram, recomp_context* ctx) {
    rush_audio_ring_trace(rdram, 0, uint32_t(ctx->r4));
    rush_real_osAiSetNextBuffer_recomp(rdram, ctx);
}
static void audio_frequency(uint32_t frequency) {
    std::lock_guard<std::mutex> guard(audio_mutex);
    if (audio_windows_output) {
        audio_windows_host.frequency(frequency);
        audio_actual_frequency = audio_host_frequency = frequency;
        audio_interval_start = ultramodern::runtime_clock::now();
        audio_interval_samples = audio_interval_empty = audio_interval_peak_bytes = 0;
        audio_bootstrap_next_ns = 0;
        audio_stream_started = true;
        std::printf("AUDIO_DEVICE_READY: %u Hz stereo, private Windows output; completion-paced delivery.\n", frequency);
        return;
    }
    if (audio_device) SDL_CloseAudioDevice(audio_device);
    SDL_AudioSpec requested{}, obtained{};
    requested.freq = frequency;
    requested.format = AUDIO_S16SYS;
    requested.channels = 2;
    requested.samples = 256;
    audio_device = SDL_OpenAudioDevice(nullptr, 0, &requested, &obtained, 0);
    if (!audio_device) { std::fprintf(stderr, "Audio device failed: %s\n", SDL_GetError()); std::_Exit(52); }
    audio_actual_frequency = frequency;
    audio_host_frequency = obtained.freq;
    audio_interval_start = ultramodern::runtime_clock::now();
    audio_interval_samples = audio_interval_empty = audio_interval_peak_bytes = 0;
    audio_buffer_ends.clear();
    audio_dma_end_ns = 0;
    audio_sdl_submitted = 0;
    audio_stream_started = !audio_dma_timing;
    SDL_PauseAudioDevice(audio_device, audio_dma_timing ? 1 : 0);
    std::printf("AUDIO_DEVICE_READY: %u Hz guest to %u Hz host stereo, %u-frame device buffer, output %s.\n", frequency, obtained.freq, obtained.samples, SDL_GetCurrentAudioDriver());
}
static void audio_queue(int16_t* samples, size_t count) {
    std::lock_guard<std::mutex> guard(audio_mutex);
    if (!audio_device && !audio_windows_output) return;
    std::vector<int16_t> stereo(count);
    const uint32_t queued_before = audio_host_queued_bytes();
    if (audio_samples_queued && queued_before == 0) { ++audio_underruns; ++audio_interval_empty; }
    for (size_t i = 0; i < count; ++i) {
        stereo[i] = samples[i ^ 1]; // RDRAM stores each stereo pair in swapped halfword order.
        audio_nonzero_samples += stereo[i] != 0;
        audio_clipped_samples += stereo[i] == INT16_MIN || stereo[i] == INT16_MAX;
    }
    if (const char* capture_path = std::getenv("RUSH_AUDIO_CAPTURE")) {
        static std::ofstream pcm_capture(capture_path, std::ios::binary);
        static size_t captured = 0;
        // Default remains 30 seconds; isolated flyover review may opt into a
        // longer bounded source recording without touching the playback data.
        static const size_t capture_seconds = [] {
            const char* value = std::getenv("RUSH_AUDIO_CAPTURE_SECONDS");
            const long seconds = value ? std::strtol(value, nullptr, 10) : 30;
            return size_t(std::clamp(seconds, 1L, 1800L));
        }();
        const size_t limit = size_t(audio_actual_frequency) * 4 * capture_seconds;
        if (captured < limit) {
            const size_t bytes = std::min(count * sizeof(int16_t), limit - captured);
            pcm_capture.write(reinterpret_cast<const char*>(stereo.data()), bytes);
            pcm_capture.flush(); captured += bytes;
        }
    }
    audio_samples_queued += count;
    if (audio_windows_output) {
        audio_windows_host.queue(audio_actual_frequency, stereo.data(), uint32_t(count * sizeof(int16_t)));
        audio_windows_submitted += count * sizeof(int16_t);
        audio_windows_buffer_ends.push_back(audio_windows_submitted);
        audio_last_buffer_duration_ns = uint64_t(count / 2) * 1000000000ull / audio_actual_frequency;
        if (!audio_bootstrap_next_ns) audio_bootstrap_next_ns = audio_clock_ns() + audio_last_buffer_duration_ns;
    } else if (SDL_QueueAudio(audio_device, stereo.data(), count * sizeof(int16_t)) != 0) {
        std::fprintf(stderr, "Audio queue failed: %s\n", SDL_GetError()); std::_Exit(52);
    }
    audio_interval_samples += count;
    if (audio_dma_timing && count && !audio_windows_output) {
        audio_sdl_submitted += count * sizeof(int16_t);
        audio_buffer_ends.push_back(audio_sdl_submitted);
    }
    const uint32_t queued_after = audio_host_queued_bytes();
    if ((check_menu_test || check_play) && queued_after > audio_host_frequency * 4 / 2) {
        std::fprintf(stderr, "AUDIO_OUTPUT_STALLED: more than 500 ms queued; host playback is not keeping up.\n");
        std::_Exit(54);
    }
    if (audio_dma_timing && !audio_stream_started && queued_after >= audio_host_frequency * 4 * native_audio_target_ms / 1000) {
        audio_stream_started = true;
        SDL_PauseAudioDevice(audio_device, 0);
        std::puts("AUDIO_STREAM_STARTED: 60 ms host-paced prefill; independent completion worker.");
    }
    audio_interval_peak_bytes = std::max(audio_interval_peak_bytes, queued_after);
    const auto now = ultramodern::runtime_clock::now();
    const double seconds = std::chrono::duration<double>(now - audio_interval_start).count();
    if (seconds >= 1.0) {
        std::printf("AUDIO_TIMING: %.0f stereo frames/sec produced, %u Hz requested, %.1f ms queued, %.1f ms peak, %llu empty arrivals.\n",
            audio_interval_samples / (2.0 * seconds), audio_actual_frequency,
            queued_after * 250.0 / audio_host_frequency, audio_interval_peak_bytes * 250.0 / audio_host_frequency,
            (unsigned long long)audio_interval_empty);
        static uint64_t previously_clipped = 0;
        std::printf("AUDIO_SOURCE_CLIPPING: interval=%llu total=%llu samples=%llu\n",
            (unsigned long long)(audio_clipped_samples - previously_clipped),
            (unsigned long long)audio_clipped_samples,
            (unsigned long long)audio_samples_queued);
        previously_clipped = audio_clipped_samples;
        audio_interval_start = now;
        audio_interval_samples = audio_interval_empty = audio_interval_peak_bytes = 0;
    }
}
static void audio_service_completions() {
    std::lock_guard<std::mutex> guard(audio_mutex);
    if (!audio_dma_timing || !audio_event_queue) return;
    const uint64_t now = audio_clock_ns();
    if (audio_windows_output) {
        const uint64_t completed = audio_windows_host.completed_bytes();
        while (!audio_windows_buffer_ends.empty() && audio_windows_buffer_ends.front() <= completed) {
            audio_windows_buffer_ends.pop_front();
            // Windows reports multiple short DMA completions in one output block.
            // Preserve each completion if the small guest message queue is full.
            ultramodern::enqueue_external_message(int32_t(audio_event_queue), int32_t(audio_event_message), false, true);
            ++audio_completion_events;
        }
        // Prime the short guest DMA packets until the Windows output has enough
        // data to start. After its first completion, its sample clock paces AI.
        if (!completed && audio_bootstrap_next_ns && now >= audio_bootstrap_next_ns &&
            audio_host_queued_bytes() < audio_host_frequency * 4 * native_audio_target_ms / 1000) {
            audio_bootstrap_next_ns = now + audio_last_buffer_duration_ns;
            ultramodern::enqueue_external_message(int32_t(audio_event_queue), int32_t(audio_event_message), false, true);
            ++audio_completion_events;
        }
        return;
    }
    // Queue size is the SDL sink's consumed-byte accounting, including its
    // host batching granularity. Keep a bounded lead instead of catching up
    // a free-running wall clock after renderer stalls. Never discard PCM.
    const uint64_t queued = audio_host_queued_bytes();
    const uint64_t consumed = audio_sdl_submitted >= queued ? audio_sdl_submitted - queued : 0;
    const uint64_t lead = uint64_t(audio_host_frequency) * 4 * native_audio_target_ms / 1000;
    while (!audio_buffer_ends.empty() && audio_buffer_ends.front() <= consumed + lead) {
        audio_buffer_ends.pop_front();
        ultramodern::enqueue_external_message(int32_t(audio_event_queue), int32_t(audio_event_message), false, true);
        ++audio_completion_events;
    }
}
static size_t audio_remaining() {
    std::lock_guard<std::mutex> guard(audio_mutex);
    return uint64_t(audio_host_queued_bytes() / 4) * audio_actual_frequency / audio_host_frequency;
}
static int64_t steady_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(ultramodern::runtime_clock::now().time_since_epoch()).count();
}
static std::atomic<unsigned> graphics_tasks{0};
static unsigned rom_dma_count = 0;
extern "C" void rush_diagnostic_screen(uint8_t* ram, uint32_t address, uint32_t width, uint32_t height, uint32_t siz, bool visible) {
    static unsigned screens = 0;
    static unsigned previous_graphics = 0;
    static auto last_graphics_time = ultramodern::runtime_clock::now();
    if (graphics_tasks.load() != previous_graphics) {
        previous_graphics = graphics_tasks.load();
        last_graphics_time = ultramodern::runtime_clock::now();
    }
    if ((check_menu_test || check_play) && previous_graphics > 2 && ultramodern::runtime_clock::now() - last_graphics_time > std::chrono::seconds(18)) {
        std::fprintf(stderr, "GAME_STALL_DETECTED: no graphics task progress for eighteen seconds after %u tasks.\n", previous_graphics);
        std::_Exit(53);
    }
    if (check_framebuffer && graphics_tasks && screens++ < 5)
        std::printf("Framebuffer check: address=0x%08X size=%ux%u format=%u visible=%u\n", address, width, height, siz, visible);
    if (check_framebuffer && graphics_tasks && screens % 120 == 0) {
        std::printf("Startup state: graphics=%u DMA=%u framebuffer=0x%08X visible=%u\n", graphics_tasks.load(), rom_dma_count, address, visible);
        for (uint32_t descriptor : {0x80034BA0u, 0x80034D50u, 0x8002E998u, 0x800344E0u, 0x80034840u, 0x80034690u, 0x80035260u, 0x800350B0u, 0x80038040u, 0x80034F00u, 0x800349F0u}) {
            auto* t = reinterpret_cast<OSThread*>(ram + (descriptor & 0x1FFFFFFF));
            std::printf("Thread %d priority=%d state=%u queue=0x%08X\n", t->id, t->priority, t->state, uint32_t(t->queue));
            if ((uint32_t(t->queue) & 0x1FFFFFFF) < 0x800000 && uint32_t(t->queue) >= 0x80000000u && uint32_t(t->queue) != 0xFFFFFFFFu) {
                auto* q = reinterpret_cast<OSMesgQueue*>(ram + (uint32_t(t->queue) & 0x1FFFFFFF));
                std::printf("Wait queue: count=%d capacity=%d first=%d waiters=0x%08X\n", q->validCount, q->msgCount, q->first, uint32_t(q->blocked_on_recv));
            }
        }
    }
    if (!check_framebuffer || !graphics_tasks || !visible || width == 0 || height == 0 ||
        width > 1024 || height > 1024 || (siz != 2 && siz != 3)) return;
    const uint32_t bytes = siz == 2 ? 2 : 4;
    if (uint64_t(address) + uint64_t(width) * height * bytes > 0x800000) return;
    std::vector<uint8_t> pixels(width * height * 3);
    bool varied = false;
    for (uint32_t i = 0; i < width * height; ++i) {
        const auto offset = address + i * bytes;
        if (bytes == 2) {
            uint16_t value = uint16_t(ram[offset ^ 3]) << 8 | ram[(offset + 1) ^ 3];
            pixels[i * 3] = ((value >> 11) & 31) * 255 / 31;
            pixels[i * 3 + 1] = ((value >> 6) & 31) * 255 / 31;
            pixels[i * 3 + 2] = ((value >> 1) & 31) * 255 / 31;
        } else {
            for (unsigned c = 0; c < 3; ++c) pixels[i * 3 + c] = ram[(offset + c) ^ 3];
        }
        for (unsigned c = 0; c < 3; ++c) varied |= pixels[i * 3 + c] != pixels[c];
    }
    if (!varied) return;
    if (first_visible_ms.load() == 0) first_visible_ms.store(steady_ms());
    static auto first_frame_time = ultramodern::runtime_clock::now();
    static bool menu_memory_captured = false;
    if (check_menu_test && !menu_memory_captured && ultramodern::runtime_clock::now() - first_frame_time > std::chrono::seconds(20)) {
        std::ofstream dump("build/runtime-boot-check/menu-rdram.bin", std::ios::binary);
        dump.write(reinterpret_cast<const char*>(ram), 0x800000);
        menu_memory_captured = true;
        std::puts("MENU_MEMORY_CAPTURED: texture and display-list memory saved.");
    }
    static uint64_t last_hash = 0;
    static unsigned changed_frames = 0;
    const auto hash = XXH3_64bits(pixels.data(), pixels.size());
    if (check_startup_run && hash == last_hash) {
        if (check_play || ultramodern::runtime_clock::now() - first_frame_time < std::chrono::seconds(check_track_test ? 120 : check_menu_test ? 60 : 30)) return;
    } else {
        last_hash = hash;
        ++changed_frames;
    }
    std::string filename = "build/runtime-boot-check/guest-framebuffer.ppm";
    if (check_startup_run) filename = "build/runtime-boot-check/startup-latest.ppm";
    std::ofstream file(filename, std::ios::binary);
    file << "P6\n" << width << ' ' << height << "\n255\n";
    file.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
    file.close();
    if (check_startup_run) {
        if (changed_frames <= 10 || changed_frames % 60 == 0)
            std::printf("STARTUP_FRAME_CHANGED: %u distinct consecutive frames, %u graphics tasks.\n", changed_frames, graphics_tasks.load());
        if (check_play || ultramodern::runtime_clock::now() - first_frame_time < std::chrono::seconds(check_track_test ? 120 : check_menu_test ? 60 : 30)) return;
        std::printf("STARTUP_RUN_COMPLETED: %u seconds after first visible frame; %u changed frames, %u graphics tasks.\n", check_track_test ? 120 : check_menu_test ? 60 : 30, changed_frames, graphics_tasks.load());
        if (check_menu_test) {
            std::ofstream dump("build/runtime-boot-check/menu-rdram.bin", std::ios::binary);
            dump.write(reinterpret_cast<const char*>(ram), 0x800000);
        }
        { std::lock_guard<std::mutex> guard(audio_mutex);
          std::printf("AUDIO_OUTPUT_CHECK: %llu samples queued, %llu nonzero samples.\n",
              (unsigned long long)audio_samples_queued, (unsigned long long)audio_nonzero_samples);
          std::printf("AUDIO_QUALITY_CHECK: %llu empty-queue arrivals, %llu clipped samples.\n", (unsigned long long)audio_underruns, (unsigned long long)audio_clipped_samples); }
        std::fflush(nullptr);
        std::_Exit(51);
    }
    std::printf("GUEST_FRAMEBUFFER_CAPTURED: VI framebuffer 0x%08X, %ux%u, after %u processed graphics tasks.\n",
        address, width, height, graphics_tasks.load());
    std::puts("Completed GPU work and presentation queue checked; native RAM snapshot saved. Verify the actual game window separately.");
    std::fflush(nullptr);
    std::_Exit(50);
}
extern "C" void rush_diagnostic_graphics_processed(uint32_t list, uint32_t size) {
    if (!check_graphics_task) return;
    ++graphics_tasks;
    std::printf("GUEST_GRAPHICS_TASK_PROCESSED: RT64 processed guest display list 0x%08X (%u bytes).\n", list, size);
    if (check_framebuffer) return;
    std::puts("Diagnostic stop after display-list processing; presentation and audio playback remain unverified.");
    std::fflush(nullptr);
    std::_Exit(49);
}
static std::atomic<bool> game_code_verified{false};
static constexpr size_t expected_game_size=647072;
static std::atomic<bool> scheduler_created{false};
static std::atomic<bool> scheduler_entered{false};
static std::atomic<bool> pi_ready{false}, vi_ready{false}, init_created{false};
static void native_phase(const char* label) {
    const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    std::printf("NATIVE_PHASE: %s ns=%lld\n",label,(long long)ns);
}
static bool native_trace_sample(std::atomic<uint64_t>& calls) {
    static const bool verbose=std::getenv("RUSH_VERBOSE_TRACE")!=nullptr;
    const auto count=++calls;
    return verbose || count<=2 || count%600==0;
}
static std::atomic<uint64_t> native_vi_swaps{0};
static void require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "Boot check failed: %s\n", message); std::exit(1); }
}
extern "C" void rush_real_osViSetEvent_recomp(uint8_t*, recomp_context*);
extern "C" void osViSetEvent_recomp(uint8_t* rdram, recomp_context* ctx) {
    std::printf("VI_EVENT_REGISTER: queue=0x%08X message=0x%08X interval=%u caller=0x%08X\n", uint32_t(ctx->r4), uint32_t(ctx->r5), uint32_t(ctx->r6), uint32_t(ultramodern::this_thread()));
    rush_real_osViSetEvent_recomp(rdram, ctx);
}
extern "C" void rush_real_osSetEventMesg_recomp(uint8_t*, recomp_context*);
extern "C" void osSetEventMesg_recomp(uint8_t* rdram, recomp_context* ctx) {
    std::printf("EVENT_REGISTER: event=%u queue=0x%08X message=0x%08X caller=0x%08X\n", uint32_t(ctx->r4), uint32_t(ctx->r5), uint32_t(ctx->r6), uint32_t(ultramodern::this_thread()));
    if (check_startup_run && audio_dma_timing && uint32_t(ctx->r4) == OS_EVENT_AI) {
        std::lock_guard<std::mutex> guard(audio_mutex);
        audio_event_queue = uint32_t(ctx->r5);
        audio_event_message = uint32_t(ctx->r6);
        ctx->r2 = 0;
        std::puts("AI_COMPLETION_MODE: audio buffer lengths determine AI interrupts; controller events use the normal runtime.");
        return;
    }
    rush_real_osSetEventMesg_recomp(rdram, ctx);
}
extern "C" void rush_real_osStopThread_recomp(uint8_t*, recomp_context*);
extern "C" void rush_real_game_loop(uint8_t*, recomp_context*);
extern "C" void game_loop(uint8_t* rdram, recomp_context* ctx) {
    static unsigned calls = 0;
    if (check_framebuffer && calls++ < 2) std::puts("GUEST_MAIN_LOOP_ENTERED");
    rush_real_game_loop(rdram, ctx);
}
extern "C" void rush_real_game_mode_handler(uint8_t*, recomp_context*);
extern "C" void game_mode_handler(uint8_t* rdram, recomp_context* ctx) {
    static std::atomic<uint64_t> calls{0};
    const bool trace=check_framebuffer && native_trace_sample(calls);
    if (trace) std::puts("Guest startup mode handler entered.");
    rush_real_game_mode_handler(rdram, ctx);
    if (trace) std::puts("Guest startup mode handler returned.");
}
extern "C" void rush_real_attract_or_transition(uint8_t*, recomp_context*);
extern "C" void attract_or_transition(uint8_t* rdram, recomp_context* ctx) {
    static std::atomic<uint64_t> calls{0};
    const bool trace=check_framebuffer && native_trace_sample(calls);
    if (trace) std::puts("Guest mode transition entered.");
    rush_real_attract_or_transition(rdram, ctx);
    if (trace) std::puts("Guest mode transition returned.");
}
extern "C" void rush_real_osViSwapBuffer_recomp(uint8_t*, recomp_context*);
extern "C" void osViSwapBuffer_recomp(uint8_t* rdram, recomp_context* ctx) {
    const auto swap=++native_vi_swaps;
    if (check_framebuffer && (swap<=2 || swap%600==0)) std::printf("Guest VI swap: framebuffer=0x%08X count=%llu\n", uint32_t(ctx->r4),(unsigned long long)swap);
    rush_real_osViSwapBuffer_recomp(rdram, ctx);
}
extern "C" void rush_real_osViBlack_recomp(uint8_t*, recomp_context*);
extern "C" void osViBlack_recomp(uint8_t* rdram, recomp_context* ctx) {
    native_phase(uint32_t(ctx->r4) ? "vi_black" : "vi_visible");
    if (check_framebuffer) std::printf("Guest VI black: %u\n", uint32_t(ctx->r4));
    rush_real_osViBlack_recomp(rdram, ctx);
}
extern "C" void rush_real_osYieldThread_recomp(uint8_t*, recomp_context*);
extern "C" void osYieldThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    static unsigned calls = 0;
    if (check_framebuffer && graphics_tasks && calls++ < 2) {
        std::printf("Guest yielding thread 0x%08X\n", uint32_t(ultramodern::this_thread()));
        std::fputs("Guest yield observed.\n", stderr);
    }
    rush_real_osYieldThread_recomp(rdram, ctx);
}
extern "C" void rush_real_osSendMesg_recomp(uint8_t*, recomp_context*);
extern "C" void osSendMesg_recomp(uint8_t* rdram, recomp_context* ctx) {
    static unsigned calls = 0;
    if (check_framebuffer && graphics_tasks && calls++ < 35)
        std::printf("After graphics: send queue=0x%08X message=0x%08X caller=0x%08X\n", uint32_t(ctx->r4), uint32_t(ctx->r5), uint32_t(ultramodern::this_thread()));
    rush_real_osSendMesg_recomp(rdram, ctx);
}
extern "C" void osStopThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    static std::atomic<uint64_t> calls{0};
    const bool trace=native_trace_sample(calls);
    if (trace) std::printf("Guest stop-thread request: target=0x%08X caller=0x%08X return=0x%08X\n",
        uint32_t(ctx->r4), uint32_t(ultramodern::this_thread()), uint32_t(ctx->r31));
    rush_real_osStopThread_recomp(rdram, ctx);
    if (trace) std::puts("Guest stop-thread call returned.");
}
extern "C" void rush_real_osPiReadIo_recomp(uint8_t*, recomp_context*);
extern "C" void osPiReadIo_recomp(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t offset = uint32_t(ctx->r4);
    rush_real_osPiReadIo_recomp(rdram, ctx);
    if (int64_t(ctx->r2) == -1) {
        std::printf("BOOT_BLOCKED: osPiReadIo offset=0x%08X ROM_size=0x%08zX\n",
            offset, recomp::get_rom().size());
        std::puts("Real runtime entered guest boot and main. Cartridge behavior for this read is unresolved.");
        std::puts("Diagnostic stop before continuing with an unsupported cartridge read. No game frame or audio playback verified.");
        // A process boundary is intentional: this early boot
        // diagnostic must not unwind through guest code or active runtime workers.
        std::fflush(nullptr);
        std::_Exit(42);
    }
}
static void guest_entry(uint8_t* rdram, recomp_context* ctx) {
    native_phase("guest_boot_entry");
    std::puts("GUEST_BOOT_ENTERED: executing the recompiled ROM entrypoint.");
    recomp_entrypoint(rdram, ctx);
    std::puts("Guest boot entry returned after starting the idle thread.");
}
extern "C" void rush_real_osCreateThread_recomp(uint8_t*, recomp_context*);
extern "C" void osCreateThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    const auto id = uint32_t(ctx->r5);
    const auto entry = uint32_t(ctx->r6);
    const auto stack = uint32_t(MEM_W(0x10, ctx->r29));
    const auto priority = uint32_t(MEM_W(0x14, ctx->r29));
    std::printf("Guest requested thread %u: entry=0x%08X stack=0x%08X priority=%u\n", id, entry, stack, priority);
    bool verified_later_thread = false;
    if (check_render_loop && game_code_verified && id != 1 && id != 6 && id != 4 && id != 8 && id != 5) {
        for (unsigned section = 0; section < 2; ++section) {
            for (size_t index = 0; index < section_table[section].num_funcs; ++index) {
                const auto& function = section_table[section].funcs[index];
                if (section_table[section].ram_addr + function.offset == entry)
                    verified_later_thread = get_function(int32_t(entry)) == function.func;
            }
        }
        verified_later_thread = verified_later_thread && stack >= 0x8002E8E0 && stack <= 0x80800000 &&
            (stack & 7) == 0 && id <= 255 && priority <= 255;
    }
    require((id == 1 && entry == 0x800021A4 && stack == 0x8002F7F0 && priority == 2) ||
        (check_game_init && id == 6 && entry == 0x80002238 && stack == 0x80030AB0 && priority == 4) ||
        (check_decompress && id == 4 && entry == 0x8000063C && stack == 0x800344E0 && priority == 12) ||
        (check_game_code && id == 8 && entry == 0x800AC75C && stack == 0x80030150 && priority == 3) ||
        (check_game_code && id == 5 && entry == 0x800E7808 && stack == 0x800326D0 && priority == 7) || verified_later_thread,
        "unexpected guest thread descriptor");
    if (id == 6) require(pi_ready, "game thread preceded PI-manager call");
    if (id == 4) require(vi_ready && init_created, "scheduler preceded game initialization");
    if (id == 8 || id == 5) {
        require(game_code_verified, "loaded-code thread preceded code verification");
        require(get_function(int32_t(entry)) == recomp::overlays::get_func_by_section_rom_function_vram(0xC00000, entry),
            "loaded thread dispatch differs from verified game section");
    }
    std::printf("Creating real guest thread %u: entry=0x%08X stack=0x%08X priority=%u\n",
        id, entry, stack, priority);
    rush_real_osCreateThread_recomp(rdram, ctx);
    std::printf("Runtime created guest thread %u.\n", id);
    if (id == 6) init_created.store(true);
    if (id == 4) scheduler_created.store(true);
}
extern "C" void rush_real_osStartThread_recomp(uint8_t*, recomp_context*);
extern "C" void osStartThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    static std::atomic<uint64_t> calls{0};
    const bool trace=native_trace_sample(calls);
    const auto address = uint32_t(ctx->r4);
    if (trace) std::printf("Starting guest thread descriptor 0x%08X\n", address);
    rush_real_osStartThread_recomp(rdram, ctx);
    if (trace) std::printf("Guest thread start returned for 0x%08X\n", address);
}
extern "C" void rush_real_osRecvMesg_recomp(uint8_t*, recomp_context*);
extern "C" void osRecvMesg_recomp(uint8_t* rdram, recomp_context* ctx) {
    static std::atomic<unsigned> traced{0};
    const auto queue_address = uint32_t(ctx->r4), result_address = uint32_t(ctx->r5);
    static unsigned later_waits = 0;
    if (check_framebuffer && graphics_tasks && later_waits++ < 20)
        std::printf("After graphics: waiting queue=0x%08X caller=0x%08X blocking=%u\n", queue_address, uint32_t(ultramodern::this_thread()), uint32_t(ctx->r6));
    if (check_decompress && uint32_t(ctx->r4) == 0x8002E928) scheduler_entered.store(true);
    if (check_decompress && traced.fetch_add(1) < 8)
        std::printf("Guest receiving message: queue=0x%08X block=%u\n", uint32_t(ctx->r4), uint32_t(ctx->r6));
    rush_real_osRecvMesg_recomp(rdram, ctx);
    static unsigned received = 0;
    if (check_framebuffer && graphics_tasks && result_address && int64_t(ctx->r2) == 0 && received++ < 35) {
        const int64_t pointer = int64_t(int32_t(result_address));
        const auto message = uint32_t(MEM_W(0, pointer));
        const int64_t msg_pointer = int64_t(int32_t(message));
        const auto type = (message & 0xC0000000) == 0x80000000 && (message & 0x1FFFFFFF) < 0x800000 ? uint16_t(MEM_H(0, msg_pointer)) : 0;
        std::printf("Received: queue=0x%08X message=0x%08X type=0x%X caller=0x%08X\n", queue_address, message, type, uint32_t(ultramodern::this_thread()));
    }
    if (check_render_loop && queue_address == 0x8017A498 && int64_t(ctx->r2) == 0) {
        const int64_t result_pointer = int64_t(int32_t(result_address));
        const auto message = uint32_t(MEM_W(0, result_pointer));
        const int64_t message_pointer = int64_t(int32_t(message));
        require(message == 0x8002E8E8 && MEM_H(0, message_pointer) == 1,
            "unexpected scheduler retrace message");
        static bool retrace_reported = false;
        if (!retrace_reported) std::puts("GUEST_RENDER_RETRACE_RECEIVED: initialized render thread received a real runtime VI event through the guest scheduler.");
        retrace_reported = true;
        if (check_graphics_task) return;
        std::puts("Diagnostic stop before processing the retrace. No game frame or audio playback verified.");
        std::fflush(nullptr);
        std::_Exit(48);
    }
}
extern "C" void rush_real_osPiStartDma_recomp(uint8_t*, recomp_context*);
extern "C" void osPiStartDma_recomp(uint8_t* rdram, recomp_context* ctx) {
    if (check_decompress) {
          const auto device_address = uint32_t(ctx->r7), destination = uint32_t(MEM_W(0x10, ctx->r29));
          // Match the runtime's cartridge-base and KSEG address conversion.
          const auto physical_address = (device_address | 0x10000000u) & 0x1FFFFFFFu;
          require(physical_address >= 0x10000000u, "guest DMA is not a ROM transfer");
          const auto offset = physical_address - 0x10000000u;
          const auto size = uint32_t(MEM_W(0x14, ctx->r29));
          if (offset > recomp::get_rom().size() || size > recomp::get_rom().size() - offset)
              std::fprintf(stderr, "DMA_RANGE_FAILURE: direction=%u offset=0x%08X destination=0x%08X size=0x%X caller=0x%08X\n",
                  uint32_t(ctx->r6), offset, destination, size, uint32_t(ultramodern::this_thread()));
        require(offset <= recomp::get_rom().size() && size <= recomp::get_rom().size() - offset,
            "guest DMA exceeds ROM bytes");
        require((destination & 0x1FFFFFFF) <= 0x800000 && size <= 0x800000 - (destination & 0x1FFFFFFF),
            "guest DMA exceeds RDRAM");
        if (++rom_dma_count <= 3) std::printf("Guest ROM DMA: offset=0x%08X destination=0x%08X size=0x%X\n",
            offset, destination, size);
    }
    rush_real_osPiStartDma_recomp(rdram, ctx);
    if (check_decompress) require(int64_t(ctx->r2) == 0, "guest ROM DMA failed");
}
extern "C" void rush_real_osCreatePiManager_recomp(uint8_t*, recomp_context*);
extern "C" void osCreatePiManager_recomp(uint8_t* rdram, recomp_context* ctx) {
    require(uint32_t(ctx->r4) == 150 && uint32_t(ctx->r7) == 200,
        "unexpected idle-thread PI manager arguments");
    std::puts("GUEST_IDLE_THREAD_ENTERED: actual runtime thread dispatched and executed guest instructions.");
    if (check_game_init) {
        rush_real_osCreatePiManager_recomp(rdram, ctx);
        pi_ready.store(true);
        std::puts("Guest PI-manager call returned through the runtime implementation.");
        return;
    }
    std::puts("Diagnostic stop at the PI-manager call before further guest threads or devices are started.");
    std::fflush(nullptr);
    std::_Exit(44);
}
extern "C" void rush_real_osCreateViManager_recomp(uint8_t*, recomp_context*);
extern "C" void osCreateViManager_recomp(uint8_t* rdram, recomp_context* ctx) {
    require(check_game_init && pi_ready, "unexpected VI-manager call");
    rush_real_osCreateViManager_recomp(rdram, ctx);
    vi_ready.store(true);
    std::puts("Guest VI-manager call returned through the runtime implementation.");
}
extern "C" void rush_real_osCreateMesgQueue_recomp(uint8_t*, recomp_context*);
extern "C" void osCreateMesgQueue_recomp(uint8_t* rdram, recomp_context* ctx) {
    static unsigned count = 0;
    constexpr uint32_t queues[] = {0x80035410, 0x80035428, 0x8002ECF8, 0x8002ECC0};
    constexpr uint32_t buffers[] = {0x8002F140, 0x8002F160, 0x8002ED10, 0x8002ECD8};
    constexpr uint32_t capacities[] = {8, 8, 60, 8};
    require(check_game_init && init_created && (count < 4 || check_decompress), "unexpected startup queue call");
    const auto address = uint32_t(ctx->r4), buffer = uint32_t(ctx->r5), capacity = uint32_t(ctx->r6);
    if (count < 4) require(address == queues[count] && buffer == buffers[count] &&
        capacity == capacities[count], "startup queue descriptor mismatch");
    const auto queue_offset = address & 0x1FFFFFFF, buffer_offset = buffer & 0x1FFFFFFF;
    require((address & 0xC0000000) == 0x80000000 && (buffer & 0xC0000000) == 0x80000000 &&
        (address & 3) == 0 && (buffer & 3) == 0 &&
        queue_offset <= 0x800000 - sizeof(OSMesgQueue) && capacity > 0 &&
        capacity <= INT32_MAX && uint64_t(buffer_offset) + uint64_t(capacity) * sizeof(OSMesg) <= 0x800000,
        "queue range mismatch");
    rush_real_osCreateMesgQueue_recomp(rdram, ctx);
    auto* queue = reinterpret_cast<OSMesgQueue*>(rdram + (address & 0x1FFFFFFF));
    require(queue->blocked_on_recv == 0 && queue->blocked_on_send == 0 && queue->validCount == 0 &&
        queue->first == 0 && queue->msgCount == int32_t(capacity) &&
        uint32_t(queue->msg) == buffer, "runtime queue state mismatch");
    std::printf("Verified startup queue %u: address=0x%08X capacity=%u\n", count + 1, address, capacity);
    ++count;
    if (check_game_code && address == 0x8017A498) {
        require(game_code_verified && buffer == 0x8017A4B8 && capacity == 8 &&
            uint32_t(ctx->r29) >= 0x80031D70 && uint32_t(ctx->r29) < 0x800326D0,
            "loaded render-thread queue or stack mismatch");
        std::puts("GUEST_GAME_CODE_EXECUTED: real thread 5 dispatched loaded code at 0x800E7808 and created its verified queue.");
        if (check_render_loop) return;
        std::puts("Diagnostic stop before the render thread continues initialization or submits graphics tasks.");
        std::fflush(nullptr);
        std::_Exit(47);
    }
    if (count == 4) {
        std::puts("GUEST_GAME_INIT_ENTERED: real thread 6 executed guest initialization and created four verified queues.");
        if (check_decompress) return;
        std::puts("Diagnostic stop before scheduler creation, code decompression, and further guest startup.");
        std::fflush(nullptr);
        std::_Exit(45);
    }
}
extern "C" void rush_real_inflate_entry(uint8_t*, recomp_context*);
extern "C" void inflate_entry(uint8_t* rdram, recomp_context* ctx) {
    const auto source = uint32_t(ctx->r4), destination = uint32_t(ctx->r5);
    rush_real_inflate_entry(rdram, ctx);
    if (destination != 0x8038A400) return;
    const size_t count=source==0xB5C534?194128:source==0xB6FEC4?43888:0;
    const char* digest=source==0xB5C534?"0d6702c320df84cc6cbc3c5967dde08d44b6e476e110667fe2a43dc2dd536667":
        "b55fc2d1b22eb1ebdf01286a69a181b496da7b45ff7aec888ff74b7db748e7cd";
    require(count && rush::rom::guest_image(rdram,0x38A400,count,digest), "guest overlay differs from verified image");
    load_overlays(source==0xB5C534?0xC9DFA0:0xCCD5F0,int32_t(destination),count);
    std::printf("GUEST_OVERLAY_REGISTERED: source=0x%08X destination=0x%08X, SHA256 verified %zu bytes.\n",source,destination,count);
}
extern "C" void rush_real_bzero_recomp(uint8_t*, recomp_context*);
extern "C" void bzero_recomp(uint8_t* rdram, recomp_context* ctx) {
    const auto address = uint32_t(ctx->r4), size = uint32_t(ctx->r5);
    rush_real_bzero_recomp(rdram, ctx);
    if (check_decompress && address == 0x801249F0 && size == 0x55C50) {
        require(scheduler_created && scheduler_entered, "scheduler did not execute before decompression");
        require(rush::rom::guest_image(rdram,0x86A50,expected_game_size,
            "bf7da3fa6283428a97372250cd4076d15e9eae10f9d5709c0387fe0742d43a1d"),
            "guest-decompressed game differs from verified image");
        for (uint32_t i = 0; i < size; ++i)
            require(rdram[(0x1249F0 + i) ^ 3] == 0, "game BSS not cleared");
        load_overlays(0xC00000, int32_t(0x80086A50), expected_game_size);
        std::printf("Native decompression used %u bounded ROM DMA transfers.\n", rom_dma_count);
        std::puts("GUEST_CODE_DECOMPRESSED: native guest decompressor output matches all 647072 verified bytes; game BSS cleared.");
        native_phase("game_code_verified");
        game_code_verified.store(true);
        if (check_game_code) {
            std::puts("Game section dispatch registered; continuing into loaded guest code.");
            return;
        }
        std::puts("Game section function dispatch registered. Diagnostic stop before calling decompressed game code.");
        std::fflush(nullptr);
        std::_Exit(46);
    }
}
static void update_window(void*) {
    // Audio completion delivery runs independently of graphics updates.
    static bool requested = false;
    if (!requested) {
        requested = true;
        recomp::start_game(u8"rush2049", "");
    }
    static const auto bounded_start=std::chrono::steady_clock::now();
    static bool bounded_quit=false;
    const char* bound=std::getenv("RUSH_RUN_SECONDS");
    if (bound && !bounded_quit && std::chrono::steady_clock::now()-bounded_start >= std::chrono::seconds(std::atoi(bound))) {
        bounded_quit=true;
        std::puts("NATIVE_BOUND_REACHED: requesting runtime shutdown");
        ultramodern::quit(); return;
    }
    SDL_Event event;
      while (SDL_PollEvent(&event)) {
          if(rush::pc::event(event))continue;
          if(event.type==rush::pc::action_event && event.user.code==300){controller_input->open_remapping(rush::pc::game);continue;}
          if (controller_input) controller_input->handle_event(event);
          else keyboard_input.handle_event(event);
          if (event.type == SDL_QUIT) { std::puts("NATIVE_QUIT_REQUESTED"); ultramodern::quit(); return; }
          if (event.type == SDL_KEYDOWN && !event.key.repeat &&
              (event.key.keysym.scancode == SDL_SCANCODE_RETURN || event.key.keysym.scancode == SDL_SCANCODE_Z)) {
              std::lock_guard<std::mutex> guard(audio_mutex);
              const uint32_t queued = audio_host_queued_bytes();
              std::printf("MENU_BUTTON_AUDIO: %s at %lld ms; %.1f ms of audio already queued.\n",
                  event.key.keysym.scancode == SDL_SCANCODE_RETURN ? "Enter" : "Z",
                  (long long)steady_ms(), queued * 250.0 / audio_host_frequency);
          }
      }
      const uint64_t dropped_taps = keyboard_input.take_dropped_tap_count();
      if(const int action=rush::pc::pending.exchange(0)){if(action==300)controller_input->open_remapping(rush::pc::game);else if(action==401)rush::pc::popup(rush::pc::cheatMenu);else if(action==409)rush::pc::popup(rush::pc::displayMenu);else rush::pc::command(action);}
      if (dropped_taps != 0)
          std::fprintf(stderr, "Keyboard input queue full; dropped %llu short tap(s).\n",
              (unsigned long long)dropped_taps);
}
extern "C" bool rush_controller_pak_attached();
int wmain(int argc, wchar_t** wide_argv) {
    require(argc==3 || argc==4,"pass the external ROM path and player profile directory");
    std::vector<std::string> arguments;
    std::vector<char*> argument_pointers;
    for(int i=0;i<argc;++i)arguments.push_back(rush::rom::utf8(wide_argv[i]));
    for(auto& argument:arguments)argument_pointers.push_back(argument.data());
    char** argv=argument_pointers.data();
    std::unique_ptr<rush::rom::File> external_rom;
    try { external_rom=std::make_unique<rush::rom::File>(std::filesystem::path(wide_argv[1])); }
    catch(const std::exception& error) {std::fprintf(stderr,"ROM_REQUIRED: %s\n",error.what());return 65;}
    std::puts("EXTERNAL_ROM_VERIFIED: original US ROM read from selected location; no stored ROM fallback");
    try { (void)rush_controller_pak_attached(); }
    catch (const std::exception& error) {
        std::fprintf(stderr, "Controller Pak initialization failed: %s\n", error.what());
        return 1;
    }
    const char* audio_timing_option = std::getenv("RUSH_AUDIO_DMA_TIMING");
    audio_dma_timing = !audio_timing_option || std::string_view(audio_timing_option) != "0";
    const char* windows_audio_option = std::getenv("RUSH_WINDOWS_AUDIO");
    audio_windows_output = windows_audio_option && std::string_view(windows_audio_option) == "1";
    if (audio_windows_output) audio_dma_timing = true;
    require(argc == 3 || argc == 4, "pass original ROM and diagnostic data directory");
    if (argc == 4) {
        require(std::string_view(argv[3]) == "game-init" || std::string_view(argv[3]) == "decompress" ||
            std::string_view(argv[3]) == "game-code" || std::string_view(argv[3]) == "render-loop" ||
            std::string_view(argv[3]) == "graphics-task" || std::string_view(argv[3]) == "framebuffer" || std::string_view(argv[3]) == "startup-run" || std::string_view(argv[3]) == "menu-test" || std::string_view(argv[3]) == "track-test" || std::string_view(argv[3]) == "play", "unknown diagnostic stage");
        check_game_init = true;
        check_track_test = std::string_view(argv[3]) == "track-test";
        check_menu_test = std::string_view(argv[3]) == "menu-test" || check_track_test;
        check_play = std::string_view(argv[3]) == "play";
        check_startup_run = std::string_view(argv[3]) == "startup-run" || check_menu_test || check_play;
        check_framebuffer = std::string_view(argv[3]) == "framebuffer" || check_startup_run;
        check_graphics_task = std::string_view(argv[3]) == "graphics-task" || check_framebuffer;
        check_render_loop = std::string_view(argv[3]) == "render-loop" || check_graphics_task;
        check_game_code = std::string_view(argv[3]) == "game-code" || check_render_loop;
        check_decompress = std::string_view(argv[3]) == "decompress" || check_game_code;
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    auto rom=external_rom->bytes;
    std::filesystem::create_directories(wide_argv[2]);
    recomp::register_config_path(std::filesystem::path(wide_argv[2]));
    recomp::GameEntry game{};
    game.rom_hash = XXH3_64bits(rom.data(), rom.size());
    game.internal_name.assign(reinterpret_cast<const char*>(rom.data() + 0x20), 20);
    game.display_name = "Rush 2049 diagnostic";
    game.game_id = u8"rush2049";
    game.is_enabled = true;
    game.entrypoint_address = int64_t(int32_t(0x80000400u));
    game.entrypoint = guest_entry;
    require(recomp::register_game(game), "game registration failed");
    require(recomp::select_rom(std::filesystem::path(wide_argv[1]), game.game_id, false) == recomp::RomValidationError::Good, "ROM selection failed");
    recomp::set_rom_contents(std::move(rom));
    recomp::overlays::register_overlays({section_table, num_sections, num_sections}, {nullptr, 0});
    require(SDL_Init(SDL_INIT_VIDEO | rush::input::sdl_init_flags | (check_startup_run && !audio_windows_output ? SDL_INIT_AUDIO : 0)) == 0, SDL_GetError());
    controller_input = std::make_unique<rush::input::System>(keyboard_input);
    auto* window = SDL_CreateWindow(_wgetenv(L"RUSH_REMASTER_TEXTURE_PACK")?"Rush 2049 Remastered 4x":"Rush 2049 Original PC", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_VULKAN | (check_framebuffer ? 0 : SDL_WINDOW_HIDDEN));
    require(window != nullptr, SDL_GetError());
    recomp::Configuration cfg{};
    cfg.project_version = {0, 0, 1, "diagnostic"};
    SDL_SysWMinfo native{};
    SDL_VERSION(&native.version);
    require(SDL_GetWindowWMInfo(window, &native) == SDL_TRUE, SDL_GetError());
    if(check_startup_run)rush::pc::attach(window,native.info.win.window,std::filesystem::path(wide_argv[2]).parent_path());
    ShowWindow(native.info.win.window, SW_SHOW);
    cfg.window_handle = {native.info.win.window, GetCurrentThreadId()};
    cfg.rsp_callbacks = rush_rsp_callbacks();
    cfg.renderer_callbacks = rush_renderer_callbacks();
    if (check_startup_run) {
        cfg.audio_callbacks.queue_samples = audio_queue;
        cfg.audio_callbacks.get_frames_remaining = audio_remaining;
        cfg.audio_callbacks.set_frequency = audio_frequency;
        cfg.input_callbacks.poll_input = [] {};
        cfg.input_callbacks.get_connected_device_info = [](int port) {
            return ultramodern::input::connected_device_info_t{
                controller_input && controller_input->connected(port) ? ultramodern::input::Device::Controller : ultramodern::input::Device::None,
                port == 0 && rush_controller_pak_attached() ? ultramodern::input::Pak::ControllerPak : ultramodern::input::Pak::None};
        };
        cfg.input_callbacks.get_input = [](int port, uint16_t* buttons, float* x, float* y) {
            if (!controller_input || !controller_input->get_input(port, buttons, x, y)) return false;
            if(port==0){uint16_t cheat_buttons=0;if(rush::pc::playback.sample(steady_ms(),cheat_buttons)){*buttons=cheat_buttons;*x=*y=0;}}
            if(port == 0 && steady_ms()<native_script_deadline.load()) {
                *buttons|=native_script_buttons.load();
                *x=native_script_x.load(); *y=native_script_y.load();
            }
            const auto elapsed = first_visible_ms.load() ? steady_ms() - first_visible_ms.load() : 0;
            if (port == 0 && check_menu_test && ((elapsed >= 8000 && elapsed < 9000) || (elapsed >= 18000 && elapsed < 19000))) {
                static unsigned logged = 0;
                const unsigned press = elapsed >= 18000 ? 2 : 1;
                if (logged != press) { std::printf("MENU_TEST_START_PRESSED: press %u through the actual controller callback.\n", press); logged = press; }
                *buttons |= 0x1000;
            }
            if (port == 0 && check_track_test && elapsed >= 26000 && elapsed < 26200) {
                static bool logged = false;
                if (!logged) { std::puts("TRACK_TEST_CONFIRM_PRESSED: A through the actual controller callback."); logged = true; }
                *buttons |= 0x8000;
            }
            if (port == 0 && check_track_test && elapsed >= 35000 && elapsed < 35200) {
                static bool logged = false;
                if (!logged) { std::puts("TRACK_TEST_RIGHT_PRESSED: stick through the actual controller callback."); logged = true; }
                *x = 1.0f;
            }
            static std::array<uint64_t, rush::input::port_count> polls{};
            static std::array<uint16_t, rush::input::port_count> previous_buttons{};
            ++polls[port];
            if(*buttons!=previous_buttons[port] || polls[port]%600==0) {
                std::printf("NATIVE_INPUT_SAMPLE: port=%d poll=%llu buttons=%u x=%.2f y=%.2f\n",port,(unsigned long long)polls[port],*buttons,*x,*y);
                previous_buttons[port]=*buttons;
            }
            return true;
        };
        cfg.input_callbacks.set_rumble = [](int, bool) {};
        std::printf("SDL controller backend and configured keyboard port connected: Enter=Start, Z=A, X=B, Space=Z trigger, arrows=stick, Q/E=L/R, I/J/K/L=C up/left/down/right. Controller Pak %s.\n", rush_controller_pak_attached() ? "connected" : "absent");
    }
    cfg.gfx_callbacks.update_gfx = update_window;
    cfg.events_callbacks.gfx_init_callback = [] { std::puts("RUNTIME_RENDERER_READY: RT64 initialized by the runtime graphics thread."); };
    cfg.error_handling_callbacks.message_box = [](const char* message) { std::fprintf(stderr, "Runtime error: %s\n", message); };
    std::jthread native_input_worker;
    if (std::getenv("RUSH_NATIVE_INPUT")) {
        native_input_worker=std::jthread([](std::stop_token stop) {
            uint64_t previous=0;
            while(!stop.stop_requested()) {
    if(const char* input=std::getenv("RUSH_NATIVE_INPUT")) {
        std::ifstream stream(input);
        uint64_t serial=0; unsigned buttons=0,duration=0; float x=0,y=0;
        if(stream>>serial>>buttons>>x>>y>>duration && serial!=previous) {
            previous=serial;
            native_script_buttons=uint16_t(buttons); native_script_x=x;native_script_y=y;
            native_script_deadline=steady_ms()+duration;
            std::printf("NATIVE_INPUT: serial=%llu buttons=%u x=%.2f y=%.2f duration=%u\n",(unsigned long long)serial,buttons,x,y,duration);
        }
    }

                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        });
    }
    std::jthread windows_audio_worker;
    if (check_startup_run) {
        windows_audio_worker = std::jthread([](std::stop_token stop) {
            while (!stop.stop_requested()) {
                audio_service_completions();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }
    std::puts(check_startup_run ? (audio_windows_output ? "Starting integrated runtime with keyboard input and private Windows audio output." : "Starting integrated runtime with keyboard input and SDL audio output.") : "Starting integrated runtime boot diagnostic. Audio playback is not installed.");
    recomp::start(cfg);
    native_input_worker.request_stop();
    if(native_input_worker.joinable()) native_input_worker.join();
    windows_audio_worker.request_stop();
    if (windows_audio_worker.joinable()) windows_audio_worker.join();
    std::printf("NATIVE_VI_TOTAL: %llu\n",(unsigned long long)native_vi_swaps.load());
    std::puts("NATIVE_RUNTIME_RETURNED: shutdown completed");
    controller_input->manager().shutdown();
    controller_input.reset();
    rush::pc::detach();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
