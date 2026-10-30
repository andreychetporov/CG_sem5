#pragma once
#include "SceneCulling.h"
#include "d3dx12.h"
#include <wrl.h>
#include <d3dcompiler.h>
#include <vector>
#include <cmath>
#include <wincodec.h>
#pragma comment(lib,"windowscodecs.lib")

// The CPU owns only the quadtree and index topology. Heights and normals are
// generated once by the compute stage in Terrain.hlsl directly into the GPU vertex buffer.
class Terrain
{
    using ComPtr = Microsoft::WRL::ComPtr<ID3D12Resource>;
    struct Vertex { float x,y,z,nx,ny,nz; };
    struct NodeGpu { float x,z,size,padding; };
    struct Node { Scene::Aabb bounds; int children[4] = {-1,-1,-1,-1}; UINT firstVertex = 0, level = 0; float size = 0; };
    static constexpr UINT Grid = 32;
    static constexpr UINT MainVertices = (Grid + 1) * (Grid + 1);
    static constexpr UINT VerticesPerNode = MainVertices + 4 * (Grid + 1);

    std::vector<Node> nodes;
    std::vector<unsigned short> sourceHeights;
    UINT sourceWidth = 0, sourceHeight = 0, indexCount = 0;
    ComPtr vb, ib, shadowConstants;
    BYTE* shadowConstantsMapped = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> solid, wire;

