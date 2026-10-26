// gtav - guest D3D state tracking and frame recording for the native renderer.
//
// Hooks report bound objects (index buffer, streams, shaders) and commands here; each draw
// snapshots the device register shadow and constants into the current FramePacket, which the
// Swap hook hands to the render thread. --gtav_native_layout_probe=N logs the objects seen by
// the first draws of guest frame N (used to verify layouts before the draw path relies on them).

#include "capture.h"

#include "d3d_map.h"
#include "frame.h"
#include "renderer.h"
#include "shaders.h"
#include "xenos_regs.h"

#include <unordered_map>
#include <vector>
#include <algorithm>
#include <rex/cvar.h>
#include <rex/kernel.h>
#include <rex/logging.h>
#include <rex/system.h>

#include <atomic>
#include <cstring>
#include <string>

REXCVAR_DEFINE_INT32(gtav_native_layout_probe, 0, "GTAV",
                     "Log objects and register shadow for the first draws of this guest frame");

namespace gtav::native {
namespace {

constexpr int kProbeDraws = 6;
constexpr uint32_t kStreamCount = kMaxStreams;

struct BoundState {
  uint32_t index_buffer = 0, vertex_shader = 0, pixel_shader = 0;
  uint32_t streams[kStreamCount][3] = {};  // vertex buffer object, offset, stride
};
BoundState g_bound;  // the game renders from one thread; revisit if that changes

// ALU constants the game writes inline into the command buffer (83813580 returns where the data
// goes), which never reach the device shadow. Kept per register until a regular
// Set*ShaderConstantF overwrites the register again.
struct InlineConstants {
  uint8_t data[2][256][16];
  bool valid[2][256];
  bool any = false;
  // The pending load: its data is written by the caller after 83813580 returns.
  uint32_t pending_ptr = 0, pending_bank = 0, pending_start = 0, pending_count = 0;
};
InlineConstants g_inline_consts;

void FlushInlineConstants() {
  auto& c = g_inline_consts;
  if (!c.pending_ptr) return;
  const uint8_t* src = REX_KERNEL_MEMORY()->TranslateVirtual(c.pending_ptr);
  for (uint32_t i = 0; i < c.pending_count && c.pending_start + i < 256; i++) {
    std::memcpy(c.data[c.pending_bank][c.pending_start + i], src + 16 * i, 16);
    c.valid[c.pending_bank][c.pending_start + i] = true;
  }
  c.any = true;
  c.pending_ptr = 0;
}
FramePacket g_packet;
std::atomic<uint64_t> g_frame{0};
int g_probe_logged = 0;

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }

const uint8_t* Guest(uint32_t addr) { return REX_KERNEL_MEMORY()->TranslateVirtual(addr); }

std::string Words(const uint8_t* p, int n) {
  std::string s;
  for (int i = 0; i < n; i++) s += fmt::format("{:08X} ", be32(p + 4 * i));
  return s;
}

bool Probing() {
  int32_t at = REXCVAR_GET(gtav_native_layout_probe);
  return at > 0 && g_frame.load(std::memory_order_relaxed) == uint64_t(at) && g_probe_logged < kProbeDraws;
}

void ProbeDraw(const char* kind, uint32_t device) {
  g_probe_logged++;
  const uint8_t* dev = Guest(device);
  REXLOG_INFO("[native-probe] {} ib={:08X} vs={:08X}/{:016X} ps={:08X}/{:016X}", kind, g_bound.index_buffer,
              g_bound.vertex_shader, ShaderHash(g_bound.vertex_shader), g_bound.pixel_shader,
              ShaderHash(g_bound.pixel_shader));
  for (int i = 0; i < 4; i++)
    if (uint32_t rt = be32(dev + d3d::kDevRenderTargets + 4 * i))
      REXLOG_INFO("[native-probe]   rt{} {:08X}: {}", i, rt, Words(Guest(rt), 12));
  if (uint32_t ds = be32(dev + d3d::kDevDepthStencil))
    REXLOG_INFO("[native-probe]   ds {:08X}: {}", ds, Words(Guest(ds), 12));
  REXLOG_INFO("[native-probe]   viewport: {}", Words(dev + d3d::kDevViewport, 6));
}

// Guest D3D resources: a 24-byte header (Common, RefCount, Fence, ReadFence, Identifier,
// BaseFlush), then the type-specific part.
constexpr uint32_t kResourceHeader = 24;

}  // namespace

