#include "EnginePCH.h"
#include "RenderResourceManager.h"

#include "RenderCommand.h"

#include "RenderUtil.h"

struct FPipelineTableEntry
{
	EPSOType Type;
	const char* ShaderPath;
	D3D11_PRIMITIVE_TOPOLOGY Topology;
	ERasterizerState RasterizerState;
	EBlendState BlendState;
	EDepthStencilState DepthStencilState;
};

constexpr FPipelineTableEntry PipelineTable[] = {
	// [StaticMesh_Opaque] 사과 5만 개 기본값
	{EPSOType::StaticMesh_Opaque, "Resources/Shader/StaticMeshShader.hlsl", D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, ERasterizerState::SolidBack, EBlendState::Opaque, EDepthStencilState::Default},
	// [StaticMesh_Translucent]
	{EPSOType::StaticMesh_Translucent,
		"Resources/Shader/StaticMeshShader.hlsl",
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
		ERasterizerState::SolidBack,
		EBlendState::AlphaBlend,
		EDepthStencilState::ReadOnly},
	// [StaticMesh_Wireframe]
	{EPSOType::StaticMesh_Wireframe, "Resources/Shader/StaticMeshShader.hlsl", D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, ERasterizerState::Wireframe, EBlendState::Opaque, EDepthStencilState::Default},
	// [Particle_AlphaBlend]
	{EPSOType::Particle_AlphaBlend,
		"Resources/Shader/ParticleSubUVShader.hlsl",
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
		ERasterizerState::SolidNone,
		EBlendState::AlphaBlend,
		EDepthStencilState::ReadOnly},
	// [Particle_Additive]
	{EPSOType::Particle_Additive, "Resources/Shader/ParticleSubUVShader.hlsl", D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, ERasterizerState::SolidNone, EBlendState::Additive, EDepthStencilState::ReadOnly},
	// [Skybox]
	{EPSOType::Skybox, "Resources/Shader/SkyboxShader.hlsl", D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, ERasterizerState::SolidNone, EBlendState::Opaque, EDepthStencilState::ReadOnly},
	// [Grid]
	{EPSOType::Grid, "Resources/Shader/GridShader.hlsl", D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, ERasterizerState::SolidBack, EBlendState::AlphaBlend, EDepthStencilState::Default},
	// [Outline_Mask]
	{EPSOType::Outline_Mask, "Resources/Shader/OutlineShader.hlsl", D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, ERasterizerState::SolidNone, EBlendState::NoColorWrite, EDepthStencilState::StencilMask},
	// [Outline_Draw]
	{EPSOType::Outline_Draw, "Resources/Shader/OutlineShader.hlsl", D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, ERasterizerState::SolidNone, EBlendState::Opaque, EDepthStencilState::StencilOutline},
	{EPSOType::Billboard, "Resources/Shader/BillboardShader.hlsl", D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, ERasterizerState::SolidNone, EBlendState::Opaque, EDepthStencilState::Default},
};

void FRenderResourceManager::ScanShaders(const fs::path& ShaderRoot)
{
	std::error_code ErrorCode;

	if (!fs::exists(ShaderRoot, ErrorCode))
	{
		LOG(Error, "[Shader] Scan Root Not Found : {}", ShaderRoot.generic_string());
		return;
	}

	for (const fs::directory_entry& Entry : fs::recursive_directory_iterator(ShaderRoot))
	{
		if (!Entry.is_regular_file())
			continue;
		if (Entry.path().extension() != ".hlsl")
			continue;
		if (Entry.path().filename().string().find("CS.hlsl") != std::string::npos)
			continue;
		if (Entry.path().filename().string().find("StaticMeshShader.hlsl") != std::string::npos)
		{
			CompileLightingVariants(Entry.path().generic_string());
		}

		FString Path = Entry.path().generic_string();
		LoadOrCompileShader(Path, nullptr);
	}
}



void FRenderResourceManager::CompileLightingVariants(FString Path)
{
	D3D_SHADER_MACRO defines[2] = {};
	defines[1] = {nullptr, nullptr};

	for (FLightingMacros LightingMacro : LightingMacros)
	{
		defines[0] = {LightingMacro.Macro, "1"};
		LoadOrCompileShader(Path, defines);
	}
}

void FRenderResourceManager::Shutdown()
{
	Get().VertexShaderMap.Empty();
	Get().PixelShaderMap.Empty();
	Get().ShaderProgramMap.Empty();
}

