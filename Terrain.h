#pragma once
#include "SceneCulling.h"
#include "d3dx12.h"
#include <wrl.h>
#include <d3dcompiler.h>
#include <fstream>
#include <string>
#include <cmath>
#include <wincodec.h>
#pragma comment(lib,"windowscodecs.lib")

// Each node is a fixed-resolution tile covering a different world-space area.
class Terrain
{
    using MicrosoftComPtr = Microsoft::WRL::ComPtr<ID3D12Resource>;
    struct Vertex { float x,y,z,nx,ny,nz; };
    struct Node {
        Scene::Aabb bounds;
        int children[4] = {-1,-1,-1,-1};
        UINT firstVertex = 0, level = 0;
        float size = 0;
    };
    std::vector<Node> nodes;
    std::vector<Vertex> vertices;
    MicrosoftComPtr vb, ib;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> solid, wire;
    UINT indexCount = 0;
    static constexpr UINT Grid = 32;
    std::vector<unsigned short> sourceHeights;
    UINT sourceWidth=0,sourceHeight=0;
    bool LoadHeightmap() {
        HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
        HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
        bool success=false;
        if(SUCCEEDED(hr)) {
            Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
            Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
            hr=factory->CreateDecoderFromFilename(L"model/terrain/gaea/Erosion_Out.png",nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder);
            if(SUCCEEDED(hr)) hr=decoder->GetFrame(0,&frame);
            if(SUCCEEDED(hr)) {
                WICPixelFormatGUID format={}; frame->GetPixelFormat(&format);
                hr=frame->GetSize(&sourceWidth,&sourceHeight);
                if(SUCCEEDED(hr) && sourceWidth>1 && sourceHeight>1 && sourceWidth<=16384 && sourceHeight<=16384 && IsEqualGUID(format,GUID_WICPixelFormat16bppGray)) {
                    sourceHeights.resize((size_t)sourceWidth*sourceHeight);
                    success=SUCCEEDED(frame->CopyPixels(nullptr,sourceWidth*2,(UINT)(sourceHeights.size()*2),(BYTE*)sourceHeights.data()));
                }
            }
        }
        factory.Reset();
        if(SUCCEEDED(init)) CoUninitialize();
        return success;
    }
    float Height(float x, float z) const {
        // Bilinear interpolation in one shared image ensures matching tile edges.
        float u=(std::min)((std::max)((x+2048.0f)/4096.0f,0.0f),1.0f)*(sourceWidth-1);
        float v=(std::min)((std::max)((z+2048.0f)/4096.0f,0.0f),1.0f)*(sourceHeight-1);
        UINT ix=(UINT)u,iz=(UINT)v,jx=(std::min)(ix+1,sourceWidth-1),jz=(std::min)(iz+1,sourceHeight-1);
        float a=sourceHeights[iz*sourceWidth+ix]*(1-(u-ix))+sourceHeights[iz*sourceWidth+jx]*(u-ix);
        float b=sourceHeights[jz*sourceWidth+ix]*(1-(u-ix))+sourceHeights[jz*sourceWidth+jx]*(u-ix);
        return (a*(1-(v-iz))+b*(v-iz))*(400.0f/65535.0f);
    }
    int Build(float x,float z,float size,UINT level) {
        int id = (int)nodes.size(); nodes.emplace_back();
        nodes[id].size=size; nodes[id].level=level;
        nodes[id].firstVertex=(UINT)vertices.size();
        nodes[id].bounds.min={x,-65,z}; nodes[id].bounds.max={x+size,400,z+size};
        // Files form a deterministic height-map pyramid. Shared edge coordinates
        // sample the same function, so equal-LOD neighbours agree exactly.
        std::string path="model/terrain/gaea_tiles/tile_"+std::to_string(level)+"_"+std::to_string(id)+".pgm";
        std::vector<unsigned short> heights((Grid+1)*(Grid+1));
        // Rebuild the derived pyramid on startup so a new Gaea export is applied.
        {
            std::ofstream file(path,std::ios::binary);
            file<<"P5\n33 33\n65535\n";
            for(UINT j=0;j<=Grid;++j) for(UINT i=0;i<=Grid;++i) {
                float height=Height(x+size*i/Grid,z+size*j/Grid);
                unsigned short value=(unsigned short)(height/400.0f*65535.0f);
                heights[j*(Grid+1)+i]=value;
                unsigned char b[2]={(unsigned char)(value>>8),(unsigned char)value}; file.write((char*)b,2);
            }
        }
        for(UINT j=0;j<=Grid;++j) for(UINT i=0;i<=Grid;++i) {
            float px=x+size*i/Grid,pz=z+size*j/Grid;
            float y=heights[j*(Grid+1)+i]*(400.0f/65535.0f);
            float nx=Height(px-1,pz)-Height(px+1,pz),nz=Height(px,pz-1)-Height(px,pz+1);
            float len=sqrtf(nx*nx+4+nz*nz);
            vertices.push_back({px,y,pz,nx/len,2/len,nz/len});
        }
        // Duplicate boundary vertices below the surface to hide LOD cracks.
        UINT base=nodes[id].firstVertex;
        for(UINT edge=0;edge<4;++edge) for(UINT i=0;i<=Grid;++i) {
            UINT idx=edge==0?i:edge==1?Grid*(Grid+1)+i:edge==2?i*(Grid+1):i*(Grid+1)+Grid;
            auto v=vertices[base+idx]; v.y-=65; vertices.push_back(v);
        }
        if(level<4) for(int c=0;c<4;++c) {
            int child=Build(x+(c&1)*size/2,z+(c>>1)*size/2,size/2,level+1);
            nodes[id].children[c]=child;
        }
        return id;
    }
    void Select(int id,const Scene::Frustum& frustum,const DirectX::XMFLOAT3& eye,std::vector<int>& out) {
        const auto& n=nodes[id];
        if(culling && !frustum.IntersectsAabb(n.bounds)) { ++culled;return; }
        float dx=(std::max)((std::max)(n.bounds.min.x-eye.x,0.0f),eye.x-n.bounds.max.x);
        float dz=(std::max)((std::max)(n.bounds.min.z-eye.z,0.0f),eye.z-n.bounds.max.z);
        float distance=sqrtf(dx*dx+dz*dz+(eye.y-100)*(eye.y-100));
        if(n.children[0]>=0 && distance<n.size*lodDistance) {
            for(int child:n.children) Select(child,frustum,eye,out);
        } else out.push_back(id);
    }
    bool Buffer(ID3D12Device* device,const void* data,UINT size,MicrosoftComPtr& resource) {
        auto heap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto desc=CD3DX12_RESOURCE_DESC::Buffer(size);
        if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&resource))))return false;
        void* p=nullptr; if(FAILED(resource->Map(0,nullptr,&p)))return false;
        memcpy(p,data,size); resource->Unmap(0,nullptr); return true;
    }
