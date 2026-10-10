#pragma once
#include <cstddef>

#include "RenderPacket.h"
#include "RHI/RHIBuffer.h"
#include "Engine/Texture2D.h"
#include "Engine/Font.h"

#include "RenderingInfo.h"

struct FScene;

struct FViewConstants
{
	FMatrix ViewProjection;
	FVector CameraPosition;
	float ViewSizeX;
	float ViewSizeY; // 현재 렌더타겟의 Width, Height
	FVector Padding_noUse;
};
static_assert(sizeof(FViewConstants) == 96);
static_assert(offsetof(FViewConstants, CameraPosition) == 64);
static_assert(offsetof(FViewConstants, ViewSizeX) == 76);
static_assert(offsetof(FViewConstants, ViewSizeY) == 80);

struct alignas(16) FPerObjectConstants
{
	FMatrix World;
	FMatrix Normal;
};

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
	// ...
	// Undefined
	// 각자 구현하시고 여기에 합치면 될 것 같습니다.

    FVector4 AmbientColor;
	float AmbientIntensity;
	FVector Padding;

	// Directional Light
	FVector DirectionalDirection;
	FVector4 DirectionalColor;
	float DirectionalIntensity;
	FVector DirectionalPadding;
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
	void RenderAll(TArray<FRenderPacket>& InPackets, UCameraComponent* CameraComponent, const FViewportSettings& ViewportSettings, const FScene& Scene);

	// Adapter가 계산한 ViewProjection을 직접 받아 View별 렌더 패킷 배열을 그린다.
	void RenderAll(TArray<FRenderPacket>& InPackets, const FMatrix& ViewProjection, const FVector& CameraPosition, const FViewportSettings& ViewportSettings, const FScene& Scene);

	// 배열을 정렬해 불투명 패킷만 그린다. 반투명은 RenderTranslucent 호출 전까지 보관한다.
	// 연속 배열 기반 불투명 패킷 고속 렌더링
	void RenderOpaque(TArray<FRenderPacket>& InPackets, const FMatrix& ViewProjection, const FVector& CameraPosition, const FViewportSettings& ViewportSettings, const FScene& Scene, bool bWireframe = false); // TODO: FScene 참조 안하게

	// RenderOpaque가 보관한 반투명 패킷을 먼 것부터 그린다.
	void RenderTranslucent(const FMatrix& ViewProjection);

private:
	// 정렬된 RenderPackets에서 반투명 패킷이 시작되는 위치
	uint32 FirstTranslucentIndex = 0;
	TUniquePtr<FRHIUniformBuffer> CB;
	TUniquePtr<FRHIUniformBuffer> Temp;
	TUniquePtr<FRHIUniformBuffer> ViewCB;
	TUniquePtr<FRHIUniformBuffer> LightingGlobalsCB;
	TUniquePtr<FRHIStructuredBuffer> LightDataSB;
	TUniquePtr<FRHIStructuredBuffer> TileLightHeaderSB;
	TUniquePtr<FRHIStructuredBuffer> TileLightIndicesSB; // data structure: uint32

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
	void BindLightingResources(ID3D11DeviceContext* Context);
};
