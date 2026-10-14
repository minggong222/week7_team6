/******************************
전처리기 매크로
*******************************/

#define     LightType_Directional 0
#define	    LightType_Point 1
#define	    LightType_Spot 2
#define	    LightType_Rect 3
#define	    LightType_Ambient 4 // 언리얼엔 미존재
#define	    LightType_MAX 5


#define PixelLengthPerTile 64

/******************************
Struct
*******************************/

struct FLightData
{
    uint LightType;
    float3 Position;
    
    float3 Direction;
    float Intensity;
    
    float3 Color;
    float Radius;
    
    float LightFalloffExponent;
    float InnerConeCos;
    float OuterConeCos;
    float Padding;
};

struct FLightCluster
{
    uint Offset;
    uint Count;
    uint ClusterKey;
};

struct FShadingData
{
    FLightData Light;
    float3 CameraPosition;
    float3 VertexWorldPosition;
    float3 VertexNormalPosition;
    float3 Albedo;
    float3 MaterialSpecularColor;
    float MaterialShininess;
};

struct VS_INPUT
{
    float3 p : POSITION; // Input position from vertex buffer
    float3 n : NORMAL;
    float4 c : COLOR; // Input color from vertex buffer
    float2 t : TEXCOORD;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
    float3 lighting : COLOR1; // For Lambert Shading
    
    float3 WorldPosition : TEXCOORD1;
};

/******************************
Constant Buffer
*******************************/

cbuffer constants : register(b0)
{
    row_major matrix World;
    row_major matrix Normal;
};

cbuffer MaterialParams : register(b1)
{
    float4 BaseColor;
    float2 UVOffset;
    float bOpaque;
    float bHasNormalMap;
    float3 SpecularColor;
    float Shininess;
    float3 EmissiveColor;
    float NormalScale;
    float bHasSpecularMap;
    float bHasEmissiveMap;
    float2 MaterialPadding;
};

cbuffer ViewConstants : register(b2)
{
    row_major matrix ViewProjection;
    float3 CameraPosition;
    float ViewSizeX;
    float ViewSizeY;
    uint bIsPerspective;
    float2 Padding_noUse;
};

cbuffer LightingGlobals : register(b3)
{
    float4 AmbientRadiance;
    uint LightCount;
    uint DirectionalLightCount;
    uint LightTreeRootIndex;
    uint LightingGlobalsPadding;
};

cbuffer LightClusterGlobals : register(b4)
{
    uint ClusterCountX;
    uint ClusterCountY;
    uint ClusterCountZ;
    float ClusterZScale;
    float ClusterZBias;
    float3 ClusterGlobalsPadding;
};

/******************************
[SRV] StructuredBuffer
*******************************/

StructuredBuffer<FLightData> LightData : register(t4);
StructuredBuffer<uint> DirectionalLightIndices : register(t5);

StructuredBuffer<uint> LightIndices : register(t6);
StructuredBuffer<FLightCluster> LightClusters : register(t7);
#define ZClusterCountPerPhysicalPage 8
#define Log2OfZClusterCountPerPhysicalPage 3
StructuredBuffer<uint> LightClusterPageTable : register(t8);
StructuredBuffer<uint> IndirectToLightClusters : register(t9);

/******************************
[SRV] Texture & Texture Sampler
*******************************/

Texture2D g_txColor : register(t0);
// t1~t3은 대응하는 bHas*Map이 1일 때만 유효하다. 0이면 이전 머티리얼의 텍스처가 남아 있다
Texture2D g_txNormal : register(t1);
Texture2D g_txSpecular : register(t2); // SpecularColor에 곱한다
Texture2D g_txEmissive : register(t3); // EmissiveColor에 곱한다

SamplerState g_Sample : register(s0);

/******************************
Constant Variable
*******************************/

static const float EPSLION = 1e-4f;

/******************************
Utility Function
*******************************/


uint GetClusterIndexZ(float ViewDepth)
{
    float DepthTerm = bIsPerspective ? log2(ViewDepth) : ViewDepth;
    return (uint)clamp(DepthTerm * ClusterZScale + ClusterZBias, 0.0f, (float)(ClusterCountZ - 1));
}