public:
    bool enabled=true,culling=true,wireframe=false,colorLOD=false;
    float lodDistance=1.5f;
    UINT visible=0,culled=0;
    bool Initialize(ID3D12Device* device) {
        CreateDirectoryA("model/terrain",nullptr);
        CreateDirectoryA("model/terrain/gaea_tiles",nullptr);
        if(!LoadHeightmap()) { MessageBoxA(nullptr,"Cannot load model/terrain/gaea/Erosion_Out.png. Expected a 16-bit grayscale PNG.","Terrain heightmap",MB_OK|MB_ICONERROR);return false; }
        Build(-2048,-2048,4096,0);
        std::vector<UINT> indices;
        for(UINT j=0;j<Grid;++j) for(UINT i=0;i<Grid;++i) {
            UINT a=j*(Grid+1)+i,b=a+1,c=a+Grid+1,d=c+1;
            for(UINT v:{a,c,b,b,c,d}) indices.push_back(v);
        }
        for(UINT edge=0;edge<4;++edge) for(UINT i=0;i<Grid;++i) {
            auto boundary=[&](UINT k){return edge==0?k:edge==1?Grid*(Grid+1)+k:edge==2?k*(Grid+1):k*(Grid+1)+Grid;};
            UINT a=boundary(i),b=boundary(i+1),c=(Grid+1)*(Grid+1)+edge*(Grid+1)+i,d=c+1;
            for(UINT v:{a,b,c,b,d,c})indices.push_back(v);
        }
        indexCount=(UINT)indices.size();
        if(!Buffer(device,vertices.data(),(UINT)(vertices.size()*sizeof(Vertex)),vb) || !Buffer(device,indices.data(),(UINT)(indices.size()*4),ib)) return false;
        Microsoft::WRL::ComPtr<ID3DBlob> vs,ps,error,serialized;
        if(FAILED(D3DCompileFromFile(L"Terrain.hlsl",nullptr,nullptr,"VSMain","vs_5_0",0,0,&vs,&error)))return false;
        if(FAILED(D3DCompileFromFile(L"Terrain.hlsl",nullptr,nullptr,"PSMain","ps_5_0",0,0,&ps,&error)))return false;
        CD3DX12_ROOT_PARAMETER param; param.InitAsConstants(20,0);
        CD3DX12_ROOT_SIGNATURE_DESC signature(1,&param,0,nullptr,D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
        if(FAILED(D3D12SerializeRootSignature(&signature,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&error)))return false;
        if(FAILED(device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&root))))return false;
        D3D12_INPUT_ELEMENT_DESC layout[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
        D3D12_GRAPHICS_PIPELINE_STATE_DESC p={}; p.pRootSignature=root.Get();
        p.VS={vs->GetBufferPointer(),vs->GetBufferSize()};p.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
        p.InputLayout={layout,2};p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        p.RasterizerState=CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);p.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
        p.BlendState=CD3DX12_BLEND_DESC(D3D12_DEFAULT);p.DepthStencilState=CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        p.SampleMask=UINT_MAX;p.NumRenderTargets=3;p.RTVFormats[0]=p.RTVFormats[1]=DXGI_FORMAT_R32G32B32A32_FLOAT;
        p.RTVFormats[2]=DXGI_FORMAT_R8G8B8A8_UNORM;p.DSVFormat=DXGI_FORMAT_D24_UNORM_S8_UINT;p.SampleDesc.Count=1;
        if(FAILED(device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&solid))))return false;
        p.RasterizerState.FillMode=D3D12_FILL_MODE_WIREFRAME;
        return SUCCEEDED(device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&wire)));
    }
    void Render(ID3D12GraphicsCommandList* cmd,const DirectX::XMMATRIX& view,const DirectX::XMMATRIX& projection,const DirectX::XMFLOAT3& eye) {
        Scene::Frustum frustum;frustum.FromViewAndProjection(view,projection);
        std::vector<int> selected;culled=0;Select(0,frustum,eye,selected);visible=(UINT)selected.size();
        D3D12_VERTEX_BUFFER_VIEW v={vb->GetGPUVirtualAddress(),(UINT)(vertices.size()*sizeof(Vertex)),sizeof(Vertex)};
        D3D12_INDEX_BUFFER_VIEW i={ib->GetGPUVirtualAddress(),indexCount*4,DXGI_FORMAT_R32_UINT};
        cmd->SetGraphicsRootSignature(root.Get());cmd->SetPipelineState(wireframe?wire.Get():solid.Get());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);cmd->IASetVertexBuffers(0,1,&v);cmd->IASetIndexBuffer(&i);
        struct Constants { DirectX::XMFLOAT4X4 vp;float lod,debug,pad[2]; } constants={};
        DirectX::XMStoreFloat4x4(&constants.vp,DirectX::XMMatrixTranspose(view*projection));
        for(int id:selected) {constants.lod=(float)nodes[id].level;constants.debug=colorLOD?1.0f:0.0f;
            cmd->SetGraphicsRoot32BitConstants(0,20,&constants,0);cmd->DrawIndexedInstanced(indexCount,1,0,nodes[id].firstVertex,0);}
    }
};
