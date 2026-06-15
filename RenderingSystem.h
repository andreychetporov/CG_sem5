#pragma once
#include <windows.h>
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include "d3dx12.h"
#include "GBuffer.h"
#include "SceneCulling.h"
#include <DirectXMath.h>
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
	ID3D12RootSignature* GetProceduralRootSignature() const { return proceduralRootSignature.Get(); }
	ID3D12PipelineState* GetProceduralPSO() const { return proceduralPSO.Get(); }
	ID3D12RootSignature* GetTessellationRootSignature() const { return tessellationRootSignature.Get(); }
	ID3D12PipelineState* GetTessellationPSO() const { return tessellationPSO.Get(); }
	ID3D12PipelineState* GetTessellationWireframePSO() const { return tessellationWireframePSO.Get(); }
	ID3D12RootSignature* GetBillboardRootSignature() const { return billboardRootSignature.Get(); }
	ID3D12PipelineState* GetBillboardPSO() const { return billboardPSO.Get(); }
	void SetCameraPosition(float x, float y, float z);
	void SetDebugMode(float mode) { debugMode = mode; }
	GBuffer* GetGBuffer() { return &gBuffer; }
	bool InitializeScene(ID3D12Device* device, UINT cubeCount, UINT sphereCount);
	void BuildOctree();
	void UpdateFrustum(const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& projection);
	void CollectVisibleObjects(const DirectX::XMFLOAT3& cameraPos);
	void SetFrustumCulling(bool enabled) { useFrustumCulling = enabled; }
	void SetOctreeEnabled(bool enabled) { useOctree = enabled; }
	bool GetFrustumCulling() const { return useFrustumCulling; }
	bool GetOctreeEnabled() const { return useOctree; }
	const std::vector<Scene::SceneObject>& GetSceneObjects() const { return sceneObjects; }
	const std::vector<uint32_t>& GetVisibleIndices() const { return visibleIndices; }
	ID3D12Resource* GetCubeVertexBuffer() const { return cubeVB.Get(); }
	ID3D12Resource* GetCubeIndexBuffer() const { return cubeIB.Get(); }
	ID3D12Resource* GetSphereVertexBuffer() const { return sphereVB.Get(); }
	ID3D12Resource* GetSphereIndexBuffer() const { return sphereIB.Get(); }
	D3D12_VERTEX_BUFFER_VIEW GetCubeVBView() const { return cubeVBView; }
	D3D12_INDEX_BUFFER_VIEW GetCubeIBView() const { return cubeIBView; }
	D3D12_VERTEX_BUFFER_VIEW GetSphereVBView() const { return sphereVBView; }
	D3D12_INDEX_BUFFER_VIEW GetSphereIBView() const { return sphereIBView; }
	UINT GetCubeIndexCount() const { return cubeIndexCount; }
	UINT GetSphereIndexCount() const { return sphereIndexCount; }
private:
	bool CreateGeometryPass(ID3D12Device* device);
	bool CreateProceduralPass(ID3D12Device* device);
	bool CreateTessellationPass(ID3D12Device* device);
	bool CreateBillboardPass(ID3D12Device* device);
	bool CreateLightingPass(ID3D12Device* device);
	bool CompileShaders(ID3D12Device* device);
	ComPtr<ID3DBlob> CompileShader(const wchar_t* filename, const char* entryPoint, const char* target);
	UINT width;
	UINT height;
	GBuffer gBuffer;
	ComPtr<ID3D12RootSignature> geometryRootSignature;
	ComPtr<ID3D12PipelineState> geometryPSO;
	ComPtr<ID3D12RootSignature> proceduralRootSignature;
	ComPtr<ID3D12PipelineState> proceduralPSO;
	ComPtr<ID3D12RootSignature> tessellationRootSignature;
	ComPtr<ID3D12PipelineState> tessellationPSO;
	ComPtr<ID3D12PipelineState> tessellationWireframePSO;
	ComPtr<ID3D12RootSignature> billboardRootSignature;
	ComPtr<ID3D12PipelineState> billboardPSO;
	ComPtr<ID3D12RootSignature> lightingRootSignature;
	ComPtr<ID3D12PipelineState> lightingPSO;
	ComPtr<ID3DBlob> geometryVS;
	ComPtr<ID3DBlob> geometryPS;
	ComPtr<ID3DBlob> proceduralVS;
	ComPtr<ID3DBlob> proceduralPS;
	ComPtr<ID3DBlob> tessellationVS;
	ComPtr<ID3DBlob> tessellationHS;
	ComPtr<ID3DBlob> tessellationDS;
	ComPtr<ID3DBlob> tessellationPS;
	ComPtr<ID3DBlob> billboardVS;
	ComPtr<ID3DBlob> billboardPS;
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
	std::vector<Scene::SceneObject> sceneObjects;
	Scene::Octree octree;
	Scene::Frustum frustum;
	std::vector<uint32_t> visibleIndices;
	bool useFrustumCulling;
	bool useOctree;
	ComPtr<ID3D12Resource> cubeVB;
	ComPtr<ID3D12Resource> cubeIB;
	ComPtr<ID3D12Resource> sphereVB;
	ComPtr<ID3D12Resource> sphereIB;
	D3D12_VERTEX_BUFFER_VIEW cubeVBView;
	D3D12_INDEX_BUFFER_VIEW cubeIBView;
	D3D12_VERTEX_BUFFER_VIEW sphereVBView;
	D3D12_INDEX_BUFFER_VIEW sphereIBView;
	UINT cubeIndexCount;
	UINT sphereIndexCount;
};
