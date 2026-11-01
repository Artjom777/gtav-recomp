// Command-line twin of gtav/src/native/adreno_shader_fix.cpp, to check it against the Python
// passes (tools/android/check_shader_fix.sh) and to run them where Python is missing.
//   shader_fix ubo FILE...        rewrite XenosRecomp HLSL in place (ubo_constants.py)
//   shader_fix int64 IN.spv OUT.spv   lower 64-bit integers (lower_int64.py)
#include "../../../gtav/src/native/adreno_shader_fix.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>

int main(int argc, char** argv) {
  if (argc >= 3 && std::string(argv[1]) == "ubo") {
    for (int i = 2; i < argc; ++i) {
      std::ifstream in(argv[i], std::ios::binary);
      std::string src((std::istreambuf_iterator<char>(in)), {});
      std::string out = src, err;
      if (!gtav::adreno::UboConstants(out, &err)) {
        std::fprintf(stderr, "%s: %s\n", argv[i], err.c_str());
        return 1;
      }
      if (out != src) std::ofstream(argv[i], std::ios::binary) << out;
    }
    return 0;
  }
  if (argc == 4 && std::string(argv[1]) == "int64") {
    std::ifstream in(argv[2], std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)), {});
    std::vector<uint32_t> words(bytes.size() / 4);
    std::memcpy(words.data(), bytes.data(), words.size() * 4);
    std::string err;
    if (!gtav::adreno::LowerInt64(words, &err)) {
      std::fprintf(stderr, "%s: %s\n", argv[2], err.c_str());
      return 1;
    }
    std::ofstream(argv[3], std::ios::binary)
        .write(reinterpret_cast<const char*>(words.data()), std::streamsize(words.size() * 4));
    return 0;
  }
  std::fprintf(stderr, "usage: shader_fix ubo FILE... | shader_fix int64 IN.spv OUT.spv\n");
  return 2;
}
