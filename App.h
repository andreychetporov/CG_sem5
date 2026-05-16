#pragma once
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl.h>
#include <d3dcompiler.h>
#include <vector>
#include <map>
#include <string>
#include "d3dx12.h"
#include "Math.h"



using Microsoft::WRL::ComPtr;

struct Material
{
	std::string name;
	std::string diffuseTexture;
	float diffuseColor[3];
};

struct Vertex
{
	float position[3];
	float color[4];
	float normal[3];
	float texCoord[2];
};

struct Submesh
{
	UINT indexStart;
	UINT indexCount;
	UINT textureIndex;
};

struct ObjectConstants
{
	float worldViewProj[16];
	float world[16];
	float lightDir[4];
	float lightColor[4];
	float ambientColor[4];
	float uvScale[2];
	float uvOffset[2];
};

class App
{

public:

	App();
	bool Initialize(HINSTANCE hInstance, int nCmdShow);
	int Run();

	void OnKeyDown(WPARAM key);
	void OnKeyUp(WPARAM key);
	void OnMouseMove(int x, int y);
	void OnMouseDown(int x, int y);
	void OnMouseUp();

private:

	bool InitWindow(HINSTANCE hInstance, int nCmdShow);
	bool InitD3D();

	bool CreateSwapChain();
	void FlushCommandQueue();
	bool CreateDescriptors();
	bool CreateRTV();
	bool CreateDepthBuffer();
	bool CreateRootSignature();

	ComPtr<ID3DBlob> CompileShader(const wchar_t* filename, const char* entryPoint, const char* target);
	bool CompileShaders();

	bool CreatePSO();
	bool CreateVertexBuffer();
	bool CreateIndexBuffer();
	bool CreateConstantBuffer();
	bool LoadTextures(const std::map<std::string, Material>& materials);
	bool LoadModel(const char* filename);

	void Update();
	void Render();

	void UpdateCamera(float deltaTime);


private:

	HWND _hwnd;
	int _width;
	int _height;

	static const wchar_t* WINDOW_CLASS_NAME;
	static const wchar_t* WINDOW_TITLE;

	static const int FRAME_COUNT = 2;

	const float clearColor[4]{ 0.53f, 0.81f, 0.92f, 1.f };

	ComPtr<ID3D12Device> device;
	ComPtr<ID3D12CommandQueue> commandQueue;
	ComPtr<IDXGIFactory4> factory;
	ComPtr<IDXGISwapChain3> swapChain;

	ComPtr<ID3D12CommandAllocator> commandAllocator;
	ComPtr<ID3D12GraphicsCommandList> commandList;

	ComPtr<ID3D12Fence> fence;
	UINT64 fenceValue = 0;

	ComPtr<ID3D12DescriptorHeap> rtvHeap;
	ComPtr<ID3D12DescriptorHeap> dsvHeap;
	UINT sizeRTVHeap = 0;

	ComPtr<ID3D12Resource> swapChainBuffers[FRAME_COUNT];

	ComPtr<ID3D12Resource> depthBuffer;

	ComPtr<ID3D12RootSignature> rootSignature;

	ComPtr<ID3DBlob> vertexShader;
	ComPtr<ID3DBlob> pixelShader;

	ComPtr<ID3D12PipelineState> pipelineState;

	ComPtr<ID3D12Resource> vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW bufferView;
	UINT vertexCount;

	ComPtr<ID3D12Resource> indexBuffer;
	D3D12_INDEX_BUFFER_VIEW indexBufferView;
	UINT indexCount;

	ComPtr<ID3D12Resource> constantBuffer;
	ComPtr<ID3D12DescriptorHeap> cbvHeap;
	UINT8* cbMappedData = nullptr;

	std::vector<ComPtr<ID3D12Resource>> textures;
	std::vector<ComPtr<ID3D12Resource>> textureUploadBuffers;
	std::vector<Submesh> submeshes;

	D3D12_VIEWPORT viewport;
	D3D12_RECT scissorRect;

	Camera camera;
	float time;

	bool keys[256];
	int lastMouseX;
	int lastMouseY;
	bool mousePressed;
	float cameraYaw;
	float cameraPitch;

};

