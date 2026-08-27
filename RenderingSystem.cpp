#include "RenderingSystem.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
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
	useFrustumCulling = true;
	useOctree = true;
	shadowMapInDepthWriteState = true;
	shadowsEnabled = true;
	pcfEnabled = true;
	particleComputeMapped = nullptr;
	particleRenderMapped = nullptr;
	currentParticleBuffer = 0;
	particleDescriptorSize = 0;
	particleBufferIsSRV[0] = false;
	particleBufferIsSRV[1] = false;
	cascadeRadiiInitialized = false;
	for (UINT cascade = 0; cascade < CASCADE_COUNT; ++cascade)
		stableCascadeRadii[cascade] = 0.0f;
	cubeIndexCount = 0;
	sphereIndexCount = 0;
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
	if (particleComputeCB && particleComputeMapped)
	{
		particleComputeCB->Unmap(0, nullptr);
	}
	if (particleRenderCB && particleRenderMapped)
	{
		particleRenderCB->Unmap(0, nullptr);
	}
}
bool RenderingSystem::Initialize(ID3D12Device* device, ID3D12CommandQueue* commandQueue, UINT width, UINT height)
{
	this->width = width;
	this->height = height;
	if (!gBuffer.Initialize(device, width, height))
		return false;
	if (!CompileShaders(device))
		return false;
	if (!CreateShadowResources(device))
		return false;
	if (!CreateShadowPass(device))
		return false;
	if (!CreateGeometryPass(device))
		return false;
	if (!CreateProceduralPass(device))
		return false;
	if (!CreateTessellationPass(device))
		return false;
	if (!CreateBillboardPass(device))
		return false;
	if (!CreateLightingPass(device))
		return false;
	if (!CreateParticleSystem(device, commandQueue))
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
	heapDesc.NumDescriptors = 6;
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
	handle.ptr += descriptorSize;
	D3D12_SHADER_RESOURCE_VIEW_DESC shadowSrvDesc = {};
	shadowSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	shadowSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	shadowSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	shadowSrvDesc.Texture2DArray.MipLevels = 1;
	shadowSrvDesc.Texture2DArray.ArraySize = CASCADE_COUNT;
	device->CreateShaderResourceView(shadowMap.Get(), &shadowSrvDesc, handle);
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
	proceduralVS = CompileShader(L"ProceduralObject.hlsl", "VSMain", "vs_5_0");
	proceduralPS = CompileShader(L"ProceduralObject.hlsl", "PSMain", "ps_5_0");
	tessellationVS = CompileShader(L"TessellationPass.hlsl", "VSMain", "vs_5_0");
	tessellationHS = CompileShader(L"TessellationPass.hlsl", "HSMain", "hs_5_0");
	tessellationDS = CompileShader(L"TessellationPass.hlsl", "DSMain", "ds_5_0");
	tessellationPS = CompileShader(L"TessellationPass.hlsl", "PSMain", "ps_5_0");
	billboardVS = CompileShader(L"BillboardPass.hlsl", "VSMain", "vs_5_0");
	billboardPS = CompileShader(L"BillboardPass.hlsl", "PSMain", "ps_5_0");
	lightingVS = CompileShader(L"LightingPass.hlsl", "VSMain", "vs_5_0");
	lightingPS = CompileShader(L"LightingPass.hlsl", "PSMain", "ps_5_0");
	shadowVS = CompileShader(L"ShadowPass.hlsl", "VSMain", "vs_5_0");
	particleCS = CompileShader(L"ParticleUpdate.hlsl", "CSMain", "cs_5_0");
	particleVS = CompileShader(L"ParticlePass.hlsl", "VSMain", "vs_5_0");
	particleGS = CompileShader(L"ParticlePass.hlsl", "GSMain", "gs_5_0");
	particlePS = CompileShader(L"ParticlePass.hlsl", "PSMain", "ps_5_0");
	if (!geometryVS || !geometryPS || !lightingVS || !lightingPS)
		return false;
	if (!proceduralVS || !proceduralPS)
		return false;
	if (!tessellationVS || !tessellationHS || !tessellationDS || !tessellationPS)
		return false;
	if (!billboardVS || !billboardPS)
		return false;
	if (!shadowVS)
		return false;
	if (!particleCS || !particleVS || !particleGS || !particlePS)
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
bool RenderingSystem::CreateProceduralPass(ID3D12Device* device)
{
	CD3DX12_ROOT_PARAMETER rootParameters[1];
	rootParameters[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(1, rootParameters, 0, nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	ComPtr<ID3DBlob> serializedRootSig;
	ComPtr<ID3DBlob> errorBlob;
	if (FAILED(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf())))
		return false;
	if (FAILED(device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&proceduralRootSignature))))
		return false;
	D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 1, DXGI_FORMAT_R32_FLOAT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = proceduralRootSignature.Get();
	psoDesc.VS.pShaderBytecode = proceduralVS->GetBufferPointer();
	psoDesc.VS.BytecodeLength = proceduralVS->GetBufferSize();
	psoDesc.PS.pShaderBytecode = proceduralPS->GetBufferPointer();
	psoDesc.PS.BytecodeLength = proceduralPS->GetBufferSize();
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
	if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&proceduralPSO))))
		return false;
	return true;
}
bool RenderingSystem::CreateTessellationPass(ID3D12Device* device)
{
	CD3DX12_DESCRIPTOR_RANGE cbvRange(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
	CD3DX12_DESCRIPTOR_RANGE srvRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0);
	CD3DX12_ROOT_PARAMETER rootParameters[2];
	rootParameters[0].InitAsDescriptorTable(1, &cbvRange, D3D12_SHADER_VISIBILITY_ALL);
	rootParameters[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_ALL);
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
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, rootParameters, 1, &sampler,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	ComPtr<ID3DBlob> serializedRootSig;
	ComPtr<ID3DBlob> errorBlob;
	if (FAILED(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf())))
		return false;
	if (FAILED(device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&tessellationRootSignature))))
		return false;
	D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = tessellationRootSignature.Get();
	psoDesc.VS.pShaderBytecode = tessellationVS->GetBufferPointer();
	psoDesc.VS.BytecodeLength = tessellationVS->GetBufferSize();
	psoDesc.HS.pShaderBytecode = tessellationHS->GetBufferPointer();
	psoDesc.HS.BytecodeLength = tessellationHS->GetBufferSize();
	psoDesc.DS.pShaderBytecode = tessellationDS->GetBufferPointer();
	psoDesc.DS.BytecodeLength = tessellationDS->GetBufferSize();
	psoDesc.PS.pShaderBytecode = tessellationPS->GetBufferPointer();
	psoDesc.PS.BytecodeLength = tessellationPS->GetBufferSize();
	psoDesc.InputLayout.pInputElementDescs = inputLayout;
	psoDesc.InputLayout.NumElements = _countof(inputLayout);
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
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
	if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&tessellationPSO))))
		return false;
	psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; 
	if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&tessellationWireframePSO))))
		return false;
	return true;
}
bool RenderingSystem::CreateLightingPass(ID3D12Device* device)
{
	CD3DX12_DESCRIPTOR_RANGE ranges[2];
	ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
	ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 0);
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
	D3D12_STATIC_SAMPLER_DESC shadowSampler = {};
	shadowSampler.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	shadowSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	shadowSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	shadowSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	shadowSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	shadowSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
	shadowSampler.MinLOD = 0.0f;
	shadowSampler.MaxLOD = D3D12_FLOAT32_MAX;
	shadowSampler.ShaderRegister = 1;
	shadowSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	D3D12_STATIC_SAMPLER_DESC samplers[] = { sampler, shadowSampler };
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, rootParameters, _countof(samplers), samplers,
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
bool RenderingSystem::CreateParticleSystem(ID3D12Device* device, ID3D12CommandQueue* commandQueue)
{
	struct ParticleData
	{
		float position[3]; float age;
		float velocity[3]; float lifetime;
		float color[4];
		float size; float padding[3];
	};
	const UINT64 particleBufferSize = sizeof(ParticleData) * PARTICLE_COUNT;
	D3D12_HEAP_PROPERTIES defaultHeap = {};
	defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC bufferDesc = {};
	bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferDesc.Width = particleBufferSize;
	bufferDesc.Height = 1;
	bufferDesc.DepthOrArraySize = 1;
	bufferDesc.MipLevels = 1;
	bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
	bufferDesc.SampleDesc.Count = 1;
	bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	if (FAILED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&particleBuffers[0])))) return false;
	if (FAILED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&particleBuffers[1])))) return false;
	bufferDesc.Width = sizeof(UINT);
	bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	for (UINT i = 0; i < 2; ++i)
	{
		if (FAILED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&particleCounters[i])))) return false;
	}

	D3D12_HEAP_PROPERTIES uploadHeap = {};
	uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
	bufferDesc.Width = particleBufferSize;
	bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	ComPtr<ID3D12Resource> particleInitUpload;
	if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&particleInitUpload)))) return false;
	std::vector<ParticleData> initialParticles(PARTICLE_COUNT);
	for (UINT i = 0; i < PARTICLE_COUNT; ++i)
	{
		initialParticles[i].age = 1.0f;
		initialParticles[i].lifetime = 0.0f;
		initialParticles[i].color[0] = 1.0f;
		initialParticles[i].color[1] = 0.5f;
		initialParticles[i].color[2] = 0.1f;
		initialParticles[i].color[3] = 1.0f;
	}
	UINT8* mapped = nullptr;
	D3D12_RANGE readRange = { 0, 0 };
	particleInitUpload->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
	memcpy(mapped, initialParticles.data(), particleBufferSize);
	particleInitUpload->Unmap(0, nullptr);

	bufferDesc.Width = 8;
	ComPtr<ID3D12Resource> counterInitUpload;
	if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&counterInitUpload)))) return false;
	counterInitUpload->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
	UINT counterValues[2] = { PARTICLE_COUNT, 0 };
	memcpy(mapped, counterValues, sizeof(counterValues));
	counterInitUpload->Unmap(0, nullptr);
	bufferDesc.Width = sizeof(UINT);
	if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&particleZeroUpload)))) return false;
	particleZeroUpload->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
	*reinterpret_cast<UINT*>(mapped) = 0;
	particleZeroUpload->Unmap(0, nullptr);

	ComPtr<ID3D12CommandAllocator> initAllocator;
	ComPtr<ID3D12GraphicsCommandList> initList;
	if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&initAllocator)))) return false;
	if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, initAllocator.Get(), nullptr,
		IID_PPV_ARGS(&initList)))) return false;
	initList->CopyBufferRegion(particleBuffers[0].Get(), 0, particleInitUpload.Get(), 0, particleBufferSize);
	initList->CopyBufferRegion(particleCounters[0].Get(), 0, counterInitUpload.Get(), 0, sizeof(UINT));
	initList->CopyBufferRegion(particleCounters[1].Get(), 0, counterInitUpload.Get(), sizeof(UINT), sizeof(UINT));
	D3D12_RESOURCE_BARRIER initBarriers[3] = {
		CD3DX12_RESOURCE_BARRIER::Transition(particleBuffers[0].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
		CD3DX12_RESOURCE_BARRIER::Transition(particleCounters[0].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
		CD3DX12_RESOURCE_BARRIER::Transition(particleCounters[1].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
	};
	initList->ResourceBarrier(3, initBarriers);
	initList->Close();
	ID3D12CommandList* initLists[] = { initList.Get() };
	commandQueue->ExecuteCommandLists(1, initLists);
	ComPtr<ID3D12Fence> initFence;
	if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&initFence)))) return false;
	HANDLE eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (!eventHandle) return false;
	commandQueue->Signal(initFence.Get(), 1);
	initFence->SetEventOnCompletion(1, eventHandle);
	WaitForSingleObject(eventHandle, INFINITE);
	CloseHandle(eventHandle);

	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = {};
	descriptorHeapDesc.NumDescriptors = 4;
	descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	if (FAILED(device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&particleDescriptorHeap)))) return false;
	particleDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	for (UINT i = 0; i < 2; ++i)
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE uavHandle(particleDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), i, particleDescriptorSize);
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uavDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavDesc.Buffer.NumElements = PARTICLE_COUNT;
		uavDesc.Buffer.StructureByteStride = sizeof(ParticleData);
		device->CreateUnorderedAccessView(particleBuffers[i].Get(), particleCounters[i].Get(), &uavDesc, uavHandle);
		CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(particleDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), 2 + i, particleDescriptorSize);
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.Buffer.NumElements = PARTICLE_COUNT;
		srvDesc.Buffer.StructureByteStride = sizeof(ParticleData);
		device->CreateShaderResourceView(particleBuffers[i].Get(), &srvDesc, srvHandle);
	}

	bufferDesc.Width = 256;
	bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
	if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&particleComputeCB)))) return false;
	if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&particleRenderCB)))) return false;
	particleComputeCB->Map(0, &readRange, reinterpret_cast<void**>(&particleComputeMapped));
	particleRenderCB->Map(0, &readRange, reinterpret_cast<void**>(&particleRenderMapped));

	CD3DX12_DESCRIPTOR_RANGE inputRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
	CD3DX12_DESCRIPTOR_RANGE outputRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
	CD3DX12_ROOT_PARAMETER computeParameters[3];
	computeParameters[0].InitAsDescriptorTable(1, &inputRange);
	computeParameters[1].InitAsDescriptorTable(1, &outputRange);
	computeParameters[2].InitAsConstantBufferView(0);
	CD3DX12_ROOT_SIGNATURE_DESC computeRootDesc(3, computeParameters, 0, nullptr);
	ComPtr<ID3DBlob> serialized;
	ComPtr<ID3DBlob> errors;
	if (FAILED(D3D12SerializeRootSignature(&computeRootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serialized.GetAddressOf(), errors.GetAddressOf()))) return false;
	if (FAILED(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
		IID_PPV_ARGS(&particleComputeRootSignature)))) return false;
	D3D12_COMPUTE_PIPELINE_STATE_DESC computePSODesc = {};
	computePSODesc.pRootSignature = particleComputeRootSignature.Get();
	computePSODesc.CS = { particleCS->GetBufferPointer(), particleCS->GetBufferSize() };
	if (FAILED(device->CreateComputePipelineState(&computePSODesc, IID_PPV_ARGS(&particleComputePSO)))) return false;

	CD3DX12_DESCRIPTOR_RANGE renderRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	CD3DX12_ROOT_PARAMETER renderParameters[2];
	renderParameters[0].InitAsDescriptorTable(1, &renderRange, D3D12_SHADER_VISIBILITY_VERTEX);
	renderParameters[1].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);
	CD3DX12_ROOT_SIGNATURE_DESC renderRootDesc(2, renderParameters, 0, nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	serialized.Reset(); errors.Reset();
	if (FAILED(D3D12SerializeRootSignature(&renderRootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serialized.GetAddressOf(), errors.GetAddressOf()))) return false;
	if (FAILED(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
		IID_PPV_ARGS(&particleRenderRootSignature)))) return false;
	D3D12_GRAPHICS_PIPELINE_STATE_DESC renderPSODesc = {};
	renderPSODesc.pRootSignature = particleRenderRootSignature.Get();
	renderPSODesc.VS = { particleVS->GetBufferPointer(), particleVS->GetBufferSize() };
	renderPSODesc.GS = { particleGS->GetBufferPointer(), particleGS->GetBufferSize() };
	renderPSODesc.PS = { particlePS->GetBufferPointer(), particlePS->GetBufferSize() };
	renderPSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	renderPSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	renderPSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	renderPSODesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	renderPSODesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	renderPSODesc.SampleMask = UINT_MAX;
	renderPSODesc.NumRenderTargets = 3;
	renderPSODesc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
	renderPSODesc.RTVFormats[1] = DXGI_FORMAT_R32G32B32A32_FLOAT;
	renderPSODesc.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;
	renderPSODesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	renderPSODesc.SampleDesc.Count = 1;
	return SUCCEEDED(device->CreateGraphicsPipelineState(&renderPSODesc, IID_PPV_ARGS(&particleRenderPSO)));
}
bool RenderingSystem::CreateShadowResources(ID3D12Device* device)
{
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = SHADOW_MAP_SIZE;
	desc.Height = SHADOW_MAP_SIZE;
	desc.DepthOrArraySize = CASCADE_COUNT;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_R32_TYPELESS;
	desc.SampleDesc.Count = 1;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = DXGI_FORMAT_D32_FLOAT;
	clearValue.DepthStencil.Depth = 1.0f;
	if (FAILED(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue, IID_PPV_ARGS(&shadowMap))))
		return false;
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = CASCADE_COUNT;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&shadowDSVHeap))))
		return false;
	UINT descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	for (UINT cascade = 0; cascade < CASCADE_COUNT; ++cascade)
	{
		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
		dsvDesc.Texture2DArray.ArraySize = 1;
		dsvDesc.Texture2DArray.FirstArraySlice = cascade;
		CD3DX12_CPU_DESCRIPTOR_HANDLE handle(shadowDSVHeap->GetCPUDescriptorHandleForHeapStart(), cascade, descriptorSize);
		device->CreateDepthStencilView(shadowMap.Get(), &dsvDesc, handle);
	}
	shadowMapInDepthWriteState = true;
	return true;
}
bool RenderingSystem::CreateShadowPass(ID3D12Device* device)
{
	CD3DX12_ROOT_PARAMETER parameter;
	parameter.InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(1, &parameter, 0, nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	ComPtr<ID3DBlob> serialized;
	ComPtr<ID3DBlob> errors;
	if (FAILED(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serialized.GetAddressOf(), errors.GetAddressOf())))
		return false;
	if (FAILED(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
		IID_PPV_ARGS(&shadowRootSignature))))
		return false;
	D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 1, DXGI_FORMAT_R32_FLOAT, 0, 44, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = shadowRootSignature.Get();
	psoDesc.VS = { shadowVS->GetBufferPointer(), shadowVS->GetBufferSize() };
	psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.RasterizerState.DepthBias = 1000;
	psoDesc.RasterizerState.SlopeScaledDepthBias = 1.5f;
	psoDesc.RasterizerState.DepthBiasClamp = 0.01f;
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.NumRenderTargets = 0;
	psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	psoDesc.SampleDesc.Count = 1;
	return SUCCEEDED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&shadowPSO)));
}
void RenderingSystem::UpdateCascades(const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& projection,
	float cameraNear, float cameraFar, float shadowDistance, const DirectX::XMFLOAT3& lightDirection)
{
	using namespace DirectX;
	const float lambda = 0.75f;
	for (UINT i = 0; i < CASCADE_COUNT; ++i)
	{
		float p = float(i + 1) / float(CASCADE_COUNT);
		float logarithmic = cameraNear * powf(shadowDistance / cameraNear, p);
		float uniform = cameraNear + (shadowDistance - cameraNear) * p;
		cascadeSplits[i] = lambda * logarithmic + (1.0f - lambda) * uniform;
	}
	XMVECTOR determinant;
	XMMATRIX inverseViewProjection = XMMatrixInverse(&determinant, XMMatrixMultiply(view, projection));
	XMMATRIX inverseView = XMMatrixInverse(&determinant, view);
	XMVECTOR cameraDirection = XMVector3Normalize(
		XMVector3TransformNormal(XMVectorSet(0, 0, 1, 0), inverseView));
	XMFLOAT3 storedCameraDirection;
	XMStoreFloat3(&storedCameraDirection, cameraDirection);
	cameraForward[0] = storedCameraDirection.x;
	cameraForward[1] = storedCameraDirection.y;
	cameraForward[2] = storedCameraDirection.z;
	XMVECTOR nearCorners[4];
	XMVECTOR farCorners[4];
	const float xy[4][2] = { {-1,-1}, {-1,1}, {1,1}, {1,-1} };
	for (UINT i = 0; i < 4; ++i)
	{
		nearCorners[i] = XMVector3TransformCoord(XMVectorSet(xy[i][0], xy[i][1], 0.0f, 1.0f), inverseViewProjection);
		farCorners[i] = XMVector3TransformCoord(XMVectorSet(xy[i][0], xy[i][1], 1.0f, 1.0f), inverseViewProjection);
	}
	XMVECTOR lightDir = XMVector3Normalize(XMLoadFloat3(&lightDirection));
	float previousSplit = cameraNear;
	for (UINT cascade = 0; cascade < CASCADE_COUNT; ++cascade)
	{
		float nearRatio = (previousSplit - cameraNear) / (cameraFar - cameraNear);
		float farRatio = (cascadeSplits[cascade] - cameraNear) / (cameraFar - cameraNear);
		XMVECTOR corners[8];
		XMVECTOR center = XMVectorZero();
		for (UINT i = 0; i < 4; ++i)
		{
			XMVECTOR ray = XMVectorSubtract(farCorners[i], nearCorners[i]);
			corners[i] = XMVectorMultiplyAdd(ray, XMVectorReplicate(nearRatio), nearCorners[i]);
			corners[i + 4] = XMVectorMultiplyAdd(ray, XMVectorReplicate(farRatio), nearCorners[i]);
			center = XMVectorAdd(center, XMVectorAdd(corners[i], corners[i + 4]));
		}
		center = XMVectorScale(center, 1.0f / 8.0f);
		float radius = 0.0f;
		for (XMVECTOR corner : corners)
			radius = (std::max)(radius, XMVectorGetX(XMVector3Length(XMVectorSubtract(corner, center))));
		radius = ceilf(radius * 16.0f) / 16.0f;
		if (!cascadeRadiiInitialized)
			stableCascadeRadii[cascade] = radius;
		else
			radius = stableCascadeRadii[cascade];
		XMVECTOR up = fabsf(XMVectorGetY(lightDir)) > 0.95f ? XMVectorSet(0, 0, 1, 0) : XMVectorSet(0, 1, 0, 0);
		XMVECTOR lightRight = XMVector3Normalize(XMVector3Cross(up, lightDir));
		XMVECTOR lightUp = XMVector3Normalize(XMVector3Cross(lightDir, lightRight));
		float texelSize = (radius * 2.0f) / float(SHADOW_MAP_SIZE);
		float centerX = XMVectorGetX(XMVector3Dot(center, lightRight));
		float centerY = XMVectorGetX(XMVector3Dot(center, lightUp));
		float snappedX = floorf(centerX / texelSize + 0.5f) * texelSize;
		float snappedY = floorf(centerY / texelSize + 0.5f) * texelSize;
		center = XMVectorAdd(center, XMVectorScale(lightRight, snappedX - centerX));
		center = XMVectorAdd(center, XMVectorScale(lightUp, snappedY - centerY));
		XMMATRIX lightView = XMMatrixLookAtLH(
			XMVectorSubtract(center, XMVectorScale(lightDir, radius * 2.0f)), center, up);
		XMMATRIX lightProjection = XMMatrixOrthographicOffCenterLH(
			-radius, radius, -radius, radius, 0.0f, radius * 4.0f);
		XMStoreFloat4x4(&cascadeViewProj[cascade], XMMatrixMultiply(lightView, lightProjection));
		previousSplit = cascadeSplits[cascade];
	}
	cascadeRadiiInitialized = true;
}
void RenderingSystem::BeginShadowPass(ID3D12GraphicsCommandList* commandList)
{
	if (!shadowMapInDepthWriteState)
	{
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(shadowMap.Get(),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
		commandList->ResourceBarrier(1, &barrier);
		shadowMapInDepthWriteState = true;
	}
	commandList->SetGraphicsRootSignature(shadowRootSignature.Get());
	commandList->SetPipelineState(shadowPSO.Get());
	D3D12_VIEWPORT viewport = { 0.0f, 0.0f, float(SHADOW_MAP_SIZE), float(SHADOW_MAP_SIZE), 0.0f, 1.0f };
	D3D12_RECT rect = { 0, 0, LONG(SHADOW_MAP_SIZE), LONG(SHADOW_MAP_SIZE) };
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &rect);
}
void RenderingSystem::SetShadowCascade(ID3D12GraphicsCommandList* commandList, UINT cascadeIndex)
{
	ID3D12Device* device = nullptr;
	shadowDSVHeap->GetDevice(IID_PPV_ARGS(&device));
	UINT size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	device->Release();
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsv(shadowDSVHeap->GetCPUDescriptorHandleForHeapStart(), cascadeIndex, size);
	commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
	commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
}
void RenderingSystem::EndShadowPass(ID3D12GraphicsCommandList* commandList)
{
	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(shadowMap.Get(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	commandList->ResourceBarrier(1, &barrier);
	shadowMapInDepthWriteState = false;
}
void RenderingSystem::UpdateParticles(ID3D12GraphicsCommandList* commandList, float deltaTime, float totalTime)
{
	struct UpdateConstants
	{
		float emitterPosition[3]; float deltaTime;
		float gravity[3]; float totalTime;
	};
	UpdateConstants constants = {};
	constants.emitterPosition[0] = 0.0f;
	constants.emitterPosition[1] = 0.25f;
	constants.emitterPosition[2] = 0.0f;
	constants.deltaTime = deltaTime;
	constants.gravity[1] = -3.2f;
	constants.totalTime = totalTime;
	memcpy(particleComputeMapped, &constants, sizeof(constants));

	UINT inputIndex = currentParticleBuffer;
	UINT outputIndex = 1 - inputIndex;
	if (particleBufferIsSRV[inputIndex])
	{
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(particleBuffers[inputIndex].Get(),
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		commandList->ResourceBarrier(1, &barrier);
		particleBufferIsSRV[inputIndex] = false;
	}
	auto counterToCopy = CD3DX12_RESOURCE_BARRIER::Transition(particleCounters[outputIndex].Get(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
	commandList->ResourceBarrier(1, &counterToCopy);
	commandList->CopyBufferRegion(particleCounters[outputIndex].Get(), 0, particleZeroUpload.Get(), 0, sizeof(UINT));
	auto counterToUAV = CD3DX12_RESOURCE_BARRIER::Transition(particleCounters[outputIndex].Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	commandList->ResourceBarrier(1, &counterToUAV);

	commandList->SetComputeRootSignature(particleComputeRootSignature.Get());
	commandList->SetPipelineState(particleComputePSO.Get());
	ID3D12DescriptorHeap* heaps[] = { particleDescriptorHeap.Get() };
	commandList->SetDescriptorHeaps(1, heaps);
	CD3DX12_GPU_DESCRIPTOR_HANDLE inputHandle(particleDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), inputIndex, particleDescriptorSize);
	CD3DX12_GPU_DESCRIPTOR_HANDLE outputHandle(particleDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), outputIndex, particleDescriptorSize);
	commandList->SetComputeRootDescriptorTable(0, inputHandle);
	commandList->SetComputeRootDescriptorTable(1, outputHandle);
	commandList->SetComputeRootConstantBufferView(2, particleComputeCB->GetGPUVirtualAddress());
	commandList->Dispatch(PARTICLE_COUNT / 256, 1, 1);
	auto uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(particleBuffers[outputIndex].Get());
	commandList->ResourceBarrier(1, &uavBarrier);
	auto toSRV = CD3DX12_RESOURCE_BARRIER::Transition(particleBuffers[outputIndex].Get(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	commandList->ResourceBarrier(1, &toSRV);
	particleBufferIsSRV[outputIndex] = true;
	currentParticleBuffer = outputIndex;
}
void RenderingSystem::RenderParticles(ID3D12GraphicsCommandList* commandList,
	const DirectX::XMMATRIX& viewProjection, const DirectX::XMFLOAT3& cameraRight,
	const DirectX::XMFLOAT3& cameraUp)
{
	struct RenderConstants
	{
		float viewProjection[16];
		float cameraRight[3]; float padding0;
		float cameraUp[3]; float padding1;
	};
	RenderConstants constants = {};
	DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(constants.viewProjection),
		DirectX::XMMatrixTranspose(viewProjection));
	constants.cameraRight[0] = cameraRight.x;
	constants.cameraRight[1] = cameraRight.y;
	constants.cameraRight[2] = cameraRight.z;
	constants.cameraUp[0] = cameraUp.x;
	constants.cameraUp[1] = cameraUp.y;
	constants.cameraUp[2] = cameraUp.z;
	memcpy(particleRenderMapped, &constants, sizeof(constants));

	commandList->SetGraphicsRootSignature(particleRenderRootSignature.Get());
	commandList->SetPipelineState(particleRenderPSO.Get());
	ID3D12DescriptorHeap* heaps[] = { particleDescriptorHeap.Get() };
	commandList->SetDescriptorHeaps(1, heaps);
	CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(particleDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
		2 + currentParticleBuffer, particleDescriptorSize);
	commandList->SetGraphicsRootDescriptorTable(0, srvHandle);
	commandList->SetGraphicsRootConstantBufferView(1, particleRenderCB->GetGPUVirtualAddress());
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
	commandList->IASetVertexBuffers(0, 0, nullptr);
	commandList->IASetIndexBuffer(nullptr);
	commandList->DrawInstanced(PARTICLE_COUNT, 1, 0, 0);
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
	constants.shadowsEnabled = shadowsEnabled ? 1 : 0;
	constants.pcfEnabled = pcfEnabled ? 1 : 0;
	constants.shadowMapSize = (float)SHADOW_MAP_SIZE;
	for (UINT cascade = 0; cascade < CASCADE_COUNT; ++cascade)
	{
		constants.cascadeSplits[cascade] = cascadeSplits[cascade];
		DirectX::XMMATRIX matrix = DirectX::XMLoadFloat4x4(&cascadeViewProj[cascade]);
		DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(constants.cascadeViewProj[cascade]),
			DirectX::XMMatrixTranspose(matrix));
	}
	constants.cameraForward[0] = cameraForward[0];
	constants.cameraForward[1] = cameraForward[1];
	constants.cameraForward[2] = cameraForward[2];
	constants.padding2 = 0.0f;
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
bool RenderingSystem::InitializeScene(ID3D12Device* device, UINT cubeCount, UINT sphereCount)
{
	using namespace DirectX;
	Scene::Aabb spawnRegion;
	spawnRegion.min = XMFLOAT3{-15.0f, 0.0f, -15.0f};
	spawnRegion.max = XMFLOAT3{15.0f, 10.0f, 15.0f};
	Scene::ScatterCubesAndSpheres(sceneObjects, cubeCount, sphereCount, spawnRegion, 42);
	Scene::ProceduralMesh centerCubeMesh;
	Scene::BuildUnitCube(centerCubeMesh);
	Scene::SceneObject centerCube;
	centerCube.kind = Scene::PrimitiveKind::Cube;
	centerCube.position = DirectX::XMFLOAT3(0.0f, 0.75f, 0.0f);
	centerCube.uniformScale = 1.5f;
	centerCube.color = DirectX::XMFLOAT4(0.95f, 0.35f, 0.12f, 1.0f);
	centerCube.localBounds = centerCubeMesh.localBounds;
	DirectX::XMMATRIX centerWorld = DirectX::XMMatrixScaling(1.5f, 1.5f, 1.5f) *
		DirectX::XMMatrixTranslation(0.0f, 0.75f, 0.0f);
	centerCube.worldBounds = Scene::TransformAabb(centerCube.localBounds, centerWorld);
	sceneObjects.push_back(centerCube);
	Scene::ProceduralMesh cubeMesh, sphereMesh;
	Scene::BuildUnitCube(cubeMesh);
	Scene::BuildUvSphere(sphereMesh, 12, 18, 0.5f);
	D3D12_HEAP_PROPERTIES uploadHeap = {};
	uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC bufferDesc = {};
	bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferDesc.Height = 1;
	bufferDesc.DepthOrArraySize = 1;
	bufferDesc.MipLevels = 1;
	bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
	bufferDesc.SampleDesc.Count = 1;
	bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	UINT cubeVBSize = (UINT)(cubeMesh.vertices.size() * sizeof(Obj::MeshVertex));
	UINT cubeIBSize = (UINT)(cubeMesh.indices.size() * sizeof(uint32_t));
	UINT sphereVBSize = (UINT)(sphereMesh.vertices.size() * sizeof(Obj::MeshVertex));
	UINT sphereIBSize = (UINT)(sphereMesh.indices.size() * sizeof(uint32_t));
	cubeIndexCount = (UINT)cubeMesh.indices.size();
	sphereIndexCount = (UINT)sphereMesh.indices.size();
	bufferDesc.Width = cubeVBSize;
	if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&cubeVB))))
		return false;
	bufferDesc.Width = cubeIBSize;
	if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&cubeIB))))
		return false;
	bufferDesc.Width = sphereVBSize;
	if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&sphereVB))))
		return false;
	bufferDesc.Width = sphereIBSize;
	if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&sphereIB))))
		return false;
	UINT8* pData;
	D3D12_RANGE readRange = {0, 0};
	cubeVB->Map(0, &readRange, reinterpret_cast<void**>(&pData));
	memcpy(pData, cubeMesh.vertices.data(), cubeVBSize);
	cubeVB->Unmap(0, nullptr);
	cubeIB->Map(0, &readRange, reinterpret_cast<void**>(&pData));
	memcpy(pData, cubeMesh.indices.data(), cubeIBSize);
	cubeIB->Unmap(0, nullptr);
	sphereVB->Map(0, &readRange, reinterpret_cast<void**>(&pData));
	memcpy(pData, sphereMesh.vertices.data(), sphereVBSize);
	sphereVB->Unmap(0, nullptr);
	sphereIB->Map(0, &readRange, reinterpret_cast<void**>(&pData));
	memcpy(pData, sphereMesh.indices.data(), sphereIBSize);
	sphereIB->Unmap(0, nullptr);
	cubeVBView.BufferLocation = cubeVB->GetGPUVirtualAddress();
	cubeVBView.StrideInBytes = sizeof(Obj::MeshVertex);
	cubeVBView.SizeInBytes = cubeVBSize;
	cubeIBView.BufferLocation = cubeIB->GetGPUVirtualAddress();
	cubeIBView.Format = DXGI_FORMAT_R32_UINT;
	cubeIBView.SizeInBytes = cubeIBSize;
	sphereVBView.BufferLocation = sphereVB->GetGPUVirtualAddress();
	sphereVBView.StrideInBytes = sizeof(Obj::MeshVertex);
	sphereVBView.SizeInBytes = sphereVBSize;
	sphereIBView.BufferLocation = sphereIB->GetGPUVirtualAddress();
	sphereIBView.Format = DXGI_FORMAT_R32_UINT;
	sphereIBView.SizeInBytes = sphereIBSize;
	BuildOctree();
	return true;
}
void RenderingSystem::BuildOctree()
{
	if (sceneObjects.empty())
		return;
	Scene::Aabb sceneBounds;
	sceneBounds.min = DirectX::XMFLOAT3{FLT_MAX, FLT_MAX, FLT_MAX};
	sceneBounds.max = DirectX::XMFLOAT3{-FLT_MAX, -FLT_MAX, -FLT_MAX};
	for (const auto& obj : sceneObjects)
	{
		sceneBounds.Merge(obj.worldBounds);
	}
	octree.Build(sceneObjects, sceneBounds);
}
void RenderingSystem::UpdateFrustum(const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& projection)
{
	frustum.FromViewAndProjection(view, projection);
}
void RenderingSystem::CollectVisibleObjects(const DirectX::XMFLOAT3& cameraPos)
{
	Scene::CollectVisibleObjects(
		sceneObjects,
		frustum,
		useFrustumCulling,
		useOctree,
		octree,
		cameraPos,
		1000.0f,
		false,
		visibleIndices
	);
}

