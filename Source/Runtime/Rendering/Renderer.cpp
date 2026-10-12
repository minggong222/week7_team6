#include "EnginePCH.h"
#include "Renderer.h"
#include "RHI/RHIShader.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"

#include "RenderCommand.h"
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

/*	struct FLightCountConstants
	{
		uint32 LightCount = 0;
		uint32 Padding[3]{};
	};
	static_assert(sizeof(FLightCountConstants) == 16);

	constexpr UINT LightSRVSlot = 4;*/
}

bool FRenderer::Init()
{
	ViewCB = FRenderCommand::CreateConstantBuffer(sizeof(FViewConstants));
	LightingGlobalsCB = FRenderCommand::CreateConstantBuffer(sizeof(FLightingGlobals));
	LightCountCB = FRenderCommand::CreateConstantBuffer(sizeof(FLightCountConstants));
	TileLightHeaderSB = FRenderCommand::CreateStructuredBuffer(sizeof(FTileLightHeader), TileCountX * TileCountY);
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
void FRenderer::RenderAll(TArray<FRenderPacket>& InPackets, UCameraComponent* CameraComponent, const FViewportSettings& ViewportSettings, const FScene& Scene)
{
	RenderAll(InPackets, CameraComponent->GetViewMatrix(), CameraComponent->GetViewProjectionMatrix(), CameraComponent->GetComponentLocation(), ViewportSettings, Scene);
}

// 불투명 우선·반투명 거리순으로 배열을 정렬해 View 행렬과 Section 범위로 그린다.
void FRenderer::RenderAll(TArray<FRenderPacket>& InPackets, const FMatrix& View, const FMatrix& ViewProjection, const FVector& CameraPosition, const FViewportSettings& ViewportSettings, const FScene& Scene)
{
	RenderOpaque(InPackets, View, ViewProjection, CameraPosition, ViewportSettings, Scene);
	RenderTranslucent(ViewProjection);
}

void FRenderer::BindLightingResources(ID3D11DeviceContext* Context)
{
	FRenderCommand::BindConstantBuffer(3, LightingGlobalsCB.get(), EShaderBindFlagBits::Pixel, Context);
	FRenderCommand::BindConstantBuffer(4, LightCountCB.get(), EShaderBindFlagBits::Vertex | EShaderBindFlagBits::Pixel, Context);

	ID3D11DeviceContext* Ctx = Context ? Context : FRenderCommand::GetRenderDevice()->GetContext();
	ID3D11ShaderResourceView* LightDataSRV = LightDataSB ? LightDataSB->GetSRV() : nullptr;
	Ctx->VSSetShaderResources(4, 1, &LightDataSRV);

	FRenderCommand::PSSetShaderResource(4, LightDataSB ? LightDataSB->GetSRV() : nullptr, Context);
	FRenderCommand::PSSetShaderResource(5, TileLightHeaderSB ? TileLightHeaderSB->GetSRV() : nullptr, Context);
	FRenderCommand::PSSetShaderResource(6, TileLightIndicesSB ? TileLightIndicesSB->GetSRV() : nullptr, Context);
}

// TArray 기반 불투명 메시 지연 컨텍스트 병렬 렌더링
void FRenderer::RenderOpaque(TArray<FRenderPacket>& InPackets,
	const FMatrix& View,
	const FMatrix& ViewProjection,
	const FVector& CameraPosition,
	const FViewportSettings& ViewportSettings,
	const FScene& Scene,
	bool bWireframe,
	ELightingModel LightingModel)
{
	FStatScope SubmitScope(StatIds::RenderSubmit());
	bViewDepthValid = false;
	const int32 TotalPackets = InPackets.Num();
	if (TotalPackets == 0)
	{
		return;
	}

	if (!ViewCB)
	{
		ViewCB = FRenderCommand::CreateConstantBuffer(sizeof(FViewConstants));
	}
	const FViewConstants ViewConstants{ViewProjection, CameraPosition, static_cast<float>(ViewportSettings.Width), static_cast<float>(ViewportSettings.Height), {}};
	FRenderCommand::UpdateBufferData(ViewCB.get(), &ViewConstants, sizeof(ViewConstants));

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

	{
		FScopedRenderTargets Targets;
		if (Targets.DSV && ViewportSettings.Width > 0 && ViewportSettings.Height > 0)
		{
			ComPtr<ID3D11Resource> DepthResource;
			ComPtr<ID3D11Texture2D> DepthTexture;
			Targets.DSV->GetResource(DepthResource.GetAddressOf());
			DepthResource.As(&DepthTexture);
			D3D11_TEXTURE2D_DESC Desc{};
			DepthTexture->GetDesc(&Desc);
			if (!ViewDepthTexture || ViewDepthTexture->GetWidth() != Desc.Width || ViewDepthTexture->GetHeight() != Desc.Height)
			{
				Desc.MipLevels = 1;
				Desc.ArraySize = 1;
				Desc.Format = DXGI_FORMAT_R32_FLOAT;
				Desc.Usage = D3D11_USAGE_DEFAULT;
				Desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
				Desc.CPUAccessFlags = 0;
				Desc.MiscFlags = 0;
				ViewDepthTexture = FRenderCommand::CreateTexture2D(Desc);
			}
			if (!ViewDepthCB) ViewDepthCB = FRenderCommand::CreateConstantBuffer(sizeof(FMatrix));
			if (ViewDepthTexture && ViewDepthTexture->GetRTV() && ViewDepthTexture->GetSRV() && ViewDepthCB && ViewDepthCB->GetBuffer())
			{
				FRenderCommand::UpdateBufferData(ViewDepthCB.get(), &View, sizeof(View));
				const float ClearDepth[4]{(std::numeric_limits<float>::max)(), 0.0f, 0.0f, 0.0f};
				Targets.Context->ClearRenderTargetView(ViewDepthTexture->GetRTV(), ClearDepth);
				Targets.Context->ClearDepthStencilView(Targets.DSV, D3D11_CLEAR_DEPTH, 1.0f, 0);
				ID3D11RenderTargetView* DepthRTV = ViewDepthTexture->GetRTV();
				Targets.Context->OMSetRenderTargets(1, &DepthRTV, Targets.DSV);
				bViewDepthValid = DrawPacketBatch(InPackets, true, bWireframe, LightingModel);
				if (!bViewDepthValid) Targets.Context->ClearDepthStencilView(Targets.DSV, D3D11_CLEAR_DEPTH, 1.0f, 0);
			}
		}
	}

	FLightingGlobals tempGlobal{.AmbientRadiance{1.0f, 1.0f, 1.0f, 0.5f}};
	if (!LightingGlobalsCB)
	{
		LightingGlobalsCB = FRenderCommand::CreateConstantBuffer(sizeof(FLightingGlobals));
	}
	FRenderCommand::UpdateBufferData(LightingGlobalsCB.get(), &tempGlobal, sizeof(tempGlobal));

	const uint32 LightCount = static_cast<uint32>(Scene.LightDatas.Num());
	if (!LightCountCB)
	{
		LightCountCB = FRenderCommand::CreateConstantBuffer(sizeof(FLightCountConstants));
	}
	const FLightCountConstants LightCountConstants{LightCount, {}};
	FRenderCommand::UpdateBufferData(LightCountCB.get(), &LightCountConstants, sizeof(LightCountConstants));

	const uint32 LightCapacity = (std::max)(1u, LightCount);
	if (!LightDataSB || LightDataSB->GetElementCount() < LightCapacity)
	{
		LightDataSB = FRenderCommand::CreateStructuredBuffer(sizeof(FLightData), LightCapacity);
	}
	FRenderCommand::UpdateStructuredBufferData(LightDataSB.get(), Scene.LightDatas.GetData(), Scene.LightDatas.Num() * sizeof(FLightData));
	if (!TileLightHeaderSB)
	{
		TileLightHeaderSB = FRenderCommand::CreateStructuredBuffer(sizeof(FTileLightHeader), TileCountX * TileCountY);
	}
	FRenderCommand::UpdateStructuredBufferData(TileLightHeaderSB.get(), Scene.TileLightHeader, TileCountX * TileCountY * sizeof(FTileLightHeader));
	const uint32 IndexCount = static_cast<uint32>(Scene.TileLightIndices.Num());
	const uint32 IndexCapacity = (std::max)(1u, IndexCount);
	if (!TileLightIndicesSB || TileLightIndicesSB->GetElementCount() < IndexCapacity)
	{
		TileLightIndicesSB = FRenderCommand::CreateStructuredBuffer(sizeof(uint32), IndexCapacity);
	}
	FRenderCommand::UpdateStructuredBufferData(TileLightIndicesSB.get(), Scene.TileLightIndices.GetData(), Scene.TileLightIndices.Num() * sizeof(uint32));

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
		FRenderCommand::BindConstantBuffer(5, ViewDepthCB.get(), EShaderBindFlagBits::Vertex, Context);
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
		if (bViewDepthValid && IsOpaquePSO(Material->PSOType)) FRenderCommand::SetDepthStencilState(EDepthStencilState::ReadOnlyLessEqual, Context);
	}
}

