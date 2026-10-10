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

#define     LightType_Directional 0
#define	    LightType_Point 1
#define	    LightType_Spot 2
#define	    LightType_Rect 3
#define	    LightType_Ambient 4 // 언리얼엔 미존재
#define	    LightType_MAX 5


// 테스트
#define	    LIGHTING_MODEL_GOURAUD 0
#define	    LIGHTING_MODEL_LAMBERT 0
#define	    LIGHTING_MODEL_PHONG 1

#define TileCountX 8
#define TileCountY 8

struct FLightData
{
    uint LightType;
    float3 Position;
    // ...
    // Undefined
    // 각자 구현하시고 여기에 합치면 될 것 같습니다.

    // Ambient Light
    float4 Amb_Color;
    float Amb_Intensity;
    float3 Amb_Padding;

    // Directional Light
    float3 Dir_Direction;
    float4 Dir_Color;
    float Dir_Intensity;
    float3 Dir_Padding;
    
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
    float3 worldPosition : POSITION0;
    float3 normal : NORMAL;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
    float3 lighting : COLOR1; // For Lambert Shading
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

    // Normal에 MVP 행렬을 그냥 곱하게 되면, Normal의 Scale 변환시 Normal이 이상하게 찌그러지는 문제가 발생한다
    // 그래서 Normal은 MVP 행렬의 역행렬의 전치행렬을 사용한다.
    // TODO: GPU 연산량을 줄이기 위해서 Normal 행렬 역시 CPU에서 딱 한번만 계산할 수도 있다.
    //matrix Normal = matrix(transpose(inverse(World)));

    // 월드 변환 후 카메라 투영 변환
    float4 worldPos = mul(float4(input.p, 1.0f), World);
    output.position = mul(worldPos, ViewProjection);
    output.worldPosition = worldPos.xyz;
    output.normal = mul(float4(input.n, 0.0f), Normal).xyz;
    output.color = input.c; 
    output.uv = input.t;
    output.lighting = float3(1.0f, 1.0f, 1.0f);

#if LIGHTING_MODEL_GOURAUD
    // Gouraud 쉐이딩. 라이팅을 Vertex Shader 단계에서 처리하고 래스터라이저로 보간하여 적용.

    // 큰 흐름은 Blinn-phong 쉐이딩과 동일
    
    float3 V = normalize(CameraPosition - output.worldPosition);
    float3 N = normalize(output.normal);
    float3 L = normalize(-Lights[0].Dir_Direction);
    float3 H = normalize(V + L);
    float3 lighting = float3(1.0f, 1.0f, 1.0f);

    // Ambient Light
    float3 Dir_Ambient = Lights[0].Amb_Intensity * Lights[0].Amb_Color.xyz;
    
    // Directional Light
    float Dir_NdotL = saturate(dot(N, L));
    float3 Dir_Diffuse = (Lights[0].Dir_Color * Dir_NdotL).xyz;
    float Dir_NDotH = saturate(dot(N, H));
    float3 Dir_Specular = pow(Dir_NDotH, /*Shininess*/ 16.0f);

    float3 Dir_Lighting = Dir_Ambient + Dir_Diffuse + Dir_Specular;


    // float3 N = normalize(output.normal);
    // float3 L = normalize(-Lights[0].Dir_Direction);
    
    // // Ambient Light
    // float3 Dir_Ambient = Lights[0].Amb_Intensity * Lights[0].Amb_Color.xyz;

    // // Directional Light
    // float Dir_NdotL = saturate(dot(N, L));
    // float3 Dir_Diffuse = Lights[0].Dir_Intensity * (Lights[0].Dir_Color * Dir_NdotL).xyz;

    // float3 Dir_Lighting = Dir_Ambient + Dir_Diffuse;

    output.lighting = Dir_Lighting * Lights[0].Dir_Intensity;

#endif

    return output;
}

float4 mainPS(PS_INPUT input) : SV_TARGET
{
    float4 texColor = g_txColor.Sample(g_Sample, input.uv + UVOffset);
    float4 albedo = texColor * BaseColor;

#if LIGHTING_MODEL_GOURAUD
    float3 lighting = input.lighting;
#endif

#if LIGHTING_MODEL_LAMBERT
    // Lambert 쉐이딩. 라이팅을 Pixel Shader 단계에서 처리. Diffuse Only. 따라서 Ambient Light는 없음

    float3 N = normalize(input.normal);
    float3 L = normalize(-Lights[0].Dir_Direction);
    float3 lighting = float3(1.0f, 1.0f, 1.0f);

    // Ambient Light
    float3 Dir_Ambient = Lights[0].Amb_Intensity * Lights[0].Amb_Color.xyz;
    
    // Directional Light
    float Dir_NdotL = saturate(dot(N, L));
    float3 Dir_Diffuse = (Lights[0].Dir_Color * Dir_NdotL).xyz;

    float3 Dir_Lighting = Dir_Ambient + Dir_Diffuse;

    lighting = Dir_Lighting * Lights[0].Dir_Intensity;
#endif


#if LIGHTING_MODEL_PHONG
    // Blinn-phong 쉐이딩. 라이팅을 Pixel Shader 단계에서 처리. Ambient, Diffuse, Specular.

    float3 V = normalize(CameraPosition - input.worldPosition);
    float3 N = normalize(input.normal);
    float3 L = normalize(-Lights[0].Dir_Direction);
    float3 H = normalize(V + L);
    float3 lighting = float3(1.0f, 1.0f, 1.0f);

    // Ambient Light
    float3 Dir_Ambient = Lights[0].Amb_Intensity * Lights[0].Amb_Color.xyz;
    
    // Directional Light
    float Dir_NdotL = saturate(dot(N, L));
    float3 Dir_Diffuse = (Lights[0].Dir_Color * Dir_NdotL).xyz;
    float Dir_NDotH = saturate(dot(N, H));
    float3 Dir_Specular = pow(Dir_NDotH, /*Shininess*/ 16.0f);

    float3 Dir_Lighting = Dir_Ambient + Dir_Diffuse + Dir_Specular;

    lighting = Dir_Lighting * Lights[0].Dir_Intensity;
#endif

    // Opaque는 알파를 1로 고정한다. 뷰포트 RT를 ImGui가 알파 블렌딩으로 그리므로 알파가 남으면 비쳐 보인다
    float alpha = bOpaque > 0.5f ? 1.0f : albedo.a;
    return float4(albedo.rgb * lighting, alpha);
}