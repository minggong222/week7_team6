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

	REFLECT_START(ClassName)
	REFLECT_END()

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
		Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent())->GetMaterial(0)->BaseColor = {Color.R, Color.G, Color.B, Color.A};
		return LightData;
	}
};
