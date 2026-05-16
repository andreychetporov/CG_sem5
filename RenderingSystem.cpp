#include "RenderingSystem.h"
#include <d3dcompiler.h>

RenderingSystem::RenderingSystem()
{
	width = 0;
	height = 0;
	lightBufferMapped = nullptr;
	lightingCBMapped = nullptr;
	cameraPosition[0] = 0.0f;
	cameraPosition[1] = 0.0f;
	cameraPosition[2] = 0.0f;
	debugMode = 0.0f;
}

RenderingSystem::~RenderingSystem()
{
	if (lightBuffer && lightBufferMapped)
	{
		lightBuffer->Unmap(0, nullptr);
	}
	if (lightingConstantBuffer && lightingCBMapped)
	{
		lightingConstantBuffer->Unmap(0, nullptr);
	}
}

bool RenderingSystem::Initialize(ID3D12Device* device, UINT width, UINT height)
{
	this->width = width;
	this->height = height;

	if (!gBuffer.Initialize(device, width, height))
		return false;

	if (!CompileShaders(device))
		return false;

	if (!CreateGeometryPass(device))
		return false;

	if (!CreateLightingPass(device))
		return false;

	const UINT maxLights = 32;
	UINT lightBufferSize = (sizeof(Light) * maxLights + 255) & ~255;

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_RESOURCE_DESC bufferDesc = {};
	bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferDesc.Width = lightBufferSize;
	bufferDesc.Height = 1;
	bufferDesc.DepthOrArraySize = 1;
	bufferDesc.MipLevels = 1;
	bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
	bufferDesc.SampleDesc.Count = 1;
	bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	if (FAILED(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&lightBuffer))))
		return false;

	D3D12_RANGE range = { 0, 0 };
	if (FAILED(lightBuffer->Map(0, &range, reinterpret_cast<void**>(&lightBufferMapped))))
		return false;

	UINT cbSize = (sizeof(LightingConstants) + 255) & ~255;
	bufferDesc.Width = cbSize;

	if (FAILED(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&lightingConstantBuffer))))
		return false;

	if (FAILED(lightingConstantBuffer->Map(0, &range, reinterpret_cast<void**>(&lightingCBMapped))))
		return false;

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = 5;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&lightingCBHeap))))
		return false;

	UINT descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	D3D12_CPU_DESCRIPTOR_HANDLE handle = lightingCBHeap->GetCPUDescriptorHandleForHeapStart();

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = lightingConstantBuffer->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = cbSize;
	device->CreateConstantBufferView(&cbvDesc, handle);

	handle.ptr += descriptorSize;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = maxLights;
	srvDesc.Buffer.StructureByteStride = sizeof(Light);

	device->CreateShaderResourceView(lightBuffer.Get(), &srvDesc, handle);

	handle.ptr += descriptorSize;

	D3D12_SHADER_RESOURCE_VIEW_DESC textureSrvDesc = {};
	textureSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	textureSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	textureSrvDesc.Texture2D.MipLevels = 1;

	textureSrvDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	device->CreateShaderResourceView(gBuffer.GetPositionBuffer(), &textureSrvDesc, handle);

	handle.ptr += descriptorSize;
	device->CreateShaderResourceView(gBuffer.GetNormalBuffer(), &textureSrvDesc, handle);

	handle.ptr += descriptorSize;
	textureSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	device->CreateShaderResourceView(gBuffer.GetAlbedoBuffer(), &textureSrvDesc, handle);

	struct FullscreenVertex
	{
		float position[3];
		float texCoord[2];
	};

	FullscreenVertex quadVertices[] = {
		{ {-1.0f, -1.0f, 0.0f}, {0.0f, 1.0f} },
		{ {-1.0f,  1.0f, 0.0f}, {0.0f, 0.0f} },
		{ { 1.0f, -1.0f, 0.0f}, {1.0f, 1.0f} },
		{ { 1.0f,  1.0f, 0.0f}, {1.0f, 0.0f} }
	};

	bufferDesc.Width = sizeof(quadVertices);

	if (FAILED(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&fullscreenQuadVB))))
		return false;

	UINT8* pData;
	if (FAILED(fullscreenQuadVB->Map(0, &range, reinterpret_cast<void**>(&pData))))
		return false;

	memcpy(pData, quadVertices, sizeof(quadVertices));
	fullscreenQuadVB->Unmap(0, nullptr);

	fullscreenQuadVBView.BufferLocation = fullscreenQuadVB->GetGPUVirtualAddress();
	fullscreenQuadVBView.StrideInBytes = sizeof(FullscreenVertex);
	fullscreenQuadVBView.SizeInBytes = sizeof(quadVertices);

	return true;
}

