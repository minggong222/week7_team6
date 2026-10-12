#pragma once

#include "Editor/EditorUI/EditorPanel.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextRenderActor.h"
#include "Components/RotatingMovementComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/FireballComponent.h"
#include "Logging/LogMacros.h"

class UWorld;
class USceneComponent;

class FDetailsPanel : public IEditorPanel
{
  public:
	FDetailsPanel() = default;
	~FDetailsPanel();

	bool Init() override;
	void Tick(float DeltaTime) override;
	void OnRender() override;
	const char* GetPanelName() const override
	{
		return "Details";
	}

	void SetTarget(UObject* InTargetOrNull)
	{
		Target = InTargetOrNull;
	}

	void SetWorld(UWorld* InWorld)
	{
		World = InWorld;
	}

  private:
	UWorld* World = nullptr;
	UObject* Target = nullptr;

	int32 SelectedIndex = 0;

	// TODO: 나중에 TObjectIterator로 변경
	const char* ComponentList[7] = {
		"StaticMeshComponent",
		"TextRenderComponent",
		"RotatingMovementComponent",
		"UDirectionalLightComponent",
		"BillboardComponent",
		"ExponentialHeightFogComponent",
		"FireballComponent",
	};

	TArray<UClass*> Classes{
		UStaticMeshComponent::StaticClass(),
		UTextRenderComponent::StaticClass(),
		URotatingMovementComponent::StaticClass(),
		UDirectionalLightComponent::StaticClass(),
		UBillboardComponent::StaticClass(),
		UExponentialHeightFogComponent::StaticClass(),
		UFireballComponent::StaticClass(),
	};
};
