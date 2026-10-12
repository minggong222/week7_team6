#include "EnginePCH.h"
#include "Engine/SpotLight.h"
#include "AssetManager.h"
#include "Rendering/RenderCommand.h"

ASpotLight::ASpotLight()
{
	UBillboardComponent* Billboard = CreateDefaultSubobject<UBillboardComponent>("UBillboardComponent");
	SetRootComponent(Billboard);
	UMaterial* BillboardMat = UMaterial::CreateInstance(UAssetManager::GetAssetByKey<UMaterial>("BillboardMaterial"));
	BillboardMat->Textures[0] = UAssetManager::GetAssetByKey<UTexture2D>("Assets/S_LightSpot.png");
	BillboardMat->ParamBuffer = FRenderCommand::CreateConstantBuffer(256);
	Billboard->SetMaterial(0, BillboardMat);
	SetBillboardComponent(Billboard);

	USpotLightComponent* SpotLight = CreateDefaultSubobject<USpotLightComponent>("USpotLightComponent");
	SpotLight->SetupAttachment(Billboard);
	SetLightComponent(SpotLight);
}

void ASpotLight::DuplicateSubObjects()
{
	Super::DuplicateSubObjects();
}