void OnSetIndices(uint32_t, uint32_t index_buffer) { g_bound.index_buffer = index_buffer; }

void OnSetStreamSource(uint32_t, uint32_t stream, uint32_t vb, uint32_t offset, uint32_t stride) {
  if (stream < kStreamCount) {
    g_bound.streams[stream][0] = vb;
    g_bound.streams[stream][1] = offset;
    g_bound.streams[stream][2] = stride;
  }
}

void OnInlineConstants(uint32_t bank, uint32_t start, uint32_t count, uint32_t data) {
  FlushInlineConstants();
  if (bank > 1 || !data) return;
  auto& c = g_inline_consts;
  c.pending_ptr = data;
  c.pending_bank = bank;
  c.pending_start = start;
  c.pending_count = count;
}

void OnSetConstants(uint32_t bank, uint32_t start, uint32_t count) {
  FlushInlineConstants();
  auto& c = g_inline_consts;
  if (!c.any || bank > 1) return;
  for (uint32_t i = start; i < start + count && i < 256; i++) c.valid[bank][i] = false;
}

void OnFlushConstants(uint32_t bank, uint64_t dirty_mask) {
  FlushInlineConstants();
  auto& c = g_inline_consts;
  if (!c.any || bank > 1) return;
  for (uint32_t group = 0; group < 64; group++)
    if (dirty_mask & (1ull << (63 - group)))
      for (uint32_t r = 4 * group; r < 4 * group + 4; r++) c.valid[bank][r] = false;
}

void OnSetVertexShader(uint32_t, uint32_t shader) { g_bound.vertex_shader = shader; }
void OnSetPixelShader(uint32_t, uint32_t shader) { g_bound.pixel_shader = shader; }

// Resource headers store the CPU virtual address; the 0xE0000000 range maps to physical memory
// with a 0x1000 offset, so masking off the top bits would point one page early.
static void FlushIndexedVertices();

static uint32_t GuestPhysical(uint32_t virtual_address) {
  uint32_t physical = REX_KERNEL_MEMORY()->GetPhysicalAddress(virtual_address);
  return physical != UINT32_MAX ? physical : virtual_address & 0x1FFFFFFF;
}

