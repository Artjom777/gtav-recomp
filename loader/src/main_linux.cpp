// gtav launcher (Linux) - first-run setup in the terminal and game launcher.
//
//   ./gtav_launcher                         installed: start the game; otherwise run setup
//   ./gtav_launcher --setup [DISC1 DISC2 TU] run setup again (paths asked for when not given)
//   ./gtav_launcher --unattended            setup from data/launcher.ini, no questions (testing)
//   ./gtav_launcher -- ARGS...              pass ARGS to the game (for example --vulkan_device=1)
//
// Setup is the same as on Windows (install.cpp): the player's own Disc 1, Disc 2 and Title Update
// 27 are copied and checked, the game executable is recompiled on this machine into gtav_game.so,
// then the game starts.
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

#include "install.h"
#include "sources.h"

namespace fs = std::filesystem;
using namespace loader;

namespace {

const char* kRowLabels[3] = {"Disc 1 (Install)", "Disc 2 (Play)", "Title Update 27"};
const char* kRowHints[3] = {
    "ISO image or extracted folder", "ISO image or extracted folder",
    "the TU file (e.g. tu00000009_00000000) or a folder with default.xexp + update.rpf"};

fs::path g_root;
volatile std::sig_atomic_t g_cancelled = 0;

fs::path SettingsPath() { return g_root / "data" / "launcher.ini"; }

std::vector<std::string> LoadInputs() {
  std::vector<std::string> v(3);
  std::ifstream f(SettingsPath());
  for (std::string& s : v) std::getline(f, s);
  return v;
}

void SaveInputs(const std::vector<std::string>& v) {
  std::error_code ec;
  fs::create_directories(g_root / "data", ec);
  std::ofstream f(SettingsPath());
  for (const std::string& s : v) f << s << "\n";
}

std::string Trim(std::string s) {
  while (!s.empty() && isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  size_t i = 0;
  while (i < s.size() && isspace(static_cast<unsigned char>(s[i]))) ++i;
  s.erase(0, i);
  // A path dropped into the terminal from a file manager arrives quoted.
  if (s.size() >= 2 && (s[0] == '\'' || s[0] == '"') && s.back() == s[0])
    s = s.substr(1, s.size() - 2);
  if (s.rfind("~/", 0) == 0) {
    if (const char* home = getenv("HOME")) s = std::string(home) + s.substr(1);
  }
  return s;
}

// Terminal UI for the install: stage lines, a progress line when stdout is a terminal, y/n
// questions on stdin, Ctrl+C cancels. Everything also goes to data/logs/install.log (install.cpp).
class TermUi : public InstallUi {
 public:
  TermUi() : tty_(isatty(STDOUT_FILENO)) {}
  void Stage(const std::wstring& t) override {
    std::string s = Narrow(t);
    if (s == last_stage_) return;
    last_stage_ = s;
    Line(s);
  }
  void Log(const std::wstring& t) override { Line("  " + Narrow(t)); }
  void Progress(double f) override {
    int v = int(f * 1000);
    if (v == last_ || !tty_) {
      if (!tty_ && v / 100 != last_ / 100) printf("progress: %d%%\n", v / 10);
      last_ = v;
      return;
    }
    last_ = v;
    int bar = v * 40 / 1000;
    printf("\r  [%.*s%*s] %5.1f%%", bar, "########################################", 40 - bar, "",
           v / 10.0);
    fflush(stdout);
    progress_shown_ = true;
  }
  bool Cancelled() override { return g_cancelled != 0; }
  bool Ask(const std::wstring& q) override {
    Line(Narrow(q));
    printf("[y/N] ");
    fflush(stdout);
    std::string answer;
    if (!std::getline(std::cin, answer)) return false;
    answer = Trim(answer);
    return answer == "y" || answer == "Y" || answer == "yes";
  }

 private:
  void Line(const std::string& s) {
    if (progress_shown_) {
      printf("\r%60s\r", "");
      progress_shown_ = false;
    }
    printf("%s\n", s.c_str());
    fflush(stdout);
  }
  bool tty_;
  bool progress_shown_ = false;
  int last_ = -1;
  std::string last_stage_;
};

// "--unattended": installs from the three paths in data/launcher.ini (one per line), writes the
// stage and progress lines to data/logs/unattended.txt, says no to every question and returns 0 on
// success. For testing a release and for scripted installs. Same as the Windows launcher.
class FileUi : public InstallUi {
 public:
  explicit FileUi(const fs::path& path) : f_(path, std::ios::app) {}
  void Stage(const std::wstring& t) override { Write(L"stage: " + t); }
  void Log(const std::wstring& t) override { Write(t); }
  void Progress(double f) override {
    int v = int(f * 100);
    if (v != last_) Write(L"progress: " + std::to_wstring(last_ = v) + L"%");
  }
  bool Cancelled() override { return g_cancelled != 0; }
  bool Ask(const std::wstring& q) override {
    Write(L"question (answered no): " + q);
    return false;
  }

 private:
  void Write(const std::wstring& s) {
    std::lock_guard lock(m_);
    f_ << Narrow(s) << "\n";
    f_.flush();
  }
  std::mutex m_;
  std::ofstream f_;
  int last_ = -1;
};

int Unattended() {
  std::error_code ec;
  fs::create_directories(g_root / "data" / "logs", ec);
  FileUi ui(g_root / "data" / "logs" / "unattended.txt");
  std::wstring error;
  if (!Preflight(g_root, &error)) {
    ui.Log(L"error: " + error);
    return 1;
  }
  std::vector<std::string> v = LoadInputs();
  if (v[0].empty() || v[1].empty() || v[2].empty()) {
    ui.Log(L"error: data/launcher.ini needs three lines: Disc 1, Disc 2, Title Update");
    return 1;
  }
  InstallInputs in{Widen(v[0]), Widen(v[1]), Widen(v[2])};
  bool ok = RunInstall(g_root, in, ui, &error);
  ui.Log(ok ? L"done" : L"error: " + error);
  return ok ? 0 : 1;
}

int Setup(std::vector<std::string> given, const std::vector<std::wstring>& game_args) {
  printf("GTA V recomp setup\n\n"
         "This builds the game from your own Xbox 360 copy of GTA V: Disc 1 (Install), Disc 2\n"
         "(Play) and Title Update 27. Nothing is downloaded. Press Ctrl+C to stop; running setup\n"
         "again continues where it stopped.\n\n");
  std::vector<std::string> saved = LoadInputs();
  std::vector<std::string> paths(3);
  for (int i = 0; i < 3; ++i) {
    if (i < int(given.size())) {
      paths[i] = Trim(given[i]);
      continue;
    }
    printf("%s - %s\n", kRowLabels[i], kRowHints[i]);
    if (!saved[i].empty()) printf("  [Enter: %s]\n", saved[i].c_str());
    printf("> ");
    fflush(stdout);
    std::string line;
    if (!std::getline(std::cin, line)) return 1;
    paths[i] = Trim(line).empty() ? saved[i] : Trim(line);
    if (paths[i].empty()) {
      fprintf(stderr, "No path given for %s.\n", kRowLabels[i]);
      return 1;
    }
  }
  for (int i = 0; i < 3; ++i) {
    std::error_code ec;
    if (!fs::exists(paths[i], ec)) {
      fprintf(stderr, "%s: %s does not exist.\n", kRowLabels[i], paths[i].c_str());
      return 1;
    }
    paths[i] = fs::absolute(paths[i], ec).string();
  }
  SaveInputs(paths);

  printf("\n");
  TermUi ui;
  std::wstring error;
  InstallInputs in{Widen(paths[0]), Widen(paths[1]), Widen(paths[2])};
  if (!RunInstall(g_root, in, ui, &error)) {
    fprintf(stderr, "\nSetup stopped: %s\n(log: %s)\n", Narrow(error).c_str(),
            (g_root / "data" / "logs" / "install.log").c_str());
    return 1;
  }
  printf("\nSetup finished. Start the game now? [Y/n] ");
  fflush(stdout);
  std::string answer;
  if (std::getline(std::cin, answer) && (Trim(answer) == "n" || Trim(answer) == "N")) return 0;
  if (!LaunchGame(g_root, &error, game_args)) {
    fprintf(stderr, "%s\n", Narrow(error).c_str());
    return 1;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::error_code ec;
  g_root = fs::read_symlink("/proc/self/exe", ec).parent_path();
  if (ec) g_root = fs::absolute(argv[0]).parent_path();
  std::signal(SIGINT, [](int) { g_cancelled = 1; });

  bool setup = false, unattended = false;
  std::vector<std::string> paths;
  std::vector<std::wstring> game_args;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--") {
      for (++i; i < argc; ++i) game_args.push_back(Widen(argv[i]));
      break;
    }
    if (a == "--setup") {
      setup = true;
    } else if (a == "--unattended") {
      unattended = true;
    } else if (a == "--help" || a == "-h") {
      printf("usage: %s [--setup [DISC1 DISC2 TU]] [--unattended] [-- GAME_ARGS...]\n", argv[0]);
      return 0;
    } else if (a.rfind("--", 0) == 0) {
      fprintf(stderr, "unknown option %s (game options go after --)\n", a.c_str());
      return 2;
    } else {
      paths.push_back(a);
    }
  }
  if (unattended) return Unattended();
  if (!paths.empty()) setup = true;

  std::wstring error;
  if (!setup && IsInstalled(g_root)) {
    if (LaunchGame(g_root, &error, game_args)) return 0;
    fprintf(stderr, "%s\n", Narrow(error).c_str());
    return 1;
  }
  if (!Preflight(g_root, &error)) {
    fprintf(stderr, "%s\n", Narrow(error).c_str());
    return 1;
  }
  return Setup(paths, game_args);
}
