#include "EnginePCH.h"
#include "DirectionalLight.h"
#include "AssetManager.h"
#include "Rendering/RenderCommand.h"
#include "Components/DirectionalLightComponent.h"

ADirectionalLight::ADirectionalLight()
{

	// Root: DirectionalLight
	UDirectionalLightComponent* DirectionalLight = CreateDefaultSubobject<UDirectionalLightComponent>("UDirectionalLightComponent");
	SetRootComponent(DirectionalLight);
	SetLightComponent(DirectionalLight);

	// DirectionalLight -> Billboard
	UBillboardComponent* Billboard = CreateDefaultSubobject<UBillboardComponent>("UBillboardComponent");
	Billboard->SetIsEditorOnly(true);
	Billboard->SetIsVisualizationComponent(true);
	Billboard->SetupAttachment(DirectionalLight);

	UMaterial* BillboardMat = UMaterial::CreateInstance(UAssetManager::GetAssetByKey<UMaterial>("BillboardMaterial"));
	BillboardMat->Textures[0] = UAssetManager::GetAssetByKey<UTexture2D>("Assets/S_LightDirectional.png");
	BillboardMat->ParamBuffer = FRenderCommand::CreateConstantBuffer(256);
	Billboard->SetMaterial(0, BillboardMat);

	SetBillboardComponent(Billboard);

	// DirectionalLight -> Arrow
	UArrowComponent* Arrow = CreateDefaultSubobject<UArrowComponent>("UArrowComponent");
	FRotator Rotation{90.0f, 0.0f, 0.0f};
	Arrow->SetRelativeRotation(Rotation);
	Arrow->SetupAttachment(DirectionalLight);
}

void ADirectionalLight::DuplicateSubObjects()
{
	Super::DuplicateSubObjects();
	ArrowComponent = FindComponentByClass<UArrowComponent>();
}