static DrawCommand* RecordDraw(uint32_t device, bool indexed, uint32_t primitive, int32_t base_vertex,
                               uint32_t start, uint32_t count) {
  FlushIndexedVertices();
  if (!Active()) return nullptr;
  const uint8_t* dev = Guest(device);
  DrawCommand d;
  d.indexed = indexed;
  d.primitive = primitive;
  d.base_vertex = base_vertex;
  d.start = start;
  d.count = count;
  d.vs_hash = ShaderHash(g_bound.vertex_shader);
  d.ps_hash = ShaderHash(g_bound.pixel_shader);
  d.ps_ucode_hash = ShaderUcodeHash(g_bound.pixel_shader);
  d.regs = g_packet.Push(dev + kRegShadowBegin, kRegShadowSize);
  {
    // RB_MODECONTROL in the device shadow lags: D3D derives it while flushing the draw, after this
    // snapshot. GTA's light volumes follow a depth-only stencil pass and still read 5 (depth only)
    // although they write color (mask 0007, a lighting pixel shader, additive blend), so the
    // lights were dropped. Depth-only draws always have mask 0 and no pixel shader.
    uint8_t* shadow = g_packet.arena.data() + d.regs;
    if ((xenos_regs::Read(shadow, xenos_regs::RB_MODECONTROL) & 7) == 5 && d.ps_hash &&
        (xenos_regs::Read(shadow, xenos_regs::RB_COLOR_MASK) & 0xFFFF) != 0) {
      uint32_t mode = xenos_regs::Read(shadow, xenos_regs::RB_MODECONTROL);
      xenos_regs::Write(shadow, xenos_regs::RB_MODECONTROL, (mode & ~7u) | 4);
    }
  }
  d.fetch = g_packet.Push(dev + d3d::kDevFetchConstants, kFetchSize);
  FlushInlineConstants();
  d.vs_consts = g_packet.Push(dev + d3d::kDevVsConstants, kAluSize);
  d.ps_consts = g_packet.Push(dev + d3d::kDevPsConstants, kAluSize);
  if (g_inline_consts.any) {
    for (uint32_t bank = 0; bank < 2; bank++) {
      uint8_t* dst = g_packet.arena.data() + (bank ? d.ps_consts : d.vs_consts);
      for (uint32_t i = 0; i < 256; i++)
        if (g_inline_consts.valid[bank][i]) std::memcpy(dst + 16 * i, g_inline_consts.data[bank][i], 16);
    }
  }
  // The device's own field, as DrawIndexedVertices reads it: SetIndices (83806E70) is not the
  // only writer, so the hooked value can be stale.
  uint32_t index_buffer = be32(dev + d3d::kDevIndexBuffer);
  if (!index_buffer) index_buffer = g_bound.index_buffer;
  if (indexed && index_buffer) {
    const uint8_t* ib = Guest(index_buffer);
    d.index32 = (be32(ib) & d3d::kIndexBuffer32Flag) != 0;
    d.index_address = GuestPhysical(be32(ib + kResourceHeader));
    d.index_size = be32(ib + kResourceHeader + 4);
  }
  if (uint32_t decl = be32(dev + d3d::kDevVertexDeclaration)) {
    const uint8_t* p = Guest(decl);
    uint32_t n = std::min(be32(p + d3d::kDeclCount), kMaxVertexElements);
    for (uint32_t i = 0; i < n; i++) {
      const uint8_t* e = p + d3d::kDeclElements + 12 * i;
      d.elements[i] = {be16(e), be16(e + 2), be32(e + 4), e[8], e[9], e[10], 0};
    }
    d.element_count = n;
  }
  for (uint32_t s = 0; s < kStreamCount; s++) {
    if (uint32_t vb = g_bound.streams[s][0]) {
      const uint8_t* v = Guest(vb);
      // Vertex buffer: its fetch constant (address | type 3, endian | size in dwords).
      d.streams[s].address = GuestPhysical(be32(v + kResourceHeader) & 0xFFFFFFFC);
      // Fetch constant dword 1: endian in bits 0-1, size in dwords in bits 2-25.
      uint32_t word1 = be32(v + kResourceHeader + 4);
      d.streams[s].size = ((word1 >> 2) & 0x00FFFFFF) * 4;
      d.streams[s].endian = word1 & 3;
      d.streams[s].offset = g_bound.streams[s][1];
      d.streams[s].stride = g_bound.streams[s][2];
    }
  }
  for (int i = 0; i < 4; i++) d.render_targets[i] = be32(dev + d3d::kDevRenderTargets + 4 * i);
  d.depth_stencil = be32(dev + d3d::kDevDepthStencil);
  g_packet.commands.push_back({CommandKind::kDraw, uint32_t(g_packet.draws.size())});
  g_packet.draws.push_back(d);
  return &g_packet.draws.back();
}

// BeginVertices(dev, primitive, vertex count, stride) returns where the CPU writes the vertices
// (inside the command buffer); EndVertices(dev) closes the draw, so the data is copied there.
static int64_t g_inline_draw = -1;
static uint32_t g_inline_ptr = 0, g_inline_bytes = 0;

void OnBeginVertices(uint32_t device, uint32_t primitive, uint32_t count, uint32_t stride, uint32_t data) {
  g_inline_draw = -1;
  DrawCommand* d = RecordDraw(device, false, primitive, 0, 0, count);
  if (!d || !data) return;
  d->is_inline = true;
  d->streams[0] = {0, count * stride, 0, stride, 2};
  g_inline_draw = int64_t(g_packet.draws.size() - 1);
  g_inline_ptr = data;
  g_inline_bytes = count * stride;
}

void OnEndVertices() {
  if (g_inline_draw < 0 || size_t(g_inline_draw) >= g_packet.draws.size()) return;
  uint32_t at = g_packet.Push(Guest(g_inline_ptr), g_inline_bytes);
  auto& d = g_packet.draws[size_t(g_inline_draw)];
  d.inline_vertices = at;
  d.inline_size = g_inline_bytes;
  g_inline_draw = -1;
}

