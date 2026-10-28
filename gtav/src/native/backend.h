// gtav - native renderer backend: replays a recorded guest frame through Plume.
//
// Render-thread only. Owns the bindless descriptor heaps, the pipeline layout the XenosRecomp
// shaders expect (texture heaps in sets 0-2, samplers in set 3, three buffer addresses as push
// constants), and the caches keyed by guest state: shaders, pipelines, render targets (by EDRAM
// base/format/pitch), resolve targets (by guest address), textures, samplers and vertex/index
// buffers.

#pragma once

#include <plume_render_interface.h>

#include <cstdint>
#include <memory>

namespace gtav::native {

struct FramePacket;

// Tree-orb diagnostic (backend.cpp DiagFor): F11 cycles the mode, logs/diag_mode sets it.
void CycleDiagMode();
void SetDiagMode(uint32_t mode);

class Backend {
 public:
  Backend();
  ~Backend();
  bool Init(plume::RenderDevice* device);

  // Records the frame into `list`. Returns the texture holding the guest front buffer (in
  // SHADER_READ layout) and its descriptor index, or nullptr when the frame has none yet.
  plume::RenderTexture* Execute(const FramePacket& frame, plume::RenderCommandList* list,
                                uint32_t* front_descriptor);

  // Frames in flight: the renderer records frame N+1 while the GPU runs frame N. Everything a
  // frame leaves for after its fence (uploads, deferred frees, resolve writebacks) is kept per
  // slot: BeginFrame(slot) before Execute, EndFrame(slot) once the command list is recorded, and
  // FrameComplete(slot) after that slot's fence passed (and before its next BeginFrame).
  static constexpr uint32_t kFramesInFlight = 2;
  void BeginFrame(uint32_t slot);
  void EndFrame(uint32_t slot);
  void FrameComplete(uint32_t slot);

  // A frame dump is being recorded (the renderer waits for that frame synchronously).
  bool Dumping() const;
  // Dump the next executed frame (F9 in the native window), like --gtav_native_dump_frame.
  void RequestDump();

  // Counters of the last executed frame, for the hitch log.
  struct FrameStats {
    uint32_t draws, skipped, pipelines_created, textures;
    double shader_ms, pipeline_ms, texture_ms, resolve_ms;  // time in cache misses / resolves
  };
  FrameStats LastStats() const;

  // Frame dump (--gtav_native_dump_frame=N): every resolve of guest frame N is captured right
  // after it executes; WriteDump saves them as PNGs once the frame's fence passed.
  void WriteDump();

  // Draws `descriptor` (a 2D texture in the heap) over the bound framebuffer.
  void Blit(plume::RenderCommandList* list, uint32_t descriptor, plume::RenderFormat target_format);

  // Draws `value` as an on-screen counter with its top-left corner at (x, y).
  void DrawCounter(plume::RenderCommandList* list, uint32_t value, int32_t x, int32_t y, uint32_t scale,
                   plume::RenderFormat target_format);

  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace gtav::native
