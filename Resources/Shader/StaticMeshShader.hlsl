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

// 조명 방향 및 색상
static const float3 LightDir = normalize(float3(0.5f, 0.5f, -1.0f));
static const float3 LightColor = float3(0.f, 0.f, 0.f);
static const float3 AmbientColor = float3(0.25f, 0.25f, 0.25f);

StructuredBuffer<FLightData> Lights : register(t4);
StructuredBuffer<FTileLightHeader> TileHeaders : register(t5);
StructuredBuffer<uint> TileLightIndices : register(t6);

float3 EvaluateDirectionalLight(FLightData Light, float3 WorldPosition, float3 WorldNormal, float3 ViewDirection, float3 Albedo, float3 SpecularColor, float Shininess)
{
#if LIGHTING_MODEL_GOURAUD
    // Gouraud 쉐이딩. 라이팅을 Vertex Shader 단계에서 처리하고 래스터라이저로 보간하여 적용.

    // 큰 흐름은 Blinn-phong 쉐이딩과 동일
    
    // 1. 필요한 변수들 계산
    float3 V = ViewDirection;
    float3 N = WorldNormal;
    float3 L = normalize(-Light.Direction);
    float3 H = normalize(V + L);
    float NdotL = saturate(dot(N, L));
    float NdotH = saturate(dot(N, H));

    // 2. Ambient, Diffuse, Specular
    float3 Dir_Ambient = AmbientColor;
    float3 Dir_Diffuse = float3(1.0f, 1.0f, 1.0f) * NdotL;
    float3 Dir_Specular = SpecularColor * pow(NdotH, Shininess);

    // 3. 모두 적용
    float3 Dir_Lighting = Dir_Ambient + Dir_Diffuse + Dir_Specular;
    return Dir_Lighting * Light.Color * Light.Intensity;

#elif LIGHTING_MODEL_LAMBERT 
    // Lambert 쉐이딩. 라이팅을 Pixel Shader 단계에서 처리. Diffuse Only.
    
    // 1. 필요한 변수들 계산
    float3 N = WorldNormal;
    float3 L = normalize(-Light.Direction);
    float NdotL = saturate(dot(N, L));

    // 2. Diffuse
    float3 Dir_Diffuse = float3(1.0f, 1.0f, 1.0f) * NdotL;

    // 3. 모두 적용
    float3 Dir_Lighting = Dir_Diffuse;
    return Dir_Lighting * Light.Color * Light.Intensity;

#elif LIGHTING_MODEL_PHONG
    // Blinn-phong 쉐이딩. 라이팅을 Pixel Shader 단계에서 처리. Ambient, Diffuse, Specular.
    
    // 1. 필요한 변수들 계산
    float3 V = ViewDirection;
    float3 N = WorldNormal;
    float3 L = normalize(-Light.Direction);
    float3 H = normalize(V + L);
    float NdotL = saturate(dot(N, L));
    float NdotH = saturate(dot(N, H));

    // 2. Ambient, Diffuse, Specular
    float3 Dir_Ambient = AmbientColor;
    float3 Dir_Diffuse = float3(1.0f, 1.0f, 1.0f) * NdotL;
    float3 Dir_Specular = SpecularColor * pow(NdotH, Shininess);

    // 3. 모두 적용
    float3 Dir_Lighting = Dir_Ambient + Dir_Diffuse + Dir_Specular;
    return Dir_Lighting * Light.Color * Light.Intensity;

#endif 

    // Unknown light type
    return float3(0.0f, 0.0f, 0.0f);
}



