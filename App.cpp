#include "App.h"
#include "OBJLoader.h"
#include "TextureLoader.h"
#include <map>
#include <string>
#include <DirectXMath.h>
const wchar_t* App::WINDOW_CLASS_NAME = L"MainWindowClass";
const wchar_t* App::WINDOW_TITLE = L"Render Sandbox";
App::App()
{
	_hwnd = nullptr;
	_width = 1280;
	_height = 720;
	time = 0.0f;
	camera.aspect = (float)_width / (float)_height;
	camera.position = Vec3(0, 5, 20);
	camera.target = Vec3(0, 5, 0);
	vertexCount = 0;
	indexCount = 0;
	for (int i = 0; i < 256; i++) keys[i] = false;
	lastMouseX = 0;
	lastMouseY = 0;
	mousePressed = false;
	cameraYaw = 3.14159f;
	cameraPitch = 0.0f;
}
bool App::Initialize(HINSTANCE hInstance, int nCmdShow)
{
	return InitWindow(hInstance, nCmdShow) && InitD3D();
}
int App::Run()
{
	MSG msg = {};
	while (msg.message != WM_QUIT) {
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else {
			if (minimized) { WaitMessage(); continue; }
			const UINT64 completed = fence->GetCompletedValue();
			if (completed == UINT64_MAX) {
				MessageBoxA(_hwnd, "The graphics device stopped responding.", "Direct3D 12", MB_OK | MB_ICONERROR);
				return 1;
			}
			if (completed < fenceValue) {
				// Keep dispatching window messages while the previous frame owns
				// the shared command allocator, resources and upload buffers.
				MsgWaitForMultipleObjectsEx(0, nullptr, 8, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
				continue;
			}
			if (resizePending) {
				resizePending = false;
				if (!ResizeFrame(pendingWidth, pendingHeight)) {
					MessageBoxA(_hwnd, "Could not resize rendering resources.", "Resize", MB_OK | MB_ICONERROR);
					return 1;
				}
			}
			Update();
			if (!Render()) {
				MessageBoxA(_hwnd, "Could not submit or present the frame.", "Direct3D 12", MB_OK | MB_ICONERROR);
				return 1;
			}
		}
	}
	return static_cast<int>(msg.wParam);
}
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	App* app = nullptr;
	if (msg == WM_CREATE) {
		CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
		app = (App*)pCreate->lpCreateParams;
		SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)app);
	}
	else {
		app = (App*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
	}
	if (app) {
		switch (msg) {
		case WM_SIZE:
			app->OnResize(LOWORD(lParam), HIWORD(lParam));
			return 0;
		case WM_KEYDOWN:
			app->OnKeyDown(wParam);
			return 0;
		case WM_KEYUP:
			app->OnKeyUp(wParam);
			return 0;
		case WM_KILLFOCUS:
			app->OnFocusLost();
			break;
		case WM_MOUSEMOVE:
			app->OnMouseMove(LOWORD(lParam), HIWORD(lParam));
			return 0;
		case WM_LBUTTONDOWN:
			SetFocus(hwnd);
			SetCapture(hwnd);
			app->OnMouseDown(LOWORD(lParam), HIWORD(lParam));
			return 0;
		case WM_LBUTTONUP:
			app->OnMouseUp();
			ReleaseCapture();
			return 0;
		case WM_CAPTURECHANGED:
			app->OnMouseUp();
			return 0;
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		}
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}
bool App::InitWindow(HINSTANCE hInstance, int nCmdShow)
{
	WNDCLASSEX wc = {};
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = 0;
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.lpszClassName = WINDOW_CLASS_NAME;
	if (!RegisterClassEx(&wc)) { return false; }
	_hwnd = CreateWindow(WINDOW_CLASS_NAME, WINDOW_TITLE, WS_OVERLAPPEDWINDOW, 0, 0, _width, _height, nullptr, nullptr, hInstance, this);
	if (_hwnd == nullptr) { return false; }
	RECT client = {};
	if (!GetClientRect(_hwnd, &client)) return false;
	_width = client.right;
	_height = client.bottom;
	camera.aspect = (float)_width / _height;
	ShowWindow(_hwnd, nCmdShow);
	UpdateWindow(_hwnd);
	return true;
}
bool App::InitD3D()
{
#if defined(_DEBUG)
	ComPtr<ID3D12Debug> debugController;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
		debugController->EnableDebugLayer();
	}
#endif
	if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) { return false; }
	if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) { return false; }
	D3D12_COMMAND_QUEUE_DESC descQueue = {};
	descQueue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	descQueue.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	if (FAILED(device->CreateCommandQueue(&descQueue, IID_PPV_ARGS(&commandQueue)))) { return false; }
	if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)))) { return false; }
	if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList)))) { return false; }
	if (FAILED(commandList->Close())) { return false; }
	if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) { return false; }
	if (!CreateSwapChain()) { return false; }
	if (!CreateDescriptors()) {return false;}
	if (!CreateRTV()) { return false; }
	if (!CreateDepthBuffer()) { return false; }
	if (!renderingSystem.Initialize(device.Get(), commandQueue.Get(), _width, _height)) { return false; }
	ApplyRenderConfig();
	if (!temporalAA.Initialize(device.Get(),_width,_height)) { MessageBoxA(nullptr,"TAA initialization failed","TAA",MB_OK);return false; }
	if (terrain.enabled)
	{
		if (!terrain.Initialize(device.Get(), commandQueue.Get())) { MessageBoxA(nullptr,"Terrain initialization failed", "Terrain",MB_OK); return false; }
		camera.position=Vec3(0,460,-1100);
		cameraYaw=0.0f; cameraPitch=atan2f(250.0f - camera.position.y, -650.0f - camera.position.z);
		if (renderingSystem.atmosphere.enabled) cameraPitch = -0.23f; // Frame sky, distant terrain and demo cube.
	}
	if (!renderingSystem.InitializeScene(device.Get(), 0, 0, terrain.enabled)) { return false; }
	if (!CreateConstantBuffer()) { return false; }
	modelConstantIndex = 4 * (UINT)renderingSystem.GetSceneObjects().size() + 4;
	if (!terrain.enabled && !LoadModel("model/sponza.obj")) { return false; }
	if (!LoadBillboardTexture()) { return false; }
	Light dirLight;
	dirLight.type = (float)LightType::Directional;
	dirLight.direction[0] = 0.28f;
	dirLight.direction[1] = -0.06f;
	dirLight.direction[2] = 0.22f;
	shadowLightDirection = DirectX::XMFLOAT3(
		dirLight.direction[0], dirLight.direction[1], dirLight.direction[2]);
	dirLight.color[0] = 1.0f;
	dirLight.color[1] = 0.95f;
	dirLight.color[2] = 0.85f;
	dirLight.intensity = 1.6f;
	renderingSystem.AddLight(dirLight);
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.Height = (float)_height;
	viewport.Width = (float)_width;
	viewport.MinDepth = 0.f;
	viewport.MaxDepth = 1.f;
	scissorRect.left = 0;
	scissorRect.top = 0;
	scissorRect.right = _width;
	scissorRect.bottom = _height;
	return true;
}
void App::OnResize(UINT width, UINT height)
{
	minimized = width == 0 || height == 0;
	if (minimized) return;
	pendingWidth = width;
	pendingHeight = height;
	resizePending = true;
}
bool App::ResizeFrame(UINT width, UINT height)
{
	if (width == (UINT)_width && height == (UINT)_height) return true;
	for (auto& buffer : swapChainBuffers) buffer.Reset();
	if (FAILED(swapChain->ResizeBuffers(FRAME_COUNT, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0)))
		return false;
	_width = width;
	_height = height;
	camera.aspect = (float)width / height;
	viewport.Width = (float)width;
	viewport.Height = (float)height;
	scissorRect.right = width;
	scissorRect.bottom = height;
	trailMouseInitialized = false;
	return CreateRTV() && CreateDepthBuffer() && renderingSystem.Resize(device.Get(), width, height)
		&& temporalAA.Initialize(device.Get(), width, height);
}
bool App::CreateSwapChain()
{
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.Width = _width;
	swapChainDesc.Height = _height;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferCount = FRAME_COUNT;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;
	ComPtr<IDXGISwapChain1> swapChain1 = {};
	if (FAILED(factory->CreateSwapChainForHwnd(commandQueue.Get(), _hwnd, &swapChainDesc, nullptr, nullptr, &swapChain1))) { return false; }
	if (FAILED(swapChain1.As(&swapChain))) { return false; }
	return true;
}
void App::FlushCommandQueue()
{
	fenceValue++;
	commandQueue->Signal(fence.Get(), fenceValue);
	if (fence->GetCompletedValue() < fenceValue) {
		HANDLE event = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
		fence->SetEventOnCompletion(fenceValue, event);
		WaitForSingleObject(event, INFINITE);
		CloseHandle(event);
	}
}
bool App::CreateDescriptors()
{
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	rtvHeapDesc.NumDescriptors = FRAME_COUNT;
	dsvHeapDesc.NumDescriptors = 1;
	if ((FAILED(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap))))) {return false;}
	if ((FAILED(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap))))) { return false; }
	sizeRTVHeap = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	return true;
}
bool App::CreateRTV()
{
	D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
	for (int i = 0; i < FRAME_COUNT; i++) {
		if (FAILED(swapChain->GetBuffer(i, IID_PPV_ARGS(&swapChainBuffers[i])))) { return false; }
		device->CreateRenderTargetView(swapChainBuffers[i].Get(), nullptr, rtv);
		rtv.ptr += sizeRTVHeap;
	}
	return true;
}
bool App::CreateDepthBuffer()
{
	D3D12_RESOURCE_DESC depthBufferDesc = {};
	depthBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthBufferDesc.Width = _width;
	depthBufferDesc.Height = _height;
	depthBufferDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthBufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	depthBufferDesc.DepthOrArraySize = 1;
	depthBufferDesc.MipLevels = 1;
	depthBufferDesc.SampleDesc.Count = 1;
	depthBufferDesc.SampleDesc.Quality = 0;
	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	clearValue.DepthStencil.Depth = 1.0f;
	clearValue.DepthStencil.Stencil = 0;
	if (FAILED(device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&depthBufferDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&clearValue,
		IID_PPV_ARGS(&depthBuffer)
	))) {
		return false;
	}
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvHeap->GetCPUDescriptorHandleForHeapStart();
	device->CreateDepthStencilView(depthBuffer.Get(), nullptr, dsvHandle);
	return true;
}
bool App::CreateRootSignature()
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
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, rootParameters, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	ComPtr<ID3DBlob> serializedRootSig;
	ComPtr<ID3DBlob> errorBlob;
	if (FAILED(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf()))) { return false; }
	if (FAILED(device->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&rootSignature)))) { return false; }
	return true;
}
ComPtr<ID3DBlob> App::CompileShader(const wchar_t* filename, const char* entryPoint, const char* target)
{
	ComPtr<ID3DBlob> errorBlob;
	ComPtr<ID3DBlob> returnBlob;
	UINT compileFlags = 0;
#if defined(_DEBUG)
	compileFlags = D3DCOMPILE_DEBUG;
#endif
	HRESULT hr = D3DCompileFromFile(filename, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entryPoint, target, compileFlags, 0, returnBlob.GetAddressOf(), errorBlob.GetAddressOf());
	if (FAILED(hr)) {
		if (errorBlob) {
			OutputDebugStringA((char*)errorBlob->GetBufferPointer());
			MessageBoxA(nullptr, (char*)errorBlob->GetBufferPointer(), "Shader Compilation Error", MB_OK);
		}
		return nullptr;
	}
	return returnBlob;
}
bool App::CompileShaders()
{
	vertexShader = CompileShader(L"Shaders.hlsl", "VSMain", "vs_5_0");
	pixelShader = CompileShader(L"Shaders.hlsl", "PSMain", "ps_5_0");
	if (pixelShader != nullptr && vertexShader != nullptr) {
		return true;
	}
	else {
		return false;
	}
}
bool App::CreatePSO()
{
	D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
		{
			"POSITION",
			0,
			DXGI_FORMAT_R32G32B32_FLOAT,
			0,
			0,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
			0
		},
		{
			"COLOR",
			0,
			DXGI_FORMAT_R32G32B32A32_FLOAT,
			0,
			12,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
			0
		},
		{
			"NORMAL",
			0,
			DXGI_FORMAT_R32G32B32_FLOAT,
			0,
			28,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
			0
		},
		{
			"TEXCOORD",
			0,
			DXGI_FORMAT_R32G32_FLOAT,
			0,
			40,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
			0
		}
	};
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = rootSignature.Get();
	psoDesc.VS.pShaderBytecode = vertexShader->GetBufferPointer();
	psoDesc.VS.BytecodeLength = vertexShader->GetBufferSize();
	psoDesc.PS.pShaderBytecode = pixelShader->GetBufferPointer();
	psoDesc.PS.BytecodeLength = pixelShader->GetBufferSize();
	psoDesc.InputLayout.pInputElementDescs = inputLayout;
	psoDesc.InputLayout.NumElements = _countof(inputLayout);
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;
	if (FAILED(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pipelineState)))) {
		return false;
	}
	return true;
}
bool App::CreateVertexBuffer()
{
	Vertex triangle[3] = {
		{ { 0.0f,  0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } },
		{ { 0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } },
		{ {-0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, -1.0f } }
	};
	D3D12_HEAP_PROPERTIES heapProperties = {};
	heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC resourceDesc = {};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = sizeof(triangle);
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.SampleDesc.Quality = 0;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	if (FAILED(device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexBuffer)))) { return false; }
	D3D12_RANGE range = {0, 0};
	UINT8* pData;
	if (FAILED(vertexBuffer->Map(0, &range, reinterpret_cast<void**>(&pData)))) { return false; }
	memcpy(pData, triangle, sizeof(triangle));
	vertexBuffer->Unmap(0, nullptr);
	bufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
	bufferView.StrideInBytes = sizeof(Vertex);
	bufferView.SizeInBytes = sizeof(triangle);
	return true;
}
bool App::CreateIndexBuffer()
{
	UINT16 indexes[3]{ 0, 1, 2 };
	D3D12_HEAP_PROPERTIES heapProperties = {};
	heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC resourceDesc = {};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = sizeof(indexes);
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.SampleDesc.Quality = 0;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	if (FAILED(device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&indexBuffer)))) { return false; }
	D3D12_RANGE range = { 0, 0 };
	UINT8* pData;
	if (FAILED(indexBuffer->Map(0, &range, reinterpret_cast<void**>(&pData)))) { return false; }
	memcpy(pData, indexes, sizeof(indexes));
	indexBuffer->Unmap(0, nullptr);
	indexBufferView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
	indexBufferView.Format = DXGI_FORMAT_R16_UINT;
	indexBufferView.SizeInBytes = sizeof(indexes);
	return true;
}
bool App::CreateConstantBuffer()
{
	UINT alignedSize = (sizeof(SimpleObjectConstants) + 255) & ~255;
	UINT maxObjects = 5000;
	UINT totalSize = alignedSize * maxObjects;
	D3D12_HEAP_PROPERTIES heapProperties = {};
	heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC resourceDesc = {};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = totalSize;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.SampleDesc.Quality = 0;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	if (FAILED(device->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&constantBuffer)
	))) {
		return false;
	}
	D3D12_RANGE range = { 0, 0 };
	if (FAILED(constantBuffer->Map(0, &range, reinterpret_cast<void**>(&cbMappedData)))) {
		return false;
	}
	return true;
}
bool App::LoadTextures(const std::map<std::string, Material>& materials)
{
	std::vector<std::string> diffuseFiles;
	std::vector<std::string> displacementFiles;
	std::vector<std::string> normalFiles;
	for (const auto& mat : materials)
	{
		if (!mat.second.diffuseTexture.empty())
		{
			diffuseFiles.push_back("model/" + mat.second.diffuseTexture);
			std::string baseName = mat.second.diffuseTexture;
			size_t dotPos = baseName.find_last_of('.');
			if (dotPos != std::string::npos)
			{
				baseName = baseName.substr(0, dotPos);
			}
			displacementFiles.push_back("model/" + baseName + "_displacement.tga");
			normalFiles.push_back("model/" + baseName + "_ddn.tga");
		}
	}
	if (diffuseFiles.empty())
	{
		MessageBoxA(nullptr, "No textures found in materials", "Error", MB_OK);
		return false;
	}
	UINT totalTextures = (UINT)(diffuseFiles.size() * 3);
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = 1 + totalTextures;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&cbvHeap))))
	{
		return false;
	}
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	UINT alignedObjectSize = (sizeof(SimpleObjectConstants) + 255) & ~255;
	cbvDesc.BufferLocation = constantBuffer->GetGPUVirtualAddress() + modelConstantIndex * alignedObjectSize;
	cbvDesc.SizeInBytes = (sizeof(ObjectConstants) + 255) & ~255;
	device->CreateConstantBufferView(&cbvDesc, cbvHeap->GetCPUDescriptorHandleForHeapStart());
	if (FAILED(commandAllocator->Reset()))
	{
		return false;
	}
	if (FAILED(commandList->Reset(commandAllocator.Get(), nullptr)))
	{
		return false;
	}
	UINT descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	UINT descriptorIndex = 1;
	for (size_t i = 0; i < diffuseFiles.size(); i++)
	{
		{
			std::vector<BYTE> textureData;
			UINT width, height;
			DXGI_FORMAT format;
			char msg[256];
			sprintf_s(msg, "Loading diffuse texture [%d/%d]: %s\n", (int)i + 1, (int)diffuseFiles.size(), diffuseFiles[i].c_str());
			OutputDebugStringA(msg);
			if (!TextureLoader::LoadTGA(diffuseFiles[i].c_str(), textureData, width, height, format))
			{
				sprintf_s(msg, "ERROR: Failed to load diffuse texture: %s\n", diffuseFiles[i].c_str());
				OutputDebugStringA(msg);
				continue;
			}
			sprintf_s(msg, "SUCCESS: Loaded diffuse %dx%d\n", width, height);
			OutputDebugStringA(msg);
			ComPtr<ID3D12Resource> texture;
			ComPtr<ID3D12Resource> uploadBuffer;
			if (!TextureLoader::CreateTexture(device.Get(), commandList.Get(),
				textureData, width, height, format, texture, uploadBuffer))
			{
				continue;
			}
			CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(cbvHeap->GetCPUDescriptorHandleForHeapStart(), descriptorIndex++, descriptorSize);
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Format = format;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;
			device->CreateShaderResourceView(texture.Get(), &srvDesc, srvHandle);
			textures.push_back(texture);
			textureUploadBuffers.push_back(uploadBuffer);
		}
		{
			std::vector<BYTE> textureData;
			UINT width, height;
			DXGI_FORMAT format;
			char msg[256];
			sprintf_s(msg, "Loading displacement texture [%d/%d]: %s\n", (int)i + 1, (int)displacementFiles.size(), displacementFiles[i].c_str());
			OutputDebugStringA(msg);
			if (!TextureLoader::LoadTGA(displacementFiles[i].c_str(), textureData, width, height, format))
			{
				sprintf_s(msg, "WARNING: Displacement texture not found, using fallback: %s\n", displacementFiles[i].c_str());
				OutputDebugStringA(msg);
				width = height = 1;
				format = DXGI_FORMAT_R8G8B8A8_UNORM;
				textureData = { 255, 255, 255, 255 };
			}
			else
			{
				sprintf_s(msg, "SUCCESS: Loaded displacement %dx%d\n", width, height);
				OutputDebugStringA(msg);
			}
			ComPtr<ID3D12Resource> texture;
			ComPtr<ID3D12Resource> uploadBuffer;
			if (!TextureLoader::CreateTexture(device.Get(), commandList.Get(),
				textureData, width, height, format, texture, uploadBuffer))
			{
				continue;
			}
			CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(cbvHeap->GetCPUDescriptorHandleForHeapStart(), descriptorIndex++, descriptorSize);
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Format = format;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;
			device->CreateShaderResourceView(texture.Get(), &srvDesc, srvHandle);
			displacementTextures.push_back(texture);
			textureUploadBuffers.push_back(uploadBuffer);
		}
		{
			std::vector<BYTE> textureData;
			UINT width, height;
			DXGI_FORMAT format;
			char msg[256];
			sprintf_s(msg, "Loading normal texture [%d/%d]: %s\n", (int)i + 1, (int)normalFiles.size(), normalFiles[i].c_str());
			OutputDebugStringA(msg);
			if (!TextureLoader::LoadTGA(normalFiles[i].c_str(), textureData, width, height, format))
			{
				sprintf_s(msg, "WARNING: Normal texture not found, using fallback: %s\n", normalFiles[i].c_str());
				OutputDebugStringA(msg);
				width = height = 1;
				format = DXGI_FORMAT_R8G8B8A8_UNORM;
				textureData = { 128, 128, 255, 255 };
			}
			else
			{
				sprintf_s(msg, "SUCCESS: Loaded normal map %dx%d\n", width, height);
				OutputDebugStringA(msg);
			}
			ComPtr<ID3D12Resource> texture;
			ComPtr<ID3D12Resource> uploadBuffer;
			if (!TextureLoader::CreateTexture(device.Get(), commandList.Get(),
				textureData, width, height, format, texture, uploadBuffer))
			{
				continue;
			}
			CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(cbvHeap->GetCPUDescriptorHandleForHeapStart(), descriptorIndex++, descriptorSize);
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Format = format;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;
			device->CreateShaderResourceView(texture.Get(), &srvDesc, srvHandle);
			normalTextures.push_back(texture);
			textureUploadBuffers.push_back(uploadBuffer);
		}
	}
	if (FAILED(commandList->Close()))
	{
		return false;
	}
	ID3D12CommandList* lists[] = { commandList.Get() };
	commandQueue->ExecuteCommandLists(1, lists);
	FlushCommandQueue();
	return true;
}
bool App::LoadModel(const char* filename)
{
	std::vector<OBJVertex> vertices;
	std::vector<UINT32> indices;
	std::map<std::string, Material> materials;
	if (!OBJLoader::Load(filename, vertices, indices, materials, submeshes))
	{
		MessageBoxA(nullptr, "Failed to load model", "Error", MB_OK);
		return false;
	}
	vertexCount = (UINT)vertices.size();
	indexCount = (UINT)indices.size();
	char msg[256];
	sprintf_s(msg, "Model loaded: %d vertices, %d indices, %d submeshes", vertexCount, indexCount, (int)submeshes.size());
	OutputDebugStringA(msg);
	if (!LoadTextures(materials))
	{
		return false;
	}
	D3D12_HEAP_PROPERTIES heapProperties = {};
	heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC resourceDesc = {};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = sizeof(OBJVertex) * vertexCount;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.SampleDesc.Quality = 0;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	if (FAILED(device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexBuffer))))
	{
		return false;
	}
	D3D12_RANGE range = { 0, 0 };
	UINT8* pData;
	if (FAILED(vertexBuffer->Map(0, &range, reinterpret_cast<void**>(&pData))))
	{
		return false;
	}
	memcpy(pData, vertices.data(), sizeof(OBJVertex) * vertexCount);
	vertexBuffer->Unmap(0, nullptr);
	bufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
	bufferView.StrideInBytes = sizeof(OBJVertex);
	bufferView.SizeInBytes = sizeof(OBJVertex) * vertexCount;
	resourceDesc.Width = sizeof(UINT32) * indexCount;
	if (FAILED(device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&indexBuffer))))
	{
		return false;
	}
	if (FAILED(indexBuffer->Map(0, &range, reinterpret_cast<void**>(&pData))))
	{
		return false;
	}
	memcpy(pData, indices.data(), sizeof(UINT32) * indexCount);
	indexBuffer->Unmap(0, nullptr);
	indexBufferView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
	indexBufferView.Format = DXGI_FORMAT_R32_UINT;
	indexBufferView.SizeInBytes = sizeof(UINT32) * indexCount;
	return true;
}
void App::Update()
{
	float deltaTime = 0.016f;
	time += deltaTime;
	if (animateTAACube) taaCubeTime += deltaTime;
	UpdateCamera(deltaTime);
	UpdateAtmosphere(deltaTime);
	renderingSystem.SetCameraPosition(camera.position.x, camera.position.y, camera.position.z);
	DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(
		DirectX::XMVectorSet(camera.position.x, camera.position.y, camera.position.z, 0.0f),
		DirectX::XMVectorSet(camera.target.x, camera.target.y, camera.target.z, 0.0f),
		DirectX::XMVectorSet(camera.up.x, camera.up.y, camera.up.z, 0.0f)
	);
	DirectX::XMMATRIX proj = DirectX::XMMatrixPerspectiveFovLH(
		camera.fov,
		camera.aspect,
		camera.nearZ,
		camera.farZ
	);
	const float shadowDistance = terrain.enabled ? 3000.0f : 200.0f;
	renderingSystem.UpdateCascades(view, proj, camera.nearZ, camera.farZ, shadowDistance, shadowLightDirection);
	renderingSystem.UpdateFrustum(view, proj);
	DirectX::XMFLOAT3 camPos(camera.position.x, camera.position.y, camera.position.z);
	renderingSystem.CollectVisibleObjects(camPos);
	const auto& visibleIndices = renderingSystem.GetVisibleIndices();
	const auto& sceneObjects = renderingSystem.GetSceneObjects();
	wchar_t title[256];
	swprintf_s(title, L"CG Lab | Visible: %d/%d | Frustum: %s | Octree: %s | Shadows: %s | PCF: %s | Eye Adapt: %s | Test Light: %s | Cam: (%.1f, %.1f, %.1f)",
		(int)visibleIndices.size(),
		(int)sceneObjects.size(),
		renderingSystem.GetFrustumCulling() ? L"ON" : L"OFF",
		renderingSystem.GetOctreeEnabled() ? L"ON" : L"OFF",
		renderingSystem.GetShadowsEnabled() ? L"ON" : L"OFF",
		renderingSystem.GetPCFEnabled() ? L"ON" : L"OFF",
		renderingSystem.GetEyeAdaptationEnabled() ? L"ON" : L"OFF",
		renderingSystem.GetEyeAdaptationTestEnabled() ? L"ON" : L"OFF",
		camera.position.x, camera.position.y, camera.position.z);
	if (terrain.enabled) {
		swprintf_s(title,L"Terrain | Tiles: %u | Culled nodes: %u | WASD + Space/Shift | Speed: 150 m/s",terrain.visible,terrain.culled);
	}
	std::wstring caption = title;
	caption += temporalAA.enabled ? L" | TAA ON" : L" | TAA OFF";
	if (temporalAA.showMotion) caption += L" | Motion vectors";
	if (!temporalAA.reproject) caption += L" | No reprojection";
	if (!temporalAA.protectHistory) caption += L" | No history protection";
	if (!animateTAACube) caption += L" | Cube paused";
	const auto& air = renderingSystem.atmosphere;
	if (air.enabled) {
		wchar_t airStatus[120];
		swprintf_s(airStatus, L" | Air %s / R %.2f M %.2f | Haze %s | Sun %.0f deg",
			atmospherePollutionTarget > 0.5f ? L"dirty" : L"clean", air.rayleigh, air.mie,
			air.aerialPerspective ? L"ON" : L"OFF", sunElevation * 57.29578f);
		caption += airStatus;
	}
	const auto now = GetTickCount64();
	if (caption != lastCaption && now - lastCaptionUpdate >= 200) {
		SetWindowText(_hwnd, caption.c_str());
		lastCaption = caption;
		lastCaptionUpdate = now;
	}
}
bool App::Render()
{
	UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();
	if (FAILED(commandAllocator->Reset()) ||
		FAILED(commandList->Reset(commandAllocator.Get(), renderingSystem.GetShadowPSO())))
		return false;
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(rtvHeap->GetCPUDescriptorHandleForHeapStart(), backBufferIndex, sizeRTVHeap);
	const auto& sceneObjects = renderingSystem.GetSceneObjects();
	// InitializeScene inserts the terrain demonstration cube after the center cube.
	const UINT demoIndex = terrain.enabled ? 1u : 0u;
	const auto objectWorld = [&](UINT idx) {
		const auto& obj = sceneObjects[idx];
		const float offset = idx == demoIndex ? sinf(taaCubeTime * 0.85f) * (terrain.enabled ? 145.0f : 2.0f) : 0.0f;
		return DirectX::XMMatrixScaling(obj.uniformScale, obj.uniformScale, obj.uniformScale) *
			DirectX::XMMatrixTranslation(obj.position.x + offset, obj.position.y, obj.position.z);
	};
	UINT alignedSize = (sizeof(SimpleObjectConstants) + 255) & ~255;
	renderingSystem.BeginShadowPass(commandList.Get());
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	for (UINT cascade = 0; cascade < 4; ++cascade)
	{
		renderingSystem.SetShadowCascade(commandList.Get(), cascade);
		commandList->SetPipelineState(renderingSystem.GetShadowPSO());
		DirectX::XMMATRIX lightViewProj = DirectX::XMLoadFloat4x4(&renderingSystem.GetCascadeMatrix(cascade));
		for (UINT idx = 0; idx < sceneObjects.size(); ++idx)
		{
			const Scene::SceneObject& obj = sceneObjects[idx];
			DirectX::XMMATRIX world = objectWorld(idx);
			DirectX::XMMATRIX worldLightViewProj = DirectX::XMMatrixMultiply(world, lightViewProj);
			SimpleObjectConstants constants = {};
			DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(constants.worldViewProj),
				DirectX::XMMatrixTranspose(worldLightViewProj));
			UINT constantIndex = cascade * (UINT)sceneObjects.size() + idx;
			memcpy(cbMappedData + constantIndex * alignedSize, &constants, sizeof(constants));
			commandList->SetGraphicsRootConstantBufferView(0,
				constantBuffer->GetGPUVirtualAddress() + constantIndex * alignedSize);
			if (obj.kind == Scene::PrimitiveKind::Cube)
			{
				D3D12_VERTEX_BUFFER_VIEW vb = renderingSystem.GetCubeVBView();
				D3D12_INDEX_BUFFER_VIEW ib = renderingSystem.GetCubeIBView();
				commandList->IASetVertexBuffers(0, 1, &vb);
				commandList->IASetIndexBuffer(&ib);
				commandList->DrawIndexedInstanced(renderingSystem.GetCubeIndexCount(), 1, 0, 0, 0);
			}
			else
			{
				D3D12_VERTEX_BUFFER_VIEW vb = renderingSystem.GetSphereVBView();
				D3D12_INDEX_BUFFER_VIEW ib = renderingSystem.GetSphereIBView();
				commandList->IASetVertexBuffers(0, 1, &vb);
				commandList->IASetIndexBuffer(&ib);
				commandList->DrawIndexedInstanced(renderingSystem.GetSphereIndexCount(), 1, 0, 0, 0);
			}
		}
		if (indexCount > 0)
		{
			DirectX::XMMATRIX sponzaWorld = DirectX::XMMatrixScaling(0.01f, 0.01f, 0.01f);
			DirectX::XMMATRIX sponzaLightViewProj = DirectX::XMMatrixMultiply(sponzaWorld, lightViewProj);
			SimpleObjectConstants shadowConstants = {};
			DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(shadowConstants.worldViewProj),
				DirectX::XMMatrixTranspose(sponzaLightViewProj));
			UINT constantIndex = 4 * (UINT)sceneObjects.size() + cascade;
			memcpy(cbMappedData + constantIndex * alignedSize, &shadowConstants, sizeof(shadowConstants));
			commandList->SetGraphicsRootConstantBufferView(0,
				constantBuffer->GetGPUVirtualAddress() + constantIndex * alignedSize);
			commandList->IASetVertexBuffers(0, 1, &bufferView);
			commandList->IASetIndexBuffer(&indexBufferView);
			commandList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
		}
		if (terrain.enabled)
		{
			commandList->SetPipelineState(renderingSystem.GetTerrainShadowPSO());
			terrain.RenderShadow(commandList.Get(), lightViewProj, cascade);
		}
	}
	renderingSystem.EndShadowPass(commandList.Get());
	renderingSystem.UpdateParticles(commandList.Get(), 0.016f, time);
	renderingSystem.BeginGeometryPass(commandList.Get());
	commandList->SetGraphicsRootSignature(renderingSystem.GetProceduralRootSignature());
	commandList->SetPipelineState(renderingSystem.GetProceduralPSO());
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissorRect);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const auto& visibleIndices = renderingSystem.GetVisibleIndices();
	DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(
		DirectX::XMVectorSet(camera.position.x, camera.position.y, camera.position.z, 0.0f),
		DirectX::XMVectorSet(camera.target.x, camera.target.y, camera.target.z, 0.0f),
		DirectX::XMVectorSet(camera.up.x, camera.up.y, camera.up.z, 0.0f)
	);
	DirectX::XMMATRIX proj = DirectX::XMMatrixPerspectiveFovLH(
		camera.fov,
		camera.aspect,
		camera.nearZ,
		camera.farZ
	);
	const DirectX::XMMATRIX unjitteredVP = view * proj;
	DirectX::XMMATRIX jitteredProjection = proj;
	temporalAA.Jitter(jitteredProjection);
	const DirectX::XMMATRIX viewProj = view * jitteredProjection;
	if (terrain.enabled) terrain.Render(commandList.Get(),view,proj,jitteredProjection,
		DirectX::XMFLOAT3(camera.position.x,camera.position.y,camera.position.z));
	if (indexCount > 0 && cbvHeap)
	{
		DirectX::XMMATRIX sponzaWorld = DirectX::XMMatrixScaling(0.01f, 0.01f, 0.01f);
		DirectX::XMMATRIX sponzaWorldViewProj = DirectX::XMMatrixMultiply(sponzaWorld, viewProj);
		ObjectConstants modelConstants = {};
		DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(modelConstants.worldViewProj),
			DirectX::XMMatrixTranspose(sponzaWorldViewProj));
		DirectX::XMStoreFloat4x4(reinterpret_cast<DirectX::XMFLOAT4X4*>(modelConstants.world),
			DirectX::XMMatrixTranspose(sponzaWorld));
		modelConstants.uvScale[0] = 1.0f;
		modelConstants.uvScale[1] = 1.0f;
		memcpy(cbMappedData + modelConstantIndex * alignedSize, &modelConstants, sizeof(modelConstants));

		commandList->SetGraphicsRootSignature(renderingSystem.GetGeometryRootSignature());
		commandList->SetPipelineState(renderingSystem.GetGeometryPSO());
		ID3D12DescriptorHeap* heaps[] = { cbvHeap.Get() };
		commandList->SetDescriptorHeaps(1, heaps);
		D3D12_GPU_DESCRIPTOR_HANDLE cbvHandle = cbvHeap->GetGPUDescriptorHandleForHeapStart();
		commandList->SetGraphicsRootDescriptorTable(0, cbvHandle);
		UINT descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->IASetVertexBuffers(0, 1, &bufferView);
		commandList->IASetIndexBuffer(&indexBufferView);
		for (const Submesh& submesh : submeshes)
		{
			CD3DX12_GPU_DESCRIPTOR_HANDLE textureHandle(cbvHandle, 1 + submesh.textureIndex * 3, descriptorSize);
			commandList->SetGraphicsRootDescriptorTable(1, textureHandle);
			commandList->DrawIndexedInstanced(submesh.indexCount, 1, submesh.indexStart, 0, 0);
		}
	}

	const float LOD_DISTANCE_THRESHOLD = 50.0f;
	DirectX::XMFLOAT3 camPos(camera.position.x, camera.position.y, camera.position.z);

	std::vector<uint32_t> nearObjects;
	std::vector<uint32_t> farObjects;

	for (uint32_t idx : visibleIndices)
	{
		if (idx == demoIndex) continue;
		const Scene::SceneObject& obj = sceneObjects[idx];
		float dx = obj.position.x - camPos.x;
		float dy = obj.position.y - camPos.y;
		float dz = obj.position.z - camPos.z;
		float distanceSquared = dx * dx + dy * dy + dz * dz;

		if (terrain.enabled || obj.color.w > 1.5f || distanceSquared < LOD_DISTANCE_THRESHOLD * LOD_DISTANCE_THRESHOLD)
			nearObjects.push_back(idx);
		else
			farObjects.push_back(idx);
	}
	// The moving cube's static octree bounds no longer describe its location.
	// Submit this one object directly; the GPU still clips it to the viewport.
	nearObjects.push_back(demoIndex);

	commandList->SetGraphicsRootSignature(renderingSystem.GetProceduralRootSignature());
	commandList->SetPipelineState(renderingSystem.GetProceduralPSO());
	UINT objectIndex = modelConstantIndex + 1;
	for (uint32_t idx : nearObjects)
	{
		const Scene::SceneObject& obj = sceneObjects[idx];
		DirectX::XMMATRIX world = objectWorld(idx);
		DirectX::XMMATRIX worldViewProj = DirectX::XMMatrixMultiply(world, viewProj);
		SimpleObjectConstants objConstants;
		DirectX::XMStoreFloat4x4((DirectX::XMFLOAT4X4*)objConstants.worldViewProj, DirectX::XMMatrixTranspose(worldViewProj));
		DirectX::XMStoreFloat4x4((DirectX::XMFLOAT4X4*)objConstants.world, DirectX::XMMatrixTranspose(world));
		objConstants.color[0] = obj.color.x;
		objConstants.color[1] = obj.color.y;
		objConstants.color[2] = obj.color.z;
		objConstants.color[3] = obj.color.w;
		objConstants.objectId = idx == demoIndex ? 2.0f : 1.0f;
		UINT8* dest = cbMappedData + (objectIndex * alignedSize);
		memcpy(dest, &objConstants, sizeof(SimpleObjectConstants));
		D3D12_GPU_VIRTUAL_ADDRESS cbAddress = constantBuffer->GetGPUVirtualAddress() + (objectIndex * alignedSize);
		commandList->SetGraphicsRootConstantBufferView(0, cbAddress);
		if (obj.kind == Scene::PrimitiveKind::Cube)
		{
			D3D12_VERTEX_BUFFER_VIEW vbView = renderingSystem.GetCubeVBView();
			D3D12_INDEX_BUFFER_VIEW ibView = renderingSystem.GetCubeIBView();
			commandList->IASetVertexBuffers(0, 1, &vbView);
			commandList->IASetIndexBuffer(&ibView);
			commandList->DrawIndexedInstanced(renderingSystem.GetCubeIndexCount(), 1, 0, 0, 0);
		}
		else
		{
			D3D12_VERTEX_BUFFER_VIEW vbView = renderingSystem.GetSphereVBView();
			D3D12_INDEX_BUFFER_VIEW ibView = renderingSystem.GetSphereIBView();
			commandList->IASetVertexBuffers(0, 1, &vbView);
			commandList->IASetIndexBuffer(&ibView);
			commandList->DrawIndexedInstanced(renderingSystem.GetSphereIndexCount(), 1, 0, 0, 0);
		}
		objectIndex++;
	}

	if (!farObjects.empty())
	{
		commandList->SetPipelineState(renderingSystem.GetBillboardPSO());
		commandList->SetGraphicsRootSignature(renderingSystem.GetBillboardRootSignature());

		DirectX::XMVECTOR camPosVec = DirectX::XMVectorSet(camera.position.x, camera.position.y, camera.position.z, 0.0f);
		DirectX::XMVECTOR camTargetVec = DirectX::XMVectorSet(camera.target.x, camera.target.y, camera.target.z, 0.0f);
		DirectX::XMVECTOR camUpVec = DirectX::XMVectorSet(camera.up.x, camera.up.y, camera.up.z, 0.0f);

		DirectX::XMVECTOR camForward = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(camTargetVec, camPosVec));
		DirectX::XMVECTOR camRight = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(camUpVec, camForward));
		DirectX::XMVECTOR camUp = DirectX::XMVector3Cross(camForward, camRight);

		DirectX::XMFLOAT3 cameraRight, cameraUp;
		DirectX::XMStoreFloat3(&cameraRight, camRight);
		DirectX::XMStoreFloat3(&cameraUp, camUp);

		struct BillboardConstants
		{
			float viewProj[16];
			float view[16];
			float cameraPos[3];
			float padding;
			float cameraRight[3];
			float padding2;
			float cameraUp[3];
			float padding3;
		};

		BillboardConstants billboardCB;
		DirectX::XMStoreFloat4x4((DirectX::XMFLOAT4X4*)billboardCB.viewProj, DirectX::XMMatrixTranspose(viewProj));
		DirectX::XMStoreFloat4x4((DirectX::XMFLOAT4X4*)billboardCB.view, DirectX::XMMatrixTranspose(view));
		billboardCB.cameraPos[0] = camera.position.x;
		billboardCB.cameraPos[1] = camera.position.y;
		billboardCB.cameraPos[2] = camera.position.z;
		billboardCB.cameraRight[0] = cameraRight.x;
		billboardCB.cameraRight[1] = cameraRight.y;
		billboardCB.cameraRight[2] = cameraRight.z;
		billboardCB.cameraUp[0] = cameraUp.x;
		billboardCB.cameraUp[1] = cameraUp.y;
		billboardCB.cameraUp[2] = cameraUp.z;

		memcpy(billboardCBMapped, &billboardCB, sizeof(BillboardConstants));

		ID3D12DescriptorHeap* heaps[] = { billboardCbvSrvHeap.Get() };
		commandList->SetDescriptorHeaps(1, heaps);

		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = billboardCbvSrvHeap->GetGPUDescriptorHandleForHeapStart();
		commandList->SetGraphicsRootDescriptorTable(0, gpuHandle);

		UINT descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		gpuHandle.ptr += descriptorSize;
		commandList->SetGraphicsRootDescriptorTable(1, gpuHandle);

		struct BillboardVertex
		{
			float center[3];
			float size[2];
		};

		std::vector<BillboardVertex> billboardVertices;
		for (uint32_t idx : farObjects)
		{
			const Scene::SceneObject& obj = sceneObjects[idx];
			BillboardVertex v;
			v.center[0] = obj.position.x;
			v.center[1] = obj.position.y;
			v.center[2] = obj.position.z;
			v.size[0] = obj.uniformScale * 2.0f;
			v.size[1] = obj.uniformScale * 2.0f;

			billboardVertices.push_back(v);
			billboardVertices.push_back(v);
			billboardVertices.push_back(v);
			billboardVertices.push_back(v);
			billboardVertices.push_back(v);
			billboardVertices.push_back(v);
		}

		if (!billboardVertices.empty())
		{
			UINT requiredSize = (UINT)(billboardVertices.size() * sizeof(BillboardVertex));

			if (!billboardVertexBuffer || billboardVBSize < requiredSize)
			{
				billboardVBSize = requiredSize * 2;
				D3D12_HEAP_PROPERTIES uploadHeapProps = {};
				uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

				D3D12_RESOURCE_DESC bufferDesc = {};
				bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
				bufferDesc.Width = billboardVBSize;
				bufferDesc.Height = 1;
				bufferDesc.DepthOrArraySize = 1;
				bufferDesc.MipLevels = 1;
				bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
				bufferDesc.SampleDesc.Count = 1;
				bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

				device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
					D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&billboardVertexBuffer));
			}

			UINT8* pData;
			D3D12_RANGE readRange = { 0, 0 };
			billboardVertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pData));
			memcpy(pData, billboardVertices.data(), requiredSize);
			billboardVertexBuffer->Unmap(0, nullptr);

			D3D12_VERTEX_BUFFER_VIEW billboardVBView = {};
			billboardVBView.BufferLocation = billboardVertexBuffer->GetGPUVirtualAddress();
			billboardVBView.StrideInBytes = sizeof(BillboardVertex);
			billboardVBView.SizeInBytes = requiredSize;

			commandList->IASetVertexBuffers(0, 1, &billboardVBView);
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			commandList->DrawInstanced((UINT)billboardVertices.size(), 1, 0, 0);
		}
	}

	DirectX::XMVECTOR particleForward = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(
		DirectX::XMVectorSet(camera.target.x, camera.target.y, camera.target.z, 0.0f),
		DirectX::XMVectorSet(camera.position.x, camera.position.y, camera.position.z, 0.0f)));
	DirectX::XMVECTOR particleRightVector = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(
		DirectX::XMVectorSet(camera.up.x, camera.up.y, camera.up.z, 0.0f), particleForward));
	DirectX::XMVECTOR particleUpVector = DirectX::XMVector3Cross(particleForward, particleRightVector);
	DirectX::XMFLOAT3 particleRight, particleUp;
	DirectX::XMStoreFloat3(&particleRight, particleRightVector);
	DirectX::XMStoreFloat3(&particleUp, particleUpVector);
	if (!terrain.enabled) renderingSystem.RenderParticles(commandList.Get(), viewProj, particleRight, particleUp);
	renderingSystem.EndGeometryPass(commandList.Get());
	renderingSystem.UpdateLights(viewProj);
	renderingSystem.RenderLightingPass(commandList.Get(), viewProj, camPos);
	temporalAA.Resolve(commandList.Get(),device.Get(),renderingSystem.GetHDRColorBuffer(),
		renderingSystem.GetGBuffer()->GetPositionBuffer(),renderingSystem.GetGBuffer()->GetNormalBuffer(),
		unjitteredVP, objectWorld(demoIndex), terrain.enabled && !animateTAACube);

	if (!trailMouseInitialized) 
	{
		trailPreviousX = lastMouseX;
		trailPreviousY = lastMouseY;
		trailMouseInitialized = true;
	}

	DirectX::XMFLOAT2 mouseUV(
		static_cast<float>(lastMouseX) / static_cast<float>(_width),
		static_cast<float>(lastMouseY) / static_cast<float>(_height)
	);

	DirectX::XMFLOAT2 previousMouseUV(
		static_cast<float>(trailPreviousX) / static_cast<float>(_width),
		static_cast<float>(trailPreviousY) / static_cast<float>(_height)
	);
	const bool mouseMoved = lastMouseX != trailPreviousX || lastMouseY != trailPreviousY;

	renderingSystem.RenderPostProcessing(commandList.Get(), swapChainBuffers[backBufferIndex].Get(), rtv,
		0.016f, mouseUV, previousMouseUV, mouseMoved);

	trailPreviousX = lastMouseX;
	trailPreviousY = lastMouseY;

	if (FAILED(commandList->Close())) return false;
	ID3D12CommandList* lists[] = { commandList.Get() };
	commandQueue->ExecuteCommandLists(1, lists);
	const HRESULT present = swapChain->Present(1, 0);
	const HRESULT signal = commandQueue->Signal(fence.Get(), ++fenceValue);
	return SUCCEEDED(present) && SUCCEEDED(signal);
}
void App::OnKeyDown(WPARAM key)
{
	const bool firstPress = key < 256 && !keys[key];
	if (key < 256)
		keys[key] = true;
	if (firstPress)
	{
		switch (key)
		{
		case 'N': renderingSystem.atmosphere.enabled = !renderingSystem.atmosphere.enabled; temporalAA.ResetHistory(); return;
		case 'P': atmospherePollutionTarget = 1.0f - atmospherePollutionTarget; atmosphereMieMultiplier = 1.0f; temporalAA.ResetHistory(); return;
		case 'O': renderingSystem.atmosphere.aerialPerspective = !renderingSystem.atmosphere.aerialPerspective; temporalAA.ResetHistory(); return;
		case 'T': temporalAA.enabled = !temporalAA.enabled; temporalAA.ResetHistory(); return;
		case 'M': temporalAA.showMotion = !temporalAA.showMotion; temporalAA.ResetHistory(); return;
		case 'H': temporalAA.protectHistory = !temporalAA.protectHistory; temporalAA.ResetHistory(); return;
		case 'J': temporalAA.reproject = !temporalAA.reproject; temporalAA.ResetHistory(); return;
		case 'B': animateTAACube = !animateTAACube; return;
		case 'K':
			alternateTAACamera = !alternateTAACamera;
			if (terrain.enabled) {
				camera.position = alternateTAACamera ? Vec3(450, 550, -1250) : Vec3(0, 460, -1100);
				const float dx = -camera.position.x;
				const float dz = -650.0f - camera.position.z;
				cameraYaw = atan2f(dx, dz);
				cameraPitch = atan2f(250.0f - camera.position.y, sqrtf(dx * dx + dz * dz));
			} else {
				camera.position = alternateTAACamera ? Vec3(8, 6, 20) : Vec3(0, 5, 20);
				cameraYaw = alternateTAACamera ? 3.52f : 3.14159f;
				cameraPitch = -0.15f;
			}
			temporalAA.ResetHistory();
			return;
		}
	}
	if (!runtimeHotkeys)
		return;
	if ((key >= '1' && key <= '5') || (key >= VK_F1 && key <= VK_F7))
		temporalAA.ResetHistory();
	if (key == '1') renderingSystem.SetDebugMode(0.0f);
	if (key == '2') renderingSystem.SetDebugMode(1.0f);
	if (key == '3') renderingSystem.SetDebugMode(2.0f);
	if (key == '4') renderingSystem.SetDebugMode(3.0f);
	if (key == '5') renderingSystem.SetDebugMode(4.0f);
	if (key == VK_F1)
	{
		renderingSystem.SetFrustumCulling(!renderingSystem.GetFrustumCulling());
	}
	if (key == VK_F2)
	{
		renderingSystem.SetOctreeEnabled(!renderingSystem.GetOctreeEnabled());
	}
	if (key == VK_F3)
	{
		renderingSystem.SetShadowsEnabled(!renderingSystem.GetShadowsEnabled());
	}
	if (key == VK_F4)
	{
		renderingSystem.SetPCFEnabled(!renderingSystem.GetPCFEnabled());
	}
	if (key == VK_F5)
	{
		renderingSystem.ToggleDepthOfField();
	}
	if (key == VK_F6)
	{
		renderingSystem.ToggleEyeAdaptation();
	}
	if (key == VK_F7)
	{
		renderingSystem.ToggleEyeAdaptationTest();
	}
	if (key == VK_F8)
	{
		renderingSystem.ToggleMouseTrail();
	}
	if (key == VK_F9)
	{
		renderingSystem.ResetMouseTrail();
	}
}
void App::ApplyRenderConfig()
{
	char executablePath[MAX_PATH] = {};
	GetModuleFileNameA(nullptr, executablePath, MAX_PATH);
	std::string executableDirectory(executablePath);
	executableDirectory.erase(executableDirectory.find_last_of("\\/") + 1);
	std::string configPath = executableDirectory + "RenderConfig.ini";
	// Visual Studio launches the executable from x64\Debug, while the editable
	// configuration stays in the project root two levels above it.
	if (GetFileAttributesA(configPath.c_str()) == INVALID_FILE_ATTRIBUTES)
		configPath = executableDirectory + "..\\..\\RenderConfig.ini";

	auto readBool = [&configPath](const char* section, const char* key, bool defaultValue)
	{
		return GetPrivateProfileIntA(section, key, defaultValue ? 1 : 0, configPath.c_str()) != 0;
	};

	renderingSystem.SetShadowsEnabled(readBool("Rendering", "Shadows", true));
	renderingSystem.SetSkyboxEnabled(readBool("Rendering", "Skybox", true));
	renderingSystem.SetPCFEnabled(readBool("Rendering", "PCF", true));
	renderingSystem.SetFrustumCulling(readBool("Rendering", "FrustumCulling", true));
	renderingSystem.SetOctreeEnabled(readBool("Rendering", "Octree", true));
	renderingSystem.SetDebugMode((float)GetPrivateProfileIntA("Rendering", "DebugMode", 0, configPath.c_str()));

	renderingSystem.SetDepthOfFieldEnabled(readBool("PostProcessing", "DepthOfField", true));
	renderingSystem.SetEyeAdaptationEnabled(readBool("PostProcessing", "EyeAdaptation", true));
	renderingSystem.SetEyeAdaptationTestEnabled(readBool("PostProcessing", "EyeAdaptationTest", false));
	renderingSystem.SetMouseTrailEnabled(readBool("PostProcessing", "MouseTrail", true));
	runtimeHotkeys = readBool("Controls", "RuntimeHotkeys", false);
	temporalAA.enabled = readBool("PostProcessing", "TAA", true);
	renderingSystem.atmosphere.enabled = readBool("Atmosphere", "Enabled", true);
	renderingSystem.atmosphere.aerialPerspective = readBool("Atmosphere", "AerialPerspective", true);
	atmospherePollutionTarget = atmospherePollution = readBool("Atmosphere", "Dirty", false) ? 1.0f : 0.0f;
	terrain.enabled = readBool("Terrain", "Enabled", true);
	terrain.culling = readBool("Terrain", "FrustumCulling", true);
	terrain.wireframe = readBool("Terrain", "Wireframe", false);
	terrain.colorLOD = readBool("Terrain", "ColorLOD", false);
	if(terrain.enabled) { renderingSystem.SetDepthOfFieldEnabled(false);renderingSystem.SetMouseTrailEnabled(false); }
}
void App::OnKeyUp(WPARAM key)
{
	if (key < 256)
		keys[key] = false;
}
void App::OnMouseMove(int x, int y)
{
	if (mousePressed)
	{
		int deltaX = x - lastMouseX;
		int deltaY = y - lastMouseY;
		cameraYaw += deltaX * 0.005f;
		cameraPitch += deltaY * 0.005f;
		if (cameraPitch > 1.5f) cameraPitch = 1.5f;
		if (cameraPitch < -1.5f) cameraPitch = -1.5f;
	}
	lastMouseX = x;
	lastMouseY = y;
}
void App::OnMouseDown(int x, int y)
{
	mousePressed = true;
	lastMouseX = x;
	lastMouseY = y;
}
void App::OnMouseUp()
{
	mousePressed = false;
}
void App::OnFocusLost()
{
	for (bool& key : keys) key = false;
	mousePressed = false;
	if (GetCapture() == _hwnd) ReleaseCapture();
}
void App::UpdateAtmosphere(float deltaTime)
{
	const auto clampValue = [](float value, float low, float high) {
		return (std::min)((std::max)(value, low), high);
	};
	auto& air = renderingSystem.atmosphere;
	const float oldRayleigh = air.rayleigh, oldMie = air.mie;
	const float oldAzimuth = sunAzimuth, oldElevation = sunElevation;
	sunAzimuth += (float(keys[VK_RIGHT]) - float(keys[VK_LEFT])) * deltaTime;
	sunElevation = clampValue(sunElevation + (float(keys[VK_UP]) - float(keys[VK_DOWN])) * deltaTime * 0.6f, 0.025f, 1.5f);
	air.rayleigh = clampValue(air.rayleigh + (float(keys[VK_OEM_6]) - float(keys[VK_OEM_4])) * deltaTime, 0.0f, 4.0f);
	atmosphereMieMultiplier = clampValue(atmosphereMieMultiplier +
		(float(keys[VK_OEM_PLUS]) - float(keys[VK_OEM_MINUS])) * deltaTime, 0.0f, 4.0f);
	const float pollutionStep = deltaTime * 1.8f;
	atmospherePollution += clampValue(atmospherePollutionTarget - atmospherePollution, -pollutionStep, pollutionStep);
	air.mie = (1.0f + 31.0f * atmospherePollution) * atmosphereMieMultiplier;
	const float horizontal = cosf(sunElevation);
	if (air.enabled)
	{
		const DirectX::XMFLOAT3 towardSun(sinf(sunAzimuth) * horizontal, sinf(sunElevation), cosf(sunAzimuth) * horizontal);
		renderingSystem.SetSunDirection(towardSun);
		shadowLightDirection = {-towardSun.x, -towardSun.y, -towardSun.z};
		if (oldRayleigh != air.rayleigh || oldMie != air.mie || oldAzimuth != sunAzimuth || oldElevation != sunElevation)
			temporalAA.ResetHistory();
	}
	else
	{
		shadowLightDirection = {0.28f, -0.06f, 0.22f};
		renderingSystem.SetSunDirection({-0.28f, 0.06f, -0.22f});
	}
}
void App::UpdateCamera(float deltaTime)
{
	float moveSpeed = (terrain.enabled ? 150.0f : 10.0f) * deltaTime;
	Vec3 forward(sinf(cameraYaw), 0, cosf(cameraYaw));
	Vec3 right(cosf(cameraYaw), 0, -sinf(cameraYaw));
	if (keys['W']) camera.position = camera.position + forward * moveSpeed;
	if (keys['S']) camera.position = camera.position + forward * (-moveSpeed);
	if (keys['A']) camera.position = camera.position + right * (-moveSpeed);
	if (keys['D']) camera.position = camera.position + right * moveSpeed;
	if (keys['E']) animationSpeed += 1.5f * deltaTime;
	if (keys['Q']) animationSpeed -= 1.5f * deltaTime;
	if (keys[VK_SPACE]) camera.position.y += moveSpeed;
	if (keys[VK_SHIFT]) camera.position.y -= moveSpeed;
	if (keys['Z']) renderingSystem.AdjustFocusDistance(-8.0f * deltaTime);
	if (keys['X']) renderingSystem.AdjustFocusDistance(8.0f * deltaTime);
	if (keys['C']) renderingSystem.AdjustFocusRange(-5.0f * deltaTime);
	if (keys['V']) renderingSystem.AdjustFocusRange(5.0f * deltaTime);
	camera.target.x = camera.position.x + sinf(cameraYaw) * cosf(cameraPitch);
	camera.target.y = camera.position.y + sinf(cameraPitch);
	camera.target.z = camera.position.z + cosf(cameraYaw) * cosf(cameraPitch);
}

