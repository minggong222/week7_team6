#include "EnginePCH.h"
#include "Renderer.h"
#include "RHI/RHIShader.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"

#include "RenderCommand.h"
#include "RenderUtil.h"
#include "RHI/GPUProfiler.h"
#include "Stats/StatDefinitions.h"
#include "Misc/App.h"
#include "Camera/CameraComponent.h"
#include "Tasks/Tasks.h"
#include "FScene.h"

#include <algorithm>
#include <functional>
#include <limits>

namespace
{
	struct FScopedRenderTargets
	{
		ID3D11DeviceContext* Context = FRenderCommand::GetContext();
		ID3D11RenderTargetView* RTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
		ID3D11DepthStencilView* DSV = nullptr;
		D3D11_VIEWPORT Viewport{};
		UINT NumRTVs = 0;
		UINT NumViewports = 1;
		FScopedRenderTargets()
		{
			Context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, RTVs, &DSV);
			Context->RSGetViewports(&NumViewports, &Viewport);
			for (UINT Index = 0; Index < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++Index)
				if (RTVs[Index]) NumRTVs = Index + 1;
		}
		~FScopedRenderTargets()
		{
			Context->OMSetRenderTargets(NumRTVs, RTVs, DSV);
			Context->RSSetViewports(NumViewports, &Viewport);
			for (ID3D11RenderTargetView* RTV : RTVs)
				if (RTV) RTV->Release();
			if (DSV) DSV->Release();
		}
	};

	struct FBatchCounters
	{
		uint64 Draws = 0;
		uint64 Triangles = 0;
		uint64 UploadBytes = 0;
	};

	// Destruction and publication occur on the calling thread, never on workers.
	struct FBatchStats : FBatchCounters
	{
		const bool bCountDraws = FStats::IsEnabled(StatIds::DrawCalls());
		const bool bCountTriangles = FStats::IsEnabled(StatIds::Triangles());
		const bool bCountUploadBytes = FStats::IsEnabled(StatIds::CBUpload());

		bool IsCollecting() const { return bCountDraws || bCountTriangles || bCountUploadBytes; }

		~FBatchStats()
		{
			if (bCountDraws)
				FStats::Add(StatIds::DrawCalls(), static_cast<double>(Draws));
			if (bCountTriangles)
				FStats::Add(StatIds::Triangles(), static_cast<double>(Triangles));
			if (bCountUploadBytes)
				FStats::Add(StatIds::CBUpload(), static_cast<double>(UploadBytes));
		}
	};

	// 오브젝트별 상수 슬롯 = World 행렬 + (있으면) 패킷별 재질 파라미터.
	// 슬롯 전체를 b0으로 VS/PS에 함께 바인딩하므로, 셰이더는 b0의 World 뒤에서 파라미터를 읽는다.
	// (예: ParticleSubUVShader의 SubUV 프레임·알파)
	constexpr EShaderBindFlagBits PerObjectStages = EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel;

	void WritePerObjectSlot(uint8* Dest, const FRenderPacket& Packet, const uint32 SlotSize)
	{
		FPerObjectConstants Constants;
		Constants.World = Packet.model;
		Constants.Normal = Packet.Normal;
		std::memcpy(Dest, &Constants, sizeof(FPerObjectConstants));

		if (Packet.MaterialParamData && Packet.MaterialParamDataSize > 0)
		{
			const uint32 MaxParamSize = SlotSize - static_cast<uint32>(sizeof(FPerObjectConstants));
			assert(Packet.MaterialParamDataSize <= MaxParamSize);
			std::memcpy(Dest + sizeof(FPerObjectConstants), Packet.MaterialParamData, (std::min)(Packet.MaterialParamDataSize, MaxParamSize));
		}
	}

	EPSOType GetPacketPSO(const FRenderPacket& Packet)
	{
		return Packet.material ? Packet.material->PSOType : EPSOType::Count;
	}

	bool IsOpaquePSO(const EPSOType PSO)
	{
		return PSO == EPSOType::StaticMesh_Opaque || PSO == EPSOType::StaticMesh_Wireframe;
	}

	bool CompareStateThenDepth(const FRenderPacket& A, const FRenderPacket& B)
	{
		const EPSOType APSO = GetPacketPSO(A);
		const EPSOType BPSO = GetPacketPSO(B);
		if (APSO != BPSO)
			return static_cast<uint8>(APSO) < static_cast<uint8>(BPSO);

		if (A.material != B.material)
			return std::less<>{}(A.material, B.material);

		if (A.mesh != B.mesh)
			return std::less<>{}(A.mesh, B.mesh);

		return IsOpaquePSO(APSO)
			? A.CameraDistanceSquared < B.CameraDistanceSquared
			: A.CameraDistanceSquared > B.CameraDistanceSquared;
	}

}

bool FRenderer::Init()
{
	ViewCB = FRenderCommand::CreateConstantBuffer(sizeof(FViewConstants));
	LightBuffers.LightingGlobalsCB = FRenderCommand::CreateConstantBuffer(sizeof(FLightingGlobals));
	LightClusters.GlobalsCB = FRenderCommand::CreateConstantBuffer(sizeof(FLightClusterGlobals));
	const char* ShaderPaths[] = {"Resources/Shader/BuildLightClusterCS_1.hlsl", "Resources/Shader/BuildLightClusterCS_2.hlsl", "Resources/Shader/BuildLightClusterCS_3.hlsl"};
	for (uint32 Index = 0; Index < 3; ++Index)
	{
		FString CSOPath;
		FShaderByteCode ByteCode = FRenderUtil::GetOrCompile(ShaderPaths[Index], nullptr, "mainCS", EShaderType::Compute, CSOPath);
		if (!ByteCode.IsValid()) return false;
		LightClusters.BuildCS[Index] = FRenderCommand::CreateComputeShader(ByteCode);
		if (!LightClusters.BuildCS[Index] || !LightClusters.BuildCS[Index]->IsValid()) return false;
	}
	if (FRenderCommand::GetRenderDevice()->SupportsConstantBufferOffsetting())
	{
		return EnsureConstantBufferCapacity(Temp, InitialPacketCapacity);
	}
	// 패킷별 재질 파라미터까지 담도록 슬롯 크기로 만든다.
	Temp = FRenderCommand::CreateConstantBuffer(PerObjectSlotSize);
	return Temp && Temp->GetBuffer();
}

