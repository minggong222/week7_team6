#pragma once

#include "LightComponent.h"
#include "GameFramework/Actor.h"
#include "Math/Quat.h"
#include "Materials/Material.h"
#include <Templates/Casts.h>
class FLineBatcher;

class UDirectionalLightComponent : public ULightComponent
{
	DECLARE_CLASS(UDirectionalLightComponent, ULightComponent)

public:
	
	UDirectionalLightComponent() = default;
  virtual ~UDirectionalLightComponent() override = default;

	void DrawDebug(FLineBatcher* LineBatcher) const;

	virtual ELightComponentType GetLightType() const override
	{
		return LightType_Directional;
	};

	virtual FLightData CreateLightData() const override
	{
		FLightData LightData{};
		LightData.LightType = static_cast<uint32>(LightType_Directional);
		LightData.Position = GetOwner()->GetActorLocation();

		LightData.Direction = GetOwner()->GetActorTransform().GetForward();
		LightData.Intensity = Intensity;

		FLinearColor Color = GetLightColor();
		LightData.Color = {Color.R, Color.G, Color.B};

		UPrimitiveComponent* PrimComponent = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());

		if (PrimComponent)
		{
			UMaterial* Material = PrimComponent->GetMaterial(0);
			Material->BaseColor = {Color.R, Color.G, Color.B, Color.A};
		}

		return LightData;
	}
};
