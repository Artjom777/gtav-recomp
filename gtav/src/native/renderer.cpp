// gtav - native renderer: render thread, Plume device and the shadow window.

#include "renderer.h"

#include "backend.h"
#include "frame.h"
#include "paths.h"
#include "shader_compiler.h"

#include <plume_render_interface.h>

#ifdef _WIN32
// rexruntime.dll links SDL statically and exports none of it, so on Windows the native window is a
// plain Win32 window (Plume's HWND path) instead of an SDL one.
#include <windows.h>
#else
#include <SDL3/SDL.h>
#endif

#include <rex/cvar.h>
#include <rex/logging.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <set>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

REXCVAR_DEFINE_STRING(gtav_native, "off", "GTAV", "Native renderer: off | shadow | on");
REXCVAR_DEFINE_BOOL(gtav_native_vsync, true, "GTAV", "Native renderer: wait for vertical sync when presenting");
REXCVAR_DEFINE_BOOL(gtav_native_fps, false, "GTAV", "Native renderer: on-screen frames-per-second counter");
REXCVAR_DEFINE_BOOL(gtav_native_hidden, false, "GTAV",
                    "Native renderer: keep every game window hidden (unattended test runs)");
REXCVAR_DEFINE_STRING(gtav_native_device, "NVIDIA", "GTAV",
                      "Substring of the Vulkan device name the native renderer prefers");
REXCVAR_DEFINE_STRING(gtav_native_spirv_dir, "", "GTAV",
                      "Directory of precompiled shaders (<vs|ps>_<hash>.spv); default derive/native/census1/spirv");

namespace plume {
std::unique_ptr<RenderInterface> CreateVulkanInterface();  // SDL3 build: window comes later
void SetVulkanPipelineCacheData(std::vector<uint8_t> data);  // plume-gtav.patch
std::vector<uint8_t> GetVulkanPipelineCacheData(RenderDevice* device);
}

namespace gtav::native {
namespace {

// Frames queued for the render thread before Submit blocks the guest (like a full GPU ring). No
// frame is ever dropped: the game renders some textures once (static shadow-map caches, ...) and
// reuses them for many frames, so a dropped frame lost them for good.
constexpr size_t kMaxQueuedFrames = 2;
constexpr uint32_t kSwapchainBuffers = 3;  // two frames in flight + one being presented

class Renderer {
 public:
  // The command processor only fakes the GPU (--gtav_native=on): it finishes a frame long before
  // this renderer replays it, and the fences after its swap tell the game that frame's memory is
  // free, so the game streams new textures and vertex data over memory a queued frame still has
  // to read (wrong textures flashing, spiky meshes). The guest's swaps are queued in order; the
  // command processor's swap N waits until the renderer has recorded guest swap N.
  void NoteGuestSwap(uint64_t frame_index) {
    std::lock_guard lock(swap_mutex_);
    guest_swaps_.push_back(frame_index);
    last_noted_ = frame_index;
    if (guest_swaps_.size() > 64) guest_swaps_.pop_front();
  }

  void WaitForGuestSwap() {
    uint64_t index;
    {
      std::lock_guard lock(swap_mutex_);
      if (guest_swaps_.empty()) return;
      index = guest_swaps_.front();
      guest_swaps_.pop_front();
    }
    auto start = std::chrono::steady_clock::now();
    bool waited = false, timed_out = false;
    {
      std::unique_lock lock(recorded_mutex_);
      if (recorded_through_ <= index) {
        waited = true;
        // Bounded: a renderer that stopped must not stall the guest's GPU for good.
        timed_out = !recorded_cv_.wait_for(lock, std::chrono::milliseconds(1000),
                                           [&] { return recorded_through_ > index; });
      }
    }
    auto& st = swap_wait_stats_;
    st.swaps++;
    if (waited) {
      st.waits++;
      double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
      st.ms_sum += ms;
      st.ms_max = std::max(st.ms_max, ms);
    }
    if (timed_out) {
      st.timeouts++;
      size_t queued;
      {
        std::lock_guard lock(mutex_);
        queued = queue_.size();
      }
      static uint32_t logged = 0;
      if (logged++ < 20)
        REXLOG_INFO("[native] swap wait timed out: waiting for guest frame {}, recorded through {}, last noted {}, "
                    "{} queued, renderer state {} on frame {}",
                    index, recorded_through_, last_noted_.load(), queued, render_state_.load(), render_frame_.load());
    }
    if (st.swaps == 600) {
      REXLOG_INFO("[native] command processor waited for the renderer at {} of 600 swaps (avg {:.2f} max {:.2f} ms, "
                  "{} timeouts), guest frame {}",
                  st.waits, st.waits ? st.ms_sum / st.waits : 0.0, st.ms_max, st.timeouts, index);
      st = {};
    }
  }