    bool LoadHeightmap() {
        HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        bool success = false;
        if (SUCCEEDED(hr)) {
            Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
            Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
            hr = factory->CreateDecoderFromFilename(L"model/terrain/gaea/Erosion_Out.png", nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
            if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
            if (SUCCEEDED(hr)) {
                WICPixelFormatGUID format = {}; frame->GetPixelFormat(&format); hr = frame->GetSize(&sourceWidth, &sourceHeight);
                if (SUCCEEDED(hr) && sourceWidth > 1 && sourceHeight > 1 && sourceWidth <= 16384 && sourceHeight <= 16384 && IsEqualGUID(format, GUID_WICPixelFormat16bppGray)) {
                    sourceHeights.resize((size_t)sourceWidth * sourceHeight);
                    success = SUCCEEDED(frame->CopyPixels(nullptr, sourceWidth * sizeof(unsigned short), (UINT)(sourceHeights.size() * sizeof(unsigned short)), reinterpret_cast<BYTE*>(sourceHeights.data())));
                }
            }
        }
        factory.Reset();
        if (SUCCEEDED(initialized)) CoUninitialize();
        return success;
    }

    int Build(float x, float z, float size, UINT level) {
        const int id = (int)nodes.size(); nodes.emplace_back(); Node& node = nodes[id];
        node.size = size; node.level = level; node.firstVertex = (UINT)id * VerticesPerNode;
        node.bounds.min = {x, -65, z}; node.bounds.max = {x + size, 400, z + size};
        if (level < 4) for (int child = 0; child < 4; ++child)
            nodes[id].children[child] = Build(x + (child & 1) * size / 2, z + (child >> 1) * size / 2, size / 2, level + 1);
        return id;
    }

    void Select(int id, const Scene::Frustum& frustum, const DirectX::XMFLOAT3& eye, std::vector<int>& out) {
        const auto& node = nodes[id];
        if (culling && !frustum.IntersectsAabb(node.bounds)) { ++culled; return; }
        const float dx = (std::max)((std::max)(node.bounds.min.x - eye.x, 0.0f), eye.x - node.bounds.max.x);
        const float dz = (std::max)((std::max)(node.bounds.min.z - eye.z, 0.0f), eye.z - node.bounds.max.z);
        const float distance = sqrtf(dx * dx + dz * dz + (eye.y - 100) * (eye.y - 100));
        if (node.children[0] >= 0 && distance < node.size * lodDistance) for (int child : node.children) Select(child, frustum, eye, out); else out.push_back(id);
    }

    bool CreateUploadBuffer(ID3D12Device* device, const void* data, UINT64 size, ComPtr& resource) {
        const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD); const auto desc = CD3DX12_RESOURCE_DESC::Buffer(size);
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&resource)))) return false;
        void* mapped = nullptr; if (FAILED(resource->Map(0, nullptr, &mapped))) return false;
        memcpy(mapped, data, (size_t)size); resource->Unmap(0, nullptr); return true;
    }

    bool GenerateVertices(ID3D12Device* device, ID3D12CommandQueue* queue) {
        std::vector<NodeGpu> gpuNodes; gpuNodes.reserve(nodes.size());
        for (const Node& node : nodes) gpuNodes.push_back({node.bounds.min.x, node.bounds.min.z, node.size, 0});
        ComPtr nodeBuffer; if (!CreateUploadBuffer(device, gpuNodes.data(), gpuNodes.size() * sizeof(NodeGpu), nodeBuffer)) return false;
        D3D12_RESOURCE_DESC heightDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16_UNORM, sourceWidth, sourceHeight);
        const auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        ComPtr heightMap;
        if (FAILED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &heightDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&heightMap)))) return false;
        UINT64 uploadSize = 0; device->GetCopyableFootprints(&heightDesc, 0, 1, 0, nullptr, nullptr, nullptr, &uploadSize);
        const auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD); const auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
        ComPtr heightUpload;
        if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&heightUpload)))) return false;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {}; UINT rows = 0; UINT64 rowBytes = 0;
        device->GetCopyableFootprints(&heightDesc, 0, 1, 0, &footprint, &rows, &rowBytes, nullptr);
        void* mapped = nullptr; if (FAILED(heightUpload->Map(0, nullptr, &mapped))) return false;
        BYTE* destination = static_cast<BYTE*>(mapped) + footprint.Offset; const BYTE* source = reinterpret_cast<const BYTE*>(sourceHeights.data());
        for (UINT row = 0; row < rows; ++row) memcpy(destination + row * footprint.Footprint.RowPitch, source + row * sourceWidth * sizeof(unsigned short), sourceWidth * sizeof(unsigned short));
        heightUpload->Unmap(0, nullptr);
        const UINT vertexCount = (UINT)nodes.size() * VerticesPerNode;
        const auto vertexDesc = CD3DX12_RESOURCE_DESC::Buffer((UINT64)vertexCount * sizeof(Vertex), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        if (FAILED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &vertexDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&vb)))) return false;

        Microsoft::WRL::ComPtr<ID3DBlob> shader, error, serialized;
        if (FAILED(D3DCompileFromFile(L"Terrain.hlsl", nullptr, nullptr, "CSGenerate", "cs_5_0", 0, 0, &shader, &error))) return false;
        CD3DX12_DESCRIPTOR_RANGE ranges[2]; ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0); ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
        CD3DX12_ROOT_PARAMETER parameters[3]; parameters[0].InitAsDescriptorTable(1, &ranges[0]); parameters[1].InitAsDescriptorTable(1, &ranges[1]); parameters[2].InitAsConstantBufferView(0);
        CD3DX12_ROOT_SIGNATURE_DESC signature(3, parameters, 0, nullptr);
        if (FAILED(D3D12SerializeRootSignature(&signature, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &error))) return false;
        Microsoft::WRL::ComPtr<ID3D12RootSignature> computeRoot;
        if (FAILED(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&computeRoot)))) return false;
        D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc = {}; computeDesc.pRootSignature = computeRoot.Get(); computeDesc.CS = {shader->GetBufferPointer(), shader->GetBufferSize()};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> computePSO;
        if (FAILED(device->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(&computePSO)))) return false;
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {}; heapDesc.NumDescriptors = 3; heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
        if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap)))) return false;
        const UINT descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC heightSrv = {}; heightSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; heightSrv.Format = DXGI_FORMAT_R16_UNORM; heightSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; heightSrv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(heightMap.Get(), &heightSrv, cpu); cpu.ptr += descriptorSize;
        D3D12_SHADER_RESOURCE_VIEW_DESC nodeSrv = {}; nodeSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; nodeSrv.Format = DXGI_FORMAT_UNKNOWN; nodeSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER; nodeSrv.Buffer.NumElements = (UINT)gpuNodes.size(); nodeSrv.Buffer.StructureByteStride = sizeof(NodeGpu);
        device->CreateShaderResourceView(nodeBuffer.Get(), &nodeSrv, cpu); cpu.ptr += descriptorSize;
        D3D12_UNORDERED_ACCESS_VIEW_DESC vertexUav = {}; vertexUav.Format = DXGI_FORMAT_UNKNOWN; vertexUav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER; vertexUav.Buffer.NumElements = vertexCount; vertexUav.Buffer.StructureByteStride = sizeof(Vertex);
        device->CreateUnorderedAccessView(vb.Get(), nullptr, &vertexUav, cpu);
        struct Constants { UINT sourceWidth, sourceHeight, nodeCount, grid; } constants = {sourceWidth, sourceHeight, (UINT)gpuNodes.size(), Grid};
        ComPtr constantsBuffer; if (!CreateUploadBuffer(device, &constants, sizeof(constants), constantsBuffer)) return false;
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator; Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;
        if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) || FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList)))) return false;
        D3D12_TEXTURE_COPY_LOCATION destinationLocation = {}; destinationLocation.pResource = heightMap.Get(); destinationLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION sourceLocation = {}; sourceLocation.pResource = heightUpload.Get(); sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; sourceLocation.PlacedFootprint = footprint;
        commandList->CopyTextureRegion(&destinationLocation, 0, 0, 0, &sourceLocation, nullptr);
        auto heightToSrv = CD3DX12_RESOURCE_BARRIER::Transition(heightMap.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); commandList->ResourceBarrier(1, &heightToSrv);
        ID3D12DescriptorHeap* heaps[] = {heap.Get()}; commandList->SetDescriptorHeaps(1, heaps); commandList->SetComputeRootSignature(computeRoot.Get()); commandList->SetPipelineState(computePSO.Get());
        auto gpu = heap->GetGPUDescriptorHandleForHeapStart(); commandList->SetComputeRootDescriptorTable(0, gpu); gpu.ptr += 2 * descriptorSize; commandList->SetComputeRootDescriptorTable(1, gpu); commandList->SetComputeRootConstantBufferView(2, constantsBuffer->GetGPUVirtualAddress());
        commandList->Dispatch((VerticesPerNode + 63) / 64, (UINT)gpuNodes.size(), 1);
        auto vertexToVB = CD3DX12_RESOURCE_BARRIER::Transition(vb.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER); commandList->ResourceBarrier(1, &vertexToVB);
        if (FAILED(commandList->Close())) return false; ID3D12CommandList* lists[] = {commandList.Get()}; queue->ExecuteCommandLists(1, lists);
        Microsoft::WRL::ComPtr<ID3D12Fence> fence;
        if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) || FAILED(queue->Signal(fence.Get(), 1))) return false;
        if (fence->GetCompletedValue() < 1) { HANDLE eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr); if (!eventHandle) return false; HRESULT waitResult = fence->SetEventOnCompletion(1, eventHandle); if (SUCCEEDED(waitResult)) WaitForSingleObject(eventHandle, INFINITE); CloseHandle(eventHandle); if (FAILED(waitResult)) return false; }
        sourceHeights.clear(); sourceHeights.shrink_to_fit(); return true;
    }

