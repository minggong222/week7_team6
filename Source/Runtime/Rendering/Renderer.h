#pragma once
#include <cstddef>

#include "RenderPacket.h"
#include "RHI/RHIBuffer.h"
#include "Engine/Texture2D.h"
#include "Engine/Font.h"

#include "RenderingInfo.h"
#include "RenderView.h"
#include "Math/Box.h"
#include "Math\Sphere.h"
#include "Rendering/RenderResourceManager.h"

struct FScene;

struct FViewConstants
{
	FMatrix ViewProjection;
	FVector CameraPosition;
	float ViewSizeX;
	float ViewSizeY; // 현재 렌더타겟의 Width, Height
	uint32 bIsPerspective = 1;
	float Padding_noUse[2]{};
};
static_assert(sizeof(FViewConstants) == 96);
static_assert(offsetof(FViewConstants, CameraPosition) == 64);
static_assert(offsetof(FViewConstants, ViewSizeX) == 76);
static_assert(offsetof(FViewConstants, ViewSizeY) == 80);
static_assert(offsetof(FViewConstants, bIsPerspective) == 84);

struct alignas(16) FPerObjectConstants
{
	FMatrix World;
	FMatrix Normal;
};

struct FLightingGlobals // Constant
{
	FVector4 AmbientRadiance{1.0f, 1.0f, 1.0f, 0.0f};
	uint32 LightCount = 0;
	uint32 DirectionalLightCount = 0;
	uint32 LightTreeRootIndex = 0;
	uint32 LightingGlobalsPadding = 0;
};
static_assert(sizeof(FLightingGlobals) == 32);
static_assert(offsetof(FLightingGlobals, LightCount) == 16);
static_assert(offsetof(FLightingGlobals, DirectionalLightCount) == 20);
static_assert(offsetof(FLightingGlobals, LightTreeRootIndex) == 24);

enum class ELightClusterDebugView : uint32 { None, LightCount, DepthSlice, Page, Validity };

struct FLightCullingDebugSettings
{
	bool bEnabled = true;
	ELightClusterDebugView View = ELightClusterDebugView::None;
	uint32 LightCountMax = 64;
};

struct FLightClusterGlobals // Constant
{
	uint32 ClusterCountX = 0;
	uint32 ClusterCountY = 0;
	uint32 ClusterCountZ = 0;
	float ClusterZScale = 0.0f;
	float ClusterZBias = 0.0f;
	uint32 ClusterDebugMode = 0;
	uint32 DebugLightCountMax = 64;
	uint32 bEnableLightCulling = 1;
};
static_assert(sizeof(FLightClusterGlobals) == 32);
static_assert(offsetof(FLightClusterGlobals, ClusterZScale) == 12);
static_assert(offsetof(FLightClusterGlobals, ClusterZBias) == 16);

struct FLightData // StructuredBuffer
{
	uint32 LightType;
	FVector Position;

	//Spot Light
	FVector Direction;
	float Intensity;

	FVector Color;
	float Radius;

	float LightFalloffExponent;
	float InnerConeCos;
	float OuterConeCos;
	float Padding;
};
static_assert(sizeof(FLightData) == 64);

inline constexpr uint32 MaxChildCount = 32; // same as threadgroup size
struct FLightTreeNode
{
	struct FSphereInternal
	{
		FVector Center;
		float Radius;
		uint32 DataIndex;
		uint32 Padding;
	};
	union
	{
		FBox AABB{};
		FSphereInternal LeafSphere;
	};
	uint32 ChildIndex = 0; // std::numeric_limits<uint32>::max(): invalid. 라이트가 0개일 때 루트에 설정
	uint32 ChildCount = 0; // 0: leaf
};
static_assert(sizeof(FLightTreeNode) == 32);
static_assert(offsetof(FLightTreeNode, ChildIndex) == 24);

struct FLightCluster // StructuredBuffer
{
	uint32 Offset;
	uint32 Count;
	uint32 ClusterKey;
};
static_assert(sizeof(FLightCluster) == 12);

class UCameraComponent;

class FRenderer
{
	friend class UTexture2D;
public:

	bool Init();
	void BuildLightDatas(const FScene& Scene);

	// 기존 단일 카메라의 ViewProjection으로 렌더 패킷 배열 전체를 그린다.
	void RenderAll(TArray<FRenderPacket>& InPackets, UCameraComponent* CameraComponent, uint32 ViewWidth, uint32 ViewHeight, const FScene& Scene);

