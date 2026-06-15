#include "GBuffer.h"
GBuffer::GBuffer()
{
	width = 0;
	height = 0;
	isInRenderTargetState = false;
}
GBuffer::~GBuffer()
{
}
bool GBuffer::Initialize(ID3D12Device* device, UINT width, UINT height)
{
	this->width = width;
	this->height = height;
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = 3;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	if (FAILED(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap))))
		return false;
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	if (FAILED(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap))))
		return false;
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 3;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	if (FAILED(device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvHeap))))
		return false;
	if (!CreateRenderTarget(device, width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, positionBuffer))
		return false;
	if (!CreateRenderTarget(device, width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, normalBuffer))
		return false;
	if (!CreateRenderTarget(device, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, albedoBuffer))
		return false;
	D3D12_RESOURCE_DESC depthDesc = {};
	depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthDesc.Width = width;
	depthDesc.Height = height;
	depthDesc.DepthOrArraySize = 1;
	depthDesc.MipLevels = 1;
	depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthDesc.SampleDesc.Count = 1;
	depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	clearValue.DepthStencil.Depth = 1.0f;
	clearValue.DepthStencil.Stencil = 0;
	if (FAILED(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue, IID_PPV_ARGS(&depthBuffer))))
		return false;
	UINT rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	UINT srvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	positionRTV = rtvHeap->GetCPUDescriptorHandleForHeapStart();
	normalRTV = CD3DX12_CPU_DESCRIPTOR_HANDLE(positionRTV, 1, rtvDescriptorSize);
	albedoRTV = CD3DX12_CPU_DESCRIPTOR_HANDLE(positionRTV, 2, rtvDescriptorSize);
	device->CreateRenderTargetView(positionBuffer.Get(), nullptr, positionRTV);
	device->CreateRenderTargetView(normalBuffer.Get(), nullptr, normalRTV);
	device->CreateRenderTargetView(albedoBuffer.Get(), nullptr, albedoRTV);
	dsv = dsvHeap->GetCPUDescriptorHandleForHeapStart();
	device->CreateDepthStencilView(depthBuffer.Get(), nullptr, dsv);
	positionSRV = srvHeap->GetCPUDescriptorHandleForHeapStart();
	normalSRV = CD3DX12_CPU_DESCRIPTOR_HANDLE(positionSRV, 1, srvDescriptorSize);
	albedoSRV = CD3DX12_CPU_DESCRIPTOR_HANDLE(positionSRV, 2, srvDescriptorSize);
	positionSRVGPU = srvHeap->GetGPUDescriptorHandleForHeapStart();
	normalSRVGPU = CD3DX12_GPU_DESCRIPTOR_HANDLE(positionSRVGPU, 1, srvDescriptorSize);
	albedoSRVGPU = CD3DX12_GPU_DESCRIPTOR_HANDLE(positionSRVGPU, 2, srvDescriptorSize);
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	device->CreateShaderResourceView(positionBuffer.Get(), &srvDesc, positionSRV);
	device->CreateShaderResourceView(normalBuffer.Get(), &srvDesc, normalSRV);
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	device->CreateShaderResourceView(albedoBuffer.Get(), &srvDesc, albedoSRV);
	isInRenderTargetState = true;
	return true;
}
bool GBuffer::CreateRenderTarget(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format, ComPtr<ID3D12Resource>& buffer)
{
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = width;
	desc.Height = height;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = format;
	if (FAILED(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_RENDER_TARGET, &clearValue, IID_PPV_ARGS(&buffer))))
		return false;
	return true;
}
void GBuffer::TransitionToRenderTarget(ID3D12GraphicsCommandList* commandList)
{
	if (!isInRenderTargetState)
	{
		D3D12_RESOURCE_BARRIER barriers[3];
		barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(positionBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(normalBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		barriers[2] = CD3DX12_RESOURCE_BARRIER::Transition(albedoBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
		commandList->ResourceBarrier(3, barriers);
		isInRenderTargetState = true;
	}
}
void GBuffer::TransitionToShaderResource(ID3D12GraphicsCommandList* commandList)
{
	if (isInRenderTargetState)
	{
		D3D12_RESOURCE_BARRIER barriers[3];
		barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(positionBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(normalBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		barriers[2] = CD3DX12_RESOURCE_BARRIER::Transition(albedoBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		commandList->ResourceBarrier(3, barriers);
		isInRenderTargetState = false;
	}
}
void GBuffer::Resize(ID3D12Device* device, UINT width, UINT height)
{
	positionBuffer.Reset();
	normalBuffer.Reset();
	albedoBuffer.Reset();
	depthBuffer.Reset();
	rtvHeap.Reset();
	dsvHeap.Reset();
	srvHeap.Reset();
	isInRenderTargetState = false;
	Initialize(device, width, height);
}
void GBuffer::ClearRenderTargets(ID3D12GraphicsCommandList* commandList)
{
	const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	commandList->ClearRenderTargetView(positionRTV, clearColor, 0, nullptr);
	commandList->ClearRenderTargetView(normalRTV, clearColor, 0, nullptr);
	commandList->ClearRenderTargetView(albedoRTV, clearColor, 0, nullptr);
	commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
}
