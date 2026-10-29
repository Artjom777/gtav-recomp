// gtav - native renderer: settings menu (Dear ImGui draw lists). Uses the backend's pipeline
// layout: texture heap in set 0, samplers in set 3. Vertices are read from the upload ring by
// device address (ImDrawVert: float2 pos, float2 uv, RGBA8 colour = 20 bytes); the bound index
// buffer supplies SV_VertexID.

struct PushConstants {
    uint64_t Vertices;
    uint TextureIndex;
    uint SamplerIndex;
    float2 InvHalfSize;  // 2 / display size
};

[[vk::push_constant]] ConstantBuffer<PushConstants> g_Push;
Texture2D<float4> g_Textures[] : register(t0, space0);
SamplerState g_Samplers[] : register(s0, space3);

void VSMenu(uint id : SV_VertexID, out float4 position : SV_Position, out float2 uv : TEXCOORD0,
            out float4 color : COLOR0) {
    uint64_t v = g_Push.Vertices + uint64_t(id) * 20;
    float2 pos = vk::RawBufferLoad<float2>(v, 4);
    uv = vk::RawBufferLoad<float2>(v + 8, 4);
    uint c = vk::RawBufferLoad<uint>(v + 16, 4);
    color = float4(c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF, c >> 24) / 255.0;
    position = float4(pos * g_Push.InvHalfSize - 1.0, 0.0, 1.0);  // Vulkan: y = -1 is the top
}

float4 PSMenu(float4 position : SV_Position, float2 uv : TEXCOORD0, float4 color : COLOR0) : SV_Target {
    return color * g_Textures[g_Push.TextureIndex].SampleLevel(g_Samplers[g_Push.SamplerIndex], uv, 0);
}