FShaderProgram* FRenderResourceManager::GetShaderProgram(const FString& InPath)
{
	if (TUniquePtr<FShaderProgram>* Found = Get().ShaderProgramMap.Find(InPath))
		return Found->get();

	LOG(Error, "[Shader] not found: {}", InPath);

	if (TUniquePtr<FShaderProgram>* Fallback = Get().ShaderProgramMap.Find("Resources/Shader/DefaultShader.hlsl"))
		return Fallback->get();

	return nullptr;
}

uint32 FRenderResourceManager::MakePSOKey(EPSOType Type, ELightingModel Model)
{
	return (static_cast<uint32>(Type) * static_cast<uint32>(ELightingModel::Count) 
		+ static_cast<uint32>(Model));
}


FPipelineState* FRenderResourceManager::GetPSO(const EPSOType& Intype, const ELightingModel InModel)
{
	// Get().을 붙여서 싱글톤 인스턴스의 멤버에 접근
	if (auto* Found = Get().PipelineStateMap.Find(MakePSOKey(Intype, InModel)))
	{
		return Found->get();
	}
	if (auto* Found = Get().PipelineStateMap.Find(MakePSOKey(Intype, ELightingModel::Unlit)))
	{
		return Found->get();
	}
	return nullptr;
}

void FRenderResourceManager::LoadOrCompileShader(const FString& Path, D3D_SHADER_MACRO* defines)
{
	FString VSCSOPath;
	FString PSCSOPath;
	FShaderByteCode VSCode = FRenderUtil::GetOrCompile(Path, defines, "mainVS", EShaderType::Vertex, VSCSOPath);
	FShaderByteCode PSCode = FRenderUtil::GetOrCompile(Path, defines, "mainPS", EShaderType::Pixel, PSCSOPath);
	FString MacroPath = Path;
	if (defines)
	{
		MacroPath = defines[0].Name != nullptr ? MacroPath = Path + "#" + defines[0].Name : Path;
	}
	
	
	if (!VSCode.IsValid() || !PSCode.IsValid())
	{
		LOG(Error, "[Shader] compile failed: {}", MacroPath);
		return;
	}

	TUniquePtr<FRHIVertexShader> Vs = FRenderCommand::CreateVertexShader(VSCode);
	TUniquePtr<FRHIPixelShader> Ps = FRenderCommand::CreatePixelShader(PSCode);

	if (!Vs || !Vs->IsValid() || !Ps || !Ps->IsValid())
	{
		LOG(Error, "[Shader] device create failed: {}", MacroPath);
		return;
	}

	FRHIVertexShader* VsRaw = Vs.get();
	FRHIPixelShader* PsRaw = Ps.get();

	VertexShaderMap[VSCSOPath] = std::move(Vs);
	PixelShaderMap[PSCSOPath] = std::move(Ps);

	ShaderProgramMap[MacroPath] = MakeUnique<FShaderProgram>(VsRaw, PsRaw);

	LOG(Info, "[Shader] loaded: {}", MacroPath);
}

void FRenderResourceManager::InitPipelineStates()
{
	static_assert(sizeof(PipelineTable) / sizeof(PipelineTable[0]) == static_cast<size_t>(EPSOType::Count), "PipelineTable size mismatch");
	for (size_t i = 0; i < static_cast<size_t>(EPSOType::Count); ++i)
	{
		const auto& Entry = PipelineTable[i];
		const EPSOType Type = Entry.Type;
		PipelineStateMap[MakePSOKey(Type, ELightingModel::Unlit)] =
			MakeUnique<FPipelineState>(GetShaderProgram(Entry.ShaderPath), Entry.Topology, Entry.RasterizerState, Entry.BlendState, Entry.DepthStencilState);

		if (Entry.Type == EPSOType::StaticMesh_Opaque || Entry.Type == EPSOType::StaticMesh_Translucent || Entry.Type == EPSOType::StaticMesh_Wireframe)
		{
			for (const FLightingMacros LightingMacro : LightingMacros)
			{
				FString MacroPath = FString(Entry.ShaderPath) + "#" + LightingMacro.Macro;

				PipelineStateMap[MakePSOKey(Type, LightingMacro.LightingModel)] =
					MakeUnique<FPipelineState>(GetShaderProgram(MacroPath), Entry.Topology, Entry.RasterizerState, Entry.BlendState, Entry.DepthStencilState);
			}
		}
	}
	LOG(Info, "Pipeline States Initialized from Table.");
}
