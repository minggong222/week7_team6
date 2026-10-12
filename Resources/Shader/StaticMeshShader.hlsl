/******************************
전처리기 매크로
*******************************/

#define     LightType_Directional 0
#define	    LightType_Point 1
#define	    LightType_Spot 2
#define	    LightType_Rect 3
#define	    LightType_Ambient 4 // 언리얼엔 미존재
#define	    LightType_MAX 5

#define TileCountX 8
#define TileCountY 8

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
    float padding;
};

struct FTileLightHeader
{
    uint Offset;
    uint Count;
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
    float3 Padding_noUse;
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

/******************************
[SRV] StructuredBuffer
*******************************/

StructuredBuffer<FLightData> Lights : register(t4);
StructuredBuffer<FTileLightHeader> TileHeaders : register(t5);
StructuredBuffer<uint> TileLightIndices : register(t6);

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

// 조명 방향 및 색상
static const float3 LightDir = normalize(float3(0.5f, 0.5f, -1.0f));
static const float3 LightColor = float3(0.f, 0.f, 0.f);
static const float3 AmbientColor = float3(0.25f, 0.25f, 0.25f);

static const float EPSLION = 1e-4f;

/******************************
Utility Function
*******************************/

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
                L = normalize(LightVector);
            }
        }
        else if (Light.LightType == LightType_Spot)
        {
            L = normalize(LightVector);
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
    if (Distance > Light.Radius)
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
        Attenuation = pow(1 - Distance / Light.Radius, Light.LightFalloffExponent);
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
    output.lighting = AmbientColor;

    for (uint i = 0; i < LightCount; ++i)
    {
        FLightData Light = Lights[i];

        FShadingData Data = 
        {
            Light,
            CameraPosition,
            output.WorldPosition,
            output.normal,
            float3(1.0f, 1.0f, 1.0f),  
            float3(1.0f, 1.0f, 1.0f),  
            32.0f,
        };

        output.lighting += EvaluateShading(Data);
    }
#endif

    return output;
}

float4 mainPS(PS_INPUT input) : SV_TARGET
{
    float4 texColor = g_txColor.Sample(g_Sample, input.uv + UVOffset);
    float4 albedo = texColor * BaseColor;

    float3 lighting = AmbientColor;
    
    float3 mappedNormal = GetBumpNormal(input, input.normal, input.uv);
    
#if NormalMap
    return float4(mappedNormal*0.5 + 0.5,1.0f);
#endif
    
#if LIGHTING_MODEL_GOURAUD
    lighting = input.lighting;
#elif LIGHTING_MODEL_LAMBERT || LIGHTING_MODEL_PHONG
    uint X = (uint) floor((input.position.x / ViewSizeX) * TileCountX);
    uint Y = (uint) floor((input.position.y / ViewSizeY) * TileCountY);
    X = min(X, TileCountX - 1);
    Y = min(Y, TileCountY - 1);

    FTileLightHeader Header = TileHeaders[Y * TileCountX + X];
    for (uint i = 0; i < Header.Count; ++i)
    {
        uint LightIndex = TileLightIndices[Header.Offset + i];
        FLightData Light = Lights[LightIndex];
        FShadingData Data = 
        {
            Light,
            CameraPosition,
            input.WorldPosition,
            mappedNormal,
            albedo.xyz,  
            SpecularColor,  
            Shininess,
        };

        lighting += EvaluateShading(Data);
    }
#else
    
#endif

    float3 FinalColor = albedo.rgb * lighting;

    // Opaque는 알파를 1로 고정한다. 뷰포트 RT를 ImGui가 알파 블렌딩으로 그리므로 알파가 남으면 비쳐 보인다
    float alpha = bOpaque > 0.5f ? 1.0f : albedo.a;
    return float4(FinalColor, alpha);
}