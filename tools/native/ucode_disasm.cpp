// Disassemble the Xenos microcode of a dumped shader container (vs_/ps_<hash>.bin) with the
// SDK's shader analyzer, to check XenosRecomp's HLSL against the real instructions.
//
// build: see tools/native/build_ucode_disasm.sh
// usage: ucode_disasm CONTAINER.bin

#include <rex/graphics/pipeline/shader/shader.h>

#include <bit>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

static uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: ucode_disasm CONTAINER.bin\n");
    return 1;
  }
  std::ifstream in(argv[1], std::ios::binary);
  std::vector<uint8_t> c((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (c.size() < 0x24) return 1;
  bool pixel = (be32(c.data()) & 1) == 0;
  uint32_t virtual_size = be32(c.data() + 4);
  uint32_t shader_offset = be32(c.data() + 0x18);  // ShaderContainer::shaderOffset
  const uint8_t* header = c.data() + shader_offset;
  uint32_t physical_offset = be32(header), size = be32(header + 4);
  const uint8_t* ucode = c.data() + virtual_size + physical_offset;
  if (virtual_size + physical_offset + size > c.size()) {
    std::fprintf(stderr, "bad container layout\n");
    return 1;
  }
  std::vector<uint32_t> dwords(size / 4);
  std::memcpy(dwords.data(), ucode, size);
  rex::graphics::Shader shader(pixel ? rex::graphics::xenos::ShaderType::kPixel
                                     : rex::graphics::xenos::ShaderType::kVertex,
                               0, dwords.data(), dwords.size(), std::endian::big);
  rex::string::StringBuffer buffer;
  shader.AnalyzeUcode(buffer);
  std::cout << shader.ucode_disassembly();
  return 0;
}

// The analyzer reads this cvar (normally defined by the graphics system).
#include <rex/cvar.h>
REXCVAR_DEFINE_STRING(dump_shaders, "", "GPU", "unused by this tool");
