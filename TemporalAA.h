#pragma once
#include "d3dx12.h"
#include <DirectXMath.h>
#include <wrl.h>
#include <d3dcompiler.h>

class TemporalAA {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D12Resource> history[2],oldPositions;
    Ptr<ID3D12DescriptorHeap> srvHeap,rtvHeap;
    Ptr<ID3D12RootSignature> root;
    Ptr<ID3D12PipelineState> pso;
    UINT width=0,height=0,descriptorSize=0,index=0,frame=0;
    bool valid=false;
    DirectX::XMFLOAT4X4 previousVP={};
    static float Halton(UINT n,UINT base) {float value=0,f=1;while(n){f/=base;value+=f*(n%base);n/=base;}return value;}
public:
    bool enabled=true;
    float weight=0.9f;
    void Jitter(DirectX::XMMATRIX& projection) {
        if(!enabled){valid=false;frame=0;return;}
        UINT sample=frame++%8+1;
        DirectX::XMFLOAT4X4 p;DirectX::XMStoreFloat4x4(&p,projection);
        p._31+=(Halton(sample,2)-0.5f)*2/width;
        p._32-=(Halton(sample,3)-0.5f)*2/height;
        projection=DirectX::XMLoadFloat4x4(&p);
    }
    bool Initialize(ID3D12Device* device,UINT w,UINT h) {
        width=w;height=h;valid=false;frame=0;index=0;
        auto heap=CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        auto desc=CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT,w,h,1,1,1,0,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        for(auto& target:history) if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&target))))return false;
        desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;desc.Flags=D3D12_RESOURCE_FLAG_NONE;
        if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&oldPositions))))return false;
        D3D12_DESCRIPTOR_HEAP_DESC hd={};hd.NumDescriptors=5;hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if(FAILED(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&srvHeap))))return false;
        descriptorSize=device->GetDescriptorHandleIncrementSize(hd.Type);
        hd.NumDescriptors=2;hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if(FAILED(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtvHeap))))return false;
        for(UINT i=0;i<2;++i){CD3DX12_CPU_DESCRIPTOR_HANDLE handle(rtvHeap->GetCPUDescriptorHandleForHeapStart(),i,device->GetDescriptorHandleIncrementSize(hd.Type));device->CreateRenderTargetView(history[i].Get(),nullptr,handle);}
        CD3DX12_DESCRIPTOR_RANGE range(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,5,0);
        CD3DX12_ROOT_PARAMETER params[2];params[0].InitAsDescriptorTable(1,&range);params[1].InitAsConstants(20,0);
        D3D12_STATIC_SAMPLER_DESC sampler={};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_NEVER;
        CD3DX12_ROOT_SIGNATURE_DESC signature(2,params,1,&sampler);
        Ptr<ID3DBlob> serialized,error,vs,ps;
        if(FAILED(D3D12SerializeRootSignature(&signature,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&error)))return false;
        if(FAILED(device->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&root))))return false;
        if(FAILED(D3DCompileFromFile(L"TAA.hlsl",nullptr,nullptr,"VSMain","vs_5_0",0,0,&vs,&error)))return false;
        if(FAILED(D3DCompileFromFile(L"TAA.hlsl",nullptr,nullptr,"PSMain","ps_5_0",0,0,&ps,&error)))return false;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC p={};p.pRootSignature=root.Get();p.VS={vs->GetBufferPointer(),vs->GetBufferSize()};p.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
        p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;p.RasterizerState=CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        p.BlendState=CD3DX12_BLEND_DESC(D3D12_DEFAULT);p.DepthStencilState.DepthEnable=FALSE;p.DepthStencilState.StencilEnable=FALSE;
        p.SampleMask=UINT_MAX;p.NumRenderTargets=1;p.RTVFormats[0]=DXGI_FORMAT_R16G16B16A16_FLOAT;p.SampleDesc.Count=1;
        return SUCCEEDED(device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&pso)));
    }
    void Resolve(ID3D12GraphicsCommandList* cmd,ID3D12Device* device,ID3D12Resource* color,ID3D12Resource* positions,ID3D12Resource* normals,const DirectX::XMMATRIX& vp) {
        if(!enabled){valid=false;return;}
        auto barrier=[&](ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b){auto transition=CD3DX12_RESOURCE_BARRIER::Transition(r,a,b);cmd->ResourceBarrier(1,&transition);};
        UINT output=1-index;
        ID3D12Resource* inputs[]={color,positions,normals,history[index].Get(),oldPositions.Get()};
        for(UINT i=0;i<5;++i){D3D12_SHADER_RESOURCE_VIEW_DESC s={};s.Format=inputs[i]->GetDesc().Format;s.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;s.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;s.Texture2D.MipLevels=1;
            CD3DX12_CPU_DESCRIPTOR_HANDLE handle(srvHeap->GetCPUDescriptorHandleForHeapStart(),i,descriptorSize);device->CreateShaderResourceView(inputs[i],&s,handle);}
        barrier(history[output].Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
        CD3DX12_CPU_DESCRIPTOR_HANDLE target(rtvHeap->GetCPUDescriptorHandleForHeapStart(),output,device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));
        cmd->OMSetRenderTargets(1,&target,FALSE,nullptr);
        D3D12_VIEWPORT viewport={0,0,(float)width,(float)height,0,1};D3D12_RECT rect={0,0,(LONG)width,(LONG)height};cmd->RSSetViewports(1,&viewport);cmd->RSSetScissorRects(1,&rect);
        ID3D12DescriptorHeap* heaps[]={srvHeap.Get()};cmd->SetDescriptorHeaps(1,heaps);cmd->SetGraphicsRootSignature(root.Get());cmd->SetPipelineState(pso.Get());cmd->SetGraphicsRootDescriptorTable(0,srvHeap->GetGPUDescriptorHandleForHeapStart());
        struct Constants{DirectX::XMFLOAT4X4 previous;float resolution[2];UINT valid;float weight;} constants={};
        constants.previous=previousVP;constants.resolution[0]=1.0f/width;constants.resolution[1]=1.0f/height;constants.valid=valid?1:0;constants.weight=weight;
        cmd->SetGraphicsRoot32BitConstants(1,20,&constants,0);cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);cmd->IASetVertexBuffers(0,0,nullptr);cmd->DrawInstanced(3,1,0,0);
        barrier(history[output].Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
        auto colorState=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barrier(color,colorState,D3D12_RESOURCE_STATE_COPY_DEST);cmd->CopyResource(color,history[output].Get());barrier(color,D3D12_RESOURCE_STATE_COPY_DEST,colorState);
        barrier(history[output].Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        barrier(positions,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
        barrier(oldPositions.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);cmd->CopyResource(oldPositions.Get(),positions);
        barrier(positions,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);barrier(oldPositions.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        DirectX::XMStoreFloat4x4(&previousVP,DirectX::XMMatrixTranspose(vp));valid=true;index=output;
    }
};
