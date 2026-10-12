#pragma once

#include "RHI/RHIDefinitions.h"

struct FShaderByteCode
{
	ComPtr<ID3DBlob> Blob;

	const void* GetData() const { return Blob ? Blob->GetBufferPointer() : nullptr; }
	SIZE_T      GetSize() const { return Blob ? Blob->GetBufferSize() : 0; }
	bool        IsValid() const { return Blob != nullptr; }
};

class FRenderUtil
{
public:
	static FShaderByteCode CompileShader(FString Path, D3D_SHADER_MACRO* defines, const char* EntryPoint, EShaderType ShaderType);
	static void SaveByteCode(FString TargetPath, const FShaderByteCode& ByteCode);
	static FShaderByteCode LoadByteCode(const FString& CsoPath);

	static FString GetCsoPath(const FString& ShaderPath, EShaderType ShaderType, D3D_SHADER_MACRO* defines);
	static bool IsCsoUpToDate(const FString& ShaderPath, const FString& CsoPath);
	static FShaderByteCode GetOrCompile(const FString& ShaderPath, D3D_SHADER_MACRO* defines, const char* EntryPoint, EShaderType ShaderType, FString& OutCSOPath);
};