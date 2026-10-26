// gtav - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/presenter.h>
#include <rex/runtime.h>
#include <rex/ui/windowed_app_context.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

class GtavApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<GtavApp>(new GtavApp(ctx, "gtav",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // GTAV_DUMP_IMAGE=<path>: write the loaded (title-update patched) guest image and exit.
  // Used to rescan the patched code for entry points the analyzer misses.
  void OnPostLoadXexImage() override {
    const char* path = std::getenv("GTAV_DUMP_IMAGE");
    if (!path) return;
    auto* image = REX_KERNEL_MEMORY()->TranslateVirtual(0x82000000);
    if (FILE* f = std::fopen(path, "wb")) {
      std::fwrite(image, 1, 0x1DC0000, f);
      std::fclose(f);
    }
    std::_Exit(0);
  }
  // GTAV_SHOT_DIR=<dir> [GTAV_SHOT_SECS=<n>, default 30]: every n seconds save the guest
  // output as <dir>/shot_<seconds>.ppm. For checking unattended boots (no screen capture needed).
  void OnPostSetup() override {
    const char* dir = std::getenv("GTAV_SHOT_DIR");
    if (!dir) return;
    const char* secs_env = std::getenv("GTAV_SHOT_SECS");
    int secs = secs_env ? std::atoi(secs_env) : 30;
    if (secs <= 0) secs = 30;
    std::thread([this, dir = std::string(dir), secs] {
      for (int t = secs;; t += secs) {
        std::this_thread::sleep_for(std::chrono::seconds(secs));
        if (shutting_down_) return;
        app_context().CallInUIThreadSynchronous([this, &dir, t] {
          auto* gs = runtime() ? runtime()->graphics_system() : nullptr;
          auto* presenter = gs ? gs->presenter() : nullptr;
          rex::ui::RawImage image;
          if (!presenter || !presenter->CaptureGuestOutput(image)) return;
          std::string path = dir + "/shot_" + std::to_string(t) + ".ppm";
          if (FILE* f = std::fopen(path.c_str(), "wb")) {
            std::fprintf(f, "P6\n%u %u\n255\n", image.width, image.height);
            for (uint32_t y = 0; y < image.height; ++y) {
              const uint8_t* row = image.data.data() + y * image.stride;
              for (uint32_t x = 0; x < image.width; ++x) std::fwrite(row + x * 4, 1, 3, f);
            }
            std::fclose(f);
          }
        });
      }
    }).detach();
  }
  void OnShutdown() override { shutting_down_ = true; }
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnConfigurePaths(rex::PathConfig& paths) override {}

 private:
  std::atomic<bool> shutting_down_{false};
};
