#include "EnginePCH.h"

#include "Rendering/RenderCommand.h"

#include "Material.h"
#include "UObject/UObjectGlobals.h"

#include "Engine/AssetManager.h"
#include "Serialization/TypeSerializer.h"

namespace
{
	// 저장된 경로로 텍스처를 찾고, 아직 로드 전이면 로드한다
	UTexture2D* FindOrLoadTexture(const FString& Path)
	{
		if (UTexture2D* Texture = UAssetManager::GetAssetByKey<UTexture2D>(Path))
		{
			return Texture;
		}
		return UAssetManager::Get().LoadTexture(Path);
	}
}

void UMaterial::SetTexture(EMaterialTextureSlot Slot, UTexture2D* Texture)
{
	const int32 Index = static_cast<int32>(Slot);
	while (static_cast<int32>(Textures.Num()) <= Index)
	{
		Textures.Add(nullptr);
	}
	Textures[Index] = Texture;
}

bool UMaterial::HasTexture(EMaterialTextureSlot Slot) const
{
	const int32 Index = static_cast<int32>(Slot);
	return Index < static_cast<int32>(Textures.Num()) && Textures[Index] != nullptr;
}

UMaterial* UMaterial::CreateInstance(const UMaterial* Source)
{
	if (Source == nullptr)
	{
		return nullptr;
	}

	UMaterial* Instance = FObjectFactory::ConstructObject<UMaterial>();

	Instance->PSOType = Source->PSOType;
	Instance->Textures = Source->Textures;
	Instance->SamplerState = Source->SamplerState;
	Instance->BaseColor = Source->BaseColor;
	Instance->SpecularColor = Source->SpecularColor;
	Instance->EmissiveColor = Source->EmissiveColor;
	Instance->Shininess = Source->Shininess;
	Instance->NormalScale = Source->NormalScale;
	Instance->bFlipNormalGreen = Source->bFlipNormalGreen;
	Instance->UVScrollSpeed = Source->UVScrollSpeed;

	// 원본에 파라미터 버퍼가 있으면 동일 크기로 새로 생성
	if (Source->ParamBuffer)
	{
		Instance->ParamBuffer = FRenderCommand::CreateConstantBuffer(Source->ParamBuffer->GetBufferSize());
	}

	Instance->BaseColor = Source->BaseColor;
	Instance->bIsInstance = true;
	Instance->Parent = Source;

	return Instance;
}

const UMaterial* UMaterial::GetBaseAsset() const
{
	for (const UMaterial* Current = this; Current; Current = Current->Parent)
	{
		if (!Current->GetPath().empty())
		{
			return Current;
		}
	}
	return nullptr;
}

// 머티리얼 정보를 JSON으로 저장
json UMaterial::SaveMaterial(const UMaterial* Material)
{
	json Out;

	if (!Material->GetPath().empty())
	{
		Out["Asset"] = Material->GetPath();
		return Out;
	}

	const UMaterial* Base = Material->GetBaseAsset();
	Out["Base"] = Base ? Base->GetPath() : FString("DefaultMaterial");
	Out["PSOType"] = static_cast<uint8>(Material->PSOType);
	Out["BaseColor"] = Material->BaseColor;
	Out["SpecularColor"] = Material->SpecularColor;
	Out["EmissiveColor"] = Material->EmissiveColor;
	Out["Shininess"] = Material->Shininess;
	Out["NormalScale"] = Material->NormalScale;
	Out["FlipNormal"] = Material->bFlipNormalGreen;
	Out["UVScrollSpeed"] = {
		Material->UVScrollSpeed.X,
		Material->UVScrollSpeed.Y
	};
	Out["SamplerState"] = Material->SamplerState == ESamplerState::LinearWrap ? "LinearWrap" : "LinearClamp";

	json Textures = json::array();
	for (UTexture2D* Texture : Material->Textures)
	{
		if (Texture && !Texture->GetPath().empty())
			Textures.push_back(Texture->GetPath());
		else
			Textures.push_back(nullptr);
	}
	Out["Textures"] = Textures;

	return Out;
}

UMaterial* UMaterial::LoadMaterial(const json& In)
{
	if (In.contains("Asset"))
	{
		return UAssetManager::GetAssetByKey<UMaterial>(In["Asset"].get<FString>());
	}

	UMaterial* Base = nullptr;
	if (In.contains("Base"))
	{
		Base = UAssetManager::GetAssetByKey<UMaterial>(In["Base"].get<FString>());
	}
	if (!Base)
	{
		Base = UAssetManager::GetAssetByKey<UMaterial>("DefaultMaterial");
	}

	UMaterial* Instance = UMaterial::CreateInstance(Base);
	if (!Instance)
	{
		return nullptr;
	}

	if (In.contains("PSOType"))
	{
		Instance->PSOType = static_cast<EPSOType>(In["PSOType"].get<uint8>());
	}
	else if (In.contains("BlendState"))
	{
		// 구버전 씬 파일 호환 처리
		const FString State = In["BlendState"].get<FString>();
		if (State == "AlphaBlend")
		{
			Instance->PSOType = EPSOType::StaticMesh_Translucent;
		}
	}

	if (In.contains("BaseColor"))
	{
		In["BaseColor"].get_to(Instance->BaseColor);
	}
	if (In.contains("SpecularColor"))
	{
		In["SpecularColor"].get_to(Instance->SpecularColor);
	}
	if (In.contains("EmissiveColor"))
	{
		In["EmissiveColor"].get_to(Instance->EmissiveColor);
	}
	if (In.contains("Shininess"))
	{
		Instance->Shininess = In["Shininess"].get<float>();
	}
	if (In.contains("NormalScale"))
	{
		Instance->NormalScale = In["NormalScale"].get<float>();
	}
	if (In.contains("FlipNormal"))
	{
		Instance->bFlipNormalGreen = In["FlipNormal"].get<bool>();
	}

	if (In.contains("Textures"))
	{
		Instance->Textures.Reset();
		for (const json& TexturePath : In["Textures"])
		{
			Instance->Textures.Add(TexturePath.is_null() ? nullptr : FindOrLoadTexture(TexturePath.get<FString>()));
		}
	}
	if (In.contains("UVScrollSpeed"))
	{
		const auto& UV = In["UVScrollSpeed"];
		Instance->UVScrollSpeed = FVector2D(UV[0].get<float>(), UV[1].get<float>());
	}
	if (In.contains("SamplerState"))
	{
		const FString State = In["SamplerState"].get<FString>();
		Instance->SamplerState = State == "LinearWrap" ? ESamplerState::LinearWrap : ESamplerState::LinearClamp;
	}

	return Instance;
}
