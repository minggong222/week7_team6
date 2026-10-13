#include "EnginePCH.h"
#include "Engine/SpotLight.h"
#include "AssetManager.h"
#include "Rendering/RenderCommand.h"

ASpotLight::ASpotLight()
{
	// Root: SpotLight
	USpotLightComponent* SpotLight = CreateDefaultSubobject<USpotLightComponent>("USpotLightComponent");
	SetRootComponent(SpotLight);
	SetLightComponent(SpotLight);

	// SpotLight -> Billboard
	UBillboardComponent* Billboard = CreateDefaultSubobject<UBillboardComponent>("UBillboardComponent");
	Billboard->SetIsEditorOnly(true);
	Billboard->SetIsVisualizationComponent(true);
	Billboard->SetupAttachment(SpotLight);

	UMaterial* BillboardMat = UMaterial::CreateInstance(UAssetManager::GetAssetByKey<UMaterial>("BillboardMaterial"));
	BillboardMat->Textures[0] = UAssetManager::GetAssetByKey<UTexture2D>("Assets/S_LightSpot.png");
	BillboardMat->ParamBuffer = FRenderCommand::CreateConstantBuffer(256);
	Billboard->SetMaterial(0, BillboardMat);
	SetBillboardComponent(Billboard);

	// SpotLight -> Arrow
	UArrowComponent* Arrow = CreateDefaultSubobject<UArrowComponent>("UArrowComponent");
	FRotator Rotation{90.0f, 0.0f, 0.0f};
	Arrow->SetRelativeRotation(Rotation);
	Arrow->SetupAttachment(SpotLight);
}

void ASpotLight::DuplicateSubObjects()
{
	Super::DuplicateSubObjects();
	ArrowComponent = FindComponentByClass<UArrowComponent>();
}