public:
    ~Terrain()
    {
        if (shadowConstants && shadowConstantsMapped)
            shadowConstants->Unmap(0, nullptr);
    }

    bool enabled = true, culling = true, wireframe = false, colorLOD = false;
    float lodDistance = 1.5f; UINT visible = 0, culled = 0;
    bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue) {
        if (!LoadHeightmap()) { MessageBoxA(nullptr, "Cannot load model/terrain/gaea/Erosion_Out.png. Expected a 16-bit grayscale PNG.", "Terrain heightmap", MB_OK | MB_ICONERROR); return false; }
        Build(-2048, -2048, 4096, 0); if (!GenerateVertices(device, queue)) return false;
        // One aligned matrix per cascade: later CPU writes cannot overwrite earlier draws.
        const BYTE emptyConstants[4 * D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT] = {};
        if (!CreateUploadBuffer(device, emptyConstants, sizeof(emptyConstants), shadowConstants))
            return false;
        if (FAILED(shadowConstants->Map(0, nullptr, reinterpret_cast<void**>(&shadowConstantsMapped))))
            return false;
        std::vector<UINT> indices;
        for (UINT j = 0; j < Grid; ++j) for (UINT i = 0; i < Grid; ++i) { UINT a = j * (Grid + 1) + i, b = a + 1, c = a + Grid + 1, d = c + 1; for (UINT v : {a, c, b, b, c, d}) indices.push_back(v); }
        for (UINT edge = 0; edge < 4; ++edge) for (UINT i = 0; i < Grid; ++i) { auto boundary = [&](UINT k) { return edge == 0 ? k : edge == 1 ? Grid * (Grid + 1) + k : edge == 2 ? k * (Grid + 1) : k * (Grid + 1) + Grid; }; UINT a = boundary(i), b = boundary(i + 1), c = MainVertices + edge * (Grid + 1) + i, d = c + 1; for (UINT v : {a, b, c, b, d, c}) indices.push_back(v); }
        indexCount = (UINT)indices.size(); if (!CreateUploadBuffer(device, indices.data(), indices.size() * sizeof(UINT), ib)) return false;
        Microsoft::WRL::ComPtr<ID3DBlob> vs, ps, error, serialized;
        if (FAILED(D3DCompileFromFile(L"Terrain.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vs, &error)) || FAILED(D3DCompileFromFile(L"Terrain.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &ps, &error))) return false;
        CD3DX12_ROOT_PARAMETER parameter; parameter.InitAsConstants(20, 0); CD3DX12_ROOT_SIGNATURE_DESC signature(1, &parameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
        if (FAILED(D3D12SerializeRootSignature(&signature, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &error)) || FAILED(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&root)))) return false;
        D3D12_INPUT_ELEMENT_DESC layout[] = {{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}, {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
        D3D12_GRAPHICS_PIPELINE_STATE_DESC p = {}; p.pRootSignature = root.Get(); p.VS = {vs->GetBufferPointer(), vs->GetBufferSize()}; p.PS = {ps->GetBufferPointer(), ps->GetBufferSize()}; p.InputLayout = {layout, 2}; p.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        p.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT); p.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; p.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT); p.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        p.SampleMask = UINT_MAX; p.NumRenderTargets = 3; p.RTVFormats[0] = p.RTVFormats[1] = DXGI_FORMAT_R32G32B32A32_FLOAT; p.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM; p.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT; p.SampleDesc.Count = 1;
        if (FAILED(device->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&solid)))) return false; p.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
        return SUCCEEDED(device->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&wire)));
    }
    void RenderShadow(ID3D12GraphicsCommandList* cmd, const DirectX::XMMATRIX& lightViewProjection, UINT cascade)
    {
        const UINT offset = cascade * D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
        DirectX::XMFLOAT4X4 matrix;
        DirectX::XMStoreFloat4x4(&matrix, DirectX::XMMatrixTranspose(lightViewProjection));
        memcpy(shadowConstantsMapped + offset, &matrix, sizeof(matrix));
        cmd->SetGraphicsRootConstantBufferView(0, shadowConstants->GetGPUVirtualAddress() + offset);

        D3D12_VERTEX_BUFFER_VIEW vertexView = {
            vb->GetGPUVirtualAddress(),
            (UINT)(nodes.size() * VerticesPerNode * sizeof(Vertex)),
            sizeof(Vertex)
        };
        D3D12_INDEX_BUFFER_VIEW indexView = {
            ib->GetGPUVirtualAddress(), indexCount * sizeof(UINT), DXGI_FORMAT_R32_UINT
        };
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->IASetVertexBuffers(0, 1, &vertexView);
        cmd->IASetIndexBuffer(&indexView);

        for (const Node& node : nodes)
        {
            if (node.children[0] < 0)
                cmd->DrawIndexedInstanced(Grid * Grid * 6, 1, 0, node.firstVertex, 0);
        }
    }

    void Render(ID3D12GraphicsCommandList* cmd, const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& projection, const DirectX::XMFLOAT3& eye) {
        Scene::Frustum frustum; frustum.FromViewAndProjection(view, projection); std::vector<int> selected; culled = 0; Select(0, frustum, eye, selected); visible = (UINT)selected.size();
        D3D12_VERTEX_BUFFER_VIEW vertexView = {vb->GetGPUVirtualAddress(), (UINT)(nodes.size() * VerticesPerNode * sizeof(Vertex)), sizeof(Vertex)}; D3D12_INDEX_BUFFER_VIEW indexView = {ib->GetGPUVirtualAddress(), indexCount * sizeof(UINT), DXGI_FORMAT_R32_UINT};
        cmd->SetGraphicsRootSignature(root.Get()); cmd->SetPipelineState(wireframe ? wire.Get() : solid.Get()); cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); cmd->IASetVertexBuffers(0, 1, &vertexView); cmd->IASetIndexBuffer(&indexView);
        struct Constants { DirectX::XMFLOAT4X4 vp; float lod, debug, pad[2]; } constants = {}; DirectX::XMStoreFloat4x4(&constants.vp, DirectX::XMMatrixTranspose(view * projection));
        for (int id : selected) { constants.lod = (float)nodes[id].level; constants.debug = colorLOD ? 1.0f : 0.0f; cmd->SetGraphicsRoot32BitConstants(0, 20, &constants, 0); cmd->DrawIndexedInstanced(indexCount, 1, 0, nodes[id].firstVertex, 0); }
    }
};
