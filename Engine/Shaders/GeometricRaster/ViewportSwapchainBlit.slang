/*============================================================================================================================================
                                                      VIEWPORTSWAPCHAINBLIT.SLANG
============================================================================================================================================*/
// 🧩 Fullscreen single-target GPU viewport blit shader presenting rendered game viewports onto OS VkSwapchainKHR or offscreen images

struct BlitVertexOutput
{
    float4 ClipPosition : SV_Position;
    float2 SampleCoord  : TEXCOORD0;
};

[[vk::binding(0, 0)]] Sampler2D SourceViewportTexture;

[shader("vertex")]
BlitVertexOutput VertexEntry(uint VertexOrdinal : SV_VulkanVertexID)
{
    BlitVertexOutput Output;
    float2 Coord        = float2(float((VertexOrdinal << 1u) & 2u), float(VertexOrdinal & 2u));
    Output.ClipPosition = float4(Coord * 2.0 - 1.0, 0.0, 1.0);
    Output.SampleCoord  = Coord;
    return Output;
}

[shader("fragment")]
float4 FragmentEntry(BlitVertexOutput Input) : SV_Target0
{
    float4 SampledColor = SourceViewportTexture.Sample(Input.SampleCoord);
    return float4(SampledColor.rgb, 1.0);
}
