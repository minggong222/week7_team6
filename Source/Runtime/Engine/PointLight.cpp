#include "EnginePCH.h"
#include "PointLight.h"
#include "AssetManager.h"
#include "Rendering/RenderCommand.h"
#include <Components/PointLightComponent.h>

APointLight::APointLight()
{
	UPointLightComponent* PointLight = CreateDefaultSubobject<UPointLightComponent>("UPointLightComponent");
	SetRootComponent(PointLight);
	SetLightComponent(PointLight);

	UBillboardComponent* Billboard = CreateDefaultSubobject<UBillboardComponent>("UBillboardComponent");
	Billboard->SetIsEditorOnly(true);
	Billboard->SetIsVisualizationComponent(true);
	Billboard->SetupAttachment(PointLight);

	UMaterial* BillboardMat = UMaterial::CreateInstance(UAssetManager::GetAssetByKey<UMaterial>("BillboardMaterial"));
	BillboardMat->Textures[0] = UAssetManager::GetAssetByKey<UTexture2D>("Assets/S_LightPoint.png");
	BillboardMat->ParamBuffer = FRenderCommand::CreateConstantBuffer(256);
	Billboard->SetMaterial(0, BillboardMat);
	SetBillboardComponent(Billboard);


}

void APointLight::DuplicateSubObjects()
{
	Super::DuplicateSubObjects();
}
