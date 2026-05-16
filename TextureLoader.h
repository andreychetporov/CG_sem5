#pragma once
#include <windows.h>
#include <d3d12.h>
#include <wrl.h>
#include <fstream>
#include <vector>

using Microsoft::WRL::ComPtr;

#pragma pack(push, 1)
struct TGAHeader
{
	BYTE idLength;
	BYTE colorMapType;
	BYTE imageType;
	WORD colorMapOrigin;
	WORD colorMapLength;
	BYTE colorMapDepth;
	WORD xOrigin;
	WORD yOrigin;
	WORD width;
	WORD height;
	BYTE bitsPerPixel;
	BYTE imageDescriptor;
};
#pragma pack(pop)

class TextureLoader
{
public:
	static bool LoadTGA(const char* filename, std::vector<BYTE>& data, UINT& width, UINT& height, DXGI_FORMAT& format)
	{
		std::ifstream file(filename, std::ios::binary);
		if (!file.is_open()) return false;

		TGAHeader header;
		file.read((char*)&header, sizeof(TGAHeader));

		if (header.imageType != 2 && header.imageType != 10) return false;

		width = header.width;
		height = header.height;
		UINT bytesPerPixel = header.bitsPerPixel / 8;

		if (bytesPerPixel == 3)
			format = DXGI_FORMAT_R8G8B8A8_UNORM;
		else if (bytesPerPixel == 4)
			format = DXGI_FORMAT_R8G8B8A8_UNORM;
		else
			return false;

		file.seekg(header.idLength, std::ios::cur);

		UINT imageSize = width * height * bytesPerPixel;
		std::vector<BYTE> tempData(imageSize);

		if (header.imageType == 2)
		{
			file.read((char*)tempData.data(), imageSize);
		}
		else if (header.imageType == 10)
		{
			UINT pixelCount = width * height;
			UINT currentPixel = 0;
			UINT currentByte = 0;

			while (currentPixel < pixelCount)
			{
				BYTE chunkHeader = 0;
				file.read((char*)&chunkHeader, 1);

				if (chunkHeader < 128)
				{
					chunkHeader++;
					for (int i = 0; i < chunkHeader; i++)
					{
						file.read((char*)&tempData[currentByte], bytesPerPixel);
						currentByte += bytesPerPixel;
						currentPixel++;
					}
				}
				else
				{
					chunkHeader -= 127;
					BYTE pixel[4];
					file.read((char*)pixel, bytesPerPixel);

					for (int i = 0; i < chunkHeader; i++)
					{
						for (UINT j = 0; j < bytesPerPixel; j++)
							tempData[currentByte + j] = pixel[j];
						currentByte += bytesPerPixel;
						currentPixel++;
					}
				}
			}
		}

		data.resize(width * height * 4);
		for (UINT i = 0; i < width * height; i++)
		{
			data[i * 4 + 0] = tempData[i * bytesPerPixel + 2];
			data[i * 4 + 1] = tempData[i * bytesPerPixel + 1];
			data[i * 4 + 2] = tempData[i * bytesPerPixel + 0];
			data[i * 4 + 3] = (bytesPerPixel == 4) ? tempData[i * bytesPerPixel + 3] : 255;
		}

		return true;
	}

	static bool CreateTexture(ID3D12Device* device, ID3D12GraphicsCommandList* commandList,
		const std::vector<BYTE>& data, UINT width, UINT height, DXGI_FORMAT format,
		ComPtr<ID3D12Resource>& texture, ComPtr<ID3D12Resource>& uploadBuffer)
	{
		D3D12_RESOURCE_DESC textureDesc = {};
		textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		textureDesc.Width = width;
		textureDesc.Height = height;
		textureDesc.DepthOrArraySize = 1;
		textureDesc.MipLevels = 1;
		textureDesc.Format = format;
		textureDesc.SampleDesc.Count = 1;
		textureDesc.SampleDesc.Quality = 0;
		textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		D3D12_HEAP_PROPERTIES heapProps = {};
		heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

		if (FAILED(device->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&textureDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&texture)
		))) {
			return false;
		}

		UINT64 uploadBufferSize = 0;
		device->GetCopyableFootprints(&textureDesc, 0, 1, 0, nullptr, nullptr, nullptr, &uploadBufferSize);

		D3D12_HEAP_PROPERTIES uploadHeapProps = {};
		uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

		D3D12_RESOURCE_DESC uploadBufferDesc = {};
		uploadBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		uploadBufferDesc.Width = uploadBufferSize;
		uploadBufferDesc.Height = 1;
		uploadBufferDesc.DepthOrArraySize = 1;
		uploadBufferDesc.MipLevels = 1;
		uploadBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
		uploadBufferDesc.SampleDesc.Count = 1;
		uploadBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		if (FAILED(device->CreateCommittedResource(
			&uploadHeapProps,
			D3D12_HEAP_FLAG_NONE,
			&uploadBufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&uploadBuffer)
		))) {
			return false;
		}

		D3D12_SUBRESOURCE_DATA textureData = {};
		textureData.pData = data.data();
		textureData.RowPitch = width * 4;
		textureData.SlicePitch = textureData.RowPitch * height;

		BYTE* pData;
		uploadBuffer->Map(0, nullptr, (void**)&pData);

		D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout;
		device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &layout, nullptr, nullptr, nullptr);

		for (UINT y = 0; y < height; y++)
		{
			memcpy(pData + layout.Footprint.RowPitch * y,
				(BYTE*)textureData.pData + textureData.RowPitch * y,
				textureData.RowPitch);
		}
		uploadBuffer->Unmap(0, nullptr);

		D3D12_TEXTURE_COPY_LOCATION dst = {};
		dst.pResource = texture.Get();
		dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.SubresourceIndex = 0;

		D3D12_TEXTURE_COPY_LOCATION src = {};
		src.pResource = uploadBuffer.Get();
		src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint = layout;

		commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			texture.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		);
		commandList->ResourceBarrier(1, &barrier);

		return true;
	}
};
