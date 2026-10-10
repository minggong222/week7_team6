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
    float padding_;
};

cbuffer LightingGlobals : register(b3)
{
    float4 AmbientRadiance;
};

cbuffer LightCountConstants : register(b4)
{
    uint LightCount;
    uint3 LightCountPadding;
}
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
    
    //Spot Light
    float3 Direction;
    float Intensity;
    
    float3 Color;
    float Radius;
    
    float InnerConeCos;
    float OuterConeCos;
    float AttenuationLinear;
    float AttenuationQuadratic;
    
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

//Spot Light
float3 EvaluateSpotLight(FLightData Light, float3 WorldPosition, float3 WorldNormal, float3 ViewDirection, float3 Albedo, float3 SpecularColor, float Shininess)
{
    float3 ToLight = Light.Position - WorldPosition;
    float DistanceToLight = length(ToLight);
    
    if(DistanceToLight >= Light.Radius)
        return float3(0, 0, 0);
    
    float3 L = ToLight / max(DistanceToLight, 0.0001f);
    float3 N = normalize(WorldNormal);
    float3 V = normalize(ViewDirection);
    
    // 거리 감쇠
    float Attenuation = 1.0f / max(1.0f + Light.AttenuationLinear * DistanceToLight + Light.AttenuationQuadratic * DistanceToLight * DistanceToLight, 1e-4f);
    
    //원뿔 감쇠
    float CosTheta = dot(normalize(Light.Direction), -L);
    
    float ConeFactor = saturate((CosTheta - Light.OuterConeCos) / max(Light.InnerConeCos - Light.OuterConeCos, 0.0001f));
    
    //부드러운 경계
    ConeFactor = ConeFactor * ConeFactor * (3.0f - 2.0f * ConeFactor);
    
    //Blinn-Phong Diffuse
    float NdotL = max(dot(N, L), 0.0f);
    float3 Diffuse = Albedo * NdotL;
    
    //Blinn-Phong Specular
    float3 Specular = float3(0.0f, 0.0f, 0.0f);
    
    if (NdotL > 0.0f)
    {
        float3 H = normalize(L + V);
        
        float SpeculaFactor = pow(max(dot(N, H), 0.0f), Shininess);
        
        Specular = SpecularColor * SpeculaFactor;
    }

    return Light.Color * Light.Intensity * Attenuation * ConeFactor * (Diffuse + Specular);
}

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
    
    float3 WorldPosition : TEXCOORD1;
};

Texture2D g_txColor : register(t0);
// t1~t3은 대응하는 bHas*Map이 1일 때만 유효하다. 0이면 이전 머티리얼의 텍스처가 남아 있다
Texture2D g_txNormal : register(t1);
Texture2D g_txSpecular : register(t2); // SpecularColor에 곱한다
Texture2D g_txEmissive : register(t3); // EmissiveColor에 곱한다
SamplerState g_Sample : register(s0);

// 조명 방향 및 색상
static const float3 LightDir = normalize(float3(0.5f, 0.5f, -1.0f));
static const float3 LightColor = float3(0.f, 0.f, 0.f);
static const float3 AmbientColor = float3(0.f, 0.f, 0.f);

PS_INPUT mainVS(VS_INPUT input)
{
    PS_INPUT output;

    // 월드 변환 후 카메라 투영 변환
    float4 worldPos = mul(float4(input.p, 1.0f), World);
    output.position = mul(worldPos, ViewProjection);
    output.normal = mul(float4(input.n, 0.0f), World).xyz;
    output.color = input.c; 
    output.uv = input.t;
    output.WorldPosition = worldPos.xyz;
    return output;
}

float4 mainPS(PS_INPUT input) : SV_TARGET
{
    float4 texColor = g_txColor.Sample(g_Sample, input.uv + UVOffset);
    float4 albedo = texColor * BaseColor;

    // 보간되면 길이가 틀어지므로 다시 정규화한다
    float3 N = normalize(input.normal);
    float NdotL = saturate(dot(N, -LightDir));
    float3 lighting = AmbientColor + LightColor * NdotL;
    
    
    float3 FinalColor = albedo.rgb * lighting;
    
    float3 ViewDirection = normalize(CameraPosition - input.WorldPosition);
    
    FinalColor += EvaluateSpotLight(Lights[0], input.WorldPosition, N, ViewDirection, albedo.rgb, SpecularColor, Shininess);
    
    // Opaque는 알파를 1로 고정한다. 뷰포트 RT를 ImGui가 알파 블렌딩으로 그리므로 알파가 남으면 비쳐 보인다
        float alpha = bOpaque > 0.5f ? 1.0f : albedo.a;
    return float4(FinalColor, alpha);
}