bool RenderingSystem::CreateBillboardPass(ID3D12Device* device)
{
	CD3DX12_DESCRIPTOR_RANGE cbvRange(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
	CD3DX12_DESCRIPTOR_RANGE srvRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	CD3DX12_ROOT_PARAMETER rootParameters[2];
	rootParameters[0].InitAsDescriptorTable(1, &cbvRange, D3D12_SHADER_VISIBILITY_ALL);
	rootParameters[1].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

	D3D12_STATIC_SAMPLER_DESC sampler = {};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
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
		serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&billboardRootSignature))))
		return false;

	D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = billboardRootSignature.Get();
	psoDesc.VS.pShaderBytecode = billboardVS->GetBufferPointer();
	psoDesc.VS.BytecodeLength = billboardVS->GetBufferSize();
	psoDesc.PS.pShaderBytecode = billboardPS->GetBufferPointer();
	psoDesc.PS.BytecodeLength = billboardPS->GetBufferSize();
	psoDesc.InputLayout.pInputElementDescs = inputLayout;
	psoDesc.InputLayout.NumElements = _countof(inputLayout);
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	psoDesc.SampleMask = UINT_MAX;

	psoDesc.NumRenderTargets = 3;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
	psoDesc.RTVFormats[1] = DXGI_FORMAT_R32G32B32A32_FLOAT;
	psoDesc.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	psoDesc.SampleDesc.Count = 1;

	if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&billboardPSO))))
		return false;

	return true;
}
