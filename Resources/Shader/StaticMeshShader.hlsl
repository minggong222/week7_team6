#include "StaticMeshShader.hlsli"

cbuffer constants : register(b0)
{
    row_major matrix World;
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
    float3 Padding_noUse;
};

cbuffer LightingGlobals : register(b3)
{
    float4 AmbientRadiance;
};

#define     LightType_Directional 0
#define	    LightType_Point 1
#define	    LightType_Spot 2
#define	    LightType_Rect 3
#define	    LightType_Ambient 4 // 언리얼엔 미존재
#define	    LightType_MAX 5

#define TileCountX 8
#define TileCountY 8

struct FLightData
{
    uint LightType;
    float3 Position;
    float4 LightColor;
    float AttenuationRadius;
    float LightFalloffExponent;
    float2 Padding;
    // ...
    // Undefined
    // 각자 구현하시고 여기에 합치면 될 것 같습니다.
};

struct FTileLightHeader
{
    uint Offset;
    uint Count;
};

StructuredBuffer<FLightData> Lights : register(t4);
StructuredBuffer<FTileLightHeader> TileHeaders : register(t5);
StructuredBuffer<uint> TileLightIndices : register(t6);

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
    
#if LIGHTING_MODEL_GOURAUD
    float3 diffuse : TEXCOORD1;
    float3 specular : TEXCOORD2;
#elif LIGHTING_MODEL_LAMBERT || LIGHTING_MODEL_PHONG
    float3 normal : NORMAL;
    float3 world : TEXCOORD1;
#endif
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
};

Texture2D g_txColor : register(t0);
// t1~t3은 대응하는 bHas*Map이 1일 때만 유효하다. 0이면 이전 머티리얼의 텍스처가 남아 있다
Texture2D g_txNormal : register(t1);
Texture2D g_txSpecular : register(t2); // SpecularColor에 곱한다
Texture2D g_txEmissive : register(t3); // EmissiveColor에 곱한다
SamplerState g_Sample : register(s0);

// 조명 방향 및 색상
static const float3 LightDir = normalize(float3(0.5f, 0.5f, -1.0f));
static const float3 LightColor = float3(0.5f, 0.5f, 0.5f);
static const float3 AmbientColor = float3(0.5f, 0.5f, 0.5f);



float3 LambertLighting(float3 normal, float3 position, float3 world)
{
    float3 N = normalize(normal);
    float3 Diffuse = 0;

    uint TileX = min((uint) (position.x / (ViewSizeX / TileCountX)), TileCountX - 1u);
    uint TileY = min((uint) (position.y / (ViewSizeY / TileCountY)), TileCountY - 1u);
    uint TileLightIndex = TileY * TileCountX + TileX;
    uint LightNum = TileHeaders[TileLightIndex].Count;
    uint Offset = TileHeaders[TileLightIndex].Offset;
    for (uint i = 0; i < LightNum; i++)
    {
        uint LightIndex = TileLightIndices[Offset + i];
        FLightData Light = Lights[LightIndex];

        float3 ToLight = Light.Position - world;
        float Dist = length(ToLight);
        float3 L = ToLight / max(Dist, 0.000001f);
        float NdotL = saturate(dot(N, L));

        float glow = DistanceAttenuation(Dist, Light.AttenuationRadius, Light.LightFalloffExponent);

        Diffuse += Light.LightColor.rgb * NdotL * glow;
    }
    
    return (Diffuse);
}


PS_INPUT mainVS(VS_INPUT input)
{
    PS_INPUT output;

    // 월드 변환 후 카메라 투영 변환
    float4 worldPos = mul(float4(input.p, 1.0f), World);
    output.position = mul(worldPos, ViewProjection);
    output.color = input.c;
    output.uv = input.t;

#if     LIGHTING_MODEL_GOURAUD
    output.diffuse = LambertLighting(mul(float4(input.n, 0.0f), World).xyz, output.position, worldPos.xyz);
    output.specular = 0.0f;
#elif   LIGHTING_MODEL_LAMBERT || LIGHTING_MODEL_PHONG
    output.normal = mul(float4(input.n, 0.0f), World).xyz;
    output.world = worldPos.xyz;
#else // Unlit 매크로를 넘기지 않은 경우
#endif

    return output;
}

float4 mainPS(PS_INPUT input) : SV_TARGET
{
    float4 texColor = g_txColor.Sample(g_Sample, input.uv + UVOffset);
    float4 albedo = texColor * BaseColor;

#if     LIGHTING_MODEL_GOURAUD
    float3 lighting = AmbientRadiance.rgb * AmbientRadiance.a + input.diffuse;
#elif   LIGHTING_MODEL_LAMBERT
    float3 lighting = AmbientRadiance.rgb * AmbientRadiance.a + LambertLighting(input.normal, input.position, input.world);
#elif   LIGHTING_MODEL_PHONG
    albedo = float4(0,0,1,1);
    float3 lighting = 1.0f;
#else // Unlit 매크로를 넘기지 않은 경우
    float3 lighting = 1.0f;
#endif
    

    // Opaque는 알파를 1로 고정한다. 뷰포트 RT를 ImGui가 알파 블렌딩으로 그리므로 알파가 남으면 비쳐 보인다
    float alpha = bOpaque > 0.5f ? 1.0f : albedo.a;
    return float4(albedo.rgb * lighting, alpha);
}