#include "EnginePCH.h"
#include "RHI/RHIBuffer.h"

FRHIBuffer::FRHIBuffer(ID3D11Device* Device, const D3D11_BUFFER_DESC& Desc, const void* InitialData)
{
	BufferSize = Desc.ByteWidth;

	HRESULT hr;
	if (InitialData)
	{
		D3D11_SUBRESOURCE_DATA Data;
		Data.pSysMem = InitialData;

		hr = Device->CreateBuffer(&Desc, &Data, Buffer.GetAddressOf());
	}
	else
	{
		hr = Device->CreateBuffer(&Desc, nullptr, Buffer.GetAddressOf());
	}
	if (FAILED(hr))
	{
		LOG(Warning, "Failed To Create Buffer!");
	}
}

void FRHIBuffer::ReCreateBuffer(ID3D11Device* Device, const D3D11_BUFFER_DESC& Desc, const void* InitialData)
{
	Buffer.Reset();
	BufferSize = Desc.ByteWidth;
	
	HRESULT hr;
	if (InitialData)
	{
		D3D11_SUBRESOURCE_DATA Data;
		Data.pSysMem = InitialData;

		hr = Device->CreateBuffer(&Desc, &Data, Buffer.GetAddressOf());
	}
	else
	{
		hr = Device->CreateBuffer(&Desc, nullptr, Buffer.GetAddressOf());
	}
	if (FAILED(hr))
	{
		LOG(Warning, "Failed To Create Buffer!");
	}
}

FRHIUniformBuffer::FRHIUniformBuffer(ID3D11Device* Device, const D3D11_BUFFER_DESC& Desc)
	:FRHIBuffer(Device, Desc)
{
}

FRHIVertexBuffer::FRHIVertexBuffer(ID3D11Device* Device, const D3D11_BUFFER_DESC& Desc, uint32 InStride, const void* InitialData)
	:FRHIBuffer(Device, Desc, InitialData)
{
	Stride = InStride;
}

FRHIIndexBuffer::FRHIIndexBuffer(ID3D11Device* Device, const D3D11_BUFFER_DESC& Desc, uint32 Count, const void* InitialData)
	:FRHIBuffer(Device, Desc, InitialData)
{
	IndexCount = Count;
}


FRHIStructuredBuffer::FRHIStructuredBuffer(ID3D11Device* Device, uint32 ByteSizePerElement, uint32 ElementCount)
{
	D3D11_BUFFER_DESC StructuredBufferDesc = {};
	StructuredBufferDesc.ByteWidth = ByteSizePerElement * ElementCount;
	StructuredBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	StructuredBufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	StructuredBufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	StructuredBufferDesc.StructureByteStride = ByteSizePerElement;
	ReCreateBuffer(Device, StructuredBufferDesc, nullptr);

	D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
	SRVDesc.Format = DXGI_FORMAT_UNKNOWN;
	SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	SRVDesc.Buffer.FirstElement = 0;
	SRVDesc.Buffer.NumElements = ElementCount;
	Device->CreateShaderResourceView(Buffer.Get(), &SRVDesc, SRV.GetAddressOf());
	D3D11_UNORDERED_ACCESS_VIEW_DESC UAVDesc = {};
	UAVDesc.Format = DXGI_FORMAT_UNKNOWN;
	UAVDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	UAVDesc.Buffer.FirstElement = 0;
	UAVDesc.Buffer.NumElements = ElementCount;
	if (FAILED(Device->CreateUnorderedAccessView(Buffer.Get(), &UAVDesc, UAV.GetAddressOf())))
		LOG(Warning, "Failed To Create Structured Buffer UAV!");
	ElementSize = ByteSizePerElement;
	this->ElementCount = ElementCount;
}
