// Default locations for native-renderer data and logs: next to the executable.
#pragma once

#include <filesystem>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace gtav::native {

inline std::filesystem::path ExeDir() {
#ifdef _WIN32
  wchar_t buf[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  return std::filesystem::path(std::wstring(buf, n)).parent_path();
#else
  std::error_code ec;
  auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
  return ec ? std::filesystem::current_path() : exe.parent_path();
#endif
}

// SPIR-V of the game's shaders (census output): <exe dir>/spirv.
inline std::string DefaultSpirvDir() { return (ExeDir() / "spirv").string(); }

// Dumps and probe output: <exe dir>/logs/<sub>.
inline std::string LogPath(const std::string& sub) { return (ExeDir() / "logs" / sub).string(); }

}  // namespace gtav::native
