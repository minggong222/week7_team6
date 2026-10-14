#pragma once

#include "LightComponent.h"
#include "GameFramework/Actor.h"
#include "Math/Quat.h"
#include "Materials/Material.h"
#include "Components/BillboardComponent.h"
class FLineBatcher;

class UPointLightComponent : public ULightComponent
{
	DECLARE_CLASS(UPointLightComponent, ULightComponent)

	REFLECT_START(ClassName)
		PROPERTY(AttenuationRadius)
		PROPERTY(LightFalloffExponent)
	REFLECT_END()

public:
	UPointLightComponent() = default;
  virtual ~UPointLightComponent() override = default;

	virtual void DrawDebug(FLineBatcher* LineBatcher) const;

	float GetFalloff() const { return LightFalloffExponent; }
	void SetFalloff(float InFalloff) { LightFalloffExponent = InFalloff; }

	float GetAttenuationRadius() const { return AttenuationRadius; }
	void SetAttenuationRadius(float InRadius) { AttenuationRadius = InRadius; }

	const FLinearColor& GetLightColor() const { return LightColor; }
	void SetLightColor(const FLinearColor& InColor) { LightColor = InColor; }

	virtual ELightComponentType GetLightType() const override
	{
		return LightType_Point;
	};
	virtual FSphere GetBoundingSphere() const override
	{
		return {GetOwner()->GetActorLocation(), GetAttenuationRadius()};
	}

	virtual FLightData CreateLightData() const override
	{
		FLightData LightData{};
		LightData.LightType = static_cast<uint32>(LightType_Point);
		LightData.Position = GetOwner()->GetActorLocation();

		LightData.Direction = GetOwner()->GetActorTransform().GetForward();
		LightData.Intensity = Intensity;
		FLinearColor Color = GetLightColor();
		LightData.Color = {Color.R, Color.G, Color.B};
		UBillboardComponent* BillboardComponent = GetOwner()->FindComponentByClass<UBillboardComponent>();

		if (BillboardComponent)
		{
			UMaterial* Material = BillboardComponent->GetMaterial(0);
			Material->BaseColor = {Color.R, Color.G, Color.B, Color.A};
		}
		LightData.Radius = GetAttenuationRadius();
		LightData.LightFalloffExponent = GetFalloff();
		return LightData;
	}
  private:
	// 빛이 닿는 거리
	float AttenuationRadius = 100.0f;

	// Inner에서 Outer로 가며 밝기가 떨어지는 정도.
	float LightFalloffExponent = 1.0f;
};
