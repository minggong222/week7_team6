#pragma once
#include "FogSceneData.h"
#include "FireballSceneData.h"
#include "Containers/Array.h"
#include "HAL\Platform.h"
#include "Components/LightComponent.h"
#include "Rendering/Renderer.h"
#include "UObject\WeakObjectPtrTemplates.h"
#include "Rendering\RenderingInfo.h"

// World에 배치된 것 중, 화면 결과에 영향을 주고, 모든 뷰에서 똑같은 것. 렌더에 넘기기 위함.
// ex) fog, skybox
struct FScene
{
	FFogSceneData FogSceneData;
    TArray<FFireballSceneData> Fireballs;
	TArray<TWeakObjectPtr<ULightComponent>> Lights;

	// TODO: 렌더러 쪽으로...
	TArray<FLightData> LightDatas;
	FTileLightHeader TileLightHeader[TileCountX * TileCountY] = {};
	TArray<uint32> TileLightIndices;

	// TODO: 렌더러 쪽으로...
	void BuildTileLightData()
	{
		LightDatas.Reset();
		TileLightIndices.Reset();

		// Temporary: include every valid light without culling.
		for (const auto& Light : Lights)
		{
			if (ULightComponent* LightComponent = Light.Get())
			{
				const uint32 LightIndex = static_cast<uint32>(LightDatas.Num());
				LightDatas.Add(LightComponent->CreateLightData());
				TileLightIndices.Add(LightIndex);
			}
		}

		for (FTileLightHeader& Header : TileLightHeader)
		{
			Header.Offset = 0;
			Header.Count = static_cast<uint32>(TileLightIndices.Num());
		}
	}

	void BuildTileLightData(const FRenderingInfo& RenderingInfo) // 미완성. 실컬링용
	{
		LightDatas.Reset();
		TileLightIndices.Reset();
	}
};