uint GetClusterKey(float2 SVPosition, float ViewDepth)
{
    uint X = (uint) (SVPosition.x / PixelLengthPerTile);
    uint Y = (uint) (SVPosition.y / PixelLengthPerTile);
    uint Z = GetClusterIndexZ(ViewDepth);
    uint PageCountZ = (ClusterCountZ + ZClusterCountPerPhysicalPage - 1) / ZClusterCountPerPhysicalPage;
    uint PageIndex = (X * ClusterCountY + Y) * PageCountZ + (Z >> Log2OfZClusterCountPerPhysicalPage);
    return (PageIndex << Log2OfZClusterCountPerPhysicalPage) | (Z & (ZClusterCountPerPhysicalPage - 1));
}
bool TryGetLightCluster(float2 PixelPosition, float ViewDepth, out FLightCluster Cluster)
{
    Cluster = (FLightCluster) 0;
    if (ClusterCountZ == 0 || ViewDepth <= 0.0f || any(PixelPosition < 0.0f) || any(PixelPosition >= float2(ViewSizeX, ViewSizeY)))
        return false;
    uint ClusterKey = GetClusterKey(PixelPosition, ViewDepth);
    uint PageBase = LightClusterPageTable[ClusterKey >> Log2OfZClusterCountPerPhysicalPage];
    if (PageBase == 0xffffffffu)
        return false;
    uint ClusterIndex = IndirectToLightClusters[PageBase + (ClusterKey & (ZClusterCountPerPhysicalPage - 1))];
    if (ClusterIndex == 0xffffffffu)
        return false;
    Cluster = LightClusters[ClusterIndex];
    return true;
}

// 빛의 벡터를 모두 계산.
// out 키워드는 참조 변수 비슷한 키워드
void CalculateLightVector(
    FShadingData Data,
    out float3 V,
    out float3 N,
    out float3 L,
    out float3 H,
    out float NdotL,
    out float NdotH,
    out float Distance
)
{
    FLightData Light = Data.Light;
    
    V = normalize(Data.CameraPosition - Data.VertexWorldPosition);
    N = normalize(Data.VertexNormalPosition);

    if (
        Light.LightType == LightType_Point ||
        Light.LightType == LightType_Spot
    )
    {
        float3 LightVector = Light.Position - Data.VertexWorldPosition;
        Distance = length(LightVector); 

        if (Light.LightType == LightType_Point)
        {
            if (Distance < EPSLION)
            {
                // PointLight에서 Vertex와 Light의 Position이 겹치면 Divide by zero가 발생하게 되므로 직접 지정
                L = float3(0.0f, 0.0f, 0.0f);
            }
            else
            {
                L = LightVector / max(Distance, EPSLION);
            }
        }
        else if (Light.LightType == LightType_Spot)
        {
            L = LightVector / max(Distance, EPSLION);
        }
    }
    else
    {
        Distance = -1.0f;
        L = normalize(-Light.Direction);
    }

    H = normalize(V + L);

    NdotL = saturate(dot(N, L));
    NdotH = saturate(dot(N, H));

}

float3 EvaluateShading(FShadingData Data)
{
    FLightData Light = Data.Light;

    float3 V, N, L, H;
    float NdotL, NdotH, Distance;
    CalculateLightVector(Data, V, N, L, H, NdotL, NdotH, Distance);

    // 빛의 영향을 받지 않으면 검정색으로 return
    if (Distance >= Light.Radius)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }
    
    float3 Diffuse = Data.Albedo * NdotL;

#if LIGHTING_MODEL_LAMBERT
    float3 Specular = float3(0.0f, 0.0f, 0.0f);
#else

    float3 SpecularFactor = pow(NdotH, Data.MaterialShininess);
    float3 Specular = Data.MaterialSpecularColor * SpecularFactor;

    // Cone 범위를 벗어나면 Spotlight 적용 안되게..?
    if (NdotL <= 0.0f)
    {
        Specular = float3(0.0f, 0.0f, 0.0f);
    }
