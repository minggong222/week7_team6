#pragma once
#include "Rendering/SceneData/FogSceneData.h"
#include "Rendering/SceneData/AmbientSceneData.h"
#include "Rendering/SceneData/FireballSceneData.h"
#include "Containers/Array.h"
#include "HAL\Platform.h"
#include "Components/LightComponent.h"
#include "Components/AmbientLightComponent.h"
#include "Rendering/Renderer.h"
#include "UObject\WeakObjectPtrTemplates.h"
//#include "Viewports\MultipleViewports.h"

// World에 배치된 것 중, 화면 결과에 영향을 주고, 모든 뷰에서 똑같은 것. 렌더에 넘기기 위함.
// ex) fog, skybox
struct FScene
{
	FFogSceneData FogSceneData;
	FAmbientSceneData AmbientSceneData;
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

		bool bAmbientSetted = false;

		// Temporary: include every valid light without culling.
		for (const auto& Light : Lights)
		{
			if (ULightComponent* LightComponent = Light.Get())
			{
				// 비활성화된 빛
				if (!LightComponent->bVisible)
				{
					continue;
				}

				if (UAmbientLightComponent* AmbientLightComponent = Cast<UAmbientLightComponent>(LightComponent))
				{
					// 앰비언트는 일반적인 경로로 지정하지 않음
					
					if (!bAmbientSetted)
					{
						// 앰비언트는 처음으로 발견한 하나만 지정
						AmbientSceneData.Color = AmbientLightComponent->GetLightColor();
						bAmbientSetted = true;
					}

					continue;
				}

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

	//void BuildTileLightData() // 미완성. 실컬링용
	//{
	//	LightDatas.Reset();
	//	TileLightIndices.Reset();
	//}
};
