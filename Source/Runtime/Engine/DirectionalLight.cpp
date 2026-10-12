#include "EnginePCH.h"
#include "DirectionalLight.h"
#include "AssetManager.h"
#include "Rendering/RenderCommand.h"
#include <Components/DirectionalLightComponent.h>

ADirectionalLight::ADirectionalLight()
{
	UBillboardComponent* Billboard = CreateDefaultSubobject<UBillboardComponent>("UBillboardComponent");
	SetRootComponent(Billboard);
	UMaterial* BillboardMat = UMaterial::CreateInstance(UAssetManager::GetAssetByKey<UMaterial>("BillboardMaterial"));
	BillboardMat->Textures[0] = UAssetManager::GetAssetByKey<UTexture2D>("Assets/S_LightDirectional.png");
	BillboardMat->ParamBuffer = FRenderCommand::CreateConstantBuffer(256);
	Billboard->SetMaterial(0, BillboardMat);
	SetBillboardComponent(Billboard);

	UDirectionalLightComponent* DirectionalLight = CreateDefaultSubobject<UDirectionalLightComponent>("UDirectionalLightComponent");
	DirectionalLight->SetupAttachment(Billboard);
	SetLightComponent(DirectionalLight);
}

void ADirectionalLight::DuplicateSubObjects()
{
}
