#define MarkPagesThreadLength 16
#define PixelLengthPerTile 64
#define ZClusterCountPerPhysicalPage 8
#define Log2OfZClusterCountPerPhysicalPage 3
#define SharedPageCount 256
cbuffer ViewConstants : register(b0)
{
    row_major matrix ViewProjection;
    float3 CameraPosition;
    float ViewSizeX;
    float ViewSizeY;
    uint bIsPerspective;
    float2 Padding_noUse;
};
cbuffer LightClusterGlobals : register(b2)
{
    uint ClusterCountX;
    uint ClusterCountY;
    uint ClusterCountZ;
    float ClusterZScale;
    float ClusterZBias;
    uint ClusterDebugMode;
    uint DebugLightCountMax;
    uint bEnableLightCulling;
};
Texture2D<float> ViewDepthTexture : register(t0);
RWStructuredBuffer<uint> LightClusterPageTable : register(u3);
groupshared uint ZPages[SharedPageCount];
uint GetPageIndex(uint2 Pixel, uint PageZ)
{
    uint2 Tile = Pixel / PixelLengthPerTile;
    uint PageCountZ = (ClusterCountZ + ZClusterCountPerPhysicalPage - 1) / ZClusterCountPerPhysicalPage;
    return (Tile.x * ClusterCountY + Tile.y) * PageCountZ + PageZ;
}
[numthreads(MarkPagesThreadLength, MarkPagesThreadLength, 1)]
void mainCS(uint3 DispatchThreadID : SV_DispatchThreadID, uint GroupIndex : SV_GroupIndex)
{
    uint PageCountZ = (ClusterCountZ + ZClusterCountPerPhysicalPage - 1) / ZClusterCountPerPhysicalPage;
    uint LocalPageCount = min(PageCountZ, SharedPageCount);
    if (GroupIndex < LocalPageCount)
        ZPages[GroupIndex] = 0;
    GroupMemoryBarrierWithGroupSync();
    uint2 Pixel = DispatchThreadID.xy;
    if (Pixel.x < (uint) ViewSizeX && Pixel.y < (uint) ViewSizeY)
    {
        float ViewDepth = ViewDepthTexture.Load(int3(Pixel, 0));
        if (ViewDepth != asfloat(0x7F7FFFFF))
        {
            uint Z = (uint) clamp(log2(ViewDepth) * ClusterZScale + ClusterZBias, 0.0f, (float) (ClusterCountZ - 1));
            uint PageZ = Z >> Log2OfZClusterCountPerPhysicalPage;
            uint Mask = 1u << (Z & (ZClusterCountPerPhysicalPage - 1));
            if (PageZ < SharedPageCount)
                InterlockedOr(ZPages[PageZ], Mask);
            else
                InterlockedOr(LightClusterPageTable[GetPageIndex(Pixel, PageZ)], Mask);
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (GroupIndex < LocalPageCount && ZPages[GroupIndex] != 0)
        InterlockedOr(LightClusterPageTable[GetPageIndex(Pixel, GroupIndex)], ZPages[GroupIndex]);
}