bool FRenderer::EnsureConstantBufferCapacity(TUniquePtr<FRHIUniformBuffer>& Buffer, uint32 PacketCount)
{
	const uint64 RequiredBytes = static_cast<uint64>(PacketCount) * PerObjectSlotSize;
	const uint64 MaxBytes = (std::numeric_limits<uint32>::max)() / PerObjectSlotSize * static_cast<uint64>(PerObjectSlotSize);
	if (RequiredBytes > MaxBytes)
	{
		LOG(Warning, "Per-object constant buffer size exceeds the D3D11 byte-width limit.");
		return false;
	}
	const uint64 CurrentBytes = Buffer && Buffer->GetBuffer() ? Buffer->GetBufferSize() : 0;
	if (CurrentBytes >= RequiredBytes)
	{
		return true;
	}
	const uint32 NewBytes = static_cast<uint32>((std::min)(MaxBytes, (std::max)(RequiredBytes, CurrentBytes * 2)));
	auto NewBuffer = FRenderCommand::CreateConstantBuffer(NewBytes);
	if (!NewBuffer || !NewBuffer->GetBuffer())
	{
		LOG(Warning, "Failed to allocate per-object constant buffer ({} bytes).", NewBytes);
		return false;
	}
	Buffer = std::move(NewBuffer);
	return true;
}

// 지연 워커 초기화
void FRenderer::EnsureDeferredWorkers()
{
	if (bDeferredWorkersInitialized)
	{
		return;
	}
	bDeferredWorkersInitialized = true;
	const FDynamicRHI* Device = FRenderCommand::GetRenderDevice();
	// 에뮬레이션된 커맨드 리스트에서는 오프셋만 바꾸는 바인딩을 사용하지 않는다.
	if (!Device->SupportsConstantBufferOffsetting() || !Device->SupportsNativeCommandLists())
	{
		return;
	}

	uint32 WorkerCount = Tasks::FTaskScheduler::Get().GetNumWorkers();
	if (WorkerCount == 0)
	{
		WorkerCount = (std::max)(1u, std::thread::hardware_concurrency());
	}

	DeferredWorkers.SetNum(WorkerCount);

	for (uint32 Index = 0; Index < WorkerCount; ++Index)
	{
		DeferredWorkers[Index].Context = FRenderCommand::CreateDeferredContext();

		if (!DeferredWorkers[Index].Context
			|| FAILED(DeferredWorkers[Index].Context.As(&DeferredWorkers[Index].Context1)))
		{
			LOG(Warning, "Deferred rendering unavailable; using immediate context.");
			DeferredWorkers.SetNum(0);
			return;
		}
	}
}

// 기존 단일 카메라의 ViewProjection으로 렌더 패킷 배열 전체를 그린다.
void FRenderer::RenderAll(TArray<FRenderPacket>& InPackets, UCameraComponent* CameraComponent, const uint32 ViewWidth, const uint32 ViewHeight, const FScene& Scene)
{
	FRenderViewData RenderView;
	RenderView.Width = (std::max)(1u, ViewWidth);
	RenderView.Height = (std::max)(1u, ViewHeight);
	RenderView.Camera.Transform = {CameraComponent->GetComponentLocation(), CameraComponent->GetComponentRotation().Quaternion()};
	RenderView.Camera.Projection = {CameraComponent->GetIsOrthogonal() ? ECameraProjectionMode::Orthographic : ECameraProjectionMode::Perspective, CameraComponent->GetFieldOfView(), CameraComponent->GetOrthoWidth(), CameraComponent->GetNearZ(), CameraComponent->GetFarZ()};
	RenderView.View = CameraComponent->GetViewMatrix();
	RenderView.Projection = BuildProjectionMatrix(RenderView.Camera.Projection, static_cast<float>(RenderView.Width) / RenderView.Height);
	RenderView.ViewProjection = RenderView.View * RenderView.Projection;
	RenderView.CameraForward = RenderView.Camera.Transform.Rotation.GetForwardVector().Normalized();
	RenderAll(InPackets, RenderView, Scene);
}

// 불투명 우선·반투명 거리순으로 배열을 정렬해 View 행렬과 Section 범위로 그린다.
void FRenderer::RenderAll(TArray<FRenderPacket>& InPackets, const FRenderViewData& RenderView, const FScene& Scene)
{
	BuildLightDatas(Scene);
	RenderOpaque(InPackets, RenderView, Scene);
	RenderTranslucent(RenderView);
}

void FRenderer::BindLightingResources(ID3D11DeviceContext* Context)
{
	FRenderCommand::BindConstantBuffer(3, LightBuffers.LightingGlobalsCB.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel, Context);
	FRenderCommand::BindConstantBuffer(4, LightClusters.GlobalsCB.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel, Context);
	ID3D11DeviceContext* Ctx = Context ? Context : FRenderCommand::GetRenderDevice()->GetContext();
	ID3D11ShaderResourceView* LightDataSRV = LightBuffers.LightDataSB ? LightBuffers.LightDataSB->GetSRV() : nullptr;
	ID3D11ShaderResourceView* SRVs[] = {
		LightDataSRV,
		LightBuffers.DirectionalLightIndicesSB ? LightBuffers.DirectionalLightIndicesSB->GetSRV() : nullptr,
		LightClusters.LightIndicesSB ? LightClusters.LightIndicesSB->GetSRV() : nullptr,
		LightClusters.ClusterSB ? LightClusters.ClusterSB->GetSRV() : nullptr,
		LightClusters.PageTableSB ? LightClusters.PageTableSB->GetSRV() : nullptr,
		LightClusters.IndirectToLightClustersSB ? LightClusters.IndirectToLightClustersSB->GetSRV() : nullptr};
	Ctx->VSSetShaderResources(4, 6, SRVs);
	Ctx->PSSetShaderResources(4, 6, SRVs);
}

static uint32 NextPowerOfTwo(uint32 x)
{
	if (x <= 1)
		return 1;
	--x;
	x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;
	x |= x >> 8;
	x |= x >> 16;
	return x + 1;
}

