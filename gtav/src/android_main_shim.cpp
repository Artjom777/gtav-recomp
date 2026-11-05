// gtav - libmain.so for the Android release layout (GTAV_GAME_DLL).
//
// The APK holds no game code. The player's install builds libgtav_game.so from their own discs
// into the app's internal files folder (external storage is mounted noexec). SDL's activity loads
// this library and calls SDL_main; it opens libgtav_game.so, whose dependency libgtav_fast.so (our
// code, in the APK) holds the real SDL_main. bionic resolves the undefined symbols of both
// libraries inside that one dlopen'd tree, so the game finds the hooks and the bridge in
// libgtav_fast.so and libgtav_fast.so finds the game functions it calls.

#include <SDL3/SDL.h>
#include <android/log.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/stat.h>

#include <cstdio>
#include <string>

namespace {

void WriteFileLog(const std::string& path, const std::string& msg) {
  if (FILE* f = fopen(path.c_str(), "w")) {
    fputs(msg.c_str(), f);
    fclose(f);
  }
}

void LogCrash(const std::string& what) {
  __android_log_print(ANDROID_LOG_ERROR, "gtav", "%s", what.c_str());
  if (const char* files = SDL_GetAndroidInternalStoragePath()) {
    WriteFileLog(std::string(files) + "/crash.log", what);
  }
  if (const char* ext = SDL_GetAndroidExternalStoragePath()) {
    WriteFileLog(std::string(ext) + "/crash.log", what);
  }
  WriteFileLog("/storage/emulated/0/gtav/crash.log", what);
  WriteFileLog("/sdcard/gtav_crash.log", what);
}

void Fail(const std::string& what) {
  LogCrash(what);
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "GTA V Error", what.c_str(), nullptr);
  // Allow time for the dialog to be displayed on screen before SDLActivity finishes
  SDL_Delay(8000);
}

}  // namespace

extern "C" __attribute__((visibility("default"))) int SDL_main(int argc, char* argv[]) {
  __android_log_print(ANDROID_LOG_INFO, "gtav", "=== Starting GTA V shim ===");
  const char* files = SDL_GetAndroidInternalStoragePath();
  if (!files) {
    Fail(std::string("No internal storage folder: ") + SDL_GetError());
    return 1;
  }

  std::string path = std::string(files) + "/libgtav_game.so";
  __android_log_print(ANDROID_LOG_INFO, "gtav", "Looking for game library: %s", path.c_str());

  struct stat st{};
  if (stat(path.c_str(), &st) != 0) {
    Fail("Game library not found: " + path + "\nRun setup and install the game pack first.");
    return 1;
  }
  __android_log_print(ANDROID_LOG_INFO, "gtav", "Game library size: %zu bytes", (size_t)st.st_size);

  void* game = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
  if (!game) {
    const char* err = dlerror();
    std::string errStr = err ? err : "unknown dlopen error";
    __android_log_print(ANDROID_LOG_ERROR, "gtav", "dlopen failed: %s", errStr.c_str());
    Fail("The game is not installed properly or dlopen failed.\n\n" + path + ":\n" + errStr);
    return 1;
  }

  __android_log_print(ANDROID_LOG_INFO, "gtav", "libgtav_game.so loaded successfully, searching for SDL_main...");

  // dlsym on the handle searches its dependency tree: libgtav_game.so, then libgtav_fast.so.
  using MainFn = int (*)(int, char**);
  auto real_main = reinterpret_cast<MainFn>(dlsym(game, "SDL_main"));
  if (!real_main) {
    __android_log_print(ANDROID_LOG_WARN, "gtav", "dlsym(game, SDL_main) null, trying RTLD_DEFAULT...");
    real_main = reinterpret_cast<MainFn>(dlsym(RTLD_DEFAULT, "SDL_main"));
  }

  if (!real_main) {
    const char* err = dlerror();
    std::string errStr = err ? err : "symbol not found";
    Fail("libgtav_fast.so has no SDL_main: " + errStr);
    return 1;
  }

  __android_log_print(ANDROID_LOG_INFO, "gtav", "Found real SDL_main at %p, executing game...", real_main);
  return real_main(argc, argv);
}
