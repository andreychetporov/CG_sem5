#pragma once
#include <windows.h>
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include "d3dx12.h"
#include "GBuffer.h"

using Microsoft::WRL::ComPtr;

enum class LightType
{
	Directional = 0,
	Point = 1,
	Spot = 2
};

struct Light
{
	float position[3];
	float type;
	float direction[3];
	float range;
	float color[3];
	float intensity;
	float spotAngle;
	float padding[3];
};

struct LightingConstants
{
	float cameraPosition[3];
	float debugMode;
	int numLights;
	float padding2[3];
};

class RenderingSystem
{
public:
	RenderingSystem();
	~RenderingSystem();

	bool Initialize(ID3D12Device* device, UINT width, UINT height);
	void Resize(ID3D12Device* device, UINT width, UINT height);

	void BeginGeometryPass(ID3D12GraphicsCommandList* commandList);
	void EndGeometryPass(ID3D12GraphicsCommandList* commandList);

	void RenderLightingPass(ID3D12GraphicsCommandList* commandList, ID3D12Resource* backBuffer, D3D12_CPU_DESCRIPTOR_HANDLE backBufferRTV);

	void AddLight(const Light& light);
	void ClearLights();
	void UpdateLights(ID3D12GraphicsCommandList* commandList);

	ID3D12RootSignature* GetGeometryRootSignature() const { return geometryRootSignature.Get(); }
	ID3D12PipelineState* GetGeometryPSO() const { return geometryPSO.Get(); }

	ID3D12RootSignature* GetTessellationRootSignature() const { return tessellationRootSignature.Get(); }
	ID3D12PipelineState* GetTessellationPSO() const { return tessellationPSO.Get(); }
	ID3D12PipelineState* GetTessellationWireframePSO() const { return tessellationWireframePSO.Get(); }

	void SetCameraPosition(float x, float y, float z);
	void SetDebugMode(float mode) { debugMode = mode; }

	GBuffer* GetGBuffer() { return &gBuffer; }

private:
	bool CreateGeometryPass(ID3D12Device* device);
	bool CreateTessellationPass(ID3D12Device* device);
	bool CreateLightingPass(ID3D12Device* device);
	bool CompileShaders(ID3D12Device* device);

	ComPtr<ID3DBlob> CompileShader(const wchar_t* filename, const char* entryPoint, const char* target);

	UINT width;
	UINT height;

	GBuffer gBuffer;

	ComPtr<ID3D12RootSignature> geometryRootSignature;
	ComPtr<ID3D12PipelineState> geometryPSO;

	ComPtr<ID3D12RootSignature> tessellationRootSignature;
	ComPtr<ID3D12PipelineState> tessellationPSO;
	ComPtr<ID3D12PipelineState> tessellationWireframePSO;

	ComPtr<ID3D12RootSignature> lightingRootSignature;
	ComPtr<ID3D12PipelineState> lightingPSO;

	ComPtr<ID3DBlob> geometryVS;
	ComPtr<ID3DBlob> geometryPS;
	ComPtr<ID3DBlob> tessellationVS;
	ComPtr<ID3DBlob> tessellationHS;
	ComPtr<ID3DBlob> tessellationDS;
	ComPtr<ID3DBlob> tessellationPS;
	ComPtr<ID3DBlob> lightingVS;
	ComPtr<ID3DBlob> lightingPS;

	std::vector<Light> lights;
	ComPtr<ID3D12Resource> lightBuffer;
	ComPtr<ID3D12DescriptorHeap> lightBufferHeap;
	UINT8* lightBufferMapped;

	ComPtr<ID3D12Resource> lightingConstantBuffer;
	ComPtr<ID3D12DescriptorHeap> lightingCBHeap;
	UINT8* lightingCBMapped;

	ComPtr<ID3D12Resource> fullscreenQuadVB;
	D3D12_VERTEX_BUFFER_VIEW fullscreenQuadVBView;

	float cameraPosition[3];
	float debugMode;
};
