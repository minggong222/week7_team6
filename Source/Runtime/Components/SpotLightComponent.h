#pragma once

#include "LightComponent.h"
#include "GameFramework/Actor.h"
#include "Math/Quat.h"
#include "Materials/Material.h"
class FLineBatcher;

class USpotLightComponent : public ULightComponent
{
	DECLARE_CLASS(USpotLightComponent, ULightComponent)

	REFLECT_START(ClassName)
		PROPERTY(InnerConeAngle)
		PROPERTY(OuterConeAngle)
		PROPERTY(AttenuationRadius)
		PROPERTY(Falloff)
	REFLECT_END()

public:
	USpotLightComponent() = default;
	virtual ~USpotLightComponent() override = default;

	void DrawDebug(FLineBatcher* LineBatcher) const;

	float GetInnerConeAngle() const { return InnerConeAngle; }
	void SetInnerConeAngle(float InAngle) { InnerConeAngle = InAngle; }

	float GetOuterConeAngle() const { return OuterConeAngle; }
	void SetOuterConeAngle(float InAngle) { OuterConeAngle = InAngle; }

	float GetFalloff() const { return Falloff; }
	void SetFalloff(float InFalloff) { Falloff = InFalloff; }

	float GetAttenuationRadius() const { return AttenuationRadius; }
	void SetAttenuationRadius(float InRadius) { AttenuationRadius = InRadius; }

	float GetAttenuationLinear() const { return AttenuationLinear; }
	void SetAttenuationLinear(float InAttenuationLinear) { AttenuationLinear = InAttenuationLinear; }

	float GetAttenuationQuadratic() const { return AttenuationQuadratic; }
	void SetAttenuationQuadratic(float InAttenuationQuadratic) { AttenuationQuadratic = InAttenuationQuadratic; }

	const FLinearColor& GetLightColor() const { return LightColor; }
	void SetLightColor(const FLinearColor& InColor) { LightColor = InColor; }

	virtual ELightComponentType GetLightType() const override
	{
		return LightType_Spot;
	};

	virtual FLightData CreateLightData() const override
	{
		FLightData LightData{};
		LightData.LightType = static_cast<uint32>(LightType_Spot);
		LightData.Position = GetOwner()->GetActorLocation();

		LightData.Direction = GetOwner()->GetActorTransform().GetForward();
		LightData.Intensity = Intensity;
		FLinearColor Color = GetLightColor();
		LightData.Color = {Color.R, Color.G, Color.B};
		Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent())->GetMaterial(0)->BaseColor = {Color.R, Color.G, Color.B, Color.A};
		LightData.Radius = GetAttenuationRadius();
		LightData.InnerConeCos = cosf(GetInnerConeAngle() * PI / 180);
		LightData.OuterConeCos = cosf(GetOuterConeAngle() * PI / 180);
		LightData.AttenuationLinear = GetAttenuationLinear();
		LightData.AttenuationQuadratic = GetAttenuationQuadratic();
		return LightData;
	}
  private:
	// 원뿔 중심축 기준 반각(도). 
	float InnerConeAngle = 15.0f;
	float OuterConeAngle = 30.0f;

	// 빛이 닿는 거리
	float AttenuationRadius = 100.0f;

	// Inner에서 Outer로 가며 밝기가 떨어지는 정도.
	float Falloff = 1.0f;

	float AttenuationLinear = 0.0f;
	float AttenuationQuadratic = 0.02f;
};