ComPtr<ID3DBlob> RenderingSystem::CompileShader(const wchar_t* filename, const char* entryPoint, const char* target)
{
	ComPtr<ID3DBlob> errorBlob;
	ComPtr<ID3DBlob> returnBlob;

	UINT compileFlags = 0;
#if defined(_DEBUG)
	compileFlags = D3DCOMPILE_DEBUG;
#endif

	HRESULT hr = D3DCompileFromFile(filename, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		entryPoint, target, compileFlags, 0, returnBlob.GetAddressOf(), errorBlob.GetAddressOf());

	if (FAILED(hr))
	{
		if (errorBlob)
		{
			OutputDebugStringA((char*)errorBlob->GetBufferPointer());
			MessageBoxA(nullptr, (char*)errorBlob->GetBufferPointer(), "Shader Compilation Error", MB_OK);
		}
		return nullptr;
	}

	return returnBlob;
}

bool RenderingSystem::CompileShaders(ID3D12Device* device)
{
	geometryVS = CompileShader(L"GeometryPass.hlsl", "VSMain", "vs_5_0");
	geometryPS = CompileShader(L"GeometryPass.hlsl", "PSMain", "ps_5_0");
	lightingVS = CompileShader(L"LightingPass.hlsl", "VSMain", "vs_5_0");
	lightingPS = CompileShader(L"LightingPass.hlsl", "PSMain", "ps_5_0");

	if (!geometryVS || !geometryPS || !lightingVS || !lightingPS)
		return false;

	return true;
}

bool RenderingSystem::CreateGeometryPass(ID3D12Device* device)
{
	CD3DX12_DESCRIPTOR_RANGE cbvRange(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
	CD3DX12_DESCRIPTOR_RANGE srvRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	CD3DX12_ROOT_PARAMETER rootParameters[2];
	rootParameters[0].InitAsDescriptorTable(1, &cbvRange, D3D12_SHADER_VISIBILITY_ALL);
	rootParameters[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

	D3D12_STATIC_SAMPLER_DESC sampler = {};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.MipLODBias = 0;
	sampler.MaxAnisotropy = 0;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	sampler.MinLOD = 0.0f;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderRegister = 0;
	sampler.RegisterSpace = 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, rootParameters, 1, &sampler,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig;
	ComPtr<ID3DBlob> errorBlob;

	if (FAILED(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf())))
		return false;

	if (FAILED(device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&geometryRootSignature))))
		return false;

	D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = geometryRootSignature.Get();
	psoDesc.VS.pShaderBytecode = geometryVS->GetBufferPointer();
	psoDesc.VS.BytecodeLength = geometryVS->GetBufferSize();
	psoDesc.PS.pShaderBytecode = geometryPS->GetBufferPointer();
	psoDesc.PS.BytecodeLength = geometryPS->GetBufferSize();
	psoDesc.InputLayout.pInputElementDescs = inputLayout;
	psoDesc.InputLayout.NumElements = _countof(inputLayout);
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.NumRenderTargets = 3;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
	psoDesc.RTVFormats[1] = DXGI_FORMAT_R32G32B32A32_FLOAT;
	psoDesc.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	psoDesc.SampleDesc.Count = 1;

	if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&geometryPSO))))
		return false;

	return true;
}