// BeginIndexedVertices(dev, primitive, base vertex, vertex count, index count, index format,
// stride, &indices, &vertices) hands the CPU two buffers in GPU ring memory and needs no End call:
// the data is complete by the next D3D call, so it is copied then (FlushIndexedVertices).
static int64_t g_up_draw = -1;
static uint32_t g_up_vertices = 0, g_up_vertex_bytes = 0, g_up_indices = 0, g_up_index_bytes = 0;

static void FlushIndexedVertices() {
  if (g_up_draw < 0) return;
  int64_t draw = g_up_draw;
  g_up_draw = -1;
  if (size_t(draw) >= g_packet.draws.size()) return;
  uint32_t vertices = g_packet.Push(Guest(g_up_vertices), g_up_vertex_bytes);
  uint32_t indices = g_packet.Push(Guest(g_up_indices), g_up_index_bytes);
  auto& d = g_packet.draws[size_t(draw)];
  d.inline_vertices = vertices;
  d.inline_size = g_up_vertex_bytes;
  d.inline_indices = indices;
  d.inline_index_size = g_up_index_bytes;
}

void OnBeginIndexedVertices(uint32_t device, uint32_t primitive, int32_t base_vertex, uint32_t vertex_count,
                            uint32_t index_count, uint32_t index_format, uint32_t stride) {
  FlushIndexedVertices();
  const uint8_t* dev = Guest(device);
  uint32_t vertices = be32(dev + d3d::kDevUpVertices), indices = be32(dev + d3d::kDevUpIndices);
  DrawCommand* d = RecordDraw(device, true, primitive, base_vertex, 0, index_count);
  if (!d || !vertices || !indices) return;
  d->is_inline = true;
  d->index32 = (index_format & 4) != 0;
  d->index_address = 0;
  d->index_size = 0;
  d->streams[0] = {0, vertex_count * stride, 0, stride, 2};
  g_up_draw = int64_t(g_packet.draws.size() - 1);
  g_up_vertices = vertices;
  g_up_vertex_bytes = vertex_count * stride;
  g_up_indices = indices;
  g_up_index_bytes = index_count * (d->index32 ? 4 : 2);
}

void OnDrawIndexed(uint8_t*, uint32_t device, uint32_t primitive, uint32_t base_vertex, uint32_t start,
                   uint32_t count) {
  if (Probing()) ProbeDraw("drawidx", device);
  RecordDraw(device, true, primitive, int32_t(base_vertex), start, count);
}

void OnDraw(uint8_t*, uint32_t device, uint32_t primitive, uint32_t start, uint32_t count) {
  if (Probing()) ProbeDraw("draw", device);
  RecordDraw(device, false, primitive, 0, start, count);
}

// Diagnostic: which shadowed registers the D3D runtime changes inside its draw calls (state it
// flushes lazily there; a snapshot taken before the call misses it).
std::vector<uint8_t> g_probe_pre;
std::unordered_map<uint32_t, uint32_t> g_probe_changes;
uint32_t g_probe_draws = 0, g_probe_reports = 0;

void ProbeDrawBefore(uint32_t device) {
  if (g_probe_reports >= 6) return;
  const uint8_t* shadow = Guest(device) + kRegShadowBegin;
  g_probe_pre.assign(shadow, shadow + kRegShadowSize);
}

void ProbeDrawAfter(uint32_t device) {
  if (g_probe_reports >= 6 || g_probe_pre.size() != kRegShadowSize) return;
  const uint8_t* shadow = Guest(device) + kRegShadowBegin;
  for (const auto& b : xenos_regs::kBlocks)
    for (uint32_t i = 0; i < b.count; i++) {
      uint32_t off = b.device_offset - kRegShadowBegin + 4 * i;
      if (off + 4 <= kRegShadowSize && std::memcmp(shadow + off, g_probe_pre.data() + off, 4) != 0)
        g_probe_changes[b.first_reg + i]++;
    }
  if (++g_probe_draws % 2000 == 0) {
    g_probe_reports++;
    std::vector<std::pair<uint32_t, uint32_t>> v(g_probe_changes.begin(), g_probe_changes.end());
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
    std::string line;
    for (size_t k = 0; k < std::min<size_t>(v.size(), 16); k++) line += fmt::format(" {:04X}x{}", v[k].first, v[k].second);
    REXLOG_INFO("[native] registers changed inside D3D draw calls (per 2000 draws):{}", line);
    g_probe_changes.clear();
  }
}