//Spot Light
float3 EvaluatePhongShading(FLightData Light, float3 WorldPosition, float3 WorldNormal, float3 ViewDirection, float3 Albedo, float3 SpecularColor, float Shininess)
{
    float3 ToLight = Light.Position - WorldPosition;
    float DistanceToLight = length(ToLight);
    
    if (Light.LightType != LightType_Directional)
    {
        if (DistanceToLight >= Light.Radius)
            return float3(0, 0, 0);
    }
    
    float3 L;
    if (Light.LightType == LightType_Directional)
    {
        L = normalize(-Light.Direction);
    }
    else
    {
        L = ToLight / max(DistanceToLight, 0.0001f);
    }
    float3 N = normalize(WorldNormal);
    float3 V = normalize(ViewDirection);
    
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
    
    if (Light.LightType == LightType_Directional)
    {
        return Light.Color * Light.Intensity * (Diffuse + Specular);
    }
    
    // 거리 감쇠
    float Attenuation = pow(1 - DistanceToLight / Light.Radius, Light.LightFalloffExponent);
    //float Attenuation = pow(max(1 - pow(DistanceToLight / Light.Radius, 4), 0), 2) / (pow(DistanceToLight, 2) + 1);
    //float Attenuation = 1.0f / max(1.0f + Light.AttenuationLinear * DistanceToLight, 1e-4f);
    if (Light.LightType == LightType_Spot)
    {
        //원뿔 감쇠
        float CosTheta = dot(normalize(Light.Direction), -L);
    
        float ConeFactor = saturate((CosTheta - Light.OuterConeCos) / max(Light.InnerConeCos - Light.OuterConeCos, 0.0001f));
    
        //부드러운 경계
        ConeFactor = ConeFactor * ConeFactor * (3.0f - 2.0f * ConeFactor);
        return Light.Color * Light.Intensity * Attenuation * ConeFactor * (Diffuse + Specular);
    }
        
    return Light.Color * Light.Intensity * Attenuation * (Diffuse + Specular);
    
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
    float3 lighting : COLOR1; // For Lambert Shading
    
    float3 WorldPosition : TEXCOORD1;
};

Texture2D g_txColor : register(t0);
// t1~t3은 대응하는 bHas*Map이 1일 때만 유효하다. 0이면 이전 머티리얼의 텍스처가 남아 있다
Texture2D g_txNormal : register(t1);
Texture2D g_txSpecular : register(t2); // SpecularColor에 곱한다
Texture2D g_txEmissive : register(t3); // EmissiveColor에 곱한다
SamplerState g_Sample : register(s0);

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
    float3 ViewDirection = normalize(CameraPosition - output.WorldPosition);

    for (uint i = 0; i < LightCount; ++i)
    {
        FLightData Light = Lights[i];
        output.lighting = float4(EvaluatePhongShading(Light, output.WorldPosition, output.normal, ViewDirection, float3(1.0f, 1.0f, 1.0f), SpecularColor, Shininess), 1.0f);
    }
#endif

    return output;
}

float4 mainPS(PS_INPUT input) : SV_TARGET
{
    float4 texColor = g_txColor.Sample(g_Sample, input.uv + UVOffset);
    float4 albedo = texColor * BaseColor;

    // 보간되면 길이가 틀어지므로 다시 정규화한다
    float3 lighting = AmbientColor;
    
#if LIGHTING_MODEL_GOURAUD
    lighting += input.lighting;
#elif LIGHTING_MODEL_LAMBERT || LIGHTING_MODEL_PHONG
    float3 ViewDirection = normalize(CameraPosition - input.WorldPosition);

    uint X = (uint) floor((input.position.x / ViewSizeX) * TileCountX);
    uint Y = (uint) floor((input.position.y / ViewSizeY) * TileCountY);
    X = min(X, TileCountX - 1);
    Y = min(Y, TileCountY - 1);

    FTileLightHeader Header = TileHeaders[Y * TileCountX + X];
    for (uint i = 0; i < Header.Count; ++i)
    {
        uint LightIndex = TileLightIndices[Header.Offset + i];
        FLightData Light = Lights[LightIndex];
        lighting += EvaluatePhongShading(Light, input.WorldPosition, input.normal, ViewDirection, albedo.rgb, SpecularColor, Shininess);
    }
#else
    
#endif


    
    float3 FinalColor = albedo.rgb * lighting;

    // Opaque는 알파를 1로 고정한다. 뷰포트 RT를 ImGui가 알파 블렌딩으로 그리므로 알파가 남으면 비쳐 보인다
    float alpha = bOpaque > 0.5f ? 1.0f : albedo.a;
    return float4(FinalColor, alpha);
}