#endif

    float Attenuation = 1.0f;
    float ConeFactor = 1.0f;

    if (
        Light.LightType == LightType_Point ||
        Light.LightType == LightType_Spot
    )
    {
        // Point, Spotlight

        // 거리 감쇠
        Attenuation = pow(saturate(1.0f - Distance / max(Light.Radius, EPSLION)), Light.LightFalloffExponent);
        //float Attenuation = pow(max(1 - pow(Distance / Light.Radius, 4), 0), 2) / (pow(Distance, 2) + 1);
        //float Attenuation = 1.0f / max(1.0f + Light.AttenuationLinear * Distance, EPSLION);

        if (Light.LightType == LightType_Spot)
        {
            // Spotlight

            // 원뿔 감쇠
            float CosTheta = dot(normalize(Light.Direction), -L);
            ConeFactor = saturate((CosTheta - Light.OuterConeCos) / max(Light.InnerConeCos - Light.OuterConeCos, EPSLION));
            
            // 부드러운 경계
            ConeFactor = ConeFactor * ConeFactor * (3.0f - 2.0f * ConeFactor);
        }
    }

    return Light.Color * Light.Intensity * Attenuation * ConeFactor * (Diffuse + Specular);
}

/******************************
Vertex, Pixel Shader
*******************************/

float3 GetBumpNormal(PS_INPUT input, float3 baseNormal, float2 uv)
{
    if (bHasNormalMap == 0)
    {
        return baseNormal;
    }
    
    uint textureWidth;
    uint textureHeight;
    uint mipLevels;
    
    g_txNormal.GetDimensions(0, textureWidth, textureHeight, mipLevels);
    
    float2 texelSize = 1.0f / float2(textureWidth, textureHeight);
    
    float heightCenter = g_txNormal.Sample(g_Sample, uv).r;
    float heightX = g_txNormal.Sample(g_Sample, uv + float2(texelSize.x, 0.0f)).r;
    float heightY = g_txNormal.Sample(g_Sample, uv + float2(0.0f, texelSize.y)).r;
    
    float dhdx = heightX - heightCenter;
    float dhdy = heightY - heightCenter;

    float3 dpdx = ddx(input.WorldPosition);
    float3 dpdy = ddy(input.WorldPosition);
    
    float2 duvdx = ddx(input.uv);
    float2 duvdy = ddy(input.uv);
    
    float determinant = duvdx.x * duvdy.y - duvdx.y * duvdy.x;
    
    if (abs(determinant) < 0.000001f)
    {
        return baseNormal;
    }
    
    float inverseDeterminant = 1.0f / determinant;
    
    float3 tangent = (dpdx * duvdy.y - dpdy * duvdx.y) * inverseDeterminant;
    float3 bitangent = (dpdy * duvdx.x - dpdx * duvdy.x) * inverseDeterminant;
    
    tangent = normalize(tangent - baseNormal * dot(baseNormal, tangent));
    bitangent = normalize(cross(baseNormal, tangent));
    
    const float BumpStrength = 1.0f;
    
    float3 bumpedNormal = normalize(baseNormal - tangent * dhdx * BumpStrength - bitangent * dhdy * BumpStrength);
    
    return bumpedNormal;

}

