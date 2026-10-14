#include "EnginePCH.h"
#include "ArrowComponent.h"
#include "Engine/AssetManager.h"
#include "Rendering/RenderCommand.h"
#include "Materials/Material.h"


// StaticMesh 컴포넌트를 초기화한다.
UArrowComponent::UArrowComponent()
{
	bIsVisualizationComponent = true;
	bIsEditorOnly = true;

	SetStaticMesh(UAssetManager::GetAssetByKey<UStaticMesh>("Arrow"));
	UMaterial* Material = UMaterial::CreateInstance(UAssetManager::GetAssetByKey<UMaterial>("DefaultMaterial"));

	Material->bDisableShading = true;
	Material->BaseColor = FVector4{0.5f, 0.5f, 1.0f, 1.0f};
	Material->ParamBuffer = FRenderCommand::CreateConstantBuffer(256);
	
	SetMaterial(0, Material);
}

// StaticMesh 컴포넌트의 소멸을 처리한다.
UArrowComponent::~UArrowComponent()
{
}

void UArrowComponent::Serialize(json& Handle, bool bIsLoading)
{
	Super::Serialize(Handle, bIsLoading);
	if (!bIsLoading)
		return;

	UMaterial* Material = GetMaterial(0);
	if (!Material)
		return;

	// 공유 에셋 머티리얼이면 화살표 전용 인스턴스에서 설정한다.
	if (!Material->bIsInstance)
	{
		Material = UMaterial::CreateInstance(Material);
		if (!Material)
			return;
		SetMaterial(0, Material);
	}

	Material->bDisableShading = true;
}
