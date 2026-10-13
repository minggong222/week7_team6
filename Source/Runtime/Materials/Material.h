#pragma once

#include "MaterialInterface.h"
#include "RHI/RHIStaticStates.h"
#include "Engine/Texture2D.h"
#include "Rendering/RenderResourceManager.h"


struct FStaticMeshMaterialParams
{
	FVector4 BaseColor;
	FVector2D UVOffset;
	float bOpaque; // 불투명 여부 플래그
	float bHasNormalMap;
	FVector SpecularColor;
	float Shininess;
	FVector EmissiveColor;
	float NormalScale;
	float bHasSpecularMap;
	float bHasEmissiveMap;
	float bDisableShading;
	float Padding;
};
struct FBillboardMaterialParams
{
	FVector4 BaseColor;
};
// StaticMeshShader.hlsl의 MaterialParams(b1)와 같은 레이아웃이어야 한다
static_assert(sizeof(FStaticMeshMaterialParams) == 80);

// StaticMeshShader.hlsl의 텍스처 레지스터 번호. UMaterial::Textures의 인덱스와 같다
// t4부터는 라이트 버퍼가 쓰므로 여기에 슬롯을 추가하려면 라이트 버퍼를 먼저 옮겨야 한다
enum class EMaterialTextureSlot : uint8
{
	Diffuse = 0,
	Normal = 1,
	Specular = 2,
	Emissive = 3,
};

class UMaterial : public UMaterialInterface
{
	DECLARE_CLASS(UMaterial, UMaterialInterface)

public:
	UMaterial() = default;
	virtual ~UMaterial() override = default;

	EPSOType PSOType = EPSOType::StaticMesh_Opaque;
	// 인덱스가 곧 t 레지스터 번호다. StaticMesh는 EMaterialTextureSlot 순서를 따르고, 없는 슬롯은 nullptr이다
	TArray<UTexture2D*> Textures;
	TUniquePtr<FRHIUniformBuffer> ParamBuffer;
	ESamplerState SamplerState = ESamplerState::LinearClamp;

	FVector4 BaseColor = FVector4(1, 1, 1, 1);
	FVector SpecularColor = FVector(0.0f, 0.0f, 0.0f);
	FVector EmissiveColor = FVector(0.0f, 0.0f, 0.0f);
	float Shininess = 32.0f;
	float NormalScale = 1.0f;
	FVector2D UVScrollSpeed = FVector2D(0.0f, 0.0f);
	bool bDisableShading = false;

	bool bIsInstance = false;

	// 인스턴스가 복제된 원본 머티리얼
	const UMaterial* Parent = nullptr;

	// 슬롯이 배열 범위 밖이면 사이를 nullptr로 채운다
	void SetTexture(EMaterialTextureSlot Slot, UTexture2D* Texture);
	bool HasTexture(EMaterialTextureSlot Slot) const;

	// 원본 머티리얼 복제본 생성
	static UMaterial* CreateInstance(const UMaterial* Source);

	// 최상위 에셋 머티리얼 반환
	const UMaterial* GetBaseAsset() const;

	static json SaveMaterial(const UMaterial* Material);
	static UMaterial* LoadMaterial(const json& In);
};
