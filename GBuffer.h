#pragma once
#include <windows.h>
#include <d3d12.h>
#include <wrl.h>
#include "d3dx12.h"

using Microsoft::WRL::ComPtr;

class GBuffer
{
public:
	GBuffer();
	~GBuffer();

	bool Initialize(ID3D12Device* device, UINT width, UINT height);
	void Resize(ID3D12Device* device, UINT width, UINT height);

	ID3D12Resource* GetPositionBuffer() const { return positionBuffer.Get(); }
	ID3D12Resource* GetNormalBuffer() const { return normalBuffer.Get(); }
	ID3D12Resource* GetAlbedoBuffer() const { return albedoBuffer.Get(); }
	ID3D12Resource* GetDepthBuffer() const { return depthBuffer.Get(); }

	D3D12_CPU_DESCRIPTOR_HANDLE GetPositionRTV() const { return positionRTV; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetNormalRTV() const { return normalRTV; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetAlbedoRTV() const { return albedoRTV; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetDSV() const { return dsv; }

	D3D12_CPU_DESCRIPTOR_HANDLE GetPositionSRV() const { return positionSRV; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetNormalSRV() const { return normalSRV; }
	D3D12_CPU_DESCRIPTOR_HANDLE GetAlbedoSRV() const { return albedoSRV; }

	D3D12_GPU_DESCRIPTOR_HANDLE GetPositionSRVGPU() const { return positionSRVGPU; }
	D3D12_GPU_DESCRIPTOR_HANDLE GetNormalSRVGPU() const { return normalSRVGPU; }
	D3D12_GPU_DESCRIPTOR_HANDLE GetAlbedoSRVGPU() const { return albedoSRVGPU; }

	void TransitionToRenderTarget(ID3D12GraphicsCommandList* commandList);
	void TransitionToShaderResource(ID3D12GraphicsCommandList* commandList);

	void ClearRenderTargets(ID3D12GraphicsCommandList* commandList);

private:
	bool CreateRenderTarget(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format, ComPtr<ID3D12Resource>& buffer);

	UINT width;
	UINT height;

	ComPtr<ID3D12Resource> positionBuffer;
	ComPtr<ID3D12Resource> normalBuffer;
	ComPtr<ID3D12Resource> albedoBuffer;
	ComPtr<ID3D12Resource> depthBuffer;

	ComPtr<ID3D12DescriptorHeap> rtvHeap;
	ComPtr<ID3D12DescriptorHeap> dsvHeap;
	ComPtr<ID3D12DescriptorHeap> srvHeap;

	D3D12_CPU_DESCRIPTOR_HANDLE positionRTV;
	D3D12_CPU_DESCRIPTOR_HANDLE normalRTV;
	D3D12_CPU_DESCRIPTOR_HANDLE albedoRTV;
	D3D12_CPU_DESCRIPTOR_HANDLE dsv;

	D3D12_CPU_DESCRIPTOR_HANDLE positionSRV;
	D3D12_CPU_DESCRIPTOR_HANDLE normalSRV;
	D3D12_CPU_DESCRIPTOR_HANDLE albedoSRV;

	D3D12_GPU_DESCRIPTOR_HANDLE positionSRVGPU;
	D3D12_GPU_DESCRIPTOR_HANDLE normalSRVGPU;
	D3D12_GPU_DESCRIPTOR_HANDLE albedoSRVGPU;

	bool isInRenderTargetState;
};
