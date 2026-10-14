#define MaxChildCount 32
#define PixelLengthPerTile 64
#define ZClusterCountPerPhysicalPage 8
#define Log2OfZClusterCountPerPhysicalPage 3
#define LightClusterCount 4
#define LightIndicesAllocator 5
#define LightIndicesBase 6
cbuffer ViewConstants : register(b0)
{
    row_major matrix ViewProjection;
    float3 CameraPosition;
    float ViewSizeX;
    float ViewSizeY;
    uint bIsPerspective;
    float2 Padding_noUse;
};
cbuffer LightingGlobals : register(b1)
{
    float4 AmbientRadiance;
    uint LightCount;
    uint DirectionalLightCount;
    uint LightTreeRootIndex;
    uint LightingGlobalsPadding;
};
cbuffer LightClusterGlobals : register(b2)
{
    uint ClusterCountX;
    uint ClusterCountY;
    uint ClusterCountZ;
    float ClusterZScale;
    float ClusterZBias;
    float3 ClusterGlobalsPadding;
};

struct FLightTreeNode
{
    float3 MinOrCenter;
    float MaxXOrRadius;
    float MaxYOrDataIndex;
    float MaxZ;
    uint ChildIndex; // std::numeric_limits<uint32>::max(): invalid. 라이트가 0개일 때 루트에 설정
    uint ChildCount; // 0: leaf
};
StructuredBuffer<FLightTreeNode> LightTree : register(t1);

RWStructuredBuffer<uint> LightIndices : register(u0);
struct FLightCluster // StructuredBuffer
{
    uint Offset;
    uint Count;
    uint ClusterKey;
};
RWStructuredBuffer<FLightCluster> LightClusters : register(u1);

groupshared float4 ClusterPlanes[6];
void BuildClusterFrustum(uint ClusterKey)
{
    uint PageCountZ = (ClusterCountZ + ZClusterCountPerPhysicalPage - 1) / ZClusterCountPerPhysicalPage;
    uint PageIndex = ClusterKey >> Log2OfZClusterCountPerPhysicalPage;
    uint TileIndex = PageIndex / PageCountZ;
    uint2 Tile = uint2(TileIndex / ClusterCountY, TileIndex % ClusterCountY);
    uint Z = (PageIndex % PageCountZ) * ZClusterCountPerPhysicalPage + (ClusterKey & (ZClusterCountPerPhysicalPage - 1));
    float2 ViewSize = float2(ViewSizeX, ViewSizeY);
    float2 PixelMin = Tile * PixelLengthPerTile;
    float2 PixelMax = min(PixelMin + PixelLengthPerTile, ViewSize);
    float Left = PixelMin.x * 2.0f / ViewSize.x - 1.0f;
    float Right = PixelMax.x * 2.0f / ViewSize.x - 1.0f;
    float Top = 1.0f - PixelMin.y * 2.0f / ViewSize.y;
    float Bottom = 1.0f - PixelMax.y * 2.0f / ViewSize.y;
    float NearDepth = exp2(((float) Z - ClusterZBias) / ClusterZScale);
    float FarDepth = exp2(((float) Z + 1.0f - ClusterZBias) / ClusterZScale);
    float4x4 Columns = transpose(ViewProjection);
    ClusterPlanes[0] = Columns[0] - Left * Columns[3];
    ClusterPlanes[1] = Right * Columns[3] - Columns[0];
    ClusterPlanes[2] = Top * Columns[3] - Columns[1];
    ClusterPlanes[3] = Columns[1] - Bottom * Columns[3];
    ClusterPlanes[4] = Columns[3] - float4(0.0f, 0.0f, 0.0f, NearDepth);
    ClusterPlanes[5] = float4(0.0f, 0.0f, 0.0f, FarDepth) - Columns[3];
    [unroll] for (uint Index = 0; Index < 6; ++Index)
        ClusterPlanes[Index] *= rsqrt(dot(ClusterPlanes[Index].xyz, ClusterPlanes[Index].xyz));
}
bool Intersects(float3 Center, float Radius)
{
    [unroll] for (uint Index = 0; Index < 6; ++Index)
        if (dot(ClusterPlanes[Index].xyz, Center) + ClusterPlanes[Index].w < -Radius)
            return false;
    return true;
}
bool Intersects(float3 Min, float3 Max)
{
    float3 Center = (Min + Max) * 0.5f;
    float3 Extent = (Max - Min) * 0.5f;
    [unroll] for (uint Index = 0; Index < 6; ++Index)
        if (dot(ClusterPlanes[Index].xyz, Center) + ClusterPlanes[Index].w < -dot(abs(ClusterPlanes[Index].xyz), Extent))
            return false;
    return true;
}
groupshared uint ActiveClusterCount;
groupshared FLightTreeNode RootNode;
groupshared uint ReservedLightCount;
groupshared uint GlobalWriteBase;
groupshared uint LocalNextWritePosition;
groupshared bool bLeaf1;
groupshared bool bLeaf2;
groupshared bool bLeaf3;
groupshared bool bLeaf4;
struct ParentNode
{
    uint ChildIndex;
    uint ChildCount;
};
groupshared ParentNode nextNode1[MaxChildCount];
groupshared ParentNode nextNode2[MaxChildCount];
groupshared ParentNode nextNode3[MaxChildCount];
groupshared ParentNode nextNode4[MaxChildCount];


