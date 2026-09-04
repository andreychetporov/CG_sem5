#pragma once

#include <windows.h>
#include <d3d12.h>
#include <dxgiformat.h>
#include <wrl.h>
#include <fstream>
#include <vector>
#include <cstdint>
#include <algorithm>
#include "d3dx12.h"

class DDSLoader
{
public:
	static bool Load(ID3D12Device* device, ID3D12GraphicsCommandList* commandList,
		const char* filename, Microsoft::WRL::ComPtr<ID3D12Resource>& texture,
		Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer)
	{
		std::ifstream file(filename, std::ios::binary | std::ios::ate);
		if (!file)
			return false;

		const std::streamsize fileSize = file.tellg();
		if (fileSize < static_cast<std::streamsize>(sizeof(uint32_t) + sizeof(DDSHeader) + sizeof(DDSHeaderDX10)))
			return false;
		file.seekg(0, std::ios::beg);
		std::vector<uint8_t> bytes(static_cast<size_t>(fileSize));
		if (!file.read(reinterpret_cast<char*>(bytes.data()), fileSize))
			return false;

		const uint8_t* cursor = bytes.data();
		const uint32_t magic = *reinterpret_cast<const uint32_t*>(cursor);
		cursor += sizeof(uint32_t);
		if (magic != 0x20534444u)
			return false;

		const DDSHeader* header = reinterpret_cast<const DDSHeader*>(cursor);
		cursor += sizeof(DDSHeader);
		if (header->size != 124 || header->pixelFormat.size != 32 ||
			header->pixelFormat.fourCC != MakeFourCC('D', 'X', '1', '0'))
			return false;

		const DDSHeaderDX10* header10 = reinterpret_cast<const DDSHeaderDX10*>(cursor);
		cursor += sizeof(DDSHeaderDX10);
		if (header10->resourceDimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || header10->arraySize == 0)
			return false;
		if (header10->format != DXGI_FORMAT_BC6H_UF16 && header10->format != DXGI_FORMAT_R32G32_FLOAT)
			return false;

		const bool isCube = (header10->miscFlag & 0x4u) != 0;
		const UINT arraySize = header10->arraySize * (isCube ? 6u : 1u);
		const UINT mipCount = (std::max)(1u, header->mipMapCount);
		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Width = header->width;
		desc.Height = header->height;
		desc.DepthOrArraySize = static_cast<UINT16>(arraySize);
		desc.MipLevels = static_cast<UINT16>(mipCount);
		desc.Format = header10->format;
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

		D3D12_HEAP_PROPERTIES defaultHeap = {};
		defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
		if (FAILED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture))))
			return false;

		const UINT subresourceCount = arraySize * mipCount;
		std::vector<D3D12_SUBRESOURCE_DATA> subresources;
		subresources.reserve(subresourceCount);
		const uint8_t* dataEnd = bytes.data() + bytes.size();
		for (UINT arraySlice = 0; arraySlice < arraySize; ++arraySlice)
		{
			UINT mipWidth = header->width;
			UINT mipHeight = header->height;
			for (UINT mip = 0; mip < mipCount; ++mip)
			{
				UINT64 rowPitch = 0;
				UINT64 slicePitch = 0;
				if (header10->format == DXGI_FORMAT_BC6H_UF16)
				{
					rowPitch = static_cast<UINT64>((std::max)(1u, (mipWidth + 3u) / 4u)) * 16u;
					slicePitch = rowPitch * (std::max)(1u, (mipHeight + 3u) / 4u);
				}
				else
				{
					rowPitch = static_cast<UINT64>(mipWidth) * 8u;
					slicePitch = rowPitch * mipHeight;
				}
				if (cursor + slicePitch > dataEnd)
					return false;

				D3D12_SUBRESOURCE_DATA subresource = {};
				subresource.pData = cursor;
				subresource.RowPitch = static_cast<LONG_PTR>(rowPitch);
				subresource.SlicePitch = static_cast<LONG_PTR>(slicePitch);
				subresources.push_back(subresource);
				cursor += slicePitch;
				mipWidth = (std::max)(1u, mipWidth >> 1u);
				mipHeight = (std::max)(1u, mipHeight >> 1u);
			}
		}

		const UINT64 uploadSize = GetRequiredIntermediateSize(texture.Get(), 0, subresourceCount);
		D3D12_HEAP_PROPERTIES uploadHeap = {};
		uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
		if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer))))
			return false;

		if (UpdateSubresources(commandList, texture.Get(), uploadBuffer.Get(), 0, 0,
			subresourceCount, subresources.data()) == 0)
			return false;
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		commandList->ResourceBarrier(1, &barrier);
		return true;
	}

private:
#pragma pack(push, 1)
	struct DDSPixelFormat
	{
		uint32_t size, flags, fourCC, rgbBitCount;
		uint32_t rMask, gMask, bMask, aMask;
	};
	struct DDSHeader
	{
		uint32_t size, flags, height, width, pitchOrLinearSize, depth, mipMapCount;
		uint32_t reserved1[11];
		DDSPixelFormat pixelFormat;
		uint32_t caps, caps2, caps3, caps4, reserved2;
	};
	struct DDSHeaderDX10
	{
		DXGI_FORMAT format;
		D3D12_RESOURCE_DIMENSION resourceDimension;
		uint32_t miscFlag, arraySize, miscFlags2;
	};
#pragma pack(pop)

	static constexpr uint32_t MakeFourCC(char a, char b, char c, char d)
	{
		return static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 8u) |
			(static_cast<uint32_t>(c) << 16u) | (static_cast<uint32_t>(d) << 24u);
	}
};
