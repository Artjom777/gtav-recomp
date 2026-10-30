// gtav launcher - first-run install (see install.h).
//
// Layout next to the launcher (root):
//   gtav_fast.exe, rexruntimerd.dll, ...   shipped: our runtime, renderer and hooks
//   rexgluerd.exe                          shipped: ReXGlue codegen (SDK tool)
//   tools\llvm\                            shipped: clang + lld (LLVM, Apache-2.0)
//   tools\game_dll\                        shipped: freestanding header, interface lists, manifest,
//                                          expected hashes (names, addresses and hashes only)
//   gtav_game.dll                          made here: the player's recompiled game code
//   data\game\                             made here: Disc 2 files + the TU's default.xexp
//   data\update\update.rpf                 made here: from the TU
//   data\user\...\545408A7\00000002\       made here: Disc 1 install partitions
//   data\spirv\                            filled by the game: shaders translated on first use
// Linux: gtav_fast, librexruntime.so, rexglue, tools/llvm/bin/{clang++,ld.lld}, gtav_game.so; the
// same data/ layout.
#include "install.h"

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/resource.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

extern char** environ;
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

#include "sources.h"

namespace fs = std::filesystem;

namespace loader {
namespace {

#ifdef _WIN32
constexpr wchar_t kInstalledMarker[] = L"data\\installed.txt";
constexpr wchar_t kGameExe[] = L"gtav_fast.exe";
constexpr wchar_t kGameLib[] = L"gtav_game.dll";
constexpr wchar_t kCodegenExe[] = L"rexgluerd.exe";
constexpr wchar_t kClang[] = L"clang++.exe";
constexpr wchar_t kObjExt[] = L".obj";
#else
constexpr wchar_t kInstalledMarker[] = L"data/installed.txt";
constexpr wchar_t kGameExe[] = L"gtav_fast";
constexpr wchar_t kGameLib[] = L"gtav_game.so";
constexpr wchar_t kCodegenExe[] = L"rexglue";
constexpr wchar_t kClang[] = L"clang++";
constexpr wchar_t kObjExt[] = L".o";
#endif
constexpr char kInstallVersion[] = "gtav-recomp install 1";
constexpr char kTitleId[] = "545408A7";
constexpr char kInstallPackageDir[] = "content/0000000000000000/545408A7/00000002/";

//-----------------------------------------------------------------------------
// Small helpers
//-----------------------------------------------------------------------------

std::string Lower(std::string s) {
  for (char& c : s) c = char(tolower(static_cast<unsigned char>(c)));
  return s;
}

std::wstring Num(uint64_t v) { return std::to_wstring(v); }

std::wstring Gb(uint64_t bytes) {
  wchar_t buf[32];
  swprintf(buf, 32, L"%.1f GB", double(bytes) / (1u << 30));
  return buf;
}

#ifdef _WIN32
class Sha256 {
 public:
  Sha256() {
    BCryptOpenAlgorithmProvider(&alg_, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    BCryptCreateHash(alg_, &hash_, nullptr, 0, nullptr, 0, 0);
  }
  ~Sha256() {
    if (hash_) BCryptDestroyHash(hash_);
    if (alg_) BCryptCloseAlgorithmProvider(alg_, 0);
  }
  void Update(const uint8_t* data, size_t size) {
    while (size) {
      ULONG n = ULONG(std::min<size_t>(size, 1u << 30));
      BCryptHashData(hash_, const_cast<PUCHAR>(data), n, 0);
      data += n;
      size -= n;
    }
  }
  std::string Hex() {
    uint8_t digest[32];
    BCryptFinishHash(hash_, digest, 32, 0);
    static const char* k = "0123456789abcdef";
    std::string s;
    for (uint8_t b : digest) {
      s += k[b >> 4];
      s += k[b & 15];
    }
    return s;
  }

 private:
  BCRYPT_ALG_HANDLE alg_ = nullptr;
  BCRYPT_HASH_HANDLE hash_ = nullptr;
};
#else
// FIPS 180-4 SHA-256 (no crypto library on the player's machine is assumed).
class Sha256 {
 public:
  void Update(const uint8_t* data, size_t size) {
    total_ += size;
    if (fill_) {
      size_t n = std::min(size, sizeof(buf_) - fill_);
      memcpy(buf_ + fill_, data, n);
      fill_ += n;
      data += n;
      size -= n;
      if (fill_ < sizeof(buf_)) return;
      Block(buf_);
      fill_ = 0;
    }
    for (; size >= 64; data += 64, size -= 64) Block(data);
    memcpy(buf_, data, size);
    fill_ = size;
  }
  std::string Hex() {
    uint64_t bits = total_ * 8;
    uint8_t pad[72] = {0x80};
    size_t n = (fill_ < 56 ? 56 : 120) - fill_;
    for (int i = 0; i < 8; ++i) pad[n + i] = uint8_t(bits >> (56 - 8 * i));
    Update(pad, n + 8);
    static const char* k = "0123456789abcdef";
    std::string s;
    for (uint32_t w : h_) {
      for (int i = 28; i >= 0; i -= 4) s += k[(w >> i) & 15];
    }
    return s;
  }

