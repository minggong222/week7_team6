#include "EnginePCH.h"
#include "DirectionalLight.h"
#include "AssetManager.h"
#include "Rendering/RenderCommand.h"
#include <Components/DirectionalLightComponent.h>

ADirectionalLight::ADirectionalLight()
{

	// Root: Billboard
	UBillboardComponent* Billboard = CreateDefaultSubobject<UBillboardComponent>("UBillboardComponent");
	SetRootComponent(Billboard);

	UMaterial* BillboardMat = UMaterial::CreateInstance(UAssetManager::GetAssetByKey<UMaterial>("BillboardMaterial"));
	BillboardMat->Textures[0] = UAssetManager::GetAssetByKey<UTexture2D>("Assets/S_LightDirectional.png");
	BillboardMat->ParamBuffer = FRenderCommand::CreateConstantBuffer(256);
	Billboard->SetMaterial(0, BillboardMat);

	SetBillboardComponent(Billboard);

	// Billboard -> DirectionalLight
	UDirectionalLightComponent* DirectionalLight = CreateDefaultSubobject<UDirectionalLightComponent>("UDirectionalLightComponent");
	DirectionalLight->SetupAttachment(Billboard);
	SetLightComponent(DirectionalLight);

	// Billboard -> Arrow
	UArrowComponent* Arrow = CreateDefaultSubobject<UArrowComponent>("UArrowComponent");
	FRotator Rotation{90.0f, 0.0f, 0.0f};
	Arrow->SetRelativeRotation(Rotation);
	Arrow->SetupAttachment(Billboard);

	SetArrowComponent(Arrow);
}

void ADirectionalLight::DuplicateSubObjects()
{
	Super::DuplicateSubObjects();
	ArrowComponent = FindComponentByClass<UArrowComponent>();
}