// 반투명 객체가 없으므로 아무 작업도 수행하지 않음
void FRenderer::RenderTranslucent(const FMatrix& ViewProjection)
{
}

/*void FRenderer::SetWorldLights(const TArray<FLightData>& InLights)
{
	WorldLights = InLights;
}*/

// 기존 인터페이스 호환용 함수
void FRenderer::DrawPackets(uint32 Begin, uint32 End, const FMatrix& ViewProjection)
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

/*bool FRenderer::UpdateWorldLightResources()
{
	auto* RenderDevice = FRenderCommand::GetRenderDevice();
	if (!RenderDevice || !LightCountCB)
		return false;

	ID3D11DeviceContext* Context = RenderDevice->GetContext();

	if (!Context)
		return false;

	const uint32 NumLights = static_cast<uint32>(WorldLights.Num());

	const uint32 RequiredCapacity = (std::max)(1u, NumLights);

	if (!LightsBuffer || RequiredCapacity > LightsBufferCapacity)
	{
		uint32 NewCapacity = (std::max)(1u, LightsBufferCapacity);

		while (NewCapacity < RequiredCapacity)
		{
			if (NewCapacity > (std::numeric_limits<uint32>::max)() / 2)
			{
				NewCapacity = RequiredCapacity;
				break;
			}

			NewCapacity *= 2;
		}

		const uint64 ByteWidth = static_cast<uint64>(NewCapacity) * sizeof(FLightData);

		if (ByteWidth > (std::numeric_limits<UINT>::max)())
			return false;

		ComPtr<ID3D11Device> Device;
		Context->GetDevice(Device.GetAddressOf());

		if (!Device)
			return false;

		D3D11_BUFFER_DESC Desc{};

		Desc.ByteWidth = static_cast<UINT>(ByteWidth);
		Desc.Usage = D3D11_USAGE_DEFAULT;
		Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		Desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		Desc.StructureByteStride = sizeof(FLightData);

		ComPtr<ID3D11Buffer> NewBuffer;

		HRESULT hr = Device->CreateBuffer(&Desc, nullptr, NewBuffer.GetAddressOf());
		if (FAILED(hr))
			return false;

		D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc{};
		SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
		SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		SRVDesc.Buffer.FirstElement = 0;
		SRVDesc.Buffer.NumElements = NewCapacity;

		ComPtr<ID3D11ShaderResourceView> NewSRV;

		hr = Device->CreateShaderResourceView(NewBuffer.Get(), &SRVDesc, NewSRV.GetAddressOf());
		if (FAILED(hr))
			return false;

		LightsBuffer = std::move(NewBuffer);
		LightsSRV = std::move(NewSRV);
		LightsBufferCapacity = NewCapacity;
	}

	if (NumLights > 0)
	{
		D3D11_BOX UpdateBox{};
		UpdateBox.left = 0;
		UpdateBox.right = NumLights * sizeof(FLightData);
		UpdateBox.top = 0;
		UpdateBox.bottom = 1;
		UpdateBox.front = 0;
		UpdateBox.back = 1;

		Context->UpdateSubresource(LightsBuffer.Get(), 0, &UpdateBox, &WorldLights[0], 0, 0);

		FLightCountConstants CountConstants{};
		CountConstants.LightCount = NumLights;

		FRenderCommand::UpdateBufferData(LightCountCB.get(), &CountConstants, sizeof(CountConstants));
		return true;
	}
}*/