 private:
  static uint32_t Ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
  void Block(const uint8_t* p) {
    static constexpr uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
      w[i] = uint32_t(p[4 * i]) << 24 | uint32_t(p[4 * i + 1]) << 16 | uint32_t(p[4 * i + 2]) << 8 |
             p[4 * i + 3];
    for (int i = 16; i < 64; ++i) {
      uint32_t s0 = Ror(w[i - 15], 7) ^ Ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
      uint32_t s1 = Ror(w[i - 2], 17) ^ Ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
    for (int i = 0; i < 64; ++i) {
      uint32_t t1 = h + (Ror(e, 6) ^ Ror(e, 11) ^ Ror(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
      uint32_t t2 = (Ror(a, 2) ^ Ror(a, 13) ^ Ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      h = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
    h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
  }
  uint32_t h_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  uint8_t buf_[64];
  size_t fill_ = 0;
  uint64_t total_ = 0;
};
#endif

bool HashFile(const fs::path& p, std::string* hex, uint64_t* size) {
  std::wstring err;
  auto blob = OpenFileBlob(p.wstring(), &err);
  if (!blob) return false;
  Sha256 h;
  std::vector<uint8_t> buf(4u << 20);
  uint64_t off = 0, total = blob->size();
  while (off < total) {
    size_t n = size_t(std::min<uint64_t>(buf.size(), total - off));
    if (!blob->ReadAt(off, buf.data(), n)) return false;
    h.Update(buf.data(), n);
    off += n;
  }
  *hex = h.Hex();
  if (size) *size = total;
  return true;
}

// Writes a file through a .part name and renames it into place when complete.
#ifdef _WIN32
class OutFile {
 public:
  bool Open(const fs::path& p, std::wstring* error) {
    final_ = p;
    part_ = p;
    part_ += L".part";
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    h_ = CreateFileW(part_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                     FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h_ == INVALID_HANDLE_VALUE) {
      *error = L"Could not create " + part_.wstring();
      return false;
    }
    return true;
  }
  bool Write(const uint8_t* data, size_t size) {
    while (size) {
      DWORD n = DWORD(std::min<size_t>(size, 64u << 20)), done = 0;
      if (!WriteFile(h_, data, n, &done, nullptr) || done == 0) return false;
      data += done;
      size -= done;
    }
    return true;
  }
  bool Commit() {
    CloseHandle(h_);
    h_ = INVALID_HANDLE_VALUE;
    return MoveFileExW(part_.c_str(), final_.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
  }
  ~OutFile() {
    if (h_ != INVALID_HANDLE_VALUE) {
      CloseHandle(h_);
      DeleteFileW(part_.c_str());
    }
  }

 private:
  HANDLE h_ = INVALID_HANDLE_VALUE;
  fs::path final_, part_;
};
#else
class OutFile {
 public:
  bool Open(const fs::path& p, std::wstring* error) {
    final_ = p;
    part_ = p;
    part_ += L".part";
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    fd_ = open(part_.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd_ < 0) {
      *error = L"Could not create " + part_.wstring() + L": " + Widen(strerror(errno));
      return false;
    }
    return true;
  }
  bool Write(const uint8_t* data, size_t size) {
    while (size) {
      ssize_t done = write(fd_, data, std::min<size_t>(size, 64u << 20));
      if (done < 0 && errno == EINTR) continue;
      if (done <= 0) return false;
      data += done;
      size -= size_t(done);
    }
    return true;
  }
  bool Commit() {
    bool ok = close(fd_) == 0;
    fd_ = -1;
    return ok && rename(part_.c_str(), final_.c_str()) == 0;
  }
  ~OutFile() {
    if (fd_ >= 0) {
      close(fd_);
      unlink(part_.c_str());
    }
  }

 private:
  int fd_ = -1;
  fs::path final_, part_;
};
#endif

//-----------------------------------------------------------------------------
// Expected files (tools\game_dll\expected.txt, written by tools/game_dll/make_expected.py)
//   <kind> <size> <sha256> <path>
// kinds: disc2 (Disc 2 file), part (install partition file, path below 00000002/),
//        tu (title update file), gen (codegen output file)
//-----------------------------------------------------------------------------

struct Expected {
  uint64_t size = 0;
  std::string sha256;
};

struct ExpectedSet {
  std::map<std::string, Expected> disc2, part, tu, gen;
  bool Load(const fs::path& p, std::wstring* error) {
    std::ifstream f(p);
    if (!f) {
      *error = L"Missing " + p.wstring() + L" (the launcher folder is incomplete).";
      return false;
    }
    std::string line;
    while (std::getline(f, line)) {
      if (line.empty() || line[0] == '#') continue;
      std::istringstream ss(line);
      std::string kind, sha, path;
      uint64_t size = 0;
      ss >> kind >> size >> sha;
      std::getline(ss >> std::ws, path);
      auto* m = kind == "disc2" ? &disc2 : kind == "part" ? &part : kind == "tu" ? &tu
                : kind == "gen" ? &gen : nullptr;
      if (!m || path.empty()) continue;
      (*m)[path] = Expected{size, sha};
    }
    if (disc2.empty() || part.empty() || tu.empty() || gen.empty()) {
      *error = p.wstring() + L" is damaged.";
      return false;
    }
    return true;
  }
  uint64_t TotalBytes() const {
    uint64_t t = 0;
    for (auto* m : {&disc2, &part, &tu})
      for (auto& [k, v] : *m) t += v.size;
    return t;
  }
};

//-----------------------------------------------------------------------------
// Child processes
//-----------------------------------------------------------------------------

std::wstring Quote(const std::wstring& s) { return L"\"" + s + L"\""; }

// One argument per element; Windows joins them into a command line.
using Args = std::vector<std::wstring>;

#ifdef _WIN32
std::wstring CommandLine(const Args& args) {
  std::wstring cmd;
  for (const std::wstring& a : args) {
    if (!cmd.empty()) cmd += L" ";
    cmd += a.empty() || a.find_first_of(L" \t\"") != std::wstring::npos ? Quote(a) : a;
  }
  return cmd;
}

struct Child {
  PROCESS_INFORMATION pi = {};
  bool Start(const Args& args, const fs::path& cwd, const fs::path& log) {
    std::wstring cmdline = CommandLine(args);
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    HANDLE out = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si = {sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = out;
    si.hStdError = out;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    std::wstring cmd = cmdline;
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr,
                             cwd.c_str(), &si, &pi);
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    return ok != 0;
  }
  // -1 while running.
  int Poll(DWORD wait_ms = 0) {
    if (WaitForSingleObject(pi.hProcess, wait_ms) != WAIT_OBJECT_0) return -1;
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    return int(code);
  }
  void Kill() { TerminateProcess(pi.hProcess, 1); }
  ~Child() {
    if (pi.hProcess) CloseHandle(pi.hProcess);
    if (pi.hThread) CloseHandle(pi.hThread);
  }
};
#else
std::vector<std::string> NarrowArgs(const Args& args) {
  std::vector<std::string> out;
  for (const std::wstring& a : args) out.push_back(Narrow(a));
  return out;
}

std::vector<char*> Argv(std::vector<std::string>& args) {
  std::vector<char*> argv;
  for (std::string& a : args) argv.push_back(a.data());
  argv.push_back(nullptr);
  return argv;
}

struct Child {
  pid_t pid = -1;
  int status = -1;
  bool Start(const Args& args, const fs::path& cwd, const fs::path& log) {
    std::vector<std::string> narrow = NarrowArgs(args);
    std::vector<char*> argv = Argv(narrow);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
    posix_spawn_file_actions_addchdir_np(&fa, cwd.c_str());
    int err = posix_spawn(&pid, argv[0], &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    if (err != 0) {
      pid = -1;
      return false;
    }
    // The install runs in the background, like BELOW_NORMAL_PRIORITY_CLASS on Windows.
    setpriority(PRIO_PROCESS, id_t(pid), 10);
    return true;
  }
  // -1 while running. wait_ms < 0 waits until it exits.
  int Poll(int wait_ms = 0) {
    if (pid < 0) return status;
    auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(wait_ms, 0));
    for (;;) {
      int st = 0;
      pid_t r = waitpid(pid, &st, wait_ms < 0 ? 0 : WNOHANG);
      if (r == pid) {
        pid = -1;
        status = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
        return status;
      }
      if (r < 0 && errno != EINTR) return status = 1;
      if (wait_ms >= 0 && std::chrono::steady_clock::now() >= until) return -1;
      if (wait_ms >= 0) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
  void Kill() {
    if (pid > 0) kill(pid, SIGKILL);
  }
  ~Child() {
    if (pid > 0) {
      kill(pid, SIGKILL);
      waitpid(pid, nullptr, 0);
    }
  }
};
constexpr int INFINITE = -1;
#endif

std::string Tail(const fs::path& p, size_t bytes) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return {};
  f.seekg(0, std::ios::end);
  auto size = size_t(f.tellg());
  size_t n = std::min(size, bytes);
  f.seekg(size - n);
  std::string s(n, '\0');
  f.read(s.data(), n);
  return s;
}

//-----------------------------------------------------------------------------
// The install
//-----------------------------------------------------------------------------

class Installer {
 public:
  Installer(fs::path root, const InstallInputs& in, InstallUi& ui)
      : root_(std::move(root)), in_(in), ui_(ui), data_(root_ / L"data") {}

  bool Run(std::wstring* error) {
    error_ = error;
    std::error_code ec;
    fs::create_directories(data_ / L"logs", ec);
    log_.open(data_ / L"logs" / L"install.log", std::ios::app);
    Log(L"---- install started");

    if (!Preflight(root_, error)) return false;
    if (!expected_.Load(root_ / L"tools" / L"game_dll" / L"expected.txt", error)) return false;
    if (!OpenInputs()) return false;
    if (!CheckSpace()) return false;
    LoadDone();

    copy_total_ = expected_.TotalBytes();
    if (!CopyDisc2() || !CopyTitleUpdate() || !CopyInstallPartitions()) return Fail();
    if (!ReviewMismatches()) return false;
    if (!Codegen() || !CheckGenerated() || !Compile() || !Link()) return Fail();

    std::ofstream(root_ / kInstalledMarker) << kInstallVersion << "\n";
    fs::remove_all(data_ / L"build", ec);
    fs::remove(data_ / L"install_state.txt", ec);
    ui_.Progress(1.0);
    ui_.Stage(L"Done. Press Play.");
    Log(L"---- install finished");
    return true;
  }

 private:
  // Progress budget: copying 0..0.55, codegen ..0.70, check ..0.72, compile ..0.97, link ..1.
  static constexpr double kCopyEnd = 0.55, kCodegenEnd = 0.70, kCheckEnd = 0.72,
                          kCompileEnd = 0.97;

  bool Fail() {
    if (ui_.Cancelled() && error_->empty()) *error_ = L"Cancelled.";
    return false;
  }

  void Log(const std::wstring& s) {
    ui_.Log(s);
    if (log_) log_ << Narrow(s) << "\n" << std::flush;
  }

  bool SetError(const std::wstring& s) {
    *error_ = s;
    Log(L"error: " + s);
    return false;
  }

  //--- inputs ----------------------------------------------------------------

  bool OpenInputs() {
    ui_.Stage(L"Reading your discs and title update...");
    std::wstring err;
    disc2_ = OpenDisc(in_.disc2, &err);
    if (!disc2_) return SetError(L"Disc 2: " + err);
    disc1_ = OpenDisc(in_.disc1, &err);
    if (!disc1_) return SetError(L"Disc 1: " + err);

    bool disc2_has_play_data = disc2_->Find("xbox360b.rpf") && disc2_->Find("audio_rel.rpf");
    bool disc2_has_install = disc2_->Find(std::string(kInstallPackageDir) + kTitleId + "00000000");
    bool disc1_has_install = disc1_->Find(std::string(kInstallPackageDir) + kTitleId + "00000000");
    if (!disc2_has_play_data && disc2_has_install)
      return SetError(L"The file picked as Disc 2 is Disc 1 (Install). Swap the two discs.");
    if (!disc2_has_play_data || !disc2_->Find("default.xex"))
      return SetError(L"Disc 2 (Play) is missing game files. Pick the Disc 2 image or folder.");
    if (!disc1_has_install)
      return SetError(L"Disc 1 (Install) has no install packages (content\\0000000000000000\\"
                      L"545408A7\\00000002). Pick the Disc 1 image or folder.");

    // Title update: an STFS package file, or a folder with default.xexp + update.rpf.
    std::error_code ec;
    if (fs::is_directory(in_.tu, ec)) {
      tu_ = OpenDisc(in_.tu, &err);
      if (!tu_) return SetError(L"Title update: " + err);
    } else {
      auto blob = OpenFileBlob(in_.tu, &err);
      if (!blob) return SetError(L"Title update: " + err);
      if (!LooksLikeStfs(*blob))
        return SetError(L"The title update file is not an Xbox 360 package. Pick the TU27 file "
                        L"itself (for example tu00000009_00000000), not a zip.");
      tu_ = OpenStfs(std::move(blob), &err);
      if (!tu_) return SetError(L"Title update: " + err);
    }
    for (const char* name : {"default.xexp", "update.rpf"}) {
      tu_files_[name] = nullptr;
      for (const char* prefix : {"disc002/", "", "disc001/"}) {
        if (auto* e = tu_->Find(std::string(prefix) + name)) {
          tu_files_[name] = e;
          break;
        }
      }
      if (!tu_files_[name])
        return SetError(L"The title update has no " + Widen(name) + L". Pick Title Update 27.");
    }
    Log(L"Disc 1: " + disc1_->Describe());
    Log(L"Disc 2: " + disc2_->Describe());
    Log(L"Title update: " + in_.tu);
    return true;
  }

  bool CheckSpace() {
#ifdef _WIN32
    ULARGE_INTEGER free_bytes;
    if (!GetDiskFreeSpaceExW(root_.c_str(), &free_bytes, nullptr, nullptr)) return true;
#else
    struct statvfs sv;
    if (statvfs(data_.c_str(), &sv) != 0) return true;
    struct {
      uint64_t QuadPart;
    } free_bytes = {uint64_t(sv.f_bavail) * sv.f_frsize};
#endif
    uint64_t need = expected_.TotalBytes() + (3ull << 30);  // + codegen and objects
    // Files already in place (an interrupted earlier run) count as done.
    auto have = [&](const fs::path& p, uint64_t size) {
      std::error_code ec;
      if (fs::file_size(p, ec) == size && !ec) need -= size;
    };
    for (auto& [path, exp] : expected_.disc2) have(data_ / L"game" / Widen(path), exp.size);
    for (auto& [path, exp] : expected_.part)
      have(data_ / L"user" / L"0000000000000000" / Widen(kTitleId) / L"00000002" / Widen(path),
           exp.size);
    if (auto* e = Find(expected_.tu, "default.xexp")) have(data_ / L"game" / L"default.xexp", e->size);
    if (auto* e = Find(expected_.tu, "update.rpf")) have(data_ / L"update" / L"update.rpf", e->size);
    if (free_bytes.QuadPart < need)
      return SetError(L"Not enough free space next to the launcher: " + Gb(need) + L" needed, " +
                      Gb(free_bytes.QuadPart) + L" free.");
    return true;
  }

  //--- copying ---------------------------------------------------------------

  // Files verified by an earlier, interrupted run: "<sha256> <size> <dest path>".
  void LoadDone() {
    std::ifstream f(data_ / L"install_state.txt");
    std::string sha, path;
    uint64_t size;
    while (f >> sha >> size && std::getline(f >> std::ws, path)) done_[path] = {size, sha};
  }

  void MarkDone(const fs::path& dest, uint64_t size, const std::string& sha) {
    std::ofstream(data_ / L"install_state.txt", std::ios::app)
        << sha << " " << size << " " << Narrow(dest.wstring()) << "\n";
    done_[Narrow(dest.wstring())] = {size, sha};
  }

  // Copies one entry to dest, hashing on the way. Records a mismatch against `exp`.
  bool CopyEntry(Source& src, const Entry& e, const fs::path& dest, const Expected* exp,
                 const std::wstring& what) {
    if (ui_.Cancelled()) return false;
    std::error_code ec;
    auto it = done_.find(Narrow(dest.wstring()));
    if (it != done_.end() && fs::file_size(dest, ec) == e.size && !ec) {
      copied_ += e.size;
      Report();
      if (exp && it->second.sha256 != exp->sha256) mismatches_.push_back(what);
      return true;
    }
    ui_.Stage(L"Copying " + what + L" (" + Gb(e.size) + L")");
    OutFile out;
    if (!out.Open(dest, error_)) return false;
    Sha256 h;
    std::wstring err;
    bool ok = src.Read(e, [&](const uint8_t* d, size_t n) {
      if (ui_.Cancelled()) return false;
      h.Update(d, n);
      if (!out.Write(d, n)) {
        err = L"Write error on " + dest.wstring() + L" (disk full?)";
        return false;
      }
      copied_ += n;
      Report();
      return true;
    }, &err);
    if (!ok) {
      if (!ui_.Cancelled()) SetError(err);
      return false;
    }
    if (!out.Commit()) return SetError(L"Could not write " + dest.wstring());
    std::string sha = h.Hex();
    MarkDone(dest, e.size, sha);
    if (exp && (sha != exp->sha256 || e.size != exp->size)) {
      mismatches_.push_back(what);
      Log(L"differs from the known dump: " + what);
    }
    return true;
  }

  void Report() {
    double f = copy_total_ ? double(copied_) / double(copy_total_) : 0;
    ui_.Progress(std::min(f, 1.0) * kCopyEnd);
  }

  bool CopyDisc2() {
    Log(L"Copying Disc 2 game files");
    for (auto& [path, exp] : expected_.disc2) {
      const Entry* e = disc2_->Find(path);
      if (!e) return SetError(L"Disc 2 is missing " + Widen(path) + L". The image is incomplete.");
      if (!CopyEntry(*disc2_, *e, data_ / L"game" / Widen(path), &exp, L"Disc 2: " + Widen(path)))
        return false;
    }
    return true;
  }

  bool CopyTitleUpdate() {
    Log(L"Copying the title update");
    const Entry* xexp = tu_files_["default.xexp"];
    const Entry* rpf = tu_files_["update.rpf"];
    auto* exp_xexp = Find(expected_.tu, "default.xexp");
    auto* exp_rpf = Find(expected_.tu, "update.rpf");
    if (!CopyEntry(*tu_, *xexp, data_ / L"game" / L"default.xexp", exp_xexp, L"TU: default.xexp") ||
        !CopyEntry(*tu_, *rpf, data_ / L"update" / L"update.rpf", exp_rpf, L"TU: update.rpf"))
      return false;
    return true;
  }

  static const Expected* Find(const std::map<std::string, Expected>& m, const std::string& k) {
    auto it = m.find(k);
    return it == m.end() ? nullptr : &it->second;
  }

  bool CopyInstallPartitions() {
    Log(L"Unpacking the Disc 1 install partitions");
    for (int n = 0; n < 4; ++n) {
      std::string pkg = std::string(kTitleId) + "0000000" + char('0' + n);
      const Entry* e = disc1_->Find(kInstallPackageDir + pkg);
      if (!e) return SetError(L"Disc 1 is missing install package " + Widen(pkg));
      std::wstring err;
      auto blob = disc1_->OpenEntry(*e, &err);
      if (!blob) return SetError(L"Disc 1: " + err);
      auto stfs = OpenStfs(std::move(blob), &err);
      if (!stfs) return SetError(L"Disc 1 package " + Widen(pkg) + L": " + err);
      for (const Entry& f : stfs->entries()) {
        std::string rel = pkg + "/" + f.path;
        fs::path dest = data_ / L"user" / L"0000000000000000" / Widen(kTitleId) / L"00000002" /
                        Widen(pkg) / Widen(f.path);
        if (!CopyEntry(*stfs, f, dest, Find(expected_.part, rel), L"Disc 1: " + Widen(rel)))
          return false;
      }
    }
    return true;
  }

  bool ReviewMismatches() {
    // The recompiled code is generated from exactly one executable and patch.
    for (const std::wstring& m : mismatches_) {
      if (m.find(L"default.xexp") != std::wstring::npos)
        return SetError(L"Your title update is not Title Update 27 for this game (its default.xexp "
                        L"differs). The recompilation is made for TU27 only.");
    }
    std::string sha;
    uint64_t size = 0;
    auto exp_xex = Find(expected_.disc2, "default.xex");
    if (!HashFile(data_ / L"game" / L"default.xex", &sha, &size) || !exp_xex ||
        sha != exp_xex->sha256)
      return SetError(L"Your Disc 2 executable (default.xex) differs from the one this "
                      L"recompilation was made for, so it cannot be used.");
    if (mismatches_.empty()) {
      Log(L"All files match the known good dump.");
      return true;
    }
    std::wstring q = Num(mismatches_.size()) + L" file(s) differ from a known good dump of the "
                     L"game, for example:\n\n";
    for (size_t i = 0; i < mismatches_.size() && i < 5; ++i) q += L"  " + mismatches_[i] + L"\n";
    q += L"\nA damaged or modified dump can crash or misbehave. Continue anyway?";
    if (!ui_.Ask(q)) return SetError(L"Stopped: some game files differ from a known good dump.");
    return true;
  }

  //--- codegen ---------------------------------------------------------------

  bool Codegen() {
    fs::path build = data_ / L"build";
    fs::path gen = build / L"generated" / L"default";
    fs::path tools = root_ / L"tools" / L"game_dll";
    std::error_code ec;
    fs::remove_all(build / L"generated", ec);
    fs::create_directories(gen, ec);
    fs::create_directories(build / L"assets", ec);
    fs::copy_file(tools / L"gtav_manifest.toml", build / L"gtav_manifest.toml",
                  fs::copy_options::overwrite_existing, ec);
    if (ec) return SetError(L"Could not stage the manifest: " + Widen(ec.message()));
    // Seeds codegen's file partitioning, so every player gets the same source files.
    fs::copy_file(tools / L"codegen.partition.json", gen / L"codegen.partition.json",
                  fs::copy_options::overwrite_existing, ec);
    for (const wchar_t* f : {L"default.xex", L"default.xexp"}) {
      fs::copy_file(data_ / L"game" / f, build / L"assets" / f, fs::copy_options::overwrite_existing,
                    ec);
      if (ec) return SetError(L"Could not stage " + std::wstring(f) + L": " + Widen(ec.message()));
    }

    ui_.Stage(L"Recompiling the game executable to C++ (several minutes)...");
    Log(L"Running codegen");
    fs::path log = data_ / L"logs" / L"codegen.log";
    Child c;
    Args cmd = {(root_ / kCodegenExe).wstring(), L"--log-level", L"info", L"codegen",
                L"gtav_manifest.toml"};
    if (!c.Start(cmd, build, log)) return SetError(L"Could not start " + std::wstring(kCodegenExe));
    size_t want = expected_.gen.size();
    for (;;) {
      int code = c.Poll(1000);
      if (code >= 0) {
        if (code != 0)
          return SetError(L"Codegen failed (exit " + Num(code) + L"). See " + log.wstring() +
                          L":\n" + Widen(Tail(log, 600)));
        break;
      }
      if (ui_.Cancelled()) {
        c.Kill();
        return false;
      }
      size_t have = 0;
      for (auto it = fs::directory_iterator(gen, ec); !ec && it != fs::directory_iterator();
           it.increment(ec))
        ++have;
      double f = want ? std::min(1.0, double(have) / double(want)) : 0;
      ui_.Progress(kCopyEnd + f * (kCodegenEnd - kCopyEnd));
    }
    ui_.Progress(kCodegenEnd);
    return true;
  }

  bool CheckGenerated() {
    ui_.Stage(L"Checking the generated code...");
    fs::path gen = data_ / L"build" / L"generated" / L"default";
    size_t n = 0, bad = 0;
    std::wstring first_bad;
    for (auto& [name, exp] : expected_.gen) {
      std::string sha;
      uint64_t size = 0;
      if (!HashFile(gen / Widen(name), &sha, &size) || sha != exp.sha256) {
        if (!bad++) first_bad = Widen(name);
      }
      if (++n % 50 == 0)
        ui_.Progress(kCodegenEnd + (kCheckEnd - kCodegenEnd) * double(n) / expected_.gen.size());
    }
    if (bad)
      return SetError(Num(bad) + L" generated file(s) differ from the reference build (first: " +
                      first_bad + L"). The codegen tool or your files differ from what this "
                      L"release was made with.");
    Log(L"Generated code matches the reference build (" + Num(n) + L" files).");
    return true;
  }

  //--- compile and link ------------------------------------------------------

  static int JobCount() {
#ifndef _WIN32
    // MemAvailable from /proc/meminfo, in kB.
    int64_t avail_mb = 4096;
    std::ifstream mi("/proc/meminfo");
    std::string key;
    int64_t kb;
    while (mi >> key >> kb) {
      if (key == "MemAvailable:") {
        avail_mb = kb / 1024;
        break;
      }
      mi.ignore(64, '\n');
    }
    int64_t by_ram_mb = (avail_mb - 2048) / 1024;
    int jobs_mb = int(std::min<int64_t>(sysconf(_SC_NPROCESSORS_ONLN), by_ram_mb));
    return std::clamp(jobs_mb, 1, 32);
#endif
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    MEMORYSTATUSEX ms = {sizeof(ms)};
    GlobalMemoryStatusEx(&ms);
    // clang needs up to ~1 GB for the largest generated file; keep 2 GB for everything else.
    int64_t by_ram = (int64_t(ms.ullAvailPhys >> 20) - 2048) / 1024;
    int jobs = int(std::min<int64_t>(si.dwNumberOfProcessors, by_ram));
    return std::clamp(jobs, 1, 32);
#endif
  }

  bool Compile() {
    fs::path gen = data_ / L"build" / L"generated" / L"default";
    fs::path obj = data_ / L"build" / L"obj";
    fs::path fsdir = root_ / L"tools" / L"game_dll" / L"fs";
    fs::path iface = root_ / L"tools" / L"game_dll" / L"interface";
    std::error_code ec;
    fs::create_directories(obj, ec);
    for (auto [src, name] : {std::pair{fsdir, L"gtav_pch.h"}, {fsdir, L"layout_check.h"},
                             {iface, L"gtav_hooks.h"}}) {
      fs::copy_file(src / name, gen / name, fs::copy_options::overwrite_existing, ec);
      if (ec) return SetError(L"Could not stage " + std::wstring(name));
    }
    std::vector<fs::path> sources;
    for (auto& [name, exp] : expected_.gen) {
      if ((name.rfind("gtav_recomp.", 0) == 0 && name.size() > 4 &&
           name.substr(name.size() - 4) == ".cpp") || name == "gtav_init.cpp")
        sources.push_back(gen / Widen(name));
    }
    sources.push_back(fsdir / L"game_dll.cpp");
    sources.push_back(fsdir / L"fs_crt.cpp");

    fs::path clang = root_ / L"tools" / L"llvm" / L"bin" / kClang;
    // Keep in sync with tools/game_dll/build_game_dll.py.
#ifdef _WIN32
    Args flags = {L"--target=x86_64-pc-windows-msvc", L"-nostdinc++", L"-nostdlibinc", L"-O2",
                  L"-std=gnu++23", L"-march=x86-64-v3", L"-msse4.1", L"-fno-exceptions",
                  L"-fno-rtti", L"-fno-threadsafe-statics", L"-fno-math-errno", L"-w"};
#else
    Args flags = {L"--target=x86_64-linux-gnu", L"-nostdinc++", L"-nostdlibinc", L"-fPIC",
                  L"-fno-semantic-interposition", L"-fno-stack-protector",
                  L"-fcf-protection=none", L"-O2", L"-std=gnu++23", L"-march=x86-64-v3",
                  L"-msse4.1", L"-fno-exceptions", L"-fno-rtti", L"-fno-threadsafe-statics",
                  L"-fno-math-errno", L"-w"};
#endif
    flags.insert(flags.end(), {L"-isystem", (fsdir / L"include").wstring(), L"-I", gen.wstring()});

    int jobs = JobCount();
    Log(L"Compiling " + Num(sources.size()) + L" files with " + Num(jobs) + L" parallel jobs");
    ui_.Stage(L"Compiling the game code (" + Num(sources.size()) + L" files, " + Num(jobs) +
              L" at a time)...");
    struct Running {
      std::unique_ptr<Child> child;
      fs::path log, src;
    };
    std::vector<Running> running;
    size_t next = 0, finished = 0;
    auto t0 = std::chrono::steady_clock::now();
    while (finished < sources.size()) {
      while (running.size() < size_t(jobs) && next < sources.size()) {
        const fs::path& src = sources[next++];
        fs::path o = obj / (src.filename().wstring() + kObjExt);
        objects_.push_back(o);
        Running r{std::make_unique<Child>(), obj / (src.filename().wstring() + L".log"), src};
        Args cmd = {clang.wstring()};
        cmd.insert(cmd.end(), flags.begin(), flags.end());
        cmd.insert(cmd.end(), {L"-c", src.wstring(), L"-o", o.wstring()});
        if (!r.child->Start(cmd, gen, r.log)) return SetError(L"Could not start " + std::wstring(kClang));
        running.push_back(std::move(r));
      }
      for (size_t i = 0; i < running.size();) {
        int code = running[i].child->Poll(i == 0 ? 200 : 0);
        if (code < 0) {
          ++i;
          continue;
        }
        if (code != 0) {
          for (auto& r : running) r.child->Kill();
          return SetError(L"Compiling " + running[i].src.filename().wstring() + L" failed:\n" +
                          Widen(Tail(running[i].log, 1500)));
        }
        fs::remove(running[i].log, ec);
        running.erase(running.begin() + i);
        ++finished;
      }
      if (ui_.Cancelled()) {
        for (auto& r : running) r.child->Kill();
        return false;
      }
      double f = double(finished) / double(sources.size());
      ui_.Progress(kCheckEnd + f * (kCompileEnd - kCheckEnd));
    }
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - t0);
    Log(L"Compiled in " + Num(uint64_t(secs.count())) + L" s");
    return true;
  }

#ifndef _WIN32
  bool Link() {
    ui_.Stage(L"Linking gtav_game.so...");
    fs::path build = data_ / L"build";
    fs::path iface = root_ / L"tools" / L"game_dll" / L"interface";
    fs::path lld = root_ / L"tools" / L"llvm" / L"bin" / L"ld.lld";
    fs::path rsp = build / L"link.rsp";
    {
      std::ofstream f(rsp);
      for (auto& o : objects_) f << "\"" << o.string() << "\"\n";
    }
    fs::path out = root_ / L"gtav_game.so.new";
    fs::path log = data_ / L"logs" / L"link.log";
    // Keep in sync with tools/game_dll/build_game_dll.py. -Bsymbolic keeps calls between game
    // functions direct; the hooks, bridge, kernel imports and log2f bind to gtav_fast,
    // librexruntime.so and libm at load time (-z now: at once, so a missing one fails at start).
    Args cmd = {lld.wstring(), L"-shared", L"-soname", L"gtav_game.so", L"-Bsymbolic", L"-z", L"now",
                L"-z", L"relro", L"-z", L"noexecstack", L"--hash-style=gnu", L"-O1",
                L"--version-script=" + (iface / L"gtav_game.exports").wstring(), L"-o",
                out.wstring(), L"@" + rsp.wstring()};
    Child c;
    if (!c.Start(cmd, build, log)) return SetError(L"Could not start ld.lld");
    int code = c.Poll(INFINITE);
    if (code != 0) return SetError(L"Linking failed:\n" + Widen(Tail(log, 1500)));
    if (rename(out.c_str(), (root_ / kGameLib).c_str()) != 0)
      return SetError(L"Could not put gtav_game.so in place: " + Widen(strerror(errno)));
    Log(L"Built gtav_game.so");
    ui_.Progress(1.0);
    return true;
  }
#else
  bool Link() {
    ui_.Stage(L"Linking gtav_game.dll...");
    fs::path build = data_ / L"build";
    fs::path iface = root_ / L"tools" / L"game_dll" / L"interface";
    fs::path lld = root_ / L"tools" / L"llvm" / L"bin" / L"lld-link.exe";
    std::vector<fs::path> libs;
    for (const wchar_t* name : {L"gtav_fast", L"rexruntime", L"ucrtbase"}) {
      fs::path lib = build / (std::wstring(name) + L".lib");
      Child c;
      Args cmd = {lld.wstring(), L"/lib", L"/machine:x64",
                  L"/def:" + (iface / (std::wstring(name) + L".def")).wstring(),
                  L"/out:" + lib.wstring()};
      fs::path log = data_ / L"logs" / L"implib.log";
      if (!c.Start(cmd, build, log) || c.Poll(INFINITE) != 0)
        return SetError(L"Could not build an import library:\n" + Widen(Tail(log, 800)));
      libs.push_back(lib);
    }
    fs::path rsp = build / L"link.rsp";
    {
      std::ofstream f(rsp);
      for (auto& o : objects_) f << "\"" << Narrow(o.wstring()) << "\"\n";
      for (auto& l : libs) f << "\"" << Narrow(l.wstring()) << "\"\n";
    }
    fs::path out = root_ / L"gtav_game.dll.new";
    fs::path log = data_ / L"logs" / L"link.log";
    Args cmd = {lld.wstring(), L"/dll", L"/noentry", L"/nodefaultlib", L"/machine:x64",
                L"/debug:none", L"/def:" + (iface / L"gtav_game.def").wstring(),
                L"/out:" + out.wstring(), L"@" + rsp.wstring()};
    Child c;
    if (!c.Start(cmd, build, log)) return SetError(L"Could not start lld-link.exe");
    int code = c.Poll(INFINITE);
    if (code != 0) return SetError(L"Linking failed:\n" + Widen(Tail(log, 1500)));
    if (!MoveFileExW(out.c_str(), (root_ / L"gtav_game.dll").c_str(), MOVEFILE_REPLACE_EXISTING))
      return SetError(L"Could not put gtav_game.dll in place (is the game running?)");
    Log(L"Built gtav_game.dll");
    ui_.Progress(1.0);
    return true;
  }
#endif

  fs::path root_;
  InstallInputs in_;
  InstallUi& ui_;
  fs::path data_;
  std::wstring* error_ = nullptr;
  std::ofstream log_;
  ExpectedSet expected_;
  std::unique_ptr<Source> disc1_, disc2_, tu_;
  std::map<std::string, const Entry*> tu_files_;
  std::map<std::string, Expected> done_;
  std::vector<std::wstring> mismatches_;
  std::vector<fs::path> objects_;
  uint64_t copy_total_ = 0, copied_ = 0;
};

}  // namespace

bool IsInstalled(const fs::path& root) {
  std::error_code ec;
  if (!fs::exists(root / kGameLib, ec)) return false;
  std::ifstream f(root / kInstalledMarker);
  std::string line;
  return std::getline(f, line) && line == kInstallVersion;
}

#ifndef _WIN32
bool Preflight(const fs::path& root, std::wstring* error) {
  // gtav_fast and the game code are built for x86-64-v3 (AVX2, BMI2, FMA, MOVBE).
  __builtin_cpu_init();
  if (!__builtin_cpu_supports("avx2") || !__builtin_cpu_supports("bmi2") ||
      !__builtin_cpu_supports("fma")) {
    *error = L"This processor has no AVX2. The game needs an x86-64 CPU from about 2013 or later "
             L"(Intel Haswell, AMD Excavator / Zen or newer).";
    return false;
  }
  if (void* vk = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL)) {
    dlclose(vk);
  } else {
    *error = L"The Vulkan loader (libvulkan.so.1) is not installed. Install your distribution's "
             L"Vulkan package (for example libvulkan1 and mesa-vulkan-drivers, or the NVIDIA "
             L"driver) and try again.";
    return false;
  }
  for (const wchar_t* f :
       {L"gtav_fast", L"librexruntime.so", L"librexgpu-xenos.so", L"rexglue",
        L"tools/llvm/bin/clang++", L"tools/llvm/bin/ld.lld", L"tools/game_dll/expected.txt",
        L"tools/game_dll/gtav_manifest.toml", L"tools/game_dll/fs/gtav_pch.h",
        L"tools/game_dll/interface/gtav_game.exports"}) {
    std::error_code ec;
    if (!fs::exists(root / f, ec)) {
      *error = std::wstring(L"The release folder is incomplete: ") + f + L" is missing. "
               L"Unpack the whole release archive again.";
      return false;
    }
  }
  return true;
}
#else
bool Preflight(const fs::path& root, std::wstring* error) {
  // gtav_fast.exe and the game code are built for x86-64-v3 (AVX2, BMI2, FMA, MOVBE).
  if (!IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE)) {
    *error = L"This processor has no AVX2. The game needs an x86-64 CPU from about 2013 or later "
             L"(Intel Haswell, AMD Excavator / Zen or newer).";
    return false;
  }
  // The runtime DLLs use the Visual C++ runtime, which Windows does not always include.
  for (const wchar_t* dll : {L"msvcp140.dll", L"msvcp140_atomic_wait.dll", L"vcruntime140.dll", L"vcruntime140_1.dll"}) {
    HMODULE m = LoadLibraryExW(dll, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!m) {
      *error = std::wstring(L"The Microsoft Visual C++ 2015-2022 Redistributable (x64) is not "
                            L"installed (") + dll + L" is missing). Install it from Microsoft: "
                            L"https://aka.ms/vs/17/release/vc_redist.x64.exe";
      return false;
    }
    FreeLibrary(m);
  }
  for (const wchar_t* f :
       {L"gtav_fast.exe", L"rexruntimerd.dll", L"rexgluerd.exe", L"tools\\llvm\\bin\\clang++.exe",
        L"tools\\llvm\\bin\\lld-link.exe", L"tools\\game_dll\\expected.txt",
        L"tools\\game_dll\\gtav_manifest.toml", L"tools\\game_dll\\fs\\gtav_pch.h",
        L"tools\\game_dll\\interface\\gtav_game.def"}) {
    std::error_code ec;
    if (!fs::exists(root / f, ec)) {
      *error = std::wstring(L"The launcher folder is incomplete: ") + f + L" is missing. "
               L"Unpack the whole release zip again.";
      return false;
    }
  }
  return true;
}
#endif

bool RunInstall(const fs::path& root, const InstallInputs& in, InstallUi& ui, std::wstring* error) {
  Installer inst(root, in, ui);
  return inst.Run(error);
}

bool LaunchGame(const fs::path& root, std::wstring* error, const std::vector<std::wstring>& extra) {
  fs::path data = root / L"data";
  std::error_code ec;
  fs::create_directories(data / L"spirv", ec);
  fs::create_directories(data / L"logs", ec);
  fs::create_directories(data / L"user", ec);
  // Read the exe and DLLs once so they are in the file cache: slow first page-ins during boot
  // were seen to hang it (see play.ps1).
  std::vector<uint8_t> buf(8u << 20);
  for (auto it = fs::directory_iterator(root, ec); !ec && it != fs::directory_iterator();
       it.increment(ec)) {
    auto ext = Lower(Narrow(it->path().extension().wstring()));
    if (ext != ".dll" && ext != ".so" && it->path().filename() != kGameExe) continue;
    std::wstring err;
    if (auto b = OpenFileBlob(it->path().wstring(), &err)) {
      for (uint64_t off = 0; off < b->size(); off += buf.size())
        b->ReadAt(off, buf.data(), size_t(std::min<uint64_t>(buf.size(), b->size() - off)));
    }
  }
  auto arg = [](const std::wstring& name, const fs::path& p) {
    return L"--" + name + L"=" + p.wstring();
  };
  Args args = {(root / kGameExe).wstring(), arg(L"game_data_root", data / L"game"),
               arg(L"user_data_root", data / L"user"), arg(L"update_data_root", data / L"update"),
               arg(L"log_file", data / L"logs" / L"gtav.log"),
               arg(L"gtav_native_spirv_dir", data / L"spirv"), L"--gpu_plugin=xenos",
               L"--headless=true", L"--log_level=info", L"--log_max_file_size_mb=256",
               L"--log_max_files=2",
#ifdef _WIN32
               L"--input_backend=xinput",
#endif
               L"--gtav_native=on", L"--gtav_fps_limit=0", L"--gpu_skip_rendering=true",
               L"--gpu_skip_rendering_lag_ms=4", L"--vsync=false", L"--gtav_native_vsync=false"};
  args.insert(args.end(), extra.begin(), extra.end());
#ifndef _WIN32
  // The launcher becomes the game: it keeps the terminal and the exit code.
  if (chdir(root.c_str()) != 0) {
    *error = L"Could not enter " + root.wstring();
    return false;
  }
  std::vector<std::string> narrow = NarrowArgs(args);
  std::vector<char*> argv = Argv(narrow);
  execv(argv[0], argv.data());
  *error = L"Could not start " + args[0] + L": " + Widen(strerror(errno));
  return false;
#else
  std::wstring cmd = CommandLine(args);
  STARTUPINFOW si = {sizeof(si)};
  PROCESS_INFORMATION pi = {};
  if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, root.c_str(), &si,
                      &pi)) {
    *error = L"Could not start gtav_fast.exe";
    return false;
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
#endif
}

}  // namespace loader
