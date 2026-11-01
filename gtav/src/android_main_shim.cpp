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

#include <string>

namespace {

void Fail(const std::string& what) {
  __android_log_print(ANDROID_LOG_ERROR, "gtav", "%s", what.c_str());
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "GTA V", what.c_str(), nullptr);
}

}  // namespace

extern "C" __attribute__((visibility("default"))) int SDL_main(int argc, char* argv[]) {
  const char* files = SDL_GetAndroidInternalStoragePath();
  if (!files) {
    Fail(std::string("No internal storage folder: ") + SDL_GetError());
    return 1;
  }
  std::string path = std::string(files) + "/libgtav_game.so";
  void* game = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!game) {
    const char* err = dlerror();
    Fail("The game is not installed yet (or the install is incomplete). Run setup first.\n\n" +
         path + ":\n" + (err ? err : "dlopen failed"));
    return 1;
  }
  // dlsym on the handle searches its dependency tree: libgtav_game.so, then libgtav_fast.so.
  using MainFn = int (*)(int, char**);
  auto real_main = reinterpret_cast<MainFn>(dlsym(game, "SDL_main"));
  if (!real_main) {
    Fail("libgtav_fast.so has no SDL_main");
    return 1;
  }
  return real_main(argc, argv);
}