  void Submit(FramePacket&& packet) {
    if (!started_.exchange(true)) thread_ = std::thread([this] { ThreadMain(); });
    {
      std::unique_lock lock(mutex_);
      space_cv_.wait(lock, [this] { return queue_.size() < kMaxQueuedFrames || failed_; });
      if (failed_) {  // no renderer: nothing will drain the queue
        dropped_++;
        Recycle(std::move(packet));
        return;
      }
      queue_.push_back(std::move(packet));
    }
    cv_.notify_one();
  }

  FramePacket TakeSpare() {
    std::lock_guard lock(mutex_);
    if (spare_.empty()) return FramePacket{};
    FramePacket p = std::move(spare_.back());
    spare_.pop_back();
    return p;
  }

 private:
  void Recycle(FramePacket&& p) {  // caller holds mutex_
    if (spare_.size() >= kMaxQueuedFrames + 2) return;
    p.Clear();
    spare_.push_back(std::move(p));
  }

#ifdef _WIN32
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // F9 (dump the next frame to logs/native_dump) and F10 (screenshot to logs/shots) are polled
    // by the window thread's timer, so they work from the SDK's window too (shadow mode).
    if (msg == WM_CLOSE) {  // the native window is the game window: closing it quits
      REXLOG_INFO("[native] window closed, exiting");
      ExitProcess(0);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
  }