static uint32 MakeMortonCode(const FVector& Position, const FVector& Min, const FVector& Max)
{
	auto Quantize = [](float Value, float MinValue, float MaxValue) -> uint32
	{
		if (MaxValue <= MinValue)
			return 0;
		float Normalized = std::clamp((Value - MinValue) / (MaxValue - MinValue), 0.0f, 1.0f);
		return (std::min)(static_cast<uint32>(Normalized * 1024.0f), 1023u);
	};
	uint32 X = Quantize(Position.X, Min.X, Max.X);
	uint32 Y = Quantize(Position.Y, Min.Y, Max.Y);
	uint32 Z = Quantize(Position.Z, Min.Z, Max.Z);
	uint32 Code = 0;
	for (uint32 Bit = 0; Bit < 10; ++Bit)
	{
		Code |= ((X >> Bit) & 1u) << (3 * Bit);
		Code |= ((Y >> Bit) & 1u) << (3 * Bit + 1);
		Code |= ((Z >> Bit) & 1u) << (3 * Bit + 2);
	}
	return Code;
}

void FRenderer::BuildLightDatas(const FScene& Scene)
{
	struct FLightBound
	{
		uint32 Morton;
		uint32 DataIndex;
		FSphere Bound;
	};
	bool AmbientDone = false;
	FVector min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
	FVector max{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};

	FLightingGlobals LightingGlobal;
	TArray<FLightData> LightDatas;
	TArray<uint32> DirectionalLightIndices;
	TArray<FLightTreeNode> LightTree;
	TArray<FLightBound> Bounds;

	LightDatas.Reserve(Scene.Lights.Num());
	Bounds.Reserve(Scene.Lights.Num());
	for (const auto& Component : Scene.Lights)
	{
		if (!Component.IsValid() || !Component->bVisible)
			continue;
		ELightComponentType LightType = Component->GetLightType();
		if (LightType == LightType_Directional || LightType == LightType_Point || LightType == LightType_Spot)
		{
			if (LightType != LightType_Directional)
			{
				FLightBound Bound{.DataIndex{(uint32)LightDatas.size()}, .Bound{Component->GetBoundingSphere()}};
				if (min.X > Bound.Bound.Center.X) min.X = Bound.Bound.Center.X;
				if (min.Y > Bound.Bound.Center.Y) min.Y = Bound.Bound.Center.Y;
				if (min.Z > Bound.Bound.Center.Z) min.Z = Bound.Bound.Center.Z;
				if (max.X < Bound.Bound.Center.X) max.X = Bound.Bound.Center.X;
				if (max.Y < Bound.Bound.Center.Y) max.Y = Bound.Bound.Center.Y;
				if (max.Z < Bound.Bound.Center.Z) max.Z = Bound.Bound.Center.Z;
				Bounds.Add(Bound);
			}
			else
				DirectionalLightIndices.Add((uint32)LightDatas.size());

			LightDatas.Add(Component->CreateLightData());
		}
		else if (LightType == LightType_Ambient && !AmbientDone)
		{
			AmbientDone = true;
			const FLightData AmbientData = Component->CreateLightData();
			LightingGlobal.AmbientRadiance = {AmbientData.Color.X, AmbientData.Color.Y, AmbientData.Color.Z, AmbientData.Intensity};
		}
	}
	for (auto& Bound : Bounds)
		Bound.Morton = MakeMortonCode(Bound.Bound.Center, min, max);

	std::sort(Bounds.begin(), Bounds.end(),
		[](const FLightBound& B1, const FLightBound& B2)
		{
			return B1.Morton < B2.Morton;
		});

	LightingGlobal.LightCount = LightDatas.Num();
	LightingGlobal.DirectionalLightCount = DirectionalLightIndices.Num();

	uint32 RootIndex = 0;

	uint32 k = Bounds.Num();

	if (k == 0)
	{
		LightTree.Add(FLightTreeNode{.ChildIndex = std::numeric_limits<uint32>::max()});
	}
	else
	{
		uint32 NodeCount = 1;
		uint32 Height = 0;
		do
		{
			++Height;
			NodeCount += k;
			k = (k + MaxChildCount - 1) / MaxChildCount;
		} while (k != 1);

		LightTree.Reserve(NodeCount);
		for (auto& Bound : Bounds)
			LightTree.Add(FLightTreeNode{.LeafSphere{.Center = Bound.Bound.Center, .Radius = Bound.Bound.Radius, .DataIndex = Bound.DataIndex}});

		k = Bounds.Num();
		uint32 currentIndex = 0;
		uint32 ParentInitalIndex = Bounds.Num();
		uint32 LoopedCount = 0;
		while (k != 0)
		{
			uint32 BoxAddCount = 0;
			if (k <= MaxChildCount)
				BoxAddCount = k;
			else
				BoxAddCount = MaxChildCount;
			FBox Box;

			for (uint32 i = 0; i < BoxAddCount; ++i)
			{
				Box += FBox(LightTree[currentIndex].LeafSphere.Center, LightTree[currentIndex].LeafSphere.Radius);
				++currentIndex;
			}

			LightTree.Add(FLightTreeNode{.AABB{Box}, .ChildIndex = LoopedCount * MaxChildCount, .ChildCount = BoxAddCount});
			k -= BoxAddCount;
			++LoopedCount;
		}

		for (uint32 i = 1; i < Height; ++i)
		{
			k = LoopedCount;
			uint32 baseIndex = ParentInitalIndex;
			currentIndex = ParentInitalIndex;
			ParentInitalIndex += LoopedCount;
			LoopedCount = 0;
			while (k != 0)
			{
				uint32 BoxAddCount = 0;
				if (k <= MaxChildCount)
					BoxAddCount = k;
				else
					BoxAddCount = MaxChildCount;
				FBox Box;

				for (uint32 j = 0; j < BoxAddCount; ++j)
				{
					Box += FBox(LightTree[currentIndex].AABB);
					++currentIndex;
				}

				LightTree.Add(FLightTreeNode{.AABB{Box}, .ChildIndex = baseIndex + LoopedCount * MaxChildCount, .ChildCount = BoxAddCount});
				k -= BoxAddCount;
				++LoopedCount;
			}
		}

		RootIndex = LightTree.Num() - 1;
	}
	LightingGlobal.LightTreeRootIndex = RootIndex;

	if (!LightBuffers.LightingGlobalsCB)
		LightBuffers.LightingGlobalsCB = FRenderCommand::CreateConstantBuffer(sizeof(FLightingGlobals));

	if (!LightBuffers.LightDataSB || LightBuffers.LightDataSB->GetElementCount() < LightDatas.Num())
		LightBuffers.LightDataSB = FRenderCommand::CreateStructuredBuffer(sizeof(FLightData), NextPowerOfTwo(LightDatas.Num()));

	if (!LightBuffers.DirectionalLightIndicesSB || LightBuffers.DirectionalLightIndicesSB->GetElementCount() < DirectionalLightIndices.Num())
		LightBuffers.DirectionalLightIndicesSB = FRenderCommand::CreateStructuredBuffer(sizeof(uint32), NextPowerOfTwo(DirectionalLightIndices.Num()));

	if (!LightBuffers.LightTreeSB || LightBuffers.LightTreeSB->GetElementCount() < LightTree.Num())
		LightBuffers.LightTreeSB = FRenderCommand::CreateStructuredBuffer(sizeof(FLightTreeNode), NextPowerOfTwo(LightTree.Num()));

	FRenderCommand::UpdateBufferData(LightBuffers.LightingGlobalsCB.get(), &LightingGlobal, sizeof(FLightingGlobals));
	FRenderCommand::UpdateStructuredBufferData(LightBuffers.LightDataSB.get(), LightDatas.GetData(), LightDatas.Num() * sizeof(FLightData));
	FRenderCommand::UpdateStructuredBufferData(LightBuffers.DirectionalLightIndicesSB.get(), DirectionalLightIndices.GetData(), DirectionalLightIndices.Num() * sizeof(uint32));
	FRenderCommand::UpdateStructuredBufferData(LightBuffers.LightTreeSB.get(), LightTree.GetData(), LightTree.Num() * sizeof(FLightTreeNode));
}