bool App::LoadBillboardTexture()
{
	std::vector<BYTE> data;
	UINT width, height;
	DXGI_FORMAT format;

	if (!TextureLoader::LoadTGA("model/textures/lion.tga", data, width, height, format))
		return false;

	commandAllocator->Reset();
	commandList->Reset(commandAllocator.Get(), nullptr);

	if (!TextureLoader::CreateTexture(device.Get(), commandList.Get(), data, width, height, format,
		billboardTexture, billboardTextureUpload))
		return false;

	commandList->Close();
	ID3D12CommandList* cmdLists[] = { commandList.Get() };
	commandQueue->ExecuteCommandLists(1, cmdLists);
	FlushCommandQueue();

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = 2;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&billboardCbvSrvHeap))))
		return false;

	UINT descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = billboardCbvSrvHeap->GetCPUDescriptorHandleForHeapStart();

	UINT cbSize = (sizeof(float) * 64 + 255) & ~255;

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = 0;
	cbvDesc.SizeInBytes = cbSize;

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_RESOURCE_DESC cbDesc = {};
	cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	cbDesc.Width = cbSize;
	cbDesc.Height = 1;
	cbDesc.DepthOrArraySize = 1;
	cbDesc.MipLevels = 1;
	cbDesc.Format = DXGI_FORMAT_UNKNOWN;
	cbDesc.SampleDesc.Count = 1;
	cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	if (FAILED(device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&cbDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&billboardConstantBuffer))))
		return false;

	cbvDesc.BufferLocation = billboardConstantBuffer->GetGPUVirtualAddress();
	device->CreateConstantBufferView(&cbvDesc, cpuHandle);

	cpuHandle.ptr += descriptorSize;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	device->CreateShaderResourceView(billboardTexture.Get(), &srvDesc, cpuHandle);

	D3D12_RANGE readRange = { 0, 0 };
	if (FAILED(billboardConstantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&billboardCBMapped))))
		return false;

	return true;
}
