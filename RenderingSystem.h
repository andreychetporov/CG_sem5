#pragma once
#include <windows.h>
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <algorithm>
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
	int shadowsEnabled;
	int pcfEnabled;
	float shadowMapSize;
	float cascadeSplits[4];
	float cameraForward[3];
	float padding2;
	float cascadeViewProj[4][16];
	float testLightPosition[3];
	float testLightEnabled;
	float testLightColor[3];
	float testLightIntensity;
	float testLightRange;
	float iblEnabled;
	float testLightPadding[2];
};
class RenderingSystem
{
public:
	RenderingSystem();
	~RenderingSystem();
	bool Initialize(ID3D12Device* device, ID3D12CommandQueue* commandQueue, UINT width, UINT height);
	void Resize(ID3D12Device* device, UINT width, UINT height);
	void BeginGeometryPass(ID3D12GraphicsCommandList* commandList);
	void EndGeometryPass(ID3D12GraphicsCommandList* commandList);
	void RenderLightingPass(ID3D12GraphicsCommandList* commandList);
	void RenderPostProcessing(ID3D12GraphicsCommandList* commandList, ID3D12Resource* backBuffer,
		D3D12_CPU_DESCRIPTOR_HANDLE backBufferRTV, float deltaTime);
	void AddLight(const Light& light);
	void ClearLights();
	void UpdateLights(ID3D12GraphicsCommandList* commandList);
	ID3D12RootSignature* GetGeometryRootSignature() const { return geometryRootSignature.Get(); }
	ID3D12PipelineState* GetGeometryPSO() const { return geometryPSO.Get(); }
	ID3D12RootSignature* GetPBRMaterialRootSignature() const { return pbrMaterialRootSignature.Get(); }
	ID3D12PipelineState* GetPBRMaterialPSO() const { return pbrMaterialPSO.Get(); }
	ID3D12RootSignature* GetProceduralRootSignature() const { return proceduralRootSignature.Get(); }
	ID3D12PipelineState* GetProceduralPSO() const { return proceduralPSO.Get(); }
	ID3D12RootSignature* GetTessellationRootSignature() const { return tessellationRootSignature.Get(); }
	ID3D12PipelineState* GetTessellationPSO() const { return tessellationPSO.Get(); }
	ID3D12PipelineState* GetTessellationWireframePSO() const { return tessellationWireframePSO.Get(); }
	ID3D12RootSignature* GetBillboardRootSignature() const { return billboardRootSignature.Get(); }
	ID3D12PipelineState* GetBillboardPSO() const { return billboardPSO.Get(); }
	ID3D12RootSignature* GetShadowRootSignature() const { return shadowRootSignature.Get(); }
	ID3D12PipelineState* GetShadowPSO() const { return shadowPSO.Get(); }
	void UpdateCascades(const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& projection,
		float cameraNear, float cameraFar, float shadowDistance, const DirectX::XMFLOAT3& lightDirection);
	void BeginShadowPass(ID3D12GraphicsCommandList* commandList);
	void SetShadowCascade(ID3D12GraphicsCommandList* commandList, UINT cascadeIndex);
	void EndShadowPass(ID3D12GraphicsCommandList* commandList);
	void UpdateParticles(ID3D12GraphicsCommandList* commandList, float deltaTime, float totalTime);
	void RenderParticles(ID3D12GraphicsCommandList* commandList, const DirectX::XMMATRIX& viewProjection,
		const DirectX::XMFLOAT3& cameraRight, const DirectX::XMFLOAT3& cameraUp);
	const DirectX::XMFLOAT4X4& GetCascadeMatrix(UINT index) const { return cascadeViewProj[index]; }
	void SetShadowsEnabled(bool enabled) { shadowsEnabled = enabled; }
	void SetPCFEnabled(bool enabled) { pcfEnabled = enabled; }
	bool GetShadowsEnabled() const { return shadowsEnabled; }
	bool GetPCFEnabled() const { return pcfEnabled; }
	void SetCameraPosition(float x, float y, float z);
	void SetDebugMode(float mode) { debugMode = mode; }
	void ToggleDepthOfField() { depthOfFieldEnabled = !depthOfFieldEnabled; }
	void ToggleEyeAdaptation() { eyeAdaptationEnabled = !eyeAdaptationEnabled; }
	void ToggleEyeAdaptationTest();
	void ToggleIBL() { iblEnabled = !iblEnabled; }
	void AdjustFocusDistance(float amount) { focusDistance = (std::max)(0.5f, focusDistance + amount); }
	void AdjustFocusRange(float amount) { focusRange = (std::max)(0.5f, focusRange + amount); }
	bool GetDepthOfFieldEnabled() const { return depthOfFieldEnabled; }
	bool GetEyeAdaptationEnabled() const { return eyeAdaptationEnabled; }
	bool GetEyeAdaptationTestEnabled() const { return eyeAdaptationTestEnabled; }
	bool GetIBLEnabled() const { return iblEnabled; }
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
	bool CreatePBRMaterialPass(ID3D12Device* device);
	bool CreateProceduralPass(ID3D12Device* device);
	bool CreateTessellationPass(ID3D12Device* device);
	bool CreateBillboardPass(ID3D12Device* device);
	bool CreateLightingPass(ID3D12Device* device);
	bool CreatePostProcessing(ID3D12Device* device);
	bool CreatePostProcessTargets(ID3D12Device* device, UINT width, UINT height);
	bool CreateShadowPass(ID3D12Device* device);
	bool CreateShadowResources(ID3D12Device* device);
	bool CreateParticleSystem(ID3D12Device* device, ID3D12CommandQueue* commandQueue);
	bool LoadIBLTextures(ID3D12Device* device, ID3D12CommandQueue* commandQueue);
	bool CompileShaders(ID3D12Device* device);
	ComPtr<ID3DBlob> CompileShader(const wchar_t* filename, const char* entryPoint, const char* target);
	UINT width;
	UINT height;
	GBuffer gBuffer;
	ComPtr<ID3D12RootSignature> geometryRootSignature;
	ComPtr<ID3D12PipelineState> geometryPSO;
	ComPtr<ID3D12RootSignature> pbrMaterialRootSignature;
	ComPtr<ID3D12PipelineState> pbrMaterialPSO;
	ComPtr<ID3D12RootSignature> proceduralRootSignature;
	ComPtr<ID3D12PipelineState> proceduralPSO;
	ComPtr<ID3D12RootSignature> tessellationRootSignature;
	ComPtr<ID3D12PipelineState> tessellationPSO;
	ComPtr<ID3D12PipelineState> tessellationWireframePSO;
	ComPtr<ID3D12RootSignature> billboardRootSignature;
	ComPtr<ID3D12PipelineState> billboardPSO;
	ComPtr<ID3D12RootSignature> lightingRootSignature;
	ComPtr<ID3D12PipelineState> lightingPSO;
	ComPtr<ID3D12RootSignature> shadowRootSignature;
	ComPtr<ID3D12PipelineState> shadowPSO;
	ComPtr<ID3DBlob> geometryVS;
	ComPtr<ID3DBlob> geometryPS;
	ComPtr<ID3DBlob> pbrMaterialVS;
	ComPtr<ID3DBlob> pbrMaterialPS;
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
	ComPtr<ID3DBlob> shadowVS;
	ComPtr<ID3DBlob> particleCS;
	ComPtr<ID3DBlob> particleVS;
	ComPtr<ID3DBlob> particleGS;
	ComPtr<ID3DBlob> particlePS;
	ComPtr<ID3DBlob> eyeReduceCS;
	ComPtr<ID3DBlob> eyeAdaptCS;
	ComPtr<ID3DBlob> postProcessVS;
	ComPtr<ID3DBlob> postProcessPS;
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
	static const UINT CASCADE_COUNT = 4;
	static const UINT SHADOW_MAP_SIZE = 2048;
	ComPtr<ID3D12Resource> shadowMap;
	ComPtr<ID3D12DescriptorHeap> shadowDSVHeap;
	DirectX::XMFLOAT4X4 cascadeViewProj[CASCADE_COUNT];
	float cascadeSplits[CASCADE_COUNT];
	float stableCascadeRadii[CASCADE_COUNT];
	bool cascadeRadiiInitialized;
	float cameraForward[3];
	bool shadowMapInDepthWriteState;
	bool shadowsEnabled;
	bool pcfEnabled;
	ComPtr<ID3D12Resource> hdrColorBuffer;
	ComPtr<ID3D12DescriptorHeap> hdrRTVHeap;
	ComPtr<ID3D12Resource> luminanceTiles;
	ComPtr<ID3D12Resource> adaptedLuminance;
	ComPtr<ID3D12DescriptorHeap> postProcessDescriptorHeap;
	ComPtr<ID3D12RootSignature> eyeAdaptationRootSignature;
	ComPtr<ID3D12PipelineState> eyeReducePSO;
	ComPtr<ID3D12PipelineState> eyeAdaptPSO;
	ComPtr<ID3D12RootSignature> postProcessRootSignature;
	ComPtr<ID3D12PipelineState> postProcessPSO;
	ComPtr<ID3D12Resource> eyeAdaptationCB;
	ComPtr<ID3D12Resource> postProcessCB;
	UINT8* eyeAdaptationMapped;
	UINT8* postProcessMapped;
	UINT postProcessDescriptorSize;
	UINT luminanceTileCountX;
	UINT luminanceTileCountY;
	bool hdrColorIsRenderTarget;
	bool adaptedLuminanceIsSRV;
	bool resetEyeAdaptation;
	bool depthOfFieldEnabled;
	bool eyeAdaptationEnabled;
	bool eyeAdaptationTestEnabled;
	bool iblEnabled;
	float focusDistance;
	float focusRange;
	float maxBlurRadius;
	float manualExposure;
	float exposureKey;
	static const UINT PARTICLE_COUNT = 1024;
	ComPtr<ID3D12RootSignature> particleComputeRootSignature;
	ComPtr<ID3D12PipelineState> particleComputePSO;
	ComPtr<ID3D12RootSignature> particleRenderRootSignature;
	ComPtr<ID3D12PipelineState> particleRenderPSO;
	ComPtr<ID3D12Resource> particleBuffers[2];
	ComPtr<ID3D12Resource> particleCounters[2];
	ComPtr<ID3D12DescriptorHeap> particleDescriptorHeap;
	ComPtr<ID3D12Resource> particleComputeCB;
	ComPtr<ID3D12Resource> particleRenderCB;
	ComPtr<ID3D12Resource> particleZeroUpload;
	UINT8* particleComputeMapped;
	UINT8* particleRenderMapped;
	UINT currentParticleBuffer;
	UINT particleDescriptorSize;
	bool particleBufferIsSRV[2];
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
	ComPtr<ID3D12Resource> irradianceMap;
	ComPtr<ID3D12Resource> prefilteredEnvMap;
	ComPtr<ID3D12Resource> brdfIntegrationMap;
};
