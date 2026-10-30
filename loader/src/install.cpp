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
#include "install.h"

#include <windows.h>
#include <bcrypt.h>

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

constexpr wchar_t kInstalledMarker[] = L"data\\installed.txt";
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

struct Child {
  PROCESS_INFORMATION pi = {};
  bool Start(const std::wstring& cmdline, const fs::path& cwd, const fs::path& log) {
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
    ULARGE_INTEGER free_bytes;
    if (!GetDiskFreeSpaceExW(root_.c_str(), &free_bytes, nullptr, nullptr)) return true;
    uint64_t need = expected_.TotalBytes() + (3ull << 30);  // + codegen and objects
    // Files already in place count as done.
    for (auto& [path, exp] : expected_.disc2) {
      std::error_code ec;
      if (fs::file_size(data_ / L"game" / Widen(path), ec) == exp.size && !ec) need -= exp.size;
    }
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
    std::wstring cmd = Quote((root_ / L"rexgluerd.exe").wstring()) +
                       L" --log-level info codegen gtav_manifest.toml";
    if (!c.Start(cmd, build, log)) return SetError(L"Could not start rexgluerd.exe");
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
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    MEMORYSTATUSEX ms = {sizeof(ms)};
    GlobalMemoryStatusEx(&ms);
    // clang needs up to ~1 GB for the largest generated file; keep 2 GB for everything else.
    int64_t by_ram = (int64_t(ms.ullAvailPhys >> 20) - 2048) / 1024;
    int jobs = int(std::min<int64_t>(si.dwNumberOfProcessors, by_ram));
    return std::clamp(jobs, 1, 32);
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

    fs::path clang = root_ / L"tools" / L"llvm" / L"bin" / L"clang++.exe";
    std::wstring flags =
        L" --target=x86_64-pc-windows-msvc -nostdinc++ -nostdlibinc -O2 -std=gnu++23"
        L" -march=x86-64-v3 -msse4.1 -fno-exceptions -fno-rtti -fno-threadsafe-statics"
        L" -fno-math-errno -w -isystem " + Quote((fsdir / L"include").wstring()) +
        L" -I " + Quote(gen.wstring());

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
        fs::path o = obj / (src.filename().wstring() + L".obj");
        objects_.push_back(o);
        Running r{std::make_unique<Child>(), obj / (src.filename().wstring() + L".log"), src};
        std::wstring cmd = Quote(clang.wstring()) + flags + L" -c " + Quote(src.wstring()) +
                           L" -o " + Quote(o.wstring());
        if (!r.child->Start(cmd, gen, r.log)) return SetError(L"Could not start clang++.exe");
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

  bool Link() {
    ui_.Stage(L"Linking gtav_game.dll...");
    fs::path build = data_ / L"build";
    fs::path iface = root_ / L"tools" / L"game_dll" / L"interface";
    fs::path lld = root_ / L"tools" / L"llvm" / L"bin" / L"lld-link.exe";
    std::vector<fs::path> libs;
    for (const wchar_t* name : {L"gtav_fast", L"rexruntime", L"ucrtbase"}) {
      fs::path lib = build / (std::wstring(name) + L".lib");
      Child c;
      std::wstring cmd = Quote(lld.wstring()) + L" /lib /machine:x64 " +
                         Quote(L"/def:" + (iface / (std::wstring(name) + L".def")).wstring()) +
                         L" " + Quote(L"/out:" + lib.wstring());
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
    std::wstring cmd = Quote(lld.wstring()) +
                       L" /dll /noentry /nodefaultlib /machine:x64 /debug:none " +
                       Quote(L"/def:" + (iface / L"gtav_game.def").wstring()) + L" " +
                       Quote(L"/out:" + out.wstring()) + L" " + Quote(L"@" + rsp.wstring());
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
  if (!fs::exists(root / L"gtav_game.dll", ec)) return false;
  std::ifstream f(root / kInstalledMarker);
  std::string line;
  return std::getline(f, line) && line == kInstallVersion;
}

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

bool RunInstall(const fs::path& root, const InstallInputs& in, InstallUi& ui, std::wstring* error) {
  Installer inst(root, in, ui);
  return inst.Run(error);
}

bool LaunchGame(const fs::path& root, std::wstring* error) {
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
    if (ext != ".dll" && it->path().filename() != L"gtav_fast.exe") continue;
    std::wstring err;
    if (auto b = OpenFileBlob(it->path().wstring(), &err)) {
      for (uint64_t off = 0; off < b->size(); off += buf.size())
        b->ReadAt(off, buf.data(), size_t(std::min<uint64_t>(buf.size(), b->size() - off)));
    }
  }
  auto arg = [](const std::wstring& name, const fs::path& p) {
    return L" " + Quote(L"--" + name + L"=" + p.wstring());
  };
  std::wstring cmd = Quote((root / L"gtav_fast.exe").wstring()) + arg(L"game_data_root", data / L"game") +
                     arg(L"user_data_root", data / L"user") +
                     arg(L"update_data_root", data / L"update") +
                     arg(L"log_file", data / L"logs" / L"gtav.log") +
                     arg(L"gtav_native_spirv_dir", data / L"spirv") +
                     L" --gpu_plugin=xenos --headless=true --log_level=info"
                     L" --log_max_file_size_mb=256 --log_max_files=2 --input_backend=xinput"
                     L" --gtav_native=on --gtav_fps_limit=0 --gpu_skip_rendering=true"
                     L" --gpu_skip_rendering_lag_ms=4 --vsync=false --gtav_native_vsync=false";
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
}

}  // namespace loader
