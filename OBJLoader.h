#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <map>
#include <algorithm>
struct OBJVertex
{
	float position[3];
	float color[4];
	float normal[3];
	float texCoord[2];
};
struct Material
{
	std::string name;
	std::string diffuseTexture;
	std::string displacementTexture;
	std::string normalTexture;
	float diffuseColor[3];
};
struct Submesh
{
	UINT indexStart;
	UINT indexCount;
	UINT textureIndex;
};
namespace Obj
{
	struct MeshVertex
	{
		float px, py, pz;
		float nx, ny, nz;
		float u, v;
		float tx, ty, tz;
		float tw;
	};
}
class OBJLoader
{
public:
	static bool LoadMTL(const char* filename, std::map<std::string, Material>& materials)
	{
		std::ifstream file(filename);
		if (!file.is_open()) return false;
		Material currentMaterial;
		std::string line;
		while (std::getline(file, line))
		{
			std::istringstream iss(line);
			std::string prefix;
			iss >> prefix;
			if (prefix == "newmtl")
			{
				if (!currentMaterial.name.empty())
				{
					materials[currentMaterial.name] = currentMaterial;
				}
				iss >> currentMaterial.name;
				currentMaterial.diffuseColor[0] = 0.8f;
				currentMaterial.diffuseColor[1] = 0.8f;
				currentMaterial.diffuseColor[2] = 0.8f;
				currentMaterial.diffuseTexture = "";
			}
			else if (prefix == "Kd")
			{
				iss >> currentMaterial.diffuseColor[0] >> currentMaterial.diffuseColor[1] >> currentMaterial.diffuseColor[2];
			}
			else if (prefix == "map_Kd")
			{
				iss >> currentMaterial.diffuseTexture;
			}
		}
		if (!currentMaterial.name.empty())
		{
			materials[currentMaterial.name] = currentMaterial;
		}
		return true;
	}
	static bool Load(const char* filename, std::vector<OBJVertex>& vertices, std::vector<UINT32>& indices, std::map<std::string, Material>& materials, std::vector<Submesh>& submeshes)
	{
		std::ifstream file(filename);
		if (!file.is_open()) return false;
		std::vector<float> positions;
		std::vector<float> normals;
		std::vector<float> texCoords;
		std::vector<int> posIndices, normIndices, texIndices;
		std::string currentMaterial;
		std::map<std::string, std::vector<UINT32>> materialIndices;
		std::string basePath = filename;
		size_t lastSlash = basePath.find_last_of("/\\");
		if (lastSlash != std::string::npos)
			basePath = basePath.substr(0, lastSlash + 1);
		else
			basePath = "";
		std::string line;
		while (std::getline(file, line))
		{
			std::istringstream iss(line);
			std::string prefix;
			iss >> prefix;
			if (prefix == "mtllib")
			{
				std::string mtlFile;
				iss >> mtlFile;
				LoadMTL((basePath + mtlFile).c_str(), materials);
			}
			else if (prefix == "usemtl")
			{
				iss >> currentMaterial;
				if (materialIndices.find(currentMaterial) == materialIndices.end())
				{
					materialIndices[currentMaterial] = std::vector<UINT32>();
				}
			}
			else if (prefix == "v")
			{
				float x, y, z;
				iss >> x >> y >> z;
				positions.push_back(x);
				positions.push_back(y);
				positions.push_back(z);
			}
			else if (prefix == "vn")
			{
				float x, y, z;
				iss >> x >> y >> z;
				normals.push_back(x);
				normals.push_back(y);
				normals.push_back(z);
			}
			else if (prefix == "vt")
			{
				float u, v;
				iss >> u >> v;
				texCoords.push_back(u);
				texCoords.push_back(v);
			}
			else if (prefix == "f")
			{
				std::string vertex;
				for (int i = 0; i < 3; i++)
				{
					iss >> vertex;
					int posIdx = 0, texIdx = -1, normIdx = -1;
					size_t slash1 = vertex.find('/');
					if (slash1 != std::string::npos)
					{
						posIdx = std::stoi(vertex.substr(0, slash1)) - 1;
						size_t slash2 = vertex.find('/', slash1 + 1);
						if (slash2 != std::string::npos)
						{
							if (slash2 > slash1 + 1)
								texIdx = std::stoi(vertex.substr(slash1 + 1, slash2 - slash1 - 1)) - 1;
							normIdx = std::stoi(vertex.substr(slash2 + 1)) - 1;
						}
					}
					else
					{
						posIdx = std::stoi(vertex) - 1;
					}
					posIndices.push_back(posIdx);
					texIndices.push_back(texIdx);
					normIndices.push_back(normIdx);
					if (!currentMaterial.empty())
					{
						materialIndices[currentMaterial].push_back((uint32_t)posIndices.size() - 1);
					}
				}
			}
		}
		for (size_t i = 0; i < posIndices.size(); i++)
		{
			OBJVertex v;
			int posIdx = posIndices[i];
			v.position[0] = positions[posIdx * 3 + 0];
			v.position[1] = positions[posIdx * 3 + 1];
			v.position[2] = positions[posIdx * 3 + 2];
			if (!normals.empty() && normIndices[i] >= 0)
			{
				int normIdx = normIndices[i];
				v.normal[0] = normals[normIdx * 3 + 0];
				v.normal[1] = normals[normIdx * 3 + 1];
				v.normal[2] = normals[normIdx * 3 + 2];
			}
			else
			{
				v.normal[0] = 0;
				v.normal[1] = 1;
				v.normal[2] = 0;
			}
			v.color[0] = 1.0f;
			v.color[1] = 1.0f;
			v.color[2] = 1.0f;
			v.color[3] = 1.0f;
			if (!texCoords.empty() && texIndices[i] >= 0)
			{
				int texIdx = texIndices[i];
				v.texCoord[0] = texCoords[texIdx * 2 + 0];
				v.texCoord[1] = texCoords[texIdx * 2 + 1];
			}
			else
			{
				v.texCoord[0] = 0.0f;
				v.texCoord[1] = 0.0f;
			}
			vertices.push_back(v);
		}
		std::map<std::string, int> materialToTextureIndex;
		int textureIndex = 0;
		for (const auto& mat : materials)
		{
			if (!mat.second.diffuseTexture.empty())
			{
				materialToTextureIndex[mat.first] = textureIndex++;
			}
		}
		for (const auto& matIndices : materialIndices)
		{
			if (matIndices.second.empty()) continue;
			Submesh submesh;
			submesh.indexStart = (UINT)indices.size();
			submesh.indexCount = (UINT)matIndices.second.size();
			if (materialToTextureIndex.find(matIndices.first) != materialToTextureIndex.end())
			{
				submesh.textureIndex = materialToTextureIndex[matIndices.first];
			}
			else
			{
				submesh.textureIndex = 0;
			}
			for (UINT32 idx : matIndices.second)
			{
				indices.push_back(idx);
			}
			submeshes.push_back(submesh);
		}
		return true;
	}
};
