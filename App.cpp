#include "App.h"
#include "OBJLoader.h"
#include "TextureLoader.h"
#include <map>

const wchar_t* App::WINDOW_CLASS_NAME = L"MainWindowClass";
const wchar_t* App::WINDOW_TITLE = L"Render Sandbox";

App::App()
{
	_hwnd = nullptr;
	_width = 1280;
	_height = 720;
	time = 0.0f;
	camera.aspect = (float)_width / (float)_height;
	camera.position = Vec3(0, 5, -15);
	camera.target = Vec3(0, 5, 0);
	vertexCount = 0;
	indexCount = 0;

	for (int i = 0; i < 256; i++) keys[i] = false;
	lastMouseX = 0;
	lastMouseY = 0;
	mousePressed = false;
	cameraYaw = 0.0f;
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
			Update();
			Render();
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
		case WM_KEYDOWN:
			app->OnKeyDown(wParam);
			return 0;
		case WM_KEYUP:
			app->OnKeyUp(wParam);
			return 0;
		case WM_MOUSEMOVE:
			app->OnMouseMove(LOWORD(lParam), HIWORD(lParam));
			return 0;
		case WM_LBUTTONDOWN:
			app->OnMouseDown(LOWORD(lParam), HIWORD(lParam));
			return 0;
		case WM_LBUTTONUP:
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
	if (!CreateRootSignature()) { return false; }
	if (!CompileShaders()) { return false; }
	if (!CreatePSO()) { return false; }
	if (!CreateConstantBuffer()) { return false; }
	if (!LoadModel("model/sponza.obj")) { return false; }

	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.Height = _height;
	viewport.Width = _width;
	viewport.MinDepth = 0.f;
	viewport.MaxDepth = 1.f;

	scissorRect.left = 0;
	scissorRect.top = 0;
	scissorRect.right = _width;
	scissorRect.bottom = _height;

	return true;
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
	UINT cbSize = (sizeof(ObjectConstants) + 255) & ~255;

	D3D12_HEAP_PROPERTIES heapProperties = {};
	heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_RESOURCE_DESC resourceDesc = {};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = cbSize;
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

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = constantBuffer->GetGPUVirtualAddress();
	cbvDesc.SizeInBytes = cbSize;

	D3D12_RANGE range = { 0, 0 };
	if (FAILED(constantBuffer->Map(0, &range, reinterpret_cast<void**>(&cbMappedData)))) {
		return false;
	}

	return true;
}

bool App::LoadTextures(const std::map<std::string, Material>& materials)
{
	std::vector<std::string> textureFiles;
	for (const auto& mat : materials)
	{
		if (!mat.second.diffuseTexture.empty())
		{
			textureFiles.push_back("model/" + mat.second.diffuseTexture);
		}
	}

	if (textureFiles.empty())
	{
		MessageBoxA(nullptr, "No textures found in materials", "Error", MB_OK);
		return false;
	}

	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = 1 + (UINT)textureFiles.size();
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&cbvHeap))))
	{
		return false;
	}

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
	cbvDesc.BufferLocation = constantBuffer->GetGPUVirtualAddress();
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

	for (size_t i = 0; i < textureFiles.size(); i++)
	{
		std::vector<BYTE> textureData;
		UINT width, height;
		DXGI_FORMAT format;

		if (!TextureLoader::LoadTGA(textureFiles[i].c_str(), textureData, width, height, format))
		{
			char msg[256];
			sprintf_s(msg, "Failed to load texture: %s", textureFiles[i].c_str());
			OutputDebugStringA(msg);
			continue;
		}

		ComPtr<ID3D12Resource> texture;
		ComPtr<ID3D12Resource> uploadBuffer;

		if (!TextureLoader::CreateTexture(device.Get(), commandList.Get(),
			textureData, width, height, format, texture, uploadBuffer))
		{
			continue;
		}

		CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(cbvHeap->GetCPUDescriptorHandleForHeapStart(), 1 + (INT)i, descriptorSize);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		device->CreateShaderResourceView(texture.Get(), &srvDesc, srvHandle);

		textures.push_back(texture);
		textureUploadBuffers.push_back(uploadBuffer);
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

	UpdateCamera(deltaTime);

	Matrix4x4 scale = Matrix4x4::Scale(0.2f, 0.2f, 0.2f);
	Matrix4x4 world = scale;
	Matrix4x4 viewProj = camera.GetViewProjectionMatrix();
	Matrix4x4 worldViewProj = Matrix4x4::Multiply(world, viewProj);

	Matrix4x4 worldViewProjT = Matrix4x4::Transpose(worldViewProj);
	Matrix4x4 worldT = Matrix4x4::Transpose(world);

	ObjectConstants objectConstants;
	memcpy(objectConstants.worldViewProj, worldViewProjT.m, sizeof(worldViewProjT.m));
	memcpy(objectConstants.world, worldT.m, sizeof(worldT.m));

	objectConstants.lightDir[0] = 0.0f;
	objectConstants.lightDir[1] = -1.0f;
	objectConstants.lightDir[2] = 1.0f;
	objectConstants.lightDir[3] = 0.0f;

	objectConstants.lightColor[0] = 1.0f;
	objectConstants.lightColor[1] = 1.0f;
	objectConstants.lightColor[2] = 1.0f;
	objectConstants.lightColor[3] = 0.8f;

	objectConstants.ambientColor[0] = 0.3f;
	objectConstants.ambientColor[1] = 0.3f;
	objectConstants.ambientColor[2] = 0.4f;
	objectConstants.ambientColor[3] = 0.2f;

	objectConstants.uvScale[0] = 2.0f;
	objectConstants.uvScale[1] = 2.0f;

	objectConstants.uvOffset[0] = time * 0.1f;
	objectConstants.uvOffset[1] = time * 0.05f;

	memcpy(cbMappedData, &objectConstants, sizeof(ObjectConstants));
}

