#pragma once

#include "RenderPacket.h"
#include "RHI/RHIBuffer.h"
#include "Engine/Texture2D.h"
#include "Engine/Font.h"


#include "RenderingInfo.h"

struct FPerObjectConstants
{
	FMatrix World;
};

static_assert(sizeof(FPerObjectConstants) == sizeof(FMatrix));

struct FLightingGlobals // Constant
{
	FVector4 AmbientRadiance;
};
static_assert(sizeof(FLightingGlobals) % 16 == 0);

#define TileCountX 8
#define TileCountY 8

struct FLightData // StructuredBuffer
{
	uint32 LightType;
	FVector Position;

	//Spot Light
	FVector Direction;
	float Intensity;

	FVector Color;
	float Radius;

	float InnerConeCos;
	float OuterConeCos;
	float AttenuationLinear;
	float AttenuationQuadratic;


	// ...
	// Undefined
	// 각자 구현하시고 여기에 합치면 될 것 같습니다.
};

struct FTileLightHeader // StructuredBuffer
{
	uint32 Offset;
	uint32 Count;
};

class UCameraComponent;

class FRenderer
{
	friend class UTexture2D;
public:

	bool Init();

	// 기존 단일 카메라의 ViewProjection으로 렌더 패킷 배열 전체를 그린다.
	void RenderAll(TArray<FRenderPacket>& InPackets, UCameraComponent* CameraComponent);

	// Adapter가 계산한 ViewProjection을 직접 받아 View별 렌더 패킷 배열을 그린다.
	void RenderAll(TArray<FRenderPacket>& InPackets, const FMatrix& ViewProjection);

	// 배열을 정렬해 불투명 패킷만 그린다. 반투명은 RenderTranslucent 호출 전까지 보관한다.
	// 연속 배열 기반 불투명 패킷 고속 렌더링
	void RenderOpaque(TArray<FRenderPacket>& InPackets, const FMatrix& ViewProjection, bool bWireframe = false);

	// RenderOpaque가 보관한 반투명 패킷을 먼 것부터 그린다.
	void RenderTranslucent(const FMatrix& ViewProjection);

	void SetWorldLights(const TArray<FLightData>& InLights);

  private:
	// 정렬된 RenderPackets에서 반투명 패킷이 시작되는 위치
	uint32 FirstTranslucentIndex = 0;
	TUniquePtr<FRHIUniformBuffer> CB;
	TUniquePtr<FRHIUniformBuffer> Temp;
	TUniquePtr<FRHIUniformBuffer> ViewCB;
	TUniquePtr<FRHIUniformBuffer> FLightingGlobalsCB;

	D3D11_VIEWPORT ViewportInfo;

	uint32 Width;
	uint32 Height;

	FLOAT ClearColor[4] = { 0.3f, 0.3f, 0.3f, 1.0f };

	static constexpr uint32 PerObjectSlotSize = 256;
	static constexpr uint32 InitialPacketCapacity = 50000;

	// 워커 전용 지연 컨텍스트와 상수 버퍼
	struct FDeferredWorker
	{
		ComPtr<ID3D11DeviceContext> Context;
		ComPtr<ID3D11DeviceContext1> Context1;
		TUniquePtr<FRHIUniformBuffer> PerObjectCB;
	};
	TArray<FDeferredWorker> DeferredWorkers;
	bool bDeferredWorkersInitialized = false;

	void EnsureDeferredWorkers();
	bool EnsureConstantBufferCapacity(TUniquePtr<FRHIUniformBuffer>& Buffer, uint32 PacketCount);
	void DrawPackets(uint32 Begin, uint32 End, const FMatrix& ViewProjection);
	void BindMaterial(UMaterial* material, ID3D11DeviceContext* Context = nullptr, bool bBindPipelineState = true, bool bWireframe = false);
	void UpdateMaterialParams(UMaterial* material);
	void UpdatePerObjectConstants(const FRenderPacket& RenderPacket, const FMatrixRegister& ViewProjection);

	bool UpdateWorldLightResources();

	TArray<FLightData> WorldLights;

	ComPtr<ID3D11Buffer> LightsBuffer;
	ComPtr<ID3D11ShaderResourceView> LightsSRV;

	uint32 LightsBufferCapacity = 0;

	TUniquePtr<FRHIUniformBuffer> LightCountCB;
};