  // The SDK's windows (SDL class "SDL_app") belong to this process; hide every visible one
  // except ours. Repeated from the message thread because the SDK may show its window late.
  void HideSdkWindows() {
    struct Ctx { HWND self; } ctx{window_};
    EnumWindows(
        [](HWND hwnd, LPARAM lp) -> BOOL {
          auto* c = reinterpret_cast<Ctx*>(lp);
          DWORD pid = 0;
          GetWindowThreadProcessId(hwnd, &pid);
          if (pid != GetCurrentProcessId() || hwnd == c->self || !IsWindowVisible(hwnd)) return TRUE;
          wchar_t cls[64];
          // Async: ShowWindow on another thread's window waits for that thread (the SDK's main
          // thread, busy for seconds during a cold boot), and this thread must keep servicing the
          // native window while the renderer creates its swap chain.
          if (GetClassNameW(hwnd, cls, 64) && std::wstring(cls).rfind(L"SDL", 0) == 0)
            ShowWindowAsync(hwnd, SW_HIDE);
          return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
  }

  // Window + message pump on their own thread (a Win32 window is serviced by its creating thread).
  bool CreateNativeWindow() {
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    window_thread_ = std::thread([&, this] {
      const bool on = REXCVAR_GET(gtav_native) == "on";
      WNDCLASSEXW wc{sizeof(wc)};
      wc.lpfnWndProc = WndProc;
      wc.hInstance = GetModuleHandleW(nullptr);
      wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
      wc.lpszClassName = L"GtavNative";
      RegisterClassExW(&wc);
      RECT rc{0, 0, 1280, 720};
      AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
      HWND hwnd = CreateWindowExW(0, wc.lpszClassName, on ? L"GTA V" : L"GTA V - native renderer (shadow)",
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left,
                                  rc.bottom - rc.top, nullptr, nullptr, wc.hInstance, nullptr);
      {
        std::lock_guard lock(m);
        window_ = hwnd;
        done = true;
        cv.notify_one();  // under the lock: the waiter's cv/mutex die once it returns
      }
      if (!hwnd) return;
      const bool hidden = REXCVAR_GET(gtav_native_hidden);
      if (!hidden) {
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
      }
      if (on || hidden) HideSdkWindows();
      SetTimer(hwnd, 1, 500, nullptr);  // hide the SDK window again if it reappears ("on")
      SetTimer(hwnd, 2, 50, nullptr);   // F9/F10 while either game window has focus
      MSG msg;
      bool f9_down = false, f10_down = false;
      while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_TIMER && msg.hwnd == hwnd) {
          if (msg.wParam == 1) {
            if (on || hidden) HideSdkWindows();
          } else {
            DWORD pid = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &pid);
            bool ours = pid == GetCurrentProcessId();
            bool f9 = ours && (GetAsyncKeyState(VK_F9) & 0x8000), f10 = ours && (GetAsyncKeyState(VK_F10) & 0x8000);
            if (f9 && !f9_down) key_dump_ = true;
            if (f10 && !f10_down) key_shot_ = true;
            f9_down = f9, f10_down = f10;
          }
          continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
    });
    window_thread_.detach();
    std::unique_lock lock(m);
    cv.wait(lock, [&] { return done; });
    if (!window_) REXLOG_ERROR("[native] CreateWindowEx failed: {}", GetLastError());
    return window_ != nullptr;
  }
#endif

  bool Init() {
#ifdef _WIN32
    if (!CreateNativeWindow()) return false;
#else
    SDL_RunOnMainThread(
        [](void* self) {
          auto* r = static_cast<Renderer*>(self);
          // --gtav_native=on: the emulated GPU presents nothing, so its window is hidden and
          // this one becomes the game window.
          bool on = REXCVAR_GET(gtav_native) == "on";
          if (on) {
            int count = 0;
            if (SDL_Window** windows = SDL_GetWindows(&count)) {
              for (int i = 0; i < count; i++) SDL_HideWindow(windows[i]);
              SDL_free(windows);
            }
          }
          r->window_ = SDL_CreateWindow(on ? "GTA V" : "GTA V - native renderer (shadow)", 1280, 720,
                                        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        },
        this, true);
    if (!window_) {
      REXLOG_ERROR("[native] SDL_CreateWindow failed: {}", SDL_GetError());
      return false;
    }
#endif
    interface_ = plume::CreateVulkanInterface();
    if (!interface_) {
      REXLOG_ERROR("[native] Plume Vulkan interface creation failed");
      return false;
    }
    std::string preferred;
    for (const auto& name : interface_->getDeviceNames()) {
      REXLOG_INFO("[native] Vulkan device: {}", name);
      if (preferred.empty() && name.find(REXCVAR_GET(gtav_native_device)) != std::string::npos)
        preferred = name;
    }
    LoadPipelineCache();
    device_ = interface_->createDevice(preferred);
    if (!device_) {
      REXLOG_ERROR("[native] Plume device creation failed");
      return false;
    }
    REXLOG_INFO("[native] using {}", device_->getDescription().name);
    queue_cmd_ = device_->createCommandQueue(plume::RenderCommandListType::DIRECT);
    // Init steps are logged: a cold first boot after a rebuild sometimes never got past here.
    REXLOG_INFO("[native] init: queue created, creating the swap chain");
    swapchain_ = queue_cmd_->createSwapChain(
        plume::RenderSwapChainDesc(window_, plume::RenderFormat::B8G8R8A8_UNORM, kSwapchainBuffers));
    swapchain_->setVsyncEnabled(REXCVAR_GET(gtav_native_vsync));
    REXLOG_INFO("[native] init: swap chain created, resizing");
    swapchain_->resize();
    REXLOG_INFO("[native] init: swap chain {}x{}", swapchain_->getWidth(), swapchain_->getHeight());
    for (uint32_t i = 0; i < Backend::kFramesInFlight; i++) {
      lists_[i] = queue_cmd_->createCommandList();
      fences_[i] = device_->createCommandFence();
      acquires_[i] = device_->createCommandSemaphore();
    }
    CreateFramebuffers();
    REXLOG_INFO("[native] init: framebuffers created, backend init");
    backend_ok_ = backend_.Init(device_.get());
    REXLOG_INFO("[native] init: done");
    if (!backend_ok_) REXLOG_ERROR("[native] backend init failed; showing clear color only");
    return true;
  }

  void CreateFramebuffers() {
    framebuffers_.clear();
    for (uint32_t i = 0; i < swapchain_->getTextureCount(); i++) {
      const plume::RenderTexture* color = swapchain_->getTexture(i);
      plume::RenderFramebufferDesc desc;
      desc.colorAttachments = &color;
      desc.colorAttachmentsCount = 1;
      framebuffers_.push_back(device_->createFramebuffer(desc));
    }
    while (release_.size() < swapchain_->getTextureCount())
      release_.push_back(device_->createCommandSemaphore());
  }

  // Vulkan pipeline cache, persisted next to the exe so pipelines compiled once load in about a
  // millisecond on later runs. Saved periodically (the game exits without unwinding).
  std::filesystem::path PipelineCachePath() { return ExeDir() / "shader_cache" / "vk_pipelines.bin"; }

  void LoadPipelineCache() {
    std::vector<uint8_t> data;
    if (FILE* f = std::fopen(PipelineCachePath().string().c_str(), "rb")) {
      std::fseek(f, 0, SEEK_END);
      data.resize(size_t(std::max(0L, std::ftell(f))));
      std::fseek(f, 0, SEEK_SET);
      if (std::fread(data.data(), 1, data.size(), f) != data.size()) data.clear();
      std::fclose(f);
    }
    REXLOG_INFO("[native] pipeline cache: {} KB loaded", data.size() / 1024);
    plume::SetVulkanPipelineCacheData(std::move(data));
  }

  void MaybeSavePipelineCache(uint32_t new_pipelines) {
    pipelines_since_save_ += new_pipelines;
    auto now = std::chrono::steady_clock::now();
    if (!pipelines_since_save_ || now - last_cache_save_ < std::chrono::seconds(20)) return;
    last_cache_save_ = now;
    pipelines_since_save_ = 0;
    auto get_start = std::chrono::steady_clock::now();
    std::vector<uint8_t> data = plume::GetVulkanPipelineCacheData(device_.get());
    double get_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - get_start).count();
    if (data.empty()) return;
    // The file is written by a background thread: ~33 MB on the busy E: disk took ~2 s, a freeze
    // every 20 s while the renderer (and so the guest, through the swap wait) waited for it.
    if (cache_writing_.exchange(true)) return;  // the previous save is still being written
    std::thread([this, data = std::move(data), get_ms] {
      auto start = std::chrono::steady_clock::now();
      std::error_code ec;
      std::filesystem::create_directories(PipelineCachePath().parent_path(), ec);
      auto tmp = PipelineCachePath();
      tmp += ".tmp";
      if (FILE* f = std::fopen(tmp.string().c_str(), "wb")) {
        bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
        std::fclose(f);
        if (ok) std::filesystem::rename(tmp, PipelineCachePath(), ec);
      }
      REXLOG_INFO("[native] pipeline cache saved: {} KB (get {:.0f} ms, write {:.0f} ms)", data.size() / 1024, get_ms,
                  std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
      cache_writing_ = false;
    }).detach();
  }
  std::atomic<bool> cache_writing_{false};

  // Waits for the slot's previous frame and recycles what it left behind.
  void Retire(uint32_t slot) {
    if (!in_flight_[slot]) return;
    queue_cmd_->waitForCommandFence(fences_[slot].get());
    in_flight_[slot] = false;
    if (backend_ok_) backend_.FrameComplete(slot);
  }

  void RetireAll() {
    for (uint32_t i = 0; i < Backend::kFramesInFlight; i++) Retire((slot_ + i) % Backend::kFramesInFlight);
  }

  // Two frames in flight: frame N+1 is recorded while the GPU runs frame N, so a frame costs
  // max(record, GPU) instead of record + GPU. Shot and dump frames are waited for at once.
  void Present(const FramePacket& frame) {
    if (swapchain_->needsResize()) {
      RetireAll();
      swapchain_->resize();
      CreateFramebuffers();
    }
    if (swapchain_->isEmpty()) return;  // minimized
    const uint32_t slot = slot_;
    auto wait_start = std::chrono::steady_clock::now();
    Retire(slot);
    auto wait_end = std::chrono::steady_clock::now();
    uint32_t image = 0;
    if (!swapchain_->acquireTexture(acquires_[slot].get(), &image)) return;
    auto acquire_end = std::chrono::steady_clock::now();
    plume::RenderTexture* target = swapchain_->getTexture(image);
    list_ = lists_[slot].get();
    list_->begin();
    if (backend_ok_) backend_.BeginFrame(slot);
    // logs/dump_request (any content): same as F9, for scripted captures without keyboard focus.
    if (frame.index % 64 == 0) {
      std::error_code ec;
      auto request = std::filesystem::path(LogPath("dump_request"));
      if (std::filesystem::exists(request, ec)) {
        std::filesystem::remove(request, ec);
        key_dump_ = true;
      }
    }
    if (backend_ok_ && key_dump_.exchange(false)) {
      if (REXCVAR_GET(gtav_native) == "shadow") {
        // The emulated renderer dumps a window of frames from now on (logs/emu_dump, SDK patch;
        // its resolves are only readable with readback on, switched on around the dump). The native
        // dump is taken a few frames later so that it falls inside that window: the tools then find
        // its twin by draw constants even while the camera moves.
        if (readback_restore_.empty()) readback_restore_ = rex::cvar::GetFlagByName("readback_resolve");
        rex::cvar::SetFlagByName("readback_resolve", "full");
        // 8 frames: twins have matched at window positions 0-3; every dumped frame is slow.
        rex::cvar::SetFlagByName("emu_draw_dump_window", "8");
        rex::cvar::SetFlagByName("emu_resolve_dump_frame", "-1");
        native_dump_at_ = frame.index + 6;
        readback_restore_at_ = frame.index + 16;  // the emulated frame count runs a few ahead
      } else {
        native_dump_at_ = frame.index;
      }
    }
    if (backend_ok_ && native_dump_at_ && frame.index >= native_dump_at_) {
      native_dump_at_ = 0;
      REXLOG_INFO("[native] F9: dumping guest frame {}", frame.index);
      backend_.RequestDump();
    }
    if (!readback_restore_.empty() && frame.index >= readback_restore_at_) {
      rex::cvar::SetFlagByName("readback_resolve", readback_restore_);
      readback_restore_.clear();
    }
    uint32_t front_descriptor = 0;
    auto record_start = std::chrono::steady_clock::now();
    plume::RenderTexture* front = backend_ok_ ? backend_.Execute(frame, list_, &front_descriptor) : nullptr;
    auto record_end = std::chrono::steady_clock::now();
    list_->barriers(plume::RenderBarrierStage::GRAPHICS,
                    plume::RenderTextureBarrier(target, plume::RenderTextureLayout::COLOR_WRITE));
    list_->setFramebuffer(framebuffers_[image].get());
    uint32_t w = swapchain_->getWidth(), h = swapchain_->getHeight();
    list_->setViewports(plume::RenderViewport(0, 0, float(w), float(h)));
    list_->setScissors(plume::RenderRect(0, 0, int32_t(w), int32_t(h)));
    bool shot = front && WantShot(frame.index);
    if (shot) {
      // Blit the front buffer (whatever its format) into an RGBA8 capture target, read it back
      // and write it as PPM once the fence passed.
      if (!shot_tex_) {
        shot_tex_ = device_->createTexture(
            plume::RenderTextureDesc::ColorTarget(kShotW, kShotH, plume::RenderFormat::R8G8B8A8_UNORM));
        const plume::RenderTexture* color = shot_tex_.get();
        plume::RenderFramebufferDesc fb;
        fb.colorAttachments = &color;
        fb.colorAttachmentsCount = 1;
        shot_fb_ = device_->createFramebuffer(fb);
        readback_ = device_->createBuffer(plume::RenderBufferDesc::ReadbackBuffer(uint64_t(kShotW) * kShotH * 4));
      }
      list_->barriers(plume::RenderBarrierStage::GRAPHICS,
                      plume::RenderTextureBarrier(shot_tex_.get(), plume::RenderTextureLayout::COLOR_WRITE));
      list_->setFramebuffer(shot_fb_.get());
      list_->setViewports(plume::RenderViewport(0, 0, float(kShotW), float(kShotH)));
      list_->setScissors(plume::RenderRect(0, 0, int32_t(kShotW), int32_t(kShotH)));
      backend_.Blit(list_, front_descriptor, plume::RenderFormat::R8G8B8A8_UNORM);
      list_->barriers(plume::RenderBarrierStage::COPY,
                      plume::RenderTextureBarrier(shot_tex_.get(), plume::RenderTextureLayout::COPY_SOURCE));
      list_->copyTextureRegion(plume::RenderTextureCopyLocation::PlacedFootprint(
                                   readback_.get(), plume::RenderFormat::R8G8B8A8_UNORM, kShotW, kShotH, 1, kShotW),
                               plume::RenderTextureCopyLocation::Subresource(shot_tex_.get()));
      list_->barriers(plume::RenderBarrierStage::GRAPHICS,
                      plume::RenderTextureBarrier(target, plume::RenderTextureLayout::COLOR_WRITE));
      list_->setFramebuffer(framebuffers_[image].get());
      list_->setViewports(plume::RenderViewport(0, 0, float(w), float(h)));
      list_->setScissors(plume::RenderRect(0, 0, int32_t(w), int32_t(h)));
    }
    if (front) {
      backend_.Blit(list_, front_descriptor, plume::RenderFormat::B8G8R8A8_UNORM);
    } else {
      list_->clearColor(0, plume::RenderColor(0.12f, 0.0f, 0.12f, 1.0f));
    }
    // Frames presented over the last second, drawn in the top-left corner (--gtav_native_fps).
    auto now = std::chrono::steady_clock::now();
    fps_frames_++;
    double fps_elapsed = std::chrono::duration<double>(now - fps_since_).count();
    if (fps_elapsed >= 1.0) {
      fps_value_ = uint32_t(fps_frames_ / fps_elapsed + 0.5);
      fps_frames_ = 0;
      fps_since_ = now;
    }
    if (backend_ok_ && REXCVAR_GET(gtav_native_fps))
      backend_.DrawCounter(list_, fps_value_, 8, 8, std::max(1u, h / 720), plume::RenderFormat::B8G8R8A8_UNORM);
    list_->barriers(plume::RenderBarrierStage::NONE,
                    plume::RenderTextureBarrier(target, plume::RenderTextureLayout::PRESENT));
    list_->end();
    if (backend_ok_) backend_.EndFrame(slot);
    const plume::RenderCommandList* lists = list_;
    plume::RenderCommandSemaphore* wait = acquires_[slot].get();
    plume::RenderCommandSemaphore* signal = release_[image].get();
    queue_cmd_->executeCommandLists(&lists, 1, &wait, 1, &signal, 1, fences_[slot].get());
    auto submit_end = std::chrono::steady_clock::now();
    swapchain_->present(image, &signal, 1);
    auto present_end = std::chrono::steady_clock::now();
    in_flight_[slot] = true;
    slot_ = (slot + 1) % Backend::kFramesInFlight;
    auto ms = [](auto d) { return std::chrono::duration<double, std::milli>(d).count(); };
    timing_.Add(ms(record_end - record_start), ms(wait_end - wait_start));
    // Hitch log: a present more than 25 ms after the previous one, with what this frame cost.
    double interval = last_present_.time_since_epoch().count() ? ms(present_end - last_present_) : 0;
    last_present_ = present_end;
    if (backend_ok_) MaybeSavePipelineCache(backend_.LastStats().pipelines_created);
    if (interval > 25.0 && backend_ok_) {
      auto st = backend_.LastStats();
      REXLOG_INFO("[native] hitch {:.1f} ms at guest frame {}: record {:.1f} ms, wait {:.1f} ms, {} new pipelines, "
                  "{} textures, {} draws (misses: shaders {:.1f} pipelines {:.1f} textures {:.1f} ms; resolves {:.1f} ms)",
                  interval, frame.index, ms(record_end - record_start), ms(wait_end - wait_start),
                  st.pipelines_created, st.textures, st.draws, st.shader_ms, st.pipeline_ms, st.texture_ms,
                  st.resolve_ms);
    }
    timing_.acquire_sum += ms(acquire_end - wait_end);
    timing_.submit_sum += ms(submit_end - record_end);
    timing_.present_sum += ms(present_end - submit_end);
    if (shot || (backend_ok_ && backend_.Dumping())) {
      // Plume's waitForCommandFence also resets the fence: wait exactly once per submit.
      queue_cmd_->waitForCommandFence(fences_[slot].get());
      in_flight_[slot] = false;
      if (backend_ok_) {
        backend_.WriteDump();
        backend_.FrameComplete(slot);
      }
      if (shot) WriteShot(frame.index);
    }
  }

  // GTAV_NATIVE_SHOT_DIR (+ GTAV_SHOT_SECS, default 10): periodic PPM captures of the native
  // front buffer, comparable with the emulated renderer's GTAV_SHOT_DIR captures.
  std::string ShotDir() {
    const char* dir = std::getenv("GTAV_NATIVE_SHOT_DIR");
    return dir && *dir ? dir : LogPath("shots");
  }

  bool WantShot(uint64_t index) {
    if (key_shot_.exchange(false)) {
      std::error_code ec;
      std::filesystem::create_directories(ShotDir(), ec);
      REXLOG_INFO("[native] F10: screenshot of guest frame {} to {}", index, ShotDir());
      return true;
    }
    const char* dir = std::getenv("GTAV_NATIVE_SHOT_DIR");
    if (!dir || !*dir) return false;
    const char* secs = std::getenv("GTAV_SHOT_SECS");
    uint64_t every = uint64_t(std::max(1, secs ? std::atoi(secs) : 10)) * 30;
    return index % every == 0;
  }

  void WriteShot(uint64_t index) {
    std::string path = fmt::format("{}/native_{:06}.ppm", ShotDir(), index);
    const uint8_t* px = static_cast<const uint8_t*>(readback_->map());
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
      std::fprintf(f, "P6\n%u %u\n255\n", kShotW, kShotH);
      for (uint32_t i = 0; i < kShotW * kShotH; i++) std::fwrite(px + 4 * i, 1, 3, f);
      std::fclose(f);
    }
    readback_->unmap();
  }

  std::string SpirvDir() {
    std::string dir = REXCVAR_GET(gtav_native_spirv_dir);
    if (dir.empty()) dir = DefaultSpirvDir();
    return dir;
  }

  bool HaveShader(bool pixel, uint64_t hash) {
    auto key = hash ^ (pixel ? 1ull << 63 : 0);
    if (have_shader_generation_ != CompileGeneration()) {
      have_shader_generation_ = CompileGeneration();
      std::erase_if(have_shader_, [](const auto& kv) { return !kv.second; });
    }
    auto it = have_shader_.find(key);
    if (it != have_shader_.end()) return it->second;
    bool ok = hash && std::filesystem::exists(fmt::format("{}/{}_{:016X}.spv", SpirvDir(), pixel ? "ps" : "vs", hash));
    have_shader_[key] = ok;
    return ok;
  }

  // Bring-up check of the recorded frame: are its shaders, buffers and targets usable?
  void Validate(const FramePacket& f) {
    if (f.index - last_validated_ < 150) return;
    last_validated_ = f.index;
    uint32_t missing_vs = 0, missing_ps = 0, no_decl = 0, bad_ib16 = 0;
    std::set<uint32_t> rts;
    uint32_t prims[16] = {};
    for (const auto& d : f.draws) {
      prims[d.primitive & 15]++;
      missing_vs += !HaveShader(false, d.vs_hash);
      if (!HaveShader(true, d.ps_hash)) {
        missing_ps++;
        if (logged_missing_.insert(d.ps_hash).second && logged_missing_.size() <= 40)
          REXLOG_INFO("[native]   no SPIR-V for ps {:016X}", d.ps_hash);
      }
      no_decl += d.element_count == 0;
      // Inline (BeginIndexedVertices) indices carry their own size; index_size is 0 for them.
      if (d.indexed && !d.index32 && !d.inline_index_size && uint64_t(d.start + d.count) * 2 > d.index_size) {
        bad_ib16++;
        if (logged_bad_ib_++ < 12)
          REXLOG_INFO("[native]   ib16 out of range: start {} count {} base {} ib {:08X} size {} prim {} vs {:016X}",
                      d.start, d.count, d.base_vertex, d.index_address, d.index_size, d.primitive, d.vs_hash);
      }
      rts.insert(d.render_targets[0]);
    }
    REXLOG_INFO("[native] frame {}: {} draws ({} vs / {} ps without SPIR-V, {} without decl, {} ib16 out of "
                "range), {} clears, {} resolves, {} render targets, front {:08X}",
                f.index, f.draws.size(), missing_vs, missing_ps, no_decl, bad_ib16, f.clears.size(),
                f.resolves.size(), rts.size(), f.front_buffer);
    std::string ph;
    for (int i = 0; i < 16; i++)
      if (prims[i]) ph += fmt::format(" {}:{}", i, prims[i]);
    REXLOG_INFO("[native]   primitives{}", ph);
    if (logged_layouts_ < 3 && !f.resolves.empty()) {
      logged_layouts_++;
      const auto& r = f.resolves.back();
      std::string w;
      for (uint32_t x : r.dest_words) w += fmt::format("{:08X} ", x);
      REXLOG_INFO("[native]   resolve flags {:08X} rect {} ({},{})-({},{}) dest {:08X}: {}", r.flags, r.has_rect,
                  r.source_rect[0], r.source_rect[1], r.source_rect[2], r.source_rect[3], r.dest_texture, w);
    }
  }

  void ThreadMain() {
    if (!Init()) {
      {
        std::lock_guard lock(mutex_);
        failed_ = true;
      }
      space_cv_.notify_all();
      return;
    }
    uint64_t presented = 0;
    for (;;) {
      FramePacket frame;
      {
        std::unique_lock lock(mutex_);
        auto idle_start = std::chrono::steady_clock::now();
        render_state_ = 0;  // waiting for a frame
        cv_.wait(lock, [this] { return !queue_.empty(); });
        timing_.idle_sum += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - idle_start).count();
        frame = std::move(queue_.front());
        queue_.pop_front();
      }
      space_cv_.notify_one();
      render_frame_ = frame.index;
      render_state_ = 1;  // recording + presenting
      Validate(frame);
      Present(frame);
      render_state_ = 2;
      {
        std::lock_guard lock(recorded_mutex_);
        recorded_through_ = frame.index + 1;  // its guest memory has been read
      }
      recorded_cv_.notify_all();
      {
        std::lock_guard lock(mutex_);
        Recycle(std::move(frame));
      }
      if (++presented % 300 == 0) {
        REXLOG_INFO("[native] presented {} frames (guest frame {}, dropped {}): record avg {:.2f} max {:.2f} ms, "
                    "wait for slot avg {:.2f} max {:.2f} ms",
                    presented, frame.index, dropped_.load(), timing_.record_sum / timing_.n, timing_.record_max,
                    timing_.gpu_sum / timing_.n, timing_.gpu_max);
        REXLOG_INFO("[native]   avg acquire {:.2f} submit {:.2f} present {:.2f} idle {:.2f} ms",
                    timing_.acquire_sum / timing_.n, timing_.submit_sum / timing_.n,
                    timing_.present_sum / timing_.n, timing_.idle_sum / timing_.n);
        timing_ = {};
      }
    }
  }

