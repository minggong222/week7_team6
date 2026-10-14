#define AllocatePagesThreadLength 64
#define ZClusterCountPerPhysicalPage 8
#define Log2OfZClusterCountPerPhysicalPage 3
#define IndirectToLightClustersAllocator 3
#define LightClusterCount 4
#define Pass3_DispatchArgX 0
#define Pass3_DispatchArgY 1
#define Pass3_DispatchArgZ 2
cbuffer LightClusterGlobals : register(b2)
{
    uint ClusterCountX;
    uint ClusterCountY;
    uint ClusterCountZ;
    float ClusterZScale;
    float ClusterZBias;
    float3 ClusterGlobalsPadding;
};
struct FLightCluster
{
    uint Offset;
    uint Count;
    uint ClusterKey;
};
RWStructuredBuffer<uint> LightIndices : register(u0);
RWStructuredBuffer<FLightCluster> LightClusters : register(u1);
RWStructuredBuffer<uint> IndirectToLightClusters : register(u2);
RWStructuredBuffer<uint> LightClusterPageTable : register(u3);
groupshared uint IndirectBase[AllocatePagesThreadLength];
groupshared uint ReservationCountThenBase[AllocatePagesThreadLength];
[numthreads(AllocatePagesThreadLength, 1, ZClusterCountPerPhysicalPage)]
void mainCS(uint3 DispatchThreadID : SV_DispatchThreadID, uint3 GroupThreadID : SV_GroupThreadID)
{
    uint LocalPage = GroupThreadID.x;
    if (GroupThreadID.z == 0)
    {
        IndirectBase[LocalPage] = 0xffffffffu;
        ReservationCountThenBase[LocalPage] = 0;
    }
    GroupMemoryBarrierWithGroupSync();
    uint PageIndex = DispatchThreadID.x;
    uint PageCount = ClusterCountX * ClusterCountY * ((ClusterCountZ + ZClusterCountPerPhysicalPage - 1) / ZClusterCountPerPhysicalPage);
    uint ReservedIndex = 0;
    bool bActive = false;
    if (PageIndex < PageCount)
        bActive = (LightClusterPageTable[PageIndex] & (1u << GroupThreadID.z)) != 0;
    if (bActive)
        InterlockedAdd(ReservationCountThenBase[LocalPage], 1u, ReservedIndex);
    GroupMemoryBarrierWithGroupSync();
    if (GroupThreadID.z == 0 && PageIndex < PageCount)
    {
        if (ReservationCountThenBase[LocalPage] != 0)
        {
            uint LocalCount = ReservationCountThenBase[LocalPage];
            InterlockedAdd(LightIndices[IndirectToLightClustersAllocator], ZClusterCountPerPhysicalPage, IndirectBase[LocalPage]);
            InterlockedAdd(LightIndices[LightClusterCount], LocalCount, ReservationCountThenBase[LocalPage]);
            LightClusterPageTable[PageIndex] = IndirectBase[LocalPage];
            uint CountAfter = ReservationCountThenBase[LocalPage] + LocalCount;
            InterlockedMax(LightIndices[Pass3_DispatchArgX], min(CountAfter, 65535u));
            InterlockedMax(LightIndices[Pass3_DispatchArgY], (CountAfter + 65534u) / 65535u);
            InterlockedMax(LightIndices[Pass3_DispatchArgZ], 1u);
        }
        else
            LightClusterPageTable[PageIndex] = 0xffffffffu;
    }
    GroupMemoryBarrierWithGroupSync();
    if (bActive)
    {
        uint ClusterIndex = ReservationCountThenBase[LocalPage] + ReservedIndex;
        IndirectToLightClusters[IndirectBase[LocalPage] + GroupThreadID.z] = ClusterIndex;
        LightClusters[ClusterIndex].ClusterKey = (PageIndex << Log2OfZClusterCountPerPhysicalPage) | GroupThreadID.z;
    }
    else if (IndirectBase[LocalPage] != 0xffffffffu)
    {
        IndirectToLightClusters[IndirectBase[LocalPage] + GroupThreadID.z] = 0xffffffffu;
    }
}