PS_INPUT mainVS(VS_INPUT input)
{
    PS_INPUT output;

    // 월드 변환 후 카메라 투영 변환
    float4 worldPos = mul(float4(input.p, 1.0f), World);
    output.position = mul(worldPos, ViewProjection);
    output.normal = mul(float4(input.n, 0.0f), Normal).xyz;
    output.color = input.c; 
    output.uv = input.t;
    output.lighting = float3(0.0f, 0.0f, 0.0f);
    output.WorldPosition = worldPos.xyz;

#if LIGHTING_MODEL_GOURAUD
    output.lighting = AmbientRadiance.rgb * AmbientRadiance.w;
    FLightCluster Cluster = (FLightCluster) 0;
    bool bHasLightCluster = false;
    if (output.position.w > 0.0f && output.position.z >= 0.0f && output.position.z <= output.position.w)
    {
        float2 PixelPosition = (output.position.xy / output.position.w * float2(0.5f, -0.5f) + 0.5f) * float2(ViewSizeX, ViewSizeY);
        bHasLightCluster = TryGetLightCluster(PixelPosition, output.position.w, Cluster);
    }
    FShadingData Data = (FShadingData) 0;
    Data.CameraPosition = CameraPosition;
    Data.VertexWorldPosition = output.WorldPosition;
    Data.VertexNormalPosition = output.normal;
    Data.Albedo = 1.0f;
    Data.MaterialSpecularColor = 1.0f;
    Data.MaterialShininess = 32.0f;
    if (bHasLightCluster)
    {
        [loop] for (uint DirectionalIndex = 0; DirectionalIndex < DirectionalLightCount; ++DirectionalIndex)
        {
            Data.Light = LightData[DirectionalLightIndices[DirectionalIndex]];
            output.lighting += EvaluateShading(Data);
        }
        [loop] for (uint LocalIndex = 0; LocalIndex < Cluster.Count; ++LocalIndex)
        {
            Data.Light = LightData[LightIndices[Cluster.Offset + LocalIndex]];
            output.lighting += EvaluateShading(Data);
        }
    }
    else
    {
        [loop] for (uint LightIndex = 0; LightIndex < LightCount; ++LightIndex)
        {
            Data.Light = LightData[LightIndex];
            output.lighting += EvaluateShading(Data);
        }
    }
#endif

    return output;
}

float4 mainPS(PS_INPUT input) : SV_TARGET
{
    float4 texColor = g_txColor.Sample(g_Sample, input.uv + UVOffset);
    float4 albedo = texColor * BaseColor;
    float3 mappedNormal = GetBumpNormal(input, input.normal, input.uv);
#if NormalMap
    return float4(mappedNormal * 0.5f + 0.5f, 1.0f);
#endif
#if LIGHTING_MODEL_GOURAUD || LIGHTING_MODEL_LAMBERT || LIGHTING_MODEL_PHONG
    FLightCluster Cluster = (FLightCluster) 0;
    bool bUseLightCluster = ClusterCountZ != 0 && bOpaque > 0.5f;
    if (bUseLightCluster)
    {
        if (!TryGetLightCluster(input.position.xy, input.position.w, Cluster))
            discard;
    }
#endif
    float3 FinalColor = albedo.rgb;
#if LIGHTING_MODEL_GOURAUD
    FinalColor *= input.lighting;
#elif LIGHTING_MODEL_LAMBERT || LIGHTING_MODEL_PHONG
    float3 lighting = albedo.rgb * AmbientRadiance.rgb * AmbientRadiance.w;
    FShadingData Data = (FShadingData) 0;
    Data.CameraPosition = CameraPosition;
    Data.VertexWorldPosition = input.WorldPosition;
    Data.VertexNormalPosition = mappedNormal;
    Data.Albedo = albedo.rgb;
    Data.MaterialSpecularColor = SpecularColor;
    Data.MaterialShininess = Shininess;
    if (bUseLightCluster)
    {
        [loop] for (uint DirectionalIndex = 0; DirectionalIndex < DirectionalLightCount; ++DirectionalIndex)
        {
            Data.Light = LightData[DirectionalLightIndices[DirectionalIndex]];
            lighting += EvaluateShading(Data);
        }
        [loop] for (uint LocalIndex = 0; LocalIndex < Cluster.Count; ++LocalIndex)
        {
            Data.Light = LightData[LightIndices[Cluster.Offset + LocalIndex]];
            lighting += EvaluateShading(Data);
        }
    }
    else
    {
        [loop] for (uint LightIndex = 0; LightIndex < LightCount; ++LightIndex)
        {
            Data.Light = LightData[LightIndex];
            lighting += EvaluateShading(Data);
        }
    }
    FinalColor = lighting;
#endif
    float alpha = bOpaque > 0.5f ? 1.0f : albedo.a;
    return float4(FinalColor, alpha);
}
