#include "EnginePCH.h"
#include "PointLight.h"
#include "AssetManager.h"
#include "Rendering/RenderCommand.h"
#include <Components/PointLightComponent.h>

APointLight::APointLight()
{
	UBillboardComponent* Billboard = CreateDefaultSubobject<UBillboardComponent>("UBillboardComponent");
	SetRootComponent(Billboard);
	UMaterial* BillboardMat = UMaterial::CreateInstance(UAssetManager::GetAssetByKey<UMaterial>("BillboardMaterial"));
	BillboardMat->Textures[0] = UAssetManager::GetAssetByKey<UTexture2D>("Assets/S_LightPoint.png");
	BillboardMat->ParamBuffer = FRenderCommand::CreateConstantBuffer(256);
	Billboard->SetMaterial(0, BillboardMat);
	SetBillboardComponent(Billboard);

	UPointLightComponent* PointLight = CreateDefaultSubobject<UPointLightComponent>("UPointLightComponent");
	PointLight->SetupAttachment(Billboard);
	SetLightComponent(PointLight);
}

void APointLight::DuplicateSubObjects()
{
	Super::DuplicateSubObjects();
}
