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
    float3 normalSample = g_txNormal.Sample(g_Sample, uv).rgb;


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


    // ============================================================
    // 6. 화면 공간에서 월드 위치의 변화량 계산
    // ============================================================

    // ddx()는 화면의 가로 방향으로 인접한 픽셀 사이의 값 변화를
    // 추정하는 픽셀 셰이더 함수다.
    //
    // input.WorldPosition은 각 픽셀의 월드 공간 위치이므로
    // dpdx는 화면 가로 방향으로 월드 위치가 얼마나 변하는지 나타낸다.
    //
    // 이 값은 월드 X축 방향이라는 뜻이 아니다.
    // 카메라 방향과 표면의 기울기에 따라 결과 방향이 달라진다.
    //
    // 이 변화량을 이용해 화면 좌표와 UV 좌표 사이의 관계를 파악하고
    // UV의 U, V 방향에 해당하는 월드 공간의 접선 벡터를 계산한다.
    float3 dpdx = ddx(input.WorldPosition);


    // ddy()는 화면의 세로 방향으로 인접한 픽셀 사이의 값 변화를
    // 추정한다.
    //
    // dpdy는 화면 세로 방향으로 이동했을 때의 월드 위치 변화량이다.
    //
    // dpdx와 dpdy를 함께 사용하면 표면이 화면상에서 어떻게 보이는지
    // 바탕으로 월드 공간의 접선 방향을 복원할 수 있다.
    float3 dpdy = ddy(input.WorldPosition);


    // ============================================================
    // 7. 화면 공간에서 UV 좌표의 변화량 계산
    // ============================================================

    // 화면의 가로 방향으로 인접한 픽셀 사이에서 UV가 얼마나
    // 변하는지 계산한다.
    //
    // duvdx.x : 화면 가로 방향의 U 좌표 변화량
    // duvdx.y : 화면 가로 방향의 V 좌표 변화량
    //
    // 이 값은 dpdx와 대응하는 UV 변화량이다.
    // 월드 위치의 변화량과 UV 변화량을 함께 사용해야
    // 텍스처 방향을 월드 공간의 방향으로 변환할 수 있다.
    //
    // uv는 노멀 맵을 샘플링할 때 사용한 좌표와 일치해야 한다.
    // 타일링이나 UV 변환을 적용했다면 해당 좌표계도 일치시켜야 한다.
    float2 duvdx = ddx(uv);


    // 화면의 세로 방향으로 인접한 픽셀 사이에서 UV가 얼마나
    // 변하는지 계산한다.
    //
    // duvdy.x : 화면 세로 방향의 U 좌표 변화량
    // duvdy.y : 화면 세로 방향의 V 좌표 변화량
    //
    // duvdx와 duvdy가 있어야 화면에서 관찰한 변화량을
    // U와 V 방향으로 분리할 수 있다.
    float2 duvdy = ddy(uv);


    // ============================================================
    // 8. UV 변화량으로 행렬식 계산
    // ============================================================

    // UV 변화량을 구성하는 2x2 행렬의 행렬식을 계산하기 위해
    // 먼저 두 곱셈 항을 분리한다.
    //
    // determinantTerm0 = du/dx * dv/dy
    // determinantTerm1 = dv/dx * du/dy
    //
    // 각 항은 화면 가로/세로 변화량에 포함된 UV 변화 관계를
    // 나타내는 값이다.
    float determinantTerm0 = duvdx.x * duvdy.y;
    float determinantTerm1 = duvdx.y * duvdy.x;


    // 행렬식은 다음과 같다.
    //
    // D = (du/dx * dv/dy) - (dv/dx * du/dy)
    //
    // 이 행렬식은 화면 공간의 UV 변화량으로부터
    // U, V 방향의 공간 변화량을 역으로 계산할 때 필요하다.
    //
    // D가 0이면 UV 변화량 행렬의 역행렬을 구할 수 없다.
    // D가 0에 매우 가까워도 역수를 계산할 때 값이 지나치게 커져
    // TBN 벡터가 불안정해질 수 있다.
    float determinant = determinantTerm0 - determinantTerm1;


    // ============================================================
    // 9. 행렬식의 퇴화 및 수치적 불안정성 검사
    // ============================================================

    // 행렬식을 구성하는 두 곱셈 항의 절댓값을 더한다.
    //
    // 이 값은 행렬식에 관여하는 항들이 어느 정도의 크기를
    // 가지고 있는지 보여준다.
    //
    // 행렬식은 두 항의 차이이므로 두 항이 매우 비슷하면
    // 서로 상쇄되어 행렬식이 매우 작아질 수 있다.
    //
    // 그때 행렬식 자체의 크기뿐 아니라 각 항의 크기도 확인하면
    // 단순한 스케일 변화와 심한 상쇄를 구분하는 데 도움이 된다.
    float determinantScale = abs(determinantTerm0) + abs(determinantTerm1);


    // 다음 두 조건을 확인한다.
    //
    // 조건 1:
    // determinantScale이 극단적으로 작으면 UV 변화량이 사실상
    // 없거나 수치적으로 의미 있는 행렬식을 구하기 어려울 수 있다.
    //
    // 조건 2:
    // determinant의 절댓값이 항들의 전체 크기에 비해 지나치게 작으면
    // 두 항이 강하게 상쇄되어 역수 계산이 불안정할 수 있다.
    //
    // 어느 조건이든 만족하면 역행렬 계산을 중단하고 기본 노멀 N을
    // 반환한다. 이렇게 하면 불안정한 TBN으로 인해 잘못된 노멀이나
    // 극단적인 값이 발생하는 것을 방지할 수 있다.
    //
    // 주의:
    // 이 임계값들은 모든 메시와 UV 배치에 맞는 절대적인 기준은 아니다.
    // 표면이 실제로 퇴화하지 않았는데도 이 조건에 걸린다면
    // 임계값과 UV 변화량을 디버깅해야 한다.
    if (determinantScale < 1e-20f || abs(determinant) < determinantScale * 1e-6f)
    {
        return N;
    }


    // 행렬식의 역수를 계산한다.
    //
    // 뒤에서 행렬식으로 나누는 연산을 수행하는 대신
    // 미리 역수를 구한 뒤 곱셈으로 계산한다.
    //
    // 앞에서 행렬식이 지나치게 작거나 불안정한 경우를 검사했으므로
    // 정상적인 경우에만 이 연산을 수행한다.
    float inverseDeterminant = 1.0f / determinant;


    // ============================================================
    // 10. Tangent와 Bitangent 계산
    // ============================================================

    // Tangent는 텍스처의 U 좌표가 증가하는 방향에 대응하는
    // 월드 공간의 접선 벡터다.
    //
    // dpdx, dpdy는 화면 공간에서의 월드 위치 변화량이고,
    // duvdx, duvdy는 같은 방향에서의 UV 변화량이다.
    //
    // 이 값들을 조합하고 행렬식의 역수를 곱하면
    // 화면 공간의 변화량을 U 방향의 월드 공간 벡터로 변환할 수 있다.
    //
    // 이 계산이 필요한 이유는 노멀 맵의 X 성분이 월드 X축을 뜻하지
    // 않기 때문이다. 노멀 맵의 X 성분은 Tangent 방향을 기준으로
    // 해석해야 하므로 실제 표면의 U 방향을 구해야 한다.
    float3 T = (dpdx * duvdy.y - dpdy * duvdx.y) * inverseDeterminant;


    // Bitangent는 텍스처의 V 좌표가 증가하는 방향에 대응하는
    // 월드 공간의 접선 벡터다.
    //
    // Tangent와 같은 원리로 화면 공간의 변화량을 조합하되
    // U 좌표의 변화량을 이용해 V 방향을 계산한다.
    //
    // T와 B를 구하면 노멀 맵이 표현하는 X, Y 방향을
    // 실제 표면 위의 두 방향으로 해석할 수 있다.
    float3 B = (dpdy * duvdx.x - dpdx * duvdy.x) * inverseDeterminant;


    // ============================================================
    // 11. Tangent 직교화 및 정규화
    // ============================================================

    // 미분을 사용해 계산한 T는 수치 오차나 보간 때문에
    // N에 완전히 수직이지 않을 수 있다.
    //
    // dot(N, T)는 T의 N 방향 성분을 구한다.
    //
    // N * dot(N, T)는 T에 포함된 노멀 방향 성분을 나타낸다.
    // 이 성분을 T에서 빼면 N과 평행한 부분이 제거된다.
    //
    // 이 연산은 Gram-Schmidt 직교화의 한 단계에 해당한다.
    // 이를 통해 T가 표면에 더 잘 맞도록 보정한다.
    //
    // 마지막 normalize()는 T의 길이를 1로 맞춘다.
    // 단, N은 이미 단위 벡터여야 한다.
    T = normalize(T - N * dot(N, T));


    // ============================================================
    // 12. Bitangent의 방향과 좌우 반전 보정
    // ============================================================

    // 앞에서 계산한 B에는 UV의 방향이나 미러링에 따라
    // 표준적인 외적 방향과 반대인 방향이 포함될 수 있다.
    //
    // cross(N, T)는 N과 T 모두에 수직인 방향을 만든다.
    // 하지만 이것만으로는 원래 계산한 B와 방향이 일치하는지
    // 보장할 수 없다.
    //
    // dot(cross(N, T), B)의 부호를 비교하면 두 벡터의 방향 관계를
    // 확인할 수 있다.
    //
    // 내적이 음수라면 반대 방향이므로 -1,
    // 그렇지 않다면 같은 방향이므로 +1을 사용한다.
    //
    // 이 부호를 handedness라고 부르며 UV 미러링 등의 방향 정보를
    // 가능한 한 보존하기 위해 사용한다.
    float handedness = dot(cross(N, T), B) < 0.0f ? -1.0f : 1.0f;


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
    B = normalize(cross(N, T)) * handedness;


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
    
    float3 mappedNormal = GetNormalFromMap(input, input.normal, input.uv);
    
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