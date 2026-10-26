// gtav - guest D3D state tracking for the native renderer.
//
// The D3D hooks report bound objects (index buffer, shaders) and draws here. Everything else a
// draw needs is read from the device's register shadow (see d3d_map.h).

#pragma once

#include <cstdint>

namespace gtav::native {

void OnSetIndices(uint32_t device, uint32_t index_buffer);
void OnSetStreamSource(uint32_t device, uint32_t stream, uint32_t vb, uint32_t offset, uint32_t stride);
// 83813580(dev, bank, start, count) -> r3 = where the caller writes count float4 constants.
void OnInlineConstants(uint32_t bank, uint32_t start, uint32_t count, uint32_t data);
// Set{Vertex,Pixel}ShaderConstantF(dev, start, data, count): the shadow is current again.
void OnSetConstants(uint32_t bank, uint32_t start, uint32_t count);
// SetPending_AluConstants: the shadow registers in dirty_mask (4 per bit, MSB first) reach the GPU.
void OnFlushConstants(uint32_t bank, uint64_t dirty_mask);
void OnSetVertexShader(uint32_t device, uint32_t shader);
void OnSetPixelShader(uint32_t device, uint32_t shader);

// D3DDevice_DrawIndexedVertices / DrawVertices arguments (r4..r7 as the guest passed them).
void OnDrawIndexed(uint8_t* base, uint32_t device, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7);
void OnDraw(uint8_t* base, uint32_t device, uint32_t a4, uint32_t a5, uint32_t a6);


void OnBeginVertices(uint32_t device, uint32_t primitive, uint32_t count, uint32_t stride, uint32_t data);
void OnEndVertices();
void OnBeginIndexedVertices(uint32_t device, uint32_t primitive, int32_t base_vertex, uint32_t vertex_count,
                            uint32_t index_count, uint32_t index_format, uint32_t stride);
void OnClearBegin(uint32_t device);
void ProbeDrawBefore(uint32_t device);  // diagnostic: registers the D3D draw call flushes
void ProbeDrawAfter(uint32_t device);  // before the D3D call (inline vertices still pending)
void OnClear(uint32_t device, uint32_t flags, uint32_t color, float z, uint32_t stencil);
void OnResolve(uint32_t device, uint32_t flags, uint32_t rect, uint32_t dest_texture, uint32_t dest_point,
               uint32_t clear_color, float clear_z, uint32_t level, uint32_t face);

// Guest frame boundary (before the guest's Swap runs): hands the frame to the renderer.
void OnCaptureSwap(uint32_t front_buffer);
uint64_t GuestFrame();

}  // namespace gtav::native
