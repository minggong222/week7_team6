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
    float4 T : TANGENT;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
    float3 lighting : COLOR1; // For Lambert Shading
    
    float3 WorldPosition : TEXCOORD1;
    
    float4 Tangent : Tangent;
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
    float bDisableShading;
    float NormalGreenSign;
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
                // PointLight에서 Vertex와 Light의 Position이 겹치면 Division by zero가 발생하게 되므로 직접 지정
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
    
    float3 Diffuse = float3(1.0f, 1.0f, 1.0f) * NdotL;

#if LIGHTING_MODEL_LAMBERT
    float3 Specular = float3(0.0f, 0.0f, 0.0f);
#else

    float3 SpecularFactor = pow(NdotH, Data.MaterialShininess);
    float3 Specular = Data.MaterialSpecularColor * SpecularFactor * pow(NdotL, 0.7f);

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

float3 GetNormalFromMap(PS_INPUT input, float3 baseNormal, float2 uv)
{
    // ============================================================
    // 1. 기본 노멀 준비
    // ============================================================

    // baseNormal은 노멀 매핑을 적용하기 전의 표면 노멀이다.
    //
    // 노멀 벡터의 길이가 1이 아닐 수 있으므로 normalize()를
    // 사용해 방향은 유지하면서 길이를 1로 만든다.
    //
    // 노멀을 단위 벡터로 만들어야 이후 내적, 외적, 조명 계산을
    // 일관된 기준으로 수행할 수 있다.
    //
    // 주의:
    // 이 함수는 baseNormal과 input.WorldPosition이 모두 월드 공간에
    // 있다는 전제로 작성되었다. 서로 다른 공간의 벡터를 사용하면
    // TBN 계산과 최종 노멀 변환이 올바르게 동작하지 않는다.
    float3 N = normalize(baseNormal);


    // ============================================================
    // 2. 노멀 맵 사용 여부 확인
    // ============================================================

    // 머티리얼에 노멀 맵이 지정되지 않았다면 추가 계산을 수행할
    // 필요가 없으므로 기본 노멀을 반환한다.
    //
    // 이 조건을 사용하면 노멀 맵을 사용하지 않는 머티리얼에서는
    // 텍스처 샘플링, 화면 공간 미분, TBN 계산을 생략할 수 있다.
    //
    // bHasNormalMap은 CPU에서 머티리얼 정보를 설정하고
    // 상수 버퍼를 통해 셰이더에 전달하는 값이다.
    if (bHasNormalMap == 0)
    {
        return N;
    }


    // ============================================================
    // 3. 노멀 맵 샘플링
    // ============================================================

    // 현재 픽셀의 UV 좌표를 사용해 노멀 맵의 RGB 값을 가져온다.
    //
    // g_txNormal : 노멀 맵 텍스처 리소스
    // g_Sample   : 텍스처 샘플링 방식과 주소 지정 방식을 결정하는 샘플러
    // uv         : 현재 픽셀에서 사용할 텍스처 좌표
    // .rgb       : 샘플링한 색상에서 R, G, B 채널만 추출
    //
    // 일반적인 RGB 노멀 맵은 색상 자체를 표현하는 것이 아니라
    // 각 픽셀의 표면 방향을 RGB 값으로 인코딩한다.
    //
    // 따라서 이 텍스처는 일반적인 색상 텍스처처럼 sRGB 색상 변환을
    // 적용하지 않고 선형 데이터로 샘플링해야 한다.
    //
    // 여기서는 Tangent Space Normal Map을 입력으로 가정한다.
#if LIGHTING_MODEL_GOURAUD
    float3 normalSample = g_txNormal.SampleLevel(g_Sample, uv, 0);
#else
    float3 normalSample = g_txNormal.Sample(g_Sample, uv).rgb;
#endif


    // ============================================================
    // 4. RGB 색상값을 노멀 벡터로 변환
    // ============================================================

    // 텍스처의 각 RGB 채널은 일반적으로 0~1 범위에 저장된다.
    // 하지만 노멀 벡터의 각 성분은 -1~1 범위로 표현해야 한다.
    //
    // 따라서 다음 변환을 수행한다.
    //
    //   -1 = 0 * 2 - 1
    //    0 = 0.5 * 2 - 1
    //   +1 = 1 * 2 - 1
    //
    // 예를 들어 RGB = (0.5, 0.5, 1.0)이라면
    // 변환 결과는 (0.0, 0.0, 1.0)이 된다.
    //
    // 이는 Tangent Space의 +Z 방향을 가리키는 노멀이다.
    // 즉, 접선 공간에서 평평한 표면을 나타내는 대표적인 값이다.
    //
    // 주의:
    // 높이 맵은 밝기를 높이로 해석하지만, 노멀 맵은 RGB 자체가
    // 방향 정보이므로 높이 차이를 계산할 필요가 없다.
    float3 tangentNormal = normalSample * 2.0f - 1.0f;


    // ============================================================
    // 5. 노멀 맵 강도 조절
    // ============================================================

    // NormalScale을 사용해 접선 공간 노멀의 X, Y 성분을 조절한다.
    //
    // X, Y 성분은 표면을 따라가는 접선 방향의 기울기와 관련이 있다.
    // 두 성분을 조절하면 표면의 요철이 조명에 미치는 영향을
    // 더 약하게 또는 더 강하게 만들 수 있다.
    //
    // Z 성분은 직접 스케일링하지 않는다.
    // 따라서 NormalScale을 0으로 설정하면 X, Y 성분이 0이 되고
    // 정규화 이후 노멀은 대략 (0, 0, 1)이 된다.
    //
    // 주의:
    // 강도를 크게 설정하면 노멀 방향이 크게 기울어질 수 있다.
    // 일반적으로 적절한 범위에서 조절하는 것이 좋다.
    tangentNormal.xy *= NormalScale;


    // X, Y 성분을 조절한 결과 벡터의 길이가 1이라는 보장은 없다.
    //
    // normalize()를 적용해 단위 벡터로 다시 만든다.
    // 이 과정을 생략하면 이후 TBN 변환 결과의 길이가 불필요하게
    // 달라져 조명 계산에 영향을 줄 수 있다.
    tangentNormal = normalize(tangentNormal);


    // N과 T의 외적을 이용해 B를 다시 구성한다.
    //
    // cross(N, T)는 N과 T에 수직인 방향이므로
    // TBN의 직교성을 확보하는 데 도움이 된다.
    //
    // normalize()로 길이를 1로 맞추고 handedness를 곱해
    // 원래 UV 방향에 따른 부호를 반영한다.
    //
    // 결과적으로 N, T, B를 노멀 맵의 좌표계를 월드 공간으로
    // 변환하기 위한 기저 벡터로 사용할 수 있다.
    float3 T = input.Tangent.xyz;
    
    if (dot(T, T) < 1e-8f)
    {
        return N;
    }
    
    T = normalize(T - N * dot(N, T));
    
    float3 B = normalize(cross(N, T)) * input.Tangent.w;
    
    
    // 노멀맵의 G 채널 규약 보정. +1이면 그대로(DirectX 방식), −1이면 반전(OpenGL 방식)
    
    B = B * NormalGreenSign;

    // ============================================================
    // 13. Tangent Space 노멀을 월드 공간으로 변환
    // ============================================================

    // tangentNormal은 노멀 맵에서 읽은 벡터다.
    //
    // 이 벡터의 X, Y, Z는 월드 X, Y, Z축 기준이 아니라
    // 각각 T, B, N 방향을 기준으로 한 성분이다.
    //
    // 따라서 각 성분을 해당 방향 벡터에 곱해 더하면
    // 월드 공간에서의 노멀 벡터를 얻을 수 있다.
    //
    // T * tangentNormal.x :
    //     노멀 맵 X 성분을 실제 표면의 U 방향에 반영한다.
    //
    // B * tangentNormal.y :
    //     노멀 맵 Y 성분을 실제 표면의 V 방향에 반영한다.
    //
    // N * tangentNormal.z :
    //     노멀 맵 Z 성분을 기존 표면 노멀 방향에 반영한다.
    //
    // 이 과정은 TBN 행렬을 사용한 좌표계 변환과 같은 원리다.
    //
    // 이 함수에서는 T, B, N이 월드 공간 벡터이므로
    // 결과 worldNormal도 월드 공간 벡터가 된다.
    float3 worldNormal = T * tangentNormal.x + B * tangentNormal.y + N * tangentNormal.z;


    // ============================================================
    // 14. 최종 노멀 정규화 및 반환
    // ============================================================

    // TBN 벡터들의 계산 오차로 인해 worldNormal의 길이가
    // 정확히 1이 아닐 수 있으므로 마지막으로 정규화한다.
    //
    // 결과는 월드 공간의 단위 노멀이다.
    //
    // 이후 Diffuse, Specular 등의 조명 계산에 이 노멀을 사용하면
    // 메시의 실제 정점이나 삼각형을 늘리지 않고도
    // 노멀 맵에 저장된 미세한 표면 방향 차이를 조명에 반영할 수 있다.
    return normalize(worldNormal);
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
    output.Tangent.xyz = mul(float4(input.T.xyz, 0.0f), World).xyz;
    output.Tangent.w = input.T.w;
    if (bDisableShading == 1.0f)
    {
        return output;
    }

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
            GetNormalFromMap(output, output.normal, output.uv),
            float3(1.0f, 1.0f, 1.0f),  
            float3(1.0f, 1.0f, 1.0f),  
            16.0f,
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

    float3 lighting = float3(1.0f, 1.0f, 1.0f);
    
    if (bDisableShading != 1.0f)
    {
        float3 mappedNormal = GetNormalFromMap(input, input.normal, input.uv);
    
#if NormalMap
        return float4(mappedNormal * 0.5 + 0.5, 1.0f);
#endif
    
#if LIGHTING_MODEL_GOURAUD
        lighting = input.lighting;
#elif LIGHTING_MODEL_LAMBERT || LIGHTING_MODEL_PHONG
        lighting = AmbientColor;
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
                /*Specular*/ float3(1.0f, 1.0f, 1.0f),  
                /*Shininess*/ 16.0f,
            };

            lighting += EvaluateShading(Data);
        }
#else
    
#endif
    }

    float3 FinalColor = albedo.rgb * lighting;

    // Opaque는 알파를 1로 고정한다. 뷰포트 RT를 ImGui가 알파 블렌딩으로 그리므로 알파가 남으면 비쳐 보인다
    float alpha = bOpaque > 0.5f ? 1.0f : albedo.a;
    return float4(FinalColor, alpha);
}