	// Adapter가 계산한 ViewProjection을 직접 받아 View별 렌더 패킷 배열을 그린다.
	void RenderAll(TArray<FRenderPacket>& InPackets, const FRenderViewData& RenderView, const FScene& Scene);

	// 배열을 정렬해 불투명 패킷만 그린다. 반투명은 RenderTranslucent 호출 전까지 보관한다.
	// 연속 배열 기반 불투명 패킷 고속 렌더링
	void RenderOpaque(TArray<FRenderPacket>& InPackets,
		const FRenderViewData& RenderView,
		const FScene& Scene,
		bool bWireframe = false,
		ELightingModel LightingModel = ELightingModel::Unlit);

	// RenderOpaque가 보관한 반투명 패킷을 먼 것부터 그린다.
	void RenderTranslucent(const FRenderViewData& RenderView);
	FRHITexture2D* GetViewDepthTexture() const { return LightClusters.bViewDepthValid ? LightClusters.ViewDepthTexture.get() : nullptr; }
	void SetLightCullingDebugSettings(const FLightCullingDebugSettings& Settings) { LightCullingDebugSettings = Settings; }
  private:
	FLightCullingDebugSettings LightCullingDebugSettings;
	// 정렬된 RenderPackets에서 반투명 패킷이 시작되는 위치
	uint32 FirstTranslucentIndex = 0;
	TUniquePtr<FRHIUniformBuffer> CB;
	TUniquePtr<FRHIUniformBuffer> Temp;
	TUniquePtr<FRHIUniformBuffer> ViewCB;

	struct
	{
		TUniquePtr<FRHIUniformBuffer> LightingGlobalsCB;
		TUniquePtr<FRHIStructuredBuffer> LightDataSB;
		TUniquePtr<FRHIStructuredBuffer> DirectionalLightIndicesSB; // value: uint32
		TUniquePtr<FRHIStructuredBuffer> LightTreeSB;
	} LightBuffers;

	struct FLightClusterResources
	{
		static constexpr uint32 LightIndicesCapacity = 1 << 24;
		static constexpr uint32 PixelLengthPerTile = 64;
		static constexpr uint32 ZClusterCountPerPhysicalPage = 8;
		static constexpr uint32 MarkPagesThreadLength = 16;
		static constexpr uint32 AllocatePagesThreadLength = 64;
		TUniquePtr<FRHIComputeShader> BuildCS[3];
		TUniquePtr<FRHIUniformBuffer> GlobalsCB;
		TUniquePtr<FRHIUniformBuffer> ViewMatrixCB;
		TUniquePtr<FRHITexture2D> ViewDepthTexture;
		TUniquePtr<FRHIStructuredBuffer> LightIndicesSB;
		TUniquePtr<FRHIStructuredBuffer> ClusterSB;
		TUniquePtr<FRHIStructuredBuffer> IndirectToLightClustersSB;
		TUniquePtr<FRHIStructuredBuffer> PageTableSB;
		TUniquePtr<FRHIBuffer> DispatchArgsBuffer;
		FLightClusterGlobals Globals{};
		bool bViewDepthValid = false;
	} LightClusters;

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
	bool DrawPacketBatch(const TArray<FRenderPacket>& InPackets, bool bViewDepthPass, bool bWireframe, ELightingModel LightingModel);
	void BindPacketPass(ID3D11DeviceContext* Context, bool bViewDepthPass);
	void BindPacketMaterial(UMaterial* Material, ID3D11DeviceContext* Context, bool bPSOChanged, bool bViewDepthPass, bool bWireframe, ELightingModel LightingModel);
	bool EnsureConstantBufferCapacity(TUniquePtr<FRHIUniformBuffer>& Buffer, uint32 PacketCount);
	void BindMaterial(UMaterial* material, ID3D11DeviceContext* Context = nullptr, bool bBindPipelineState = true, bool bWireframe = false, ELightingModel LightModel = ELightingModel::Unlit);
	void UpdateMaterialParams(UMaterial* material);
	void UpdatePerObjectConstants(const FRenderPacket& RenderPacket, const FMatrixRegister& ViewProjection);
	void BindLightingResources(ID3D11DeviceContext* Context);

	void BuildLightClusterBuffers(const FRenderViewData& RenderView);
};