void App::Render()
{
	FlushCommandQueue();

	commandAllocator->Reset();
	commandList->Reset(commandAllocator.Get(), pipelineState.Get());

	UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(rtvHeap->GetCPUDescriptorHandleForHeapStart(), backBufferIndex, sizeRTVHeap);

	D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsvHeap->GetCPUDescriptorHandleForHeapStart();

	CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(swapChainBuffers[backBufferIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

	commandList->ResourceBarrier(1, &barrier);

	commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);

	commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.f, 0, 0, nullptr);

	commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

	commandList->SetGraphicsRootSignature(rootSignature.Get());
	commandList->SetPipelineState(pipelineState.Get());
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissorRect);

	ID3D12DescriptorHeap* heaps[] = { cbvHeap.Get() };
	commandList->SetDescriptorHeaps(1, heaps);
	commandList->SetGraphicsRootDescriptorTable(0, cbvHeap->GetGPUDescriptorHandleForHeapStart());

	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->IASetVertexBuffers(0, 1, &bufferView);
	commandList->IASetIndexBuffer(&indexBufferView);

	UINT descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	for (const auto& submesh : submeshes)
	{
		CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(cbvHeap->GetGPUDescriptorHandleForHeapStart(), 1 + submesh.textureIndex, descriptorSize);
		commandList->SetGraphicsRootDescriptorTable(1, srvHandle);
		commandList->DrawIndexedInstanced(submesh.indexCount, 1, submesh.indexStart, 0, 0);
	}

	barrier = CD3DX12_RESOURCE_BARRIER::Transition(swapChainBuffers[backBufferIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	commandList->ResourceBarrier(1, &barrier);

	commandList->Close();

	ID3D12CommandList* lists[] = { commandList.Get() };
	commandQueue->ExecuteCommandLists(1, lists);

	swapChain->Present(1, 0);
}

void App::OnKeyDown(WPARAM key)
{
	if (key < 256)
		keys[key] = true;
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

void App::UpdateCamera(float deltaTime)
{
	float moveSpeed = 10.0f * deltaTime;

	Vec3 forward(sinf(cameraYaw), 0, cosf(cameraYaw));
	Vec3 right(cosf(cameraYaw), 0, -sinf(cameraYaw));

	if (keys['W']) camera.position = camera.position + forward * moveSpeed;
	if (keys['S']) camera.position = camera.position + forward * (-moveSpeed);
	if (keys['A']) camera.position = camera.position + right * (-moveSpeed);
	if (keys['D']) camera.position = camera.position + right * moveSpeed;
	if (keys[VK_SPACE]) camera.position.y += moveSpeed;
	if (keys[VK_SHIFT]) camera.position.y -= moveSpeed;

	camera.target.x = camera.position.x + sinf(cameraYaw) * cosf(cameraPitch);
	camera.target.y = camera.position.y + sinf(cameraPitch);
	camera.target.z = camera.position.z + cosf(cameraYaw) * cosf(cameraPitch);
}