void FRenderer::BuildLightClusterBuffers(const FRenderViewData& RenderView)
{
	auto& Resources = LightClusters;
	auto& Globals = Resources.Globals;
	Globals.ClusterCountX = (RenderView.Width + Resources.PixelLengthPerTile - 1) / Resources.PixelLengthPerTile;
	Globals.ClusterCountY = (RenderView.Height + Resources.PixelLengthPerTile - 1) / Resources.PixelLengthPerTile;
	const auto& Projection = RenderView.Camera.Projection;
	const bool bIsPerspective = Projection.Mode == ECameraProjectionMode::Perspective;
	Globals.ClusterCountZ = 1;
	Globals.ClusterZScale = 0.0f;
	Globals.ClusterZBias = 0.0f;
	if (bIsPerspective)
	{
		const float HalfFovRadians = Projection.FovDegrees * 3.14159265358979323846f / 360.0f;
		Globals.ClusterZScale = 1.0f / log2(1.0f + 2.0f * tan(HalfFovRadians) / Globals.ClusterCountY);
		Globals.ClusterZBias = -log2(Projection.NearClip) * Globals.ClusterZScale;
		Globals.ClusterCountZ = (std::max)(1u, static_cast<uint32>(ceil(log2(Projection.FarClip) * Globals.ClusterZScale + Globals.ClusterZBias)));
	}
	const uint32 PageCount = Globals.ClusterCountX * Globals.ClusterCountY * ((Globals.ClusterCountZ + Resources.ZClusterCountPerPhysicalPage - 1) / Resources.ZClusterCountPerPhysicalPage);
	const uint32 ClusterCount = Globals.ClusterCountX * Globals.ClusterCountY * Globals.ClusterCountZ;
	const uint32 IndirectCount = PageCount * Resources.ZClusterCountPerPhysicalPage;
	if (!Resources.LightIndicesSB)
		Resources.LightIndicesSB = FRenderCommand::CreateStructuredBuffer(sizeof(uint32), Resources.LightIndicesCapacity);
	if (!Resources.ClusterSB || Resources.ClusterSB->GetElementCount() < ClusterCount)
		Resources.ClusterSB = FRenderCommand::CreateStructuredBuffer(sizeof(FLightCluster), NextPowerOfTwo(ClusterCount));
	if (!Resources.IndirectToLightClustersSB || Resources.IndirectToLightClustersSB->GetElementCount() < IndirectCount)
		Resources.IndirectToLightClustersSB = FRenderCommand::CreateStructuredBuffer(sizeof(uint32), NextPowerOfTwo(IndirectCount));
	if (!Resources.PageTableSB || Resources.PageTableSB->GetElementCount() < PageCount)
		Resources.PageTableSB = FRenderCommand::CreateStructuredBuffer(sizeof(uint32), NextPowerOfTwo(PageCount));
	if (!Resources.DispatchArgsBuffer)
	{
		D3D11_BUFFER_DESC Desc{};
		Desc.ByteWidth = sizeof(uint32) * 3;
		Desc.Usage = D3D11_USAGE_DEFAULT;
		Desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
		Resources.DispatchArgsBuffer = MakeUnique<FRHIBuffer>(FRenderCommand::GetDevice(), Desc);
	}
	ID3D11DeviceContext* Context = FRenderCommand::GetContext();
	const uint32 Zeros[6]{};
	const uint32 TileMask[4]{1, 1, 1, 1};
	Context->ClearUnorderedAccessViewUint(Resources.PageTableSB->GetUAV(), bIsPerspective ? Zeros : TileMask);
	Context->ClearUnorderedAccessViewUint(Resources.ClusterSB->GetUAV(), Zeros);
	FRenderCommand::UpdateStructuredBufferData(Resources.LightIndicesSB.get(), Zeros, sizeof(Zeros), Context);
	FRenderCommand::UpdateBufferData(Resources.GlobalsCB.get(), &Globals, sizeof(Globals), Context);
	ID3D11Buffer* ConstantBuffers[] = {ViewCB->GetBuffer(), LightBuffers.LightingGlobalsCB->GetBuffer(), Resources.GlobalsCB->GetBuffer()};
	Context->CSSetConstantBuffers(0, 3, ConstantBuffers);
	ID3D11ShaderResourceView* SRVs[] = {bIsPerspective ? Resources.ViewDepthTexture->GetSRV() : nullptr, LightBuffers.LightTreeSB->GetSRV()};
	Context->CSSetShaderResources(0, 2, SRVs);
	ID3D11UnorderedAccessView* UAVs[] = {Resources.LightIndicesSB->GetUAV(), Resources.ClusterSB->GetUAV(), Resources.IndirectToLightClustersSB->GetUAV(), Resources.PageTableSB->GetUAV()};
	Context->CSSetUnorderedAccessViews(0, 4, UAVs, nullptr);
	if (bIsPerspective)
	{
		FRenderCommand::CSSetShader(Resources.BuildCS[0].get());
		Context->Dispatch((RenderView.Width + Resources.MarkPagesThreadLength - 1) / Resources.MarkPagesThreadLength, (RenderView.Height + Resources.MarkPagesThreadLength - 1) / Resources.MarkPagesThreadLength, 1);
	}
	FRenderCommand::CSSetShader(Resources.BuildCS[1].get());
	Context->Dispatch((PageCount + Resources.AllocatePagesThreadLength - 1) / Resources.AllocatePagesThreadLength, 1, 1);
	const D3D11_BOX ArgsBox{0, 0, 0, sizeof(uint32) * 3, 1, 1};
	Context->CopySubresourceRegion(Resources.DispatchArgsBuffer->GetBuffer(), 0, 0, 0, 0, Resources.LightIndicesSB->GetBuffer(), 0, &ArgsBox);
	FRenderCommand::CSSetShader(Resources.BuildCS[2].get());
	Context->DispatchIndirect(Resources.DispatchArgsBuffer->GetBuffer(), 0);
	ID3D11UnorderedAccessView* EmptyUAVs[4]{};
	ID3D11ShaderResourceView* EmptySRVs[2]{};
	Context->CSSetUnorderedAccessViews(0, 4, EmptyUAVs, nullptr);
	Context->CSSetShaderResources(0, 2, EmptySRVs);
	FRenderCommand::CSSetShader(nullptr);
}

