#pragma once

class FRHIBuffer
{
public:
	FRHIBuffer() = default;
	FRHIBuffer(ID3D11Device* Device, const D3D11_BUFFER_DESC& Desc, const void* InitialData = nullptr);
	virtual ~FRHIBuffer() = default;

	inline uint32 GetBufferSize() const { return BufferSize; }
	inline ID3D11Buffer* GetBuffer() const { return Buffer.Get(); }
	void ReCreateBuffer(ID3D11Device* Device, const D3D11_BUFFER_DESC& Desc, const void* InitialData = nullptr);

  protected:
	uint32 BufferSize = 0;
	ComPtr<ID3D11Buffer> Buffer = nullptr;
private:
};

class FRHIVertexBuffer : public FRHIBuffer
{
public:
	//static
	FRHIVertexBuffer(ID3D11Device* Device, const D3D11_BUFFER_DESC& Desc, uint32 InStride, const void* InitialData = nullptr);
	~FRHIVertexBuffer() override = default;

	inline uint32 GetStride() const { return Stride; }
private:
	uint32 Stride = 0;
};


class FRHIIndexBuffer : public FRHIBuffer
{
public:
	FRHIIndexBuffer(ID3D11Device* Device, const D3D11_BUFFER_DESC& Desc, uint32 Count, const void* InitialData = nullptr);
	~FRHIIndexBuffer() override = default;

	inline uint32 GetIndexCount() const { return IndexCount; }
private:
	uint32 IndexCount;

};


class FRHIUniformBuffer : public FRHIBuffer
{
public:
	FRHIUniformBuffer(ID3D11Device* Device, const D3D11_BUFFER_DESC& Desc);
	~FRHIUniformBuffer() override = default;

private:

};

class FRHIStructuredBuffer : public FRHIBuffer
{
public:
	FRHIStructuredBuffer(ID3D11Device* Device, uint32 ByteSizePerElement, uint32 ElementCount);
	~FRHIStructuredBuffer() override = default;
	inline ID3D11ShaderResourceView* GetSRV() const{return SRV.Get();}
	inline uint32 GetElementCount() const
	{
		return ElementCount;
	}
  private:
	uint32 ElementSize = 0;
	uint32 ElementCount = 0;
	ComPtr<ID3D11ShaderResourceView> SRV = nullptr;
};