  static inline std::atomic<bool> key_dump_{false}, key_shot_{false};  // F9 / F10 (window thread)
  std::string readback_restore_;  // readback_resolve to restore after a shadow-mode F9 dump
  uint64_t readback_restore_at_ = 0;
  uint64_t native_dump_at_ = 0;  // guest frame to dump (F9; delayed in shadow mode)
  std::chrono::steady_clock::time_point last_present_{}, last_cache_save_{};
  uint32_t pipelines_since_save_ = 0;
  std::mutex mutex_;
  std::condition_variable cv_, space_cv_;  // frame queued / queue has room
  std::deque<FramePacket> queue_;
  std::vector<FramePacket> spare_;  // presented/dropped packets, emptied, for TakeSpare
  std::atomic<bool> started_{false}, failed_{false};
  std::atomic<uint64_t> last_noted_{0}, render_frame_{0};
  std::atomic<int> render_state_{0};
  std::mutex swap_mutex_, recorded_mutex_;
  std::condition_variable recorded_cv_;
  std::deque<uint64_t> guest_swaps_;
  uint64_t recorded_through_ = 0;  // guest frames below this have been recorded
  struct SwapWaitStats {
    uint32_t swaps = 0, waits = 0, timeouts = 0;
    double ms_sum = 0, ms_max = 0;
  } swap_wait_stats_;  // command processor thread only
  std::atomic<uint64_t> dropped_{0};
  std::chrono::steady_clock::time_point fps_since_ = std::chrono::steady_clock::now();
  uint32_t fps_frames_ = 0, fps_value_ = 0;
  struct Timing {  // render thread, per 300 presented frames
    double record_sum = 0, record_max = 0, gpu_sum = 0, gpu_max = 0;
    double acquire_sum = 0, submit_sum = 0, present_sum = 0, idle_sum = 0;
    uint32_t n = 0;
    void Add(double record, double gpu) {
      record_sum += record, gpu_sum += gpu, n++;
      record_max = std::max(record_max, record), gpu_max = std::max(gpu_max, gpu);
    }
  } timing_;
  std::thread thread_;

#ifdef _WIN32
  HWND window_ = nullptr;
  std::thread window_thread_;
#else
  SDL_Window* window_ = nullptr;
#endif
  std::unique_ptr<plume::RenderInterface> interface_;
  std::unique_ptr<plume::RenderDevice> device_;
  std::unique_ptr<plume::RenderCommandQueue> queue_cmd_;
  std::unique_ptr<plume::RenderSwapChain> swapchain_;
  std::unique_ptr<plume::RenderCommandList> lists_[Backend::kFramesInFlight];
  std::unique_ptr<plume::RenderCommandFence> fences_[Backend::kFramesInFlight];
  std::unique_ptr<plume::RenderCommandSemaphore> acquires_[Backend::kFramesInFlight];
  bool in_flight_[Backend::kFramesInFlight] = {};
  uint32_t slot_ = 0;
  plume::RenderCommandList* list_ = nullptr;  // the slot being recorded
  std::vector<std::unique_ptr<plume::RenderCommandSemaphore>> release_;
  std::vector<std::unique_ptr<plume::RenderFramebuffer>> framebuffers_;

