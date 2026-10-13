#pragma once

#include <imgui.h>
#include <Windows.h>

#include <d3d11.h>

class FImGuiRenderer
{
public:
	FImGuiRenderer() = default;
	~FImGuiRenderer();

	bool Init(class FWindowsWindow* Window, ID3D11Device* Device, ID3D11DeviceContext* DeviceContext);
	void Begin();
	void End();
	void Shutdown();
	
	void CreateStyle();
	void UpdateScale(float Scale);

  private:
	float LastScale = 1.0f;

	ImGuiStyle OriginalStyle;

};