// Times N render passes (one fullscreen draw each) in one Metal command buffer: the per-pass GPU cost
// floor of a tile GPU. clang++ -std=c++17 -O2 -fobjc-arc metal_pass_bench.mm -framework Metal -framework Foundation -o bench
// ./bench <passes> <w> <h> <untracked 0|1> <draws per pass> <mode: 0 chain, 1 independent targets, 2 same target>
// M1 (2026-10-30, game paused): ~55 us per dependent pass, ~48 us per independent one, at 64x64 or 720p alike.
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#include <cstdio>
#include <cstdlib>
static const char* kSrc = R"(
#include <metal_stdlib>
using namespace metal;
struct VO { float4 pos [[position]]; float2 uv; };
vertex VO vs(uint vid [[vertex_id]]) { VO o; float2 p = float2((vid << 1) & 2, vid & 2); o.pos = float4(p * 2 - 1, 0, 1); o.uv = p; return o; }
fragment half4 fs(VO in [[stage_in]], texture2d<half> t [[texture(0)]], sampler s [[sampler(0)]]) { return t.sample(s, in.uv) * 0.99h + 0.01h; }
)";
int main(int argc, char** argv) {
  int passes = argc > 1 ? atoi(argv[1]) : 150;
  int w = argc > 2 ? atoi(argv[2]) : 1280, h = argc > 3 ? atoi(argv[3]) : 720;
  bool untracked = argc > 4 && atoi(argv[4]);
  int drawsPerPass = argc > 5 ? atoi(argv[5]) : 1;
  int mode = argc > 6 ? atoi(argv[6]) : 0;  // 0 chain, 1 independent targets, 2 same target no sampling
  id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
  NSError* err = nil;
  id<MTLLibrary> lib = [dev newLibraryWithSource:[NSString stringWithUTF8String:kSrc] options:nil error:&err];
  if (!lib) { printf("lib: %s\n", err.description.UTF8String); return 1; }
  MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
  pd.vertexFunction = [lib newFunctionWithName:@"vs"];
  pd.fragmentFunction = [lib newFunctionWithName:@"fs"];
  pd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
  id<MTLRenderPipelineState> ps = [dev newRenderPipelineStateWithDescriptor:pd error:&err];
  MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float width:w height:h mipmapped:NO];
  td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
  td.storageMode = MTLStorageModePrivate;
  if (untracked) td.hazardTrackingMode = MTLHazardTrackingModeUntracked;
  id<MTLTexture> tex[2] = {[dev newTextureWithDescriptor:td], [dev newTextureWithDescriptor:td]};
  NSMutableArray* many = [NSMutableArray new]; for (int i = 0; i < passes; i++) [many addObject:[dev newTextureWithDescriptor:td]];
  id<MTLTexture> src = [dev newTextureWithDescriptor:td];
  id<MTLSamplerState> samp = [dev newSamplerStateWithDescriptor:[MTLSamplerDescriptor new]];
  id<MTLCommandQueue> q = [dev newCommandQueue];
  for (int iter = 0; iter < 6; iter++) {
    id<MTLCommandBuffer> cb = [q commandBuffer];
    for (int i = 0; i < passes; i++) {
      MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
      rp.colorAttachments[0].texture = mode == 1 ? many[i] : mode == 2 ? tex[0] : tex[i & 1];
      rp.colorAttachments[0].loadAction = MTLLoadActionLoad;
      rp.colorAttachments[0].storeAction = MTLStoreActionStore;
      id<MTLRenderCommandEncoder> e = [cb renderCommandEncoderWithDescriptor:rp];
      [e setRenderPipelineState:ps];
      [e setFragmentTexture:(mode ? src : tex[(i + 1) & 1]) atIndex:0];
      [e setFragmentSamplerState:samp atIndex:0];
      for (int d = 0; d < drawsPerPass; d++) [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
      [e endEncoding];
    }
    [cb commit];
    [cb waitUntilCompleted];
    if (iter > 0) printf("passes %d %dx%d untracked %d: GPU %.2f ms (%.1f us/pass)\n", passes, w, h, untracked,
                        (cb.GPUEndTime - cb.GPUStartTime) * 1000, (cb.GPUEndTime - cb.GPUStartTime) * 1e6 / passes);
  }
  return 0;
}
