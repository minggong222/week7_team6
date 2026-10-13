#pragma once

#include "PointLightComponent.h"
#include "GameFramework/Actor.h"
#include "Math/Quat.h"
#include "Materials/Material.h"
#include "Components/BillboardComponent.h"
class FLineBatcher;

class USpotLightComponent : public UPointLightComponent
{
	DECLARE_CLASS(USpotLightComponent, UPointLightComponent)

	REFLECT_START(ClassName)
		PROPERTY(InnerConeAngle)
		PROPERTY(OuterConeAngle)
	REFLECT_END()

public:
	USpotLightComponent() = default;
	virtual ~USpotLightComponent() override = default;

	virtual void DrawDebug(FLineBatcher* LineBatcher) const override;

	float GetInnerConeAngle() const { return InnerConeAngle; }
	void SetInnerConeAngle(float InAngle) { InnerConeAngle = InAngle; }

	float GetOuterConeAngle() const { return OuterConeAngle; }
	void SetOuterConeAngle(float InAngle) { OuterConeAngle = InAngle; }

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
		UBillboardComponent* BillboardComponent = GetOwner()->FindComponentByClass<UBillboardComponent>();

		if (BillboardComponent)
		{
			UMaterial* Material = BillboardComponent->GetMaterial(0);
			Material->BaseColor = {Color.R, Color.G, Color.B, Color.A};
		}
		LightData.Radius = GetAttenuationRadius();
		LightData.InnerConeCos = cosf(GetInnerConeAngle() * PI / 180);
		LightData.OuterConeCos = cosf(GetOuterConeAngle() * PI / 180);
		LightData.LightFalloffExponent = GetFalloff();
		return LightData;
	}
  private:
	// 원뿔 중심축 기준 반각(도). 
	float InnerConeAngle = 15.0f;
	float OuterConeAngle = 30.0f;
};