// TArray 기반 불투명 메시 지연 컨텍스트 병렬 렌더링
void FRenderer::RenderOpaque(TArray<FRenderPacket>& InPackets,
	const FRenderViewData& RenderView,
	const FScene& Scene,
	bool bWireframe,
	ELightingModel LightingModel)
{
	FStatScope SubmitScope(StatIds::RenderSubmit());
	LightClusters.bViewDepthValid = false;
	const int32 TotalPackets = InPackets.Num();
	if (TotalPackets == 0)
	{
		return;
	}
	const bool bIsPerspective = RenderView.Camera.Projection.Mode == ECameraProjectionMode::Perspective;

	if (!ViewCB)
	{
		ViewCB = FRenderCommand::CreateConstantBuffer(sizeof(FViewConstants));
	}
	const FViewConstants ViewConstants{RenderView.ViewProjection, RenderView.Camera.Transform.Location, static_cast<float>(RenderView.Width), static_cast<float>(RenderView.Height), bIsPerspective ? 1u : 0u, {}};
	FRenderCommand::UpdateBufferData(ViewCB.get(), &ViewConstants, sizeof(ViewConstants));

	ID3D11ShaderResourceView* EmptySRVs[6]{};
	FRenderCommand::GetContext()->PSSetShaderResources(4, 6, EmptySRVs);
	FRenderCommand::GetContext()->VSSetShaderResources(4, 6, EmptySRVs);
	LightClusters.Globals = {};
	LightClusters.Globals.ClusterDebugMode = static_cast<uint32>(LightCullingDebugSettings.View);
	LightClusters.Globals.DebugLightCountMax = (std::max)(1u, LightCullingDebugSettings.LightCountMax);
	LightClusters.Globals.bEnableLightCulling = LightCullingDebugSettings.bEnabled ? 1u : 0u;
	FRenderCommand::UpdateBufferData(LightClusters.GlobalsCB.get(), &LightClusters.Globals, sizeof(LightClusters.Globals));
	EnsureDeferredWorkers();

	{
		FStatScope MaterialScope(StatIds::RenderMaterials());
		// 머티리얼 파라미터 사전 일괄 갱신
		TArray<UMaterial*> UniqueMaterials;
		UniqueMaterials.Reserve(8);
		for (const FRenderPacket& Packet : InPackets)
		{
			if (Packet.material && std::find(UniqueMaterials.begin(), UniqueMaterials.end(), Packet.material) == UniqueMaterials.end())
			{
				UniqueMaterials.Add(Packet.material);
			}
		}
		for (UMaterial* Mat : UniqueMaterials)
		{
			UpdateMaterialParams(Mat);
		}

	}

	if (bIsPerspective)
	{
		FScopedRenderTargets Targets;
		if (Targets.DSV && RenderView.Width > 0 && RenderView.Height > 0)
		{
			ComPtr<ID3D11Resource> DepthResource;
			ComPtr<ID3D11Texture2D> DepthTexture;
			Targets.DSV->GetResource(DepthResource.GetAddressOf());
			DepthResource.As(&DepthTexture);
			D3D11_TEXTURE2D_DESC Desc{};
			DepthTexture->GetDesc(&Desc);
			if (!LightClusters.ViewDepthTexture || LightClusters.ViewDepthTexture->GetWidth() != Desc.Width || LightClusters.ViewDepthTexture->GetHeight() != Desc.Height)
			{
				Desc.MipLevels = 1;
				Desc.ArraySize = 1;
				Desc.Format = DXGI_FORMAT_R32_FLOAT;
				Desc.Usage = D3D11_USAGE_DEFAULT;
				Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
				Desc.CPUAccessFlags = 0;
				Desc.MiscFlags = 0;
				LightClusters.ViewDepthTexture = FRenderCommand::CreateTexture2D(Desc);
			}
			if (!LightClusters.ViewMatrixCB) LightClusters.ViewMatrixCB = FRenderCommand::CreateConstantBuffer(sizeof(FMatrix));
			if (LightClusters.ViewDepthTexture && LightClusters.ViewDepthTexture->GetRTV() && LightClusters.ViewDepthTexture->GetSRV() && LightClusters.ViewMatrixCB && LightClusters.ViewMatrixCB->GetBuffer())
			{
				FRenderCommand::UpdateBufferData(LightClusters.ViewMatrixCB.get(), &RenderView.View, sizeof(RenderView.View));
				const float ClearDepth[4]{(std::numeric_limits<float>::max)(), 0.0f, 0.0f, 0.0f};
				Targets.Context->ClearRenderTargetView(LightClusters.ViewDepthTexture->GetRTV(), ClearDepth);
				Targets.Context->ClearDepthStencilView(Targets.DSV, D3D11_CLEAR_DEPTH, 1.0f, 0);
				ID3D11RenderTargetView* DepthRTV = LightClusters.ViewDepthTexture->GetRTV();
				Targets.Context->OMSetRenderTargets(1, &DepthRTV, Targets.DSV);
				LightClusters.bViewDepthValid = DrawPacketBatch(InPackets, true, bWireframe, LightingModel);
				if (!LightClusters.bViewDepthValid) Targets.Context->ClearDepthStencilView(Targets.DSV, D3D11_CLEAR_DEPTH, 1.0f, 0);
			}
		}
	}

	if (RenderView.Width > 0 && RenderView.Height > 0 &&
		(LightClusters.bViewDepthValid || !bIsPerspective) &&
		(LightCullingDebugSettings.bEnabled || LightCullingDebugSettings.View != ELightClusterDebugView::None))
		BuildLightClusterBuffers(RenderView);
	DrawPacketBatch(InPackets, false, bWireframe, LightingModel);
}

