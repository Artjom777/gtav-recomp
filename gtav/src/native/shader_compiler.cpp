// gtav - background compiler for shaders the precompiled set lacks.
//
// The SPIR-V shipped next to the exe covers the shaders seen while the census was taken; the game
// streams in more as the world loads (hundreds outdoors). A shader created without SPIR-V is
// written to <exe>/shader_cache/<name>.bin and compiled here exactly like the offline census
// (tools/native/shader_census.py): XenosRecomp -> HLSL -> DXC -> SPIR-V into the SPIR-V dir.
// The backend retries missing shaders whenever CompileGeneration() moves, so a draw that was
// skipped renders a few hundred milliseconds later and the result persists for later runs.

#include "shader_compiler.h"

#include "paths.h"

#include <rex/cvar.h>
#include <rex/logging.h>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

REXCVAR_DEFINE_BOOL(gtav_native_compile, true, "GTAV",
                    "Compile shaders that have no precompiled SPIR-V in the background (tools/ next to the exe)");
REXCVAR_DECLARE(std::string, gtav_native_spirv_dir);

namespace gtav::native {
namespace fs = std::filesystem;

namespace {

std::atomic<uint64_t> g_generation{0};
std::mutex g_mutex;
std::condition_variable g_cv;
std::deque<std::string> g_queue;  // shader names ("ps_<HASH>")
std::unordered_set<std::string> g_seen;
std::atomic<uint32_t> g_ok{0}, g_failed{0};
bool g_started = false;

fs::path ToolsDir() { return ExeDir() / "tools"; }
fs::path CacheDir() { return ExeDir() / "shader_cache"; }
fs::path SpirvDirPath() {
  const std::string& dir = REXCVAR_GET(gtav_native_spirv_dir);
  return dir.empty() ? fs::path(DefaultSpirvDir()) : fs::path(dir);
}

// Runs a command line without a console window; true on exit code 0.
bool Run(const std::vector<std::string>& args) {
#ifdef _WIN32
  std::wstring cmd;
  for (const auto& a : args) {
    if (!cmd.empty()) cmd += L' ';
    cmd += L'"' + fs::path(a).wstring() + L'"';
  }
  STARTUPINFOW si{sizeof(si)};
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS,
                      nullptr, nullptr, &si, &pi))
    return false;
  WaitForSingleObject(pi.hProcess, 120000);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return code == 0;
#else
  std::string cmd;
  for (const auto& a : args) cmd += "'" + a + "' ";
  cmd += ">/dev/null 2>&1";
  return std::system(cmd.c_str()) == 0;
#endif
}

bool Compile(const std::string& name) {
  const fs::path tools = ToolsDir(), cache = CacheDir(), spirv = SpirvDirPath();
  const bool pixel = name.rfind("ps_", 0) == 0;
  const fs::path bin = cache / (name + ".bin"), hlsl = cache / (name + ".hlsl");
  const fs::path tmp = spirv / (name + ".spv.tmp"), out = spirv / (name + ".spv");
#ifdef _WIN32
  const fs::path xr = tools / "XenosRecomp.exe", dxc = tools / "dxc.exe";
#else
  const fs::path xr = tools / "XenosRecomp", dxc = tools / "dxc";
#endif
  if (!Run({xr.string(), bin.string(), hlsl.string(), (tools / "shader_common.h").string()}) || !fs::exists(hlsl))
    return false;
  // Same DXC arguments as the offline census.
  std::vector<std::string> args = {dxc.string(), "-T", pixel ? "ps_6_0" : "vs_6_0", "-HV", "2021",
                                   "-all-resources-bound", "-spirv", "-fvk-use-dx-layout",
                                   "-Wno-ignored-attributes", "-Qstrip_debug"};
  if (!pixel) args.push_back("-fvk-invert-y");
  args.insert(args.end(), {"-Fo", tmp.string(), hlsl.string()});
  if (!Run(args)) return false;
  // Vertex shaders with computed-index fetches need the raw streams (see Backend::Shader).
  if (!pixel) {
    std::ifstream f(hlsl);
    std::stringstream ss;
    ss << f.rdbuf();
    if (ss.str().find("= vfetchIndirect(") != std::string::npos) std::ofstream(spirv / (name + ".indirect"));
  }
  std::error_code ec;
  fs::rename(tmp, out, ec);  // the marker exists before the backend can see the .spv
  return !ec;
}

void Worker() {
  for (;;) {
    std::string name;
    {
      std::unique_lock lock(g_mutex);
      g_cv.wait(lock, [] { return !g_queue.empty(); });
      name = std::move(g_queue.front());
      g_queue.pop_front();
    }
    if (Compile(name)) {
      g_generation.fetch_add(1, std::memory_order_release);
      uint32_t n = g_ok.fetch_add(1) + 1;
      if (n <= 5 || n % 50 == 0) REXLOG_INFO("[native] compiled {} new shaders (latest {})", n, name);
    } else {
      uint32_t n = g_failed.fetch_add(1) + 1;
      REXLOG_WARN("[native] shader {} failed to compile ({} failures); its draws stay skipped", name, n);
    }
  }
}

void Enqueue(const std::string& name) {
  std::lock_guard lock(g_mutex);
  if (!g_seen.insert(name).second) return;
  if (!g_started) {
    g_started = true;
    // Two low-priority workers: enough to catch up after a streaming burst without competing
    // with the game threads.
    for (int i = 0; i < 2; i++) std::thread(Worker).detach();
  }
  g_queue.push_back(name);
  g_cv.notify_one();
}

}  // namespace

uint64_t CompileGeneration() { return g_generation.load(std::memory_order_acquire); }

void OnShaderCreated(const uint8_t* container, size_t size, uint64_t hash, bool pixel) {
  if (!REXCVAR_GET(gtav_native_compile)) return;
  std::string name = fmt::format("{}_{:016X}", pixel ? "ps" : "vs", hash);
  {
    std::lock_guard lock(g_mutex);
    if (g_seen.count(name)) return;
  }
  std::error_code ec;
  if (fs::exists(SpirvDirPath() / (name + ".spv"), ec)) {
    std::lock_guard lock(g_mutex);
    g_seen.insert(name);
    return;
  }
  fs::create_directories(CacheDir(), ec);
  fs::path bin = CacheDir() / (name + ".bin");
  if (!fs::exists(bin, ec)) {
    if (FILE* f = std::fopen(bin.string().c_str(), "wb")) {
      std::fwrite(container, 1, size, f);
      std::fclose(f);
    }
  }
  Enqueue(name);
}

}  // namespace gtav::native