uint32_t g_clear_surface_before = 0, g_clear_depth_before = 0, g_clear_logged = 0;

void OnClearBegin(uint32_t device) {
  FlushIndexedVertices();
  const uint8_t* shadow = Guest(device) + kRegShadowBegin;
  g_clear_surface_before = xenos_regs::Read(shadow, xenos_regs::RB_SURFACE_INFO);
  g_clear_depth_before = xenos_regs::Read(shadow, xenos_regs::RB_DEPTH_INFO);
}

void OnClear(uint32_t device, uint32_t flags, uint32_t color, float z, uint32_t stencil) {
  if (!Active()) return;
  {
    const uint8_t* shadow = Guest(device) + kRegShadowBegin;
    uint32_t surface = xenos_regs::Read(shadow, xenos_regs::RB_SURFACE_INFO);
    uint32_t depth = xenos_regs::Read(shadow, xenos_regs::RB_DEPTH_INFO);
    if ((surface != g_clear_surface_before || depth != g_clear_depth_before) && g_clear_logged < 40) {
      g_clear_logged++;
      REXLOG_INFO("[native] clear flags {:X}: surface info {:08X} -> {:08X}, depth info {:08X} -> {:08X} across D3D Clear",
                  flags, g_clear_surface_before, surface, g_clear_depth_before, depth);
    }
  }
  ClearCommand c{flags, color, stencil, z, g_packet.Push(Guest(device) + kRegShadowBegin, kRegShadowSize)};
  g_packet.commands.push_back({CommandKind::kClear, uint32_t(g_packet.clears.size())});
  g_packet.clears.push_back(c);
}

void OnResolve(uint32_t device, uint32_t flags, uint32_t rect, uint32_t dest_texture, uint32_t dest_point,
               uint32_t clear_color, float clear_z, uint32_t level, uint32_t face) {
  FlushIndexedVertices();
  if (!Active()) return;
  ResolveCommand r;
  r.flags = flags;
  r.dest_texture = dest_texture;
  r.dest_level = level;
  r.dest_face = face;
  r.regs = g_packet.Push(Guest(device) + kRegShadowBegin, kRegShadowSize);
  if (rect) {
    const uint8_t* p = Guest(rect);
    for (int i = 0; i < 4; i++) r.source_rect[i] = be32(p + 4 * i);
    r.has_rect = true;
  }
  r.clear_z = clear_z;
  if (clear_color) {
    const uint8_t* p = Guest(clear_color);
    for (int i = 0; i < 4; i++) {
      uint32_t v = be32(p + 4 * i);
      std::memcpy(&r.clear_color[i], &v, 4);
    }
  }
  if (dest_point) {
    const uint8_t* p = Guest(dest_point);
    r.dest_point[0] = be32(p);
    r.dest_point[1] = be32(p + 4);
    r.has_point = true;
  }
  if (dest_texture) {
    const uint8_t* t = Guest(dest_texture);
    for (int i = 0; i < 13; i++) r.dest_words[i] = be32(t + 4 * i);
    // The texture object stores a virtual base address; the fetch constants the GPU samples with
    // carry the physical one (the 0xE0000000 range maps with a 0x1000 offset). Use the physical
    // address so resolves and later texture fetches agree.
    uint32_t& dword1 = r.dest_words[8];
    uint32_t physical = REX_KERNEL_MEMORY()->GetPhysicalAddress(dword1 & 0xFFFFF000);
    if (physical != UINT32_MAX) dword1 = (dword1 & 0xFFF) | (physical & 0xFFFFF000);
  }
  g_packet.commands.push_back({CommandKind::kResolve, uint32_t(g_packet.resolves.size())});
  g_packet.resolves.push_back(r);
}

void OnCaptureSwap(uint32_t front_buffer) {
  FlushIndexedVertices();
  uint64_t frame = g_frame.fetch_add(1, std::memory_order_relaxed);
  if (!Active()) return;
  g_packet.index = frame;
  g_packet.front_buffer = front_buffer;
  Submit(std::move(g_packet));
  g_packet = TakeSpare();
}

uint64_t GuestFrame() { return g_frame.load(std::memory_order_relaxed); }

}  // namespace gtav::native