void Traversal(uint GroupIndex, FLightTreeNode Root, bool bCountOnly)
{
    bool bParticipate = GroupIndex < Root.ChildCount;
    FLightTreeNode Depth = (FLightTreeNode) 0;
    if (bParticipate)
        Depth = LightTree[Root.ChildIndex + GroupIndex];
    nextNode1[GroupIndex].ChildCount = 0;
    nextNode1[GroupIndex].ChildIndex = 0xffffffffu;
    
    if (bParticipate)
    {
        if (Depth.ChildCount == 0)
        {
            if (GroupIndex == 0) bLeaf1 = true;
            if (Intersects(Depth.MinOrCenter, Depth.MaxXOrRadius))
            {
                if (bCountOnly)
                    InterlockedAdd(ReservedLightCount, 1);
                else
                {
                    uint WritePosition;
                    InterlockedAdd(LocalNextWritePosition, 1, WritePosition);
                    LightIndices[LightIndicesBase + GlobalWriteBase + WritePosition] = asuint(Depth.MaxYOrDataIndex);
                }
            }
        }
        else
        {
            if (GroupIndex == 0) bLeaf1 = false;
            float3 max = { Depth.MaxXOrRadius, Depth.MaxYOrDataIndex, Depth.MaxZ };
            if (Intersects(Depth.MinOrCenter, max))
            {
                nextNode1[GroupIndex].ChildCount = Depth.ChildCount;
                nextNode1[GroupIndex].ChildIndex = Depth.ChildIndex;
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();
    
    if (!bLeaf1)
        [loop] for (int a = 0; a < MaxChildCount; ++a)
        {
            if (nextNode1[a].ChildIndex == 0xffffffff)
                continue;
        
            bParticipate = GroupIndex < nextNode1[a].ChildCount;
            if (bParticipate)
                Depth = LightTree[nextNode1[a].ChildIndex + GroupIndex];
            nextNode2[GroupIndex].ChildCount = 0;
            nextNode2[GroupIndex].ChildIndex = 0xffffffffu;
        
            if (bParticipate)
            {
                if (Depth.ChildCount == 0)
                {
                    if (GroupIndex == 0) bLeaf2 = true;
                    if (Intersects(Depth.MinOrCenter, Depth.MaxXOrRadius))
                    {
                        if (bCountOnly)
                            InterlockedAdd(ReservedLightCount, 1);
                        else
                        {
                            uint WritePosition;
                            InterlockedAdd(LocalNextWritePosition, 1, WritePosition);
                            LightIndices[LightIndicesBase + GlobalWriteBase + WritePosition] = asuint(Depth.MaxYOrDataIndex);
                        }
                    }
                }
                else
                {
                    if (GroupIndex == 0) bLeaf2 = false;
                    float3 max = { Depth.MaxXOrRadius, Depth.MaxYOrDataIndex, Depth.MaxZ };
                    if (Intersects(Depth.MinOrCenter, max))
                    {
                        nextNode2[GroupIndex].ChildCount = Depth.ChildCount;
                        nextNode2[GroupIndex].ChildIndex = Depth.ChildIndex;
                    }
                }
            }
            GroupMemoryBarrierWithGroupSync();
            
            if (!bLeaf2)
                [loop] for (int b = 0; b < MaxChildCount; ++b)
                {
                    if (nextNode2[b].ChildIndex == 0xffffffff)
                        continue;
                
                    bParticipate = GroupIndex < nextNode2[b].ChildCount;
                    if (bParticipate)
                        Depth = LightTree[nextNode2[b].ChildIndex + GroupIndex];
                    nextNode3[GroupIndex].ChildCount = 0;
                    nextNode3[GroupIndex].ChildIndex = 0xffffffffu;
                
                    if (bParticipate)
                    {
                        if (Depth.ChildCount == 0)
                        {
                            if (GroupIndex == 0) bLeaf3 = true;
                            if (Intersects(Depth.MinOrCenter, Depth.MaxXOrRadius))
                            {
                                if (bCountOnly)
                                    InterlockedAdd(ReservedLightCount, 1);
                                else
                                {
                                    uint WritePosition;
                                    InterlockedAdd(LocalNextWritePosition, 1, WritePosition);
                                    LightIndices[LightIndicesBase + GlobalWriteBase + WritePosition] = asuint(Depth.MaxYOrDataIndex);
                                }
                            }
                        }
                        else
                        {
                            if (GroupIndex == 0) bLeaf3 = false;
                            float3 max = { Depth.MaxXOrRadius, Depth.MaxYOrDataIndex, Depth.MaxZ };
                            if (Intersects(Depth.MinOrCenter, max))
                            {
                                nextNode3[GroupIndex].ChildCount = Depth.ChildCount;
                                nextNode3[GroupIndex].ChildIndex = Depth.ChildIndex;
                            }
                        }
                    }
                    GroupMemoryBarrierWithGroupSync();
                
                    if (!bLeaf3)
                        [loop] for (int c = 0; c < MaxChildCount; ++c)
                        {
                            if (nextNode3[c].ChildIndex == 0xffffffff)
                                continue;
                
                            bParticipate = GroupIndex < nextNode3[c].ChildCount;
                            if (bParticipate)
                                Depth = LightTree[nextNode3[c].ChildIndex + GroupIndex];
                            nextNode4[GroupIndex].ChildCount = 0;
                            nextNode4[GroupIndex].ChildIndex = 0xffffffffu;
                
                            if (bParticipate)
                            {
                                if (Depth.ChildCount == 0)
                                {
                                    if (GroupIndex == 0) bLeaf4 = true;
                                    if (Intersects(Depth.MinOrCenter, Depth.MaxXOrRadius))
                                    {
                                        if (bCountOnly)
                                            InterlockedAdd(ReservedLightCount, 1);
                                        else
                                        {
                                            uint WritePosition;
                                            InterlockedAdd(LocalNextWritePosition, 1, WritePosition);
                                            LightIndices[LightIndicesBase + GlobalWriteBase + WritePosition] = asuint(Depth.MaxYOrDataIndex);
                                        }
                                    }
                                }
                                else
                                {
                                    if (GroupIndex == 0) bLeaf4 = false;
                                    float3 max = { Depth.MaxXOrRadius, Depth.MaxYOrDataIndex, Depth.MaxZ };
                                    if (Intersects(Depth.MinOrCenter, max))
                                    {
                                        nextNode4[GroupIndex].ChildCount = Depth.ChildCount;
                                        nextNode4[GroupIndex].ChildIndex = Depth.ChildIndex;
                                    }
                                }
                            }
                            GroupMemoryBarrierWithGroupSync();
                
                            if (!bLeaf4)
                                [loop] for (int d = 0; d < MaxChildCount; ++d)
                                {
                                    if (nextNode4[d].ChildIndex == 0xffffffff)
                                        continue;
                
                                    bParticipate = GroupIndex < nextNode4[d].ChildCount;
                                    if (bParticipate)
                                        Depth = LightTree[nextNode4[d].ChildIndex + GroupIndex];
                
                                    if (bParticipate)
                                    {
                                        if (Depth.ChildCount == 0)
                                        {
                                            if (Intersects(Depth.MinOrCenter, Depth.MaxXOrRadius))
                                            {
                                                if (bCountOnly)
                                                    InterlockedAdd(ReservedLightCount, 1);
                                                else
                                                {
                                                    uint WritePosition;
                                                    InterlockedAdd(LocalNextWritePosition, 1, WritePosition);
                                                    LightIndices[LightIndicesBase + GlobalWriteBase + WritePosition] = asuint(Depth.MaxYOrDataIndex);
                                                }
                                            }
                                        }
                                    }
                                    GroupMemoryBarrierWithGroupSync();
                                }
                            GroupMemoryBarrierWithGroupSync();
                        }
                    GroupMemoryBarrierWithGroupSync();
                }
            GroupMemoryBarrierWithGroupSync();
        }
}


[numthreads(MaxChildCount, 1, 1)]
void mainCS(uint3 GroupID : SV_GroupID, uint GroupIndex : SV_GroupIndex)
{
    uint clusterIndex = GroupID.x + GroupID.y * 65535u;
    if (GroupIndex == 0)
    {
        ActiveClusterCount = LightIndices[LightClusterCount];
        RootNode = LightTree[LightTreeRootIndex];
    }
    GroupMemoryBarrierWithGroupSync();
    if (clusterIndex >= ActiveClusterCount)
        return;
    
    FLightCluster cluster = LightClusters[clusterIndex];
    
    FLightTreeNode Root = RootNode;
    if (Root.ChildIndex == 0xffffffffu)
        return;
    
    if (GroupIndex == 0)
    {
        BuildClusterFrustum(cluster.ClusterKey);
        ReservedLightCount = 0;
        LocalNextWritePosition = 0;
        GlobalWriteBase = 0;
    }
    GroupMemoryBarrierWithGroupSync();
    
    Traversal(GroupIndex, Root, true);
    
    if (GroupIndex == 0)
    {
        InterlockedAdd(LightIndices[LightIndicesAllocator], ReservedLightCount, GlobalWriteBase);
        LightClusters[clusterIndex].Offset = GlobalWriteBase + LightIndicesBase;
        LightClusters[clusterIndex].Count = ReservedLightCount;
    }
    GroupMemoryBarrierWithGroupSync();
    
    Traversal(GroupIndex, Root, false);
}
