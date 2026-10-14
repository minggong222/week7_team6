#include "EnginePCH.h"
#include "AmbientLight.h"
#include "AssetManager.h"
#include "Rendering/RenderCommand.h"
#include "Components/AmbientLightComponent.h"

AAmbientLight::AAmbientLight()
{

	// Root: AmbientLight
	UAmbientLightComponent* AmbientLight = CreateDefaultSubobject<UAmbientLightComponent>("UAmbientLightComponent");
	SetRootComponent(AmbientLight);
	SetLightComponent(AmbientLight);

	// AmbientLight -> Billboard
	UBillboardComponent* Billboard = CreateDefaultSubobject<UBillboardComponent>("UBillboardComponent");
	Billboard->SetIsEditorOnly(true);
	Billboard->SetIsVisualizationComponent(true);
	Billboard->SetupAttachment(AmbientLight);

	UMaterial* BillboardMat = UMaterial::CreateInstance(UAssetManager::GetAssetByKey<UMaterial>("BillboardMaterial"));
	BillboardMat->Textures[0] = UAssetManager::GetAssetByKey<UTexture2D>("Assets/SkyLight.png");
	BillboardMat->ParamBuffer = FRenderCommand::CreateConstantBuffer(256);
	Billboard->SetMaterial(0, BillboardMat);

	SetBillboardComponent(Billboard);
}