bool FRenderer::DrawPacketBatch(const TArray<FRenderPacket>& InPackets, bool bViewDepthPass, bool bWireframe, ELightingModel LightingModel)
{
	FBatchStats BatchStats;
	const int32 TotalPackets = InPackets.Num();
	const int32 NumWorkers = DeferredWorkers.Num();
	const bool bUseOffsets = FRenderCommand::GetRenderDevice()->SupportsConstantBufferOffsetting();

	if (TotalPackets <= 500 || NumWorkers <= 1)
	{
		FGPUStatScope OpaqueScope(StatIds::GpuOpaque(), bViewDepthPass ? L"View Depth Immediate" : L"Opaque Immediate");
		const UStaticMesh* LastMesh = nullptr;
		uint8 LastLOD = 0xFF;
		const UMaterial* LastMaterial = nullptr;
		EPSOType LastPSO = EPSOType::Count;

		if (bUseOffsets)
		{
			FStatScope UploadScope(StatIds::RenderUpload());
			if (!EnsureConstantBufferCapacity(Temp, TotalPackets))
			{
				return false;
			}
			void* MappedData = FRenderCommand::MapBufferWriteDiscard(Temp.get());
			if (!MappedData)
			{
				return false;
			}
			uint8* Base = static_cast<uint8*>(MappedData);
			for (int32 i = 0; i < TotalPackets; ++i)
			{
				WritePerObjectSlot(Base + static_cast<size_t>(i) * PerObjectSlotSize, InPackets[i], PerObjectSlotSize);
			}
			FRenderCommand::UnmapBuffer(Temp.get());
			if (BatchStats.bCountUploadBytes)
				BatchStats.UploadBytes += static_cast<uint64>(TotalPackets) * sizeof(FPerObjectConstants);
		}

		BindPacketPass(nullptr, bViewDepthPass);

		FStatScope DrawScope(StatIds::RenderDrawLoop());
		for (int32 i = 0; i < TotalPackets; ++i)
		{
			const FRenderPacket& RenderPacket = InPackets[i];

			if (RenderPacket.mesh == nullptr || RenderPacket.material == nullptr || (bViewDepthPass && !IsOpaquePSO(GetPacketPSO(RenderPacket))))
			{
				continue;
			}

			if (LastMesh != RenderPacket.mesh || LastLOD != RenderPacket.LODIndex)
			{
				FRenderCommand::BindMesh(RenderPacket.mesh, RenderPacket.LODIndex);
				LastMesh = RenderPacket.mesh;
				LastLOD = RenderPacket.LODIndex;
			}

			if (LastMaterial != RenderPacket.material)
			{
				const bool bPSOChanged = LastPSO != RenderPacket.material->PSOType;
				BindPacketMaterial(RenderPacket.material, nullptr, bPSOChanged, bViewDepthPass, bWireframe, LightingModel);
				LastMaterial = RenderPacket.material;
				LastPSO = RenderPacket.material->PSOType;
			}

			const uint32 FirstConstant = i * (PerObjectSlotSize / 16);
			const uint32 NumConstants = PerObjectSlotSize / 16;

			if (bUseOffsets)
			{
				FRenderCommand::BindConstantBufferRange(0, Temp.get(), PerObjectStages, FirstConstant, NumConstants);
			}
			else
			{
				void* MappedData = FRenderCommand::MapBufferWriteDiscard(Temp.get());
				if (!MappedData)
				{
					return false;
				}
				WritePerObjectSlot(static_cast<uint8*>(MappedData), RenderPacket, PerObjectSlotSize);
				FRenderCommand::UnmapBuffer(Temp.get());
				if (BatchStats.bCountUploadBytes)
					BatchStats.UploadBytes += sizeof(FPerObjectConstants);
				FRenderCommand::BindConstantBuffer(0, Temp.get(), PerObjectStages);
			}

			const uint32 IndexCount = RenderPacket.IndexCount ? RenderPacket.IndexCount : RenderPacket.mesh->GetIndexCount(RenderPacket.LODIndex);
			FRenderCommand::DrawIndexed(IndexCount, RenderPacket.StartIndex);
			if (BatchStats.bCountDraws)
				++BatchStats.Draws;
			if (BatchStats.bCountTriangles)
				BatchStats.Triangles += IndexCount / 3;
		}
		return true;
	}

	const int32 ChunkSize = 1 + (TotalPackets - 1) / NumWorkers;
	const int32 NumJobs = 1 + (TotalPackets - 1) / ChunkSize;
	for (int32 Index = 0; Index < NumJobs; ++Index)
	{
		const int32 PacketCount = (std::min)(ChunkSize, TotalPackets - Index * ChunkSize);
		if (!EnsureConstantBufferCapacity(DeferredWorkers[Index].PerObjectCB, PacketCount))
		{
			return false;
		}
	}

	FScopedRenderTargets Targets;

	std::vector<ComPtr<ID3D11CommandList>> CommandLists(NumJobs);
	std::vector<FBatchCounters> WorkerCounters;
	if (BatchStats.IsCollecting())
		WorkerCounters.resize(NumJobs);

	{
		FStatScope WorkerScope(StatIds::RenderWorkers());
		Tasks::ParallelFor(TotalPackets, ChunkSize, [&](int32 Start, int32 End)
		{
			const int32 JobIndex = Start / ChunkSize;
			if (JobIndex >= DeferredWorkers.Num())
			{
				return;
			}

			// Accumulate locally; publish once instead of writing adjacent worker slots per draw.
			FBatchCounters LocalCounters;

			ID3D11DeviceContext* Context = DeferredWorkers[JobIndex].Context.Get();
			FRHIUniformBuffer* WorkerCB = DeferredWorkers[JobIndex].PerObjectCB.get();
			ID3D11DeviceContext1* Context1 = DeferredWorkers[JobIndex].Context1.Get();

			Context->OMSetRenderTargets(Targets.NumRTVs, Targets.RTVs, Targets.DSV);
			Context->RSSetViewports(Targets.NumViewports, &Targets.Viewport);
			BindPacketPass(Context, bViewDepthPass);

			const UStaticMesh* LastMesh  = nullptr;
			const UMaterial* LastMaterial = nullptr;
			uint8 LastLOD = 0xFF;
			EPSOType LastPSO = EPSOType::Count;

			assert(static_cast<uint32>(End - Start) * PerObjectSlotSize <= WorkerCB->GetBufferSize());

			void* MappedData = FRenderCommand::MapBufferWriteDiscard(WorkerCB, Context);

			if (!MappedData)
			{
				ComPtr<ID3D11CommandList> Discarded;
				Context->FinishCommandList(FALSE, Discarded.GetAddressOf());
				return;
			}

			uint8* Base = static_cast<uint8*>(MappedData);

			for (int32 i = Start; i < End; ++i)
			{
				const uint32 LocalIndex = i - Start;
				WritePerObjectSlot(Base + LocalIndex * PerObjectSlotSize, InPackets[i], PerObjectSlotSize);
			}

			FRenderCommand::UnmapBuffer(WorkerCB, Context);
			if (BatchStats.bCountUploadBytes)
				LocalCounters.UploadBytes = static_cast<uint64>(End - Start) * sizeof(FPerObjectConstants);

		for (int32 i = Start; i < End; ++i)
		{
			const FRenderPacket& RenderPacket = InPackets[i];
			if (RenderPacket.mesh != nullptr && RenderPacket.material != nullptr && (!bViewDepthPass || IsOpaquePSO(GetPacketPSO(RenderPacket))))
			{
				if (LastMesh != RenderPacket.mesh || LastLOD != RenderPacket.LODIndex)
				{
					FRenderCommand::BindMesh(RenderPacket.mesh, RenderPacket.LODIndex, Context);
					LastMesh = RenderPacket.mesh;
					LastLOD = RenderPacket.LODIndex;
				}

				if (LastMaterial != RenderPacket.material)
				{
					const bool bPSOChanged = LastPSO != RenderPacket.material->PSOType;
					BindPacketMaterial(RenderPacket.material, Context, bPSOChanged, bViewDepthPass, bWireframe, LightingModel);
					LastMaterial = RenderPacket.material;
					LastPSO = RenderPacket.material->PSOType;
				}

				const uint32 LocalIndex = i - Start;
				const uint32 FirstConstant = LocalIndex * (PerObjectSlotSize / 16);
				const uint32 NumConstants = PerObjectSlotSize / 16;

				FRenderCommand::BindConstantBufferRange(0, WorkerCB, PerObjectStages, FirstConstant, NumConstants, Context1);

				const uint32 IndexCount = RenderPacket.IndexCount ? RenderPacket.IndexCount : RenderPacket.mesh->GetIndexCount(RenderPacket.LODIndex);
				FRenderCommand::DrawIndexed(
					IndexCount,
					RenderPacket.StartIndex,
					0,
					Context
				);
				if (BatchStats.bCountDraws)
					++LocalCounters.Draws;
				if (BatchStats.bCountTriangles)
					LocalCounters.Triangles += IndexCount / 3;
			}
		}

			if (FAILED(Context->FinishCommandList(FALSE, CommandLists[JobIndex].GetAddressOf())))
			{
				CommandLists[JobIndex].Reset();
			}
			if (BatchStats.IsCollecting())
				WorkerCounters[JobIndex] = LocalCounters;
		});
	}

	// 메인 스레드에서 커맨드 리스트 순차 실행
	const bool bAllJobsSucceeded = std::all_of(CommandLists.begin(), CommandLists.end(), [](const auto& List) { return List != nullptr; });
	if (!bAllJobsSucceeded)
	{
		// 로그와 컨텍스트 폐기는 워커가 모두 종료된 뒤 메인 스레드에서 처리한다.
		LOG(Warning, "Per-object buffer upload or command-list recording failed; skipping this render batch.");
		DeferredWorkers.SetNum(0);
		bDeferredWorkersInitialized = false;
	}
	if (BatchStats.bCountUploadBytes)
	{
		for (const FBatchCounters& Counters : WorkerCounters)
			BatchStats.UploadBytes += Counters.UploadBytes;
	}
	{
		FStatScope ExecuteScope(StatIds::RenderExecute());
		FGPUStatScope OpaqueScope(StatIds::GpuOpaque(), bViewDepthPass ? L"View Depth Command Lists" : L"Opaque Command Lists");
		for (int32 i = 0; bAllJobsSucceeded && i < NumJobs; ++i)
		{
			if (CommandLists[i])
			{
				FRenderCommand::ExecuteCommandList(CommandLists[i].Get(), false);
				if (BatchStats.bCountDraws)
					BatchStats.Draws += WorkerCounters[i].Draws;
				if (BatchStats.bCountTriangles)
					BatchStats.Triangles += WorkerCounters[i].Triangles;
			}
		}
	}

	return bAllJobsSucceeded;
}

