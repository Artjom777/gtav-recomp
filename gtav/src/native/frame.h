// gtav - one guest frame as recorded for the native renderer.
//
// Guest threads append commands between two Swaps; the render thread replays them. Bulky
// per-command state (register shadow, constants, index data) lives in the packet's byte arena
// and is referenced by offset, so a frame is two allocations however many draws it has.

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace gtav::native {

// Copied verbatim from the D3D device at draw time (offsets in d3d_map.h).
constexpr uint32_t kRegShadowBegin = 10112;  // bool/loop constants, then register blocks
constexpr uint32_t kRegShadowEnd = 11008;
constexpr uint32_t kRegShadowSize = kRegShadowEnd - kRegShadowBegin;
constexpr uint32_t kFetchSize = 32 * 6 * 4;
constexpr uint32_t kAluSize = 256 * 16;
constexpr uint32_t kMaxVertexElements = 16;
constexpr uint32_t kMaxStreams = 4;

struct VertexElement {  // D3DVERTEXELEMENT9 as the XDK stores it (host-endian copy)
  uint16_t stream, offset;
  uint32_t type;
  uint8_t method, usage, usage_index, pad;
};

struct StreamBinding {
  uint32_t address = 0;  // guest physical
  uint32_t size = 0;     // bytes, from the vertex buffer's fetch constant
  uint32_t offset = 0, stride = 0;
  uint32_t endian = 0;   // fetch constant endian mode (xenos::Endian)
};

enum class CommandKind : uint8_t { kDraw, kClear, kResolve, kQuery };

struct DrawCommand {
  bool indexed = false;
  bool index32 = false;
  uint32_t primitive = 0;
  int32_t base_vertex = 0;
  uint32_t start = 0, count = 0;
  uint32_t index_address = 0, index_size = 0;  // guest physical, bytes
  // BeginVertices/EndVertices draws: stream 0 is CPU-written data copied into the arena.
  uint32_t inline_vertices = 0, inline_size = 0;
  // BeginIndexedVertices draws: the CPU-written indices, copied into the arena as well.
  uint32_t inline_indices = 0, inline_index_size = 0;
  bool is_inline = false;
  uint64_t vs_hash = 0, ps_hash = 0;
  uint64_t ps_ucode_hash = 0;  // Xenia's key, for comparisons with the emulator
  uint32_t regs = 0, fetch = 0, vs_consts = 0, ps_consts = 0;  // arena offsets
  uint32_t element_count = 0;
  VertexElement elements[kMaxVertexElements];
  StreamBinding streams[kMaxStreams];
  uint32_t render_targets[4] = {}, depth_stencil = 0;  // guest surface objects
};

struct ClearCommand {
  uint32_t flags = 0, color = 0, stencil = 0;
  float z = 1.0f;
  uint32_t regs = 0;  // arena offset of the register shadow (which RT is bound)
};

struct ResolveCommand {
  uint32_t flags = 0, dest_texture = 0, regs = 0;
  uint32_t source_rect[4] = {};  // x1, y1, x2, y2 when has_rect
  bool has_rect = false;
  uint32_t dest_point[2] = {};  // destination x, y when has_point
  bool has_point = false;
  float clear_color[4] = {};  // for D3DRESOLVE_CLEARRENDERTARGET
  float clear_z = 0.0f;       // for D3DRESOLVE_CLEARDEPTHSTENCIL
  uint32_t dest_level = 0, dest_face = 0;  // mip level and cube face / array slice
  uint32_t dest_words[13] = {};  // dest texture object header + fetch constant (layout check)
};

// D3D occlusion query Issue(BEGIN / END). The native renderer counts the samples the draws between
// them pass and publishes the count for the guest's GetData (see PublishQueryResult).
struct QueryCommand {
  uint32_t query = 0;   // guest D3DQuery object
  uint32_t serial = 0;  // which END of that query (counts per query object)
  bool begin = false;
};

struct Command {
  CommandKind kind;
  uint32_t index;  // into the per-kind vector
  // Guest physical PM4 write address when captured: inside a D3D command buffer while the game
  // records one (see OnCommandBufferRun); 0 for commands replayed from a recorded buffer.
  uint32_t pm4 = 0;
};

// Append-only bytes without std::vector's zero fill: a frame pushes tens of MB of register
// shadows and constants, and packets are recycled (TakeSpare), so the capacity is kept.
class ByteArena {
 public:
  uint8_t* data() { return buf_.get(); }
  const uint8_t* data() const { return buf_.get(); }
  size_t size() const { return size_; }
  void clear() { size_ = 0; }
  void Shrink(size_t n) { size_ -= std::min(n, size_); }
  uint8_t* Grow(size_t n) {
    if (size_ + n > cap_) {
      size_t cap = std::max(size_ + n, cap_ ? cap_ * 2 : size_t(4) << 20);
      auto buf = std::make_unique_for_overwrite<uint8_t[]>(cap);
      if (size_) std::memcpy(buf.get(), buf_.get(), size_);
      buf_ = std::move(buf);
      cap_ = cap;
    }
    uint8_t* p = buf_.get() + size_;
    size_ += n;
    return p;
  }

 private:
  std::unique_ptr<uint8_t[]> buf_;
  size_t size_ = 0, cap_ = 0;
};

struct FramePacket {
  uint64_t index = 0;
  std::vector<Command> commands;
  std::vector<DrawCommand> draws;
  std::vector<ClearCommand> clears;
  std::vector<ResolveCommand> resolves;
  std::vector<QueryCommand> queries;
  ByteArena arena;
  uint32_t front_buffer = 0;  // guest texture object passed to Swap

  uint32_t Push(const void* data, uint32_t size) {
    uint32_t at = uint32_t(arena.size());
    std::memcpy(arena.Grow(size), data, size);
    return at;
  }
  // Empties the packet but keeps every buffer's capacity for the next frame.
  void Clear() {
    index = 0;
    commands.clear();
    draws.clear();
    clears.clear();
    resolves.clear();
    queries.clear();
    arena.clear();
    front_buffer = 0;
  }
  const uint8_t* At(uint32_t offset) const { return arena.data() + offset; }
};

}  // namespace gtav::native
