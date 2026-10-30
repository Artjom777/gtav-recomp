// gtav launcher - first-run install: the player's own discs and title update in, a playable
// folder out. Nothing retail ships with the launcher; everything below data\ and gtav_game.dll is
// made here from the player's files.
#pragma once

#include <filesystem>
#include <string>

namespace loader {

struct InstallInputs {
  std::wstring disc1;  // Disc 1 (Install): ISO image or extracted folder
  std::wstring disc2;  // Disc 2 (Play): ISO image or extracted folder
  std::wstring tu;     // Title Update 27: the STFS package file or an extracted folder
};

// Callbacks into the window; called from the install thread.
class InstallUi {
 public:
  virtual ~InstallUi() = default;
  virtual void Stage(const std::wstring& text) = 0;   // one line: what is happening now
  virtual void Log(const std::wstring& text) = 0;     // appended to the log box and install.log
  virtual void Progress(double fraction) = 0;         // 0..1 for the whole install
  virtual bool Cancelled() = 0;
  virtual bool Ask(const std::wstring& question) = 0;  // yes/no, blocks the install thread
};

// root = the folder holding the launcher, gtav_fast.exe and tools\.
bool IsInstalled(const std::filesystem::path& root);
bool RunInstall(const std::filesystem::path& root, const InstallInputs& in, InstallUi& ui,
                std::wstring* error);
bool LaunchGame(const std::filesystem::path& root, std::wstring* error);
// Checks that can fail before anything is touched (CPU, runtime DLLs, bundled tools).
bool Preflight(const std::filesystem::path& root, std::wstring* error);

}  // namespace loader