  Backend backend_;
  static constexpr uint32_t kShotW = 1280, kShotH = 720;
  std::unique_ptr<plume::RenderBuffer> readback_;
  std::unique_ptr<plume::RenderTexture> shot_tex_;
  std::unique_ptr<plume::RenderFramebuffer> shot_fb_;
  bool backend_ok_ = false;
  std::unordered_map<uint64_t, bool> have_shader_;
  uint64_t have_shader_generation_ = 0;
  std::set<uint64_t> logged_missing_;
  uint32_t logged_bad_ib_ = 0;
  uint64_t last_validated_ = 0;
  int logged_layouts_ = 0;
};

Renderer g_renderer;


}  // namespace

bool Active() { return REXCVAR_GET(gtav_native) != "off"; }

void Submit(FramePacket&& frame) { g_renderer.Submit(std::move(frame)); }

void NoteGuestSwap(uint64_t frame_index) { g_renderer.NoteGuestSwap(frame_index); }

FramePacket TakeSpare() { return g_renderer.TakeSpare(); }

}  // namespace gtav::native

// Looked up by the SDK's D3D12 and Vulkan command processors (gpu_skip_rendering) and called at
// every swap they execute; see Renderer::NoteGuestSwap.
#ifdef _WIN32
#define GTAV_EXPORT __declspec(dllexport)
#else
#define GTAV_EXPORT __attribute__((visibility("default")))  // exported by the link options
#endif
extern "C" GTAV_EXPORT void rex_skip_rendering_swap(uint64_t) { gtav::native::g_renderer.WaitForGuestSwap(); }