void FRenderer::BindPacketPass(ID3D11DeviceContext* Context, bool bViewDepthPass)
{
	FRenderCommand::BindConstantBuffer(2, ViewCB.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel, Context);
	if (bViewDepthPass)
	{
		FRenderCommand::BindPipelineState(FRenderResourceManager::GetPSO(EPSOType::StaticMesh_ViewDepth), Context);
		FRenderCommand::BindConstantBuffer(5, LightClusters.ViewMatrixCB.get(), EShaderBindFlagBits::Vertex, Context);
	}
	else
	{
		BindLightingResources(Context);
	}
}

void FRenderer::BindPacketMaterial(UMaterial* Material, ID3D11DeviceContext* Context, bool bPSOChanged, bool bViewDepthPass, bool bWireframe, ELightingModel LightingModel)
{
	if (bViewDepthPass)
	{
		FRenderCommand::SetRasterizerState(bWireframe || Material->PSOType == EPSOType::StaticMesh_Wireframe ? ERasterizerState::Wireframe : ERasterizerState::SolidBack, Context);
	}
	else
	{
		BindMaterial(Material, Context, bPSOChanged, bWireframe, LightingModel);
		if (LightClusters.bViewDepthValid && IsOpaquePSO(Material->PSOType)) FRenderCommand::SetDepthStencilState(EDepthStencilState::ReadOnlyLessEqual, Context);
	}
}

