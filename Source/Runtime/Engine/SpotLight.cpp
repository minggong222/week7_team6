#include "EnginePCH.h"
#include "SpotLight.h"
#include "AssetManager.h"
#include "Rendering/RenderCommand.h"
ASpotLight::ASpotLight()
{
	BillboardComponent = CreateDefaultSubobject<UBillboardComponent>("UBillboardComponent");
	SetRootComponent(BillboardComponent);
	UMaterial* BillboardMat = UMaterial::CreateInstance(UAssetManager::GetAssetByKey<UMaterial>("BillboardMaterial"));
	BillboardMat->Textures[0] = UAssetManager::GetAssetByKey<UTexture2D>("Assets/SpotLight_64x.png");
	BillboardMat->ParamBuffer = FRenderCommand::CreateConstantBuffer(256);
	BillboardComponent->SetMaterial(0, BillboardMat);
	// 아이콘용 메시/머티리얼. Plane은 FVertex 포맷이라 DefaultShader로 그대로 그려진다.
	// 라이트 아이콘 텍스처가 준비되면 Property 창에서 머티리얼 슬롯에 끼우면 된다.

	SpotLightComponent = CreateDefaultSubobject<USpotLightComponent>("USpotLightComponent");
	SpotLightComponent->SetupAttachment(BillboardComponent);
}

void ASpotLight::DuplicateSubObjects()
{
	Super::DuplicateSubObjects();
	SpotLightComponent = FindComponentByClass<USpotLightComponent>();
	BillboardComponent = FindComponentByClass<UBillboardComponent>();
}
