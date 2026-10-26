# GTA V TU27: XDK D3D runtime map (native renderer milestone 1)

How the addresses were found:
- **Fingerprints.** Fuzzy matches against Skate 3's named D3D functions (`tools/native/fnsim.py`, using caches built by `tools/native/genasm.py`). The two games link nearly the same XDK D3D library; most functions score 0.9 to 1.0.
- **Call probe.** The runtime probe (`gtav/src/native/d3d_probe.cpp`, `--gtav_d3d_probe=true`, writes `logs/d3d_probe.csv`) counts calls to all 2,896 functions in 0x837F0000-0x83840000.
- **Static reads.** The flusher call sites were read by hand.

The D3D device is at **0x4004D500**: r3 of every device method.

## Confirmed functions
| addr | function | evidence |
|---|---|---|
| 838146B8 | D3DDevice_Swap | Skate 0.61; calls VdSwap; 1 call per frame |
| 83811030 | DrawIndexedVertices | Skate 0.91; about 675 per frame in 3D |
| 83810C18 | DrawVertices | Skate 0.91 |
| 83810218 | BeginVertices (inline vertex ring) | Skate 0.86 |
| 838106D8 | draw sibling (unnamed) | about 137 per frame |
| 83806E70 | SetIndices | Skate 1.00 |
| 83806D50 | SetStreamSource | Skate 0.77 |
| 83812190 / 83812398 | SetPixelShader / SetVertexShader | Skate 1.00; Skate's labels were swapped, the bound containers' type flags decide |
| 83818698 | SetPending_AluConstants | Skate 1.00 |
| 83818300 | SetPending registers (generic): (dev, mask64, reg_base, shadow*) | Skate 1.00, read by hand |
| 83818540 | SetPending_FetchConstants: mask64, shadow dev+0x480, reg 0x4800, 6 dwords each | read by hand |
| 838072C8 | SetScissorRect (83807530 is its caller/wrapper) | Skate 1.00 |

## Likely (from probe arguments; confirm when used)
| addr | guess | evidence |
|---|---|---|
| 838125B0 | SetVertexDeclaration: stores at dev+12240, sets a dirty bit | read by hand |
| 83806D00 / 83806F00 / 83806F48 | GetViewport / GetRenderTarget(i) / GetDepthStencilSurface | read by hand |
| 838045E0 | Resolve(dev, flags, src rect, dest texture, …); 72 per frame | probe + args |
| 8380BCD8 | Clear(dev, count, rects, flags, color, Z f1, stencil) | probe + args |
| 83811FC8 / 83811EF0 | Set{Vertex,Pixel}ShaderConstantF(start, data, count) | about 2,690 / 1,670 per frame; (16, ptr, 1) |
| 838045E0 → 83803488 | Resolve → its body | 83803488 writes RB_COPY_*; dest 0x40000780 = front buffer |
| 8380BCD8 | Clear(count, rects, flags=0xF, color, Z f1, stencil) | (0, 0, 0xF, 0xFF000000, f1 = 1.0) |
| 83805040-838061xx (about 50) | per-state SetRenderState_* (dispatch 8381BA5C, r8 = state offset) | |
| 838079F0 / 83806D00 | SetRenderTarget / SetViewport? | (…, 0x500, 0x2D0) = 1280x720 |
| 83807CC8 / 83807660 | SetTexture? | about 165 per frame, called from RAGE |
| 8380DC68 | SetViewport? | Skate 0.54, never called in the probe run |

## Device layout (D3D::CDevice)
| offset | contents |
|---|---|
| +0x30 / +0x34 | PM4 command-buffer write pointer / limit |
| +0x480 | fetch-constant shadow, 32 x 6 dwords (reg 0x4800) |
| +1920 | VS ALU constants, 256 float4 (reg 0x4000) |
| +6016 | PS ALU constants, 256 float4 (reg 0x4400) |
| +10112 | bool/loop constants (reg 0x4900) |
| +10368 | registers 0x2000.. (RB_SURFACE_INFO, RB_COLOR_INFO, RB_DEPTH_INFO, …) |
| +10444 | registers 0x2100.. |
| +10528 | registers 0x2180.. |
| +10548 | registers 0x2200.. (depth/blend/stencil controls) |
| +10596 | registers 0x2280.. |
| +10680 | registers 0x2300.. (RB copy/resolve) |
| +10832 | registers 0x2380.. |

From the call sites of 83818300 in 83803228. Each block is a dense run of dwords, one per register, so register `reg` in a block lives at `dev + offset + 4 * (reg - base)`.

## Baseline (M0, emulated renderer, logs/stats0.log and probe0.log)
- 30.0 fps steady: the game's own cap. Guest time blocked in Swap is about 0.01-0.03 ms per frame, so the emulated GPU keeps up with the cap.
- Spikes: worst frames of 120-220 ms during boot and scene changes (pipeline creation). This matches the user's "small pauses at cutscenes".
- Per frame in 3D: about 675 indexed draws, 80 draws, 125 BeginVertices, 345 VS / 359 PS binds, 1,920 render-state flushes, 58 resolves.
- So the native renderer's measurable win is removing stalls (precompiled shaders and PSOs, no EDRAM transfers) and lower GPU power. More fps means lifting the 30 fps cap separately.
