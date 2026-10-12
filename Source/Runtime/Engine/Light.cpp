#include "EnginePCH.h"
#include "Light.h"
#include "AssetManager.h"
#include "Rendering/RenderCommand.h"
ALight::ALight()
{
	
}

void ALight::DuplicateSubObjects()
{
	Super::DuplicateSubObjects();
	LightComponent = FindComponentByClass<ULightComponent>();
	BillboardComponent = FindComponentByClass<UBillboardComponent>();
}