// 반투명 객체가 없으므로 아무 작업도 수행하지 않음
// TODO: 라이트컬링을 위한 클러스터 구조는 반투명용과 불투명용이 따로 존재함. 
// 현재 renderopaque에서 빌드하는 불투명용 클러스터는 오클루전컬링에서 살아남은 인스턴스들을 리드백하여 만든 z버퍼를 사용해 만드는데, 이 인스턴스들이 전부 불투명인지는 확인 안하였음 - 불투명용 클러스터구조기 때문에 불투명 인스턴스들만 있어야 함.
// 한편 반투명용은 z버퍼 대신 직접 픽셀셰이더에 반투명물체들을 가볍게 드로우하며 반투명용 클러스터페이지테이블에 마킹해야함.
void FRenderer::RenderTranslucent(const FRenderViewData& RenderView)
{
}

// 머티리얼 바인딩
void FRenderer::BindMaterial(UMaterial* material, ID3D11DeviceContext* Context, const bool bBindPipelineState, const bool bWireframe, ELightingModel LightModel)
{
	if (bBindPipelineState)
	{
		const EPSOType EffectivePSO = (bWireframe && (material->PSOType == EPSOType::StaticMesh_Opaque || material->PSOType == EPSOType::StaticMesh_Wireframe))
			? EPSOType::StaticMesh_Wireframe
			: material->PSOType;
		FRenderCommand::BindPipelineState(FRenderResourceManager::GetPSO(EffectivePSO, LightModel), Context);
	}
	for (int i = 0; i < material->Textures.size(); i++)
	{
		// 빈 슬롯에는 이전 머티리얼의 텍스처가 남는다. 셰이더는 bHas*Map 플래그로 구분한다
		if (!material->Textures[i])
		{
			continue;
		}
		FRenderCommand::BindShaderResource(i, material->Textures[i], EShaderBindFlagBits::Vertex, Context);
		FRenderCommand::BindShaderResource(i, material->Textures[i], EShaderBindFlagBits::Pixel, Context);
	}
	FRenderCommand::BindSamplerState(0, material->SamplerState, EShaderBindFlagBits::Vertex, Context);
	FRenderCommand::BindSamplerState(0, material->SamplerState, EShaderBindFlagBits::Pixel, Context);
	if (material->ParamBuffer)
	{
		FRenderCommand::BindConstantBuffer(1, material->ParamBuffer.get(), EShaderBindFlagBits::Vertex, Context);
		FRenderCommand::BindConstantBuffer(1, material->ParamBuffer.get(), EShaderBindFlagBits::Pixel, Context);
	}
}

// 머티리얼 파라미터 버퍼 갱신
void FRenderer::UpdateMaterialParams(UMaterial* material)
{
	if (!material || !material->ParamBuffer)
	{
		return;
	}

	switch (material->PSOType)
	{
	case EPSOType::StaticMesh_Opaque:
	case EPSOType::StaticMesh_Wireframe:
	case EPSOType::StaticMesh_Translucent:
	{
		const float TotalTime = FApp::GetCurrentTime();
		FStaticMeshMaterialParams Params{};
		Params.BaseColor = material->BaseColor;
		Params.UVOffset = material->UVScrollSpeed * TotalTime;
		Params.bOpaque = material->PSOType == EPSOType::StaticMesh_Translucent ? 0.0f : 1.0f;
		Params.bHasNormalMap = material->HasTexture(EMaterialTextureSlot::Normal) ? 1.0f : 0.0f;
		Params.bHasSpecularMap = material->HasTexture(EMaterialTextureSlot::Specular) ? 1.0f : 0.0f;
		Params.bHasEmissiveMap = material->HasTexture(EMaterialTextureSlot::Emissive) ? 1.0f : 0.0f;
		Params.SpecularColor = material->SpecularColor;
		// pow(x, 0)은 항상 1이라 표면 전체가 하이라이트가 된다
		Params.Shininess = (std::max)(material->Shininess, 1.0f);
		Params.EmissiveColor = material->EmissiveColor;
		Params.NormalScale = material->NormalScale;
		Params.bDisableShading = material->bDisableShading ? 1.0f : 0.0f;
		Params.NormalGreenSign = material->bFlipNormalGreen ? -1.0f : 1.0f;
		FRenderCommand::UpdateBufferData(material->ParamBuffer.get(), &Params, sizeof(FStaticMeshMaterialParams));
		break;
	}
	case EPSOType::Billboard:
	{
		FBillboardMaterialParams Params{};
		Params.BaseColor = material->BaseColor;
		FRenderCommand::UpdateBufferData(material->ParamBuffer.get(), &Params, sizeof(FBillboardMaterialParams));
	}
	default:
		break;
	}
}

// 오브젝트 상수 버퍼 갱신
void FRenderer::UpdatePerObjectConstants(const FRenderPacket& RenderPacket, const FMatrixRegister& ViewProjection)
{
	FPerObjectConstants Constants;
	Constants.World = RenderPacket.model;
	Constants.Normal = RenderPacket.Normal;
	FRenderCommand::UpdateBufferData(Temp.get(), &Constants);
}