bool RenderingSystem::CreateLightingPass(ID3D12Device* device)
{
	CD3DX12_DESCRIPTOR_RANGE ranges[2];
	ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
	ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0);

	CD3DX12_ROOT_PARAMETER rootParameters[2];
	rootParameters[0].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_PIXEL);
	rootParameters[1].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_PIXEL);

	D3D12_STATIC_SAMPLER_DESC sampler = {};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.MipLODBias = 0;
	sampler.MaxAnisotropy = 0;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
	sampler.MinLOD = 0.0f;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderRegister = 0;
	sampler.RegisterSpace = 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, rootParameters, 1, &sampler,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig;
	ComPtr<ID3DBlob> errorBlob;

	if (FAILED(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf())))
		return false;

	if (FAILED(device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&lightingRootSignature))))
		return false;

	D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = lightingRootSignature.Get();
	psoDesc.VS.pShaderBytecode = lightingVS->GetBufferPointer();
	psoDesc.VS.BytecodeLength = lightingVS->GetBufferSize();
	psoDesc.PS.pShaderBytecode = lightingPS->GetBufferPointer();
	psoDesc.PS.BytecodeLength = lightingPS->GetBufferSize();
	psoDesc.InputLayout.pInputElementDescs = inputLayout;
	psoDesc.InputLayout.NumElements = _countof(inputLayout);
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

	D3D12_DEPTH_STENCIL_DESC dsDesc = {};
	dsDesc.DepthEnable = FALSE;
	dsDesc.StencilEnable = FALSE;
	psoDesc.DepthStencilState = dsDesc;

	psoDesc.SampleMask = UINT_MAX;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.SampleDesc.Count = 1;

	if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&lightingPSO))))
		return false;

	return true;
}

void RenderingSystem::BeginGeometryPass(ID3D12GraphicsCommandList* commandList)
{
	gBuffer.TransitionToRenderTarget(commandList);
	gBuffer.ClearRenderTargets(commandList);

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[3] = {
		gBuffer.GetPositionRTV(),
		gBuffer.GetNormalRTV(),
		gBuffer.GetAlbedoRTV()
	};

	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = gBuffer.GetDSV();

	commandList->OMSetRenderTargets(3, rtvHandles, FALSE, &dsvHandle);
}

void RenderingSystem::EndGeometryPass(ID3D12GraphicsCommandList* commandList)
{
	gBuffer.TransitionToShaderResource(commandList);
}

void RenderingSystem::RenderLightingPass(ID3D12GraphicsCommandList* commandList,
	ID3D12Resource* backBuffer, D3D12_CPU_DESCRIPTOR_HANDLE backBufferRTV)
{
	D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(backBuffer,
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	commandList->ResourceBarrier(1, &barrier);

	const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	commandList->ClearRenderTargetView(backBufferRTV, clearColor, 0, nullptr);

	commandList->OMSetRenderTargets(1, &backBufferRTV, FALSE, nullptr);

	commandList->SetGraphicsRootSignature(lightingRootSignature.Get());
	commandList->SetPipelineState(lightingPSO.Get());

	ID3D12DescriptorHeap* heaps[] = { lightingCBHeap.Get() };
	commandList->SetDescriptorHeaps(1, heaps);

	commandList->SetGraphicsRootDescriptorTable(0, lightingCBHeap->GetGPUDescriptorHandleForHeapStart());

	ID3D12Device* device;
	lightingCBHeap->GetDevice(IID_PPV_ARGS(&device));
	UINT descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	device->Release();

	CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(lightingCBHeap->GetGPUDescriptorHandleForHeapStart(), 1,
		descriptorSize);
	commandList->SetGraphicsRootDescriptorTable(1, srvHandle);

	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	commandList->IASetVertexBuffers(0, 1, &fullscreenQuadVBView);
	commandList->DrawInstanced(4, 1, 0, 0);

	barrier = CD3DX12_RESOURCE_BARRIER::Transition(backBuffer,
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	commandList->ResourceBarrier(1, &barrier);
}

void RenderingSystem::AddLight(const Light& light)
{
	lights.push_back(light);
}

void RenderingSystem::ClearLights()
{
	lights.clear();
}

void RenderingSystem::UpdateLights(ID3D12GraphicsCommandList* commandList)
{
	if (!lights.empty())
	{
		memcpy(lightBufferMapped, lights.data(), sizeof(Light) * lights.size());
	}

	LightingConstants constants;
	constants.cameraPosition[0] = cameraPosition[0];
	constants.cameraPosition[1] = cameraPosition[1];
	constants.cameraPosition[2] = cameraPosition[2];
	constants.debugMode = debugMode;
	constants.numLights = (int)lights.size();

	memcpy(lightingCBMapped, &constants, sizeof(LightingConstants));
}

void RenderingSystem::SetCameraPosition(float x, float y, float z)
{
	cameraPosition[0] = x;
	cameraPosition[1] = y;
	cameraPosition[2] = z;
}

void RenderingSystem::Resize(ID3D12Device* device, UINT width, UINT height)
{
	this->width = width;
	this->height = height;
	gBuffer.Resize(device, width, height);
}
