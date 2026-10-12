#pragma once

#include "RHI/RHIShader.h"
#include "RHI/DynamicRHI.h"
#include "RHI/PipelineState.h"

enum class EPSOType : uint8
{
	StaticMesh_Opaque,       // 사과 5만 개 전용 (기본값)
	StaticMesh_Translucent,
	StaticMesh_Wireframe,
	Particle_AlphaBlend,
	Particle_Additive,
	Skybox,
	Grid,
	Outline_Mask,
	Outline_Draw,
	Billboard,
	StaticMesh_ViewDepth,
	Count
};

enum class ELightingModel : uint8
{
	Unlit,
	Gouraud,
	Lambert,
	Phong,
	Normal,
	Count
};

struct FLightingMacros
{
	const char* Macro;
	ELightingModel LightingModel;
};

class FRenderResourceManager
{
public:
	static FRenderResourceManager& Get()
	{
		static FRenderResourceManager* Instance = new FRenderResourceManager();
		return *Instance;
	}

	static void Init()
	{
		Get().ScanShaders("Resources/Shader");
		Get().InitPipelineStates();
	}

	void ScanShaders(const fs::path& ShaderRoot);

	void CompileLightingVariants(FString Path);

	static void Shutdown();

	static FShaderProgram* GetShaderProgram(const FString& InPath);

	static uint32 MakePSOKey(EPSOType Type, ELightingModel Model);

	static FPipelineState* GetPSO(const EPSOType& Intype, const ELightingModel InModel = ELightingModel::Unlit);

private:
	void LoadOrCompileShader(const FString& Path, D3D_SHADER_MACRO* defines);
	void InitPipelineStates();

	TMap<FString, TUniquePtr<FRHIVertexShader>> VertexShaderMap;
	TMap<FString, TUniquePtr<FRHIPixelShader>>  PixelShaderMap;
	TMap<FString, TUniquePtr<FShaderProgram>> ShaderProgramMap;


	TMap<uint32, TUniquePtr<FPipelineState>> PipelineStateMap;

	inline static const FLightingMacros LightingMacros[4] =
		{"LIGHTING_MODEL_GOURAUD", ELightingModel::Gouraud,
		"LIGHTING_MODEL_LAMBERT", ELightingModel::Lambert,
		"LIGHTING_MODEL_PHONG", ELightingModel::Phong,
		"NormalMap", ELightingModel::Normal};

};
