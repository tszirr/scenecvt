#include "pch.h"

#include "stdx"

#include <iostream>
#include <string>
#include <vector>
#include <functional>

#include "mathx"

#include <assimp/Importer.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/mesh.h>
#include <assimp/material.h>
#include <assimp/color4.h>
#include <assimp/vector3.h>
#include <assimp/DefaultLogger.hpp>

#include <scenex>
#include <filex>

void scene_help()
{
	std::cout << " Syntax: scenecvt mesh [/VDn] [/Vc] [/VDt] [/Vtan] [/Vbtan] [/Vsn] [/Vsna] [/Von] [/Tsf] [/Iw] [/O] [/S]  [/Ms] <input> <output>"  << std::endl << std::endl;

	std::cout << " Arguments:"  << std::endl;
	std::cout << "  /VDn           Don't include vertex normals"  << std::endl;
	std::cout << "  /Vc            Include vertex colors"  << std::endl;
	std::cout << "  /VDt           Don't include vertex tex coords"  << std::endl;
	std::cout << "  /VFt           Enforce vertex tex coords"  << std::endl;
	std::cout << "  /Vtan          Include vertex tangents"  << std::endl;
	std::cout << "  /Vsn           Re-generate smoothed normals"  << std::endl;
	std::cout << "  /Vsna <float>  Set maximum smoothing angle to <float> degrees (default 30°)"  << std::endl;
	std::cout << "  /Mo            Optimize meshes"  << std::endl;
//	std::cout << "  /Ms            Sort meshes"  << std::endl;
	std::cout << "  /Sg            Geometry only, single material"  << std::endl;
	std::cout << "  /Sm            Identify and merge redundant materials"  << std::endl;
	std::cout << "  /Sp            Pretransform and merge all nodes and instances"  << std::endl;
	std::cout << "  /Ssf <float>   Set scale factor to <float> (default 1.0)"  << std::endl;
	std::cout << "  /E <fmt>       Exports to 3rd-party format"  << std::endl;
	std::cout << "  /S+ <inputs>   Merges many input meshes into one output mesh"  << std::endl;
	std::cout << "  <input>        Input mesh file path"  << std::endl;
	std::cout << "  <output>       Output mesh file path"  << std::endl;
}

namespace
{

template <class D, class S>
void fastcpyn(D* dest, S const* src, size_t N)
{
	static_assert(sizeof(*dest) == sizeof(*src), "Incompatible types");
	memcpy(dest, src, sizeof(*dest) * N);
}

template <class D, class S, class C>
void castcpyn(D* dest, S const* src, size_t N, C&& cast)
{
	for (auto destEnd = dest + N; dest < destEnd; ++dest, ++src)
		*dest = cast(*src);
}

template <class D, class S, class C>
void cnvtcpyn(D* dest, S const* src, size_t N, C&& convert)
{
	for (auto destEnd = dest + N; dest < destEnd; ++dest, ++src)
		convert(*dest, *src);
}

struct binary_converter
{
	template <class Dest, class Src>
	void operator ()(Dest& dest, Src const& src) const
	{
		static_assert(sizeof(dest) <= sizeof(src), "Too few bytes");
		dest = reinterpret_cast<Dest const&>(src);
	}
};

struct binary_duplicator
{
	template <class Dest, class Src>
	void operator ()(Dest& dest, Src const& src) const
	{
		static_assert(sizeof(dest) % sizeof(src) == 0, "Dest has to be a multiple of source");
		typedef Src dest_array[sizeof(dest) / sizeof(src)];
		for (auto& v : reinterpret_cast<dest_array&>(dest))
			v = src;
	}
};

inline unsigned color_cast(aiColor4t<float> const& c)
{
	unsigned a = math::clamp(unsigned(c.a * 256.0f), 0U, 255U);
	unsigned r = math::clamp(unsigned(c.r * 256.0f), 0U, 255U);
	unsigned g = math::clamp(unsigned(c.g * 256.0f), 0U, 255U);
	unsigned b = math::clamp(unsigned(c.b * 256.0f), 0U, 255U);
	return (a << 24U) | (r << 16U) | (g << 8U) | (b);
}

template <class Type, class Fun, class A, class B, class C>
void get_material_property(aiMaterial const& mat, A&& a, B&& b, C&& c, Fun&& fun)
{
	Type t;
	if (AI_SUCCESS == mat.Get(a, b, c, t))
		fun(t);
}

template <class Type, class Dest, class Converter, class A, class B, class C>
void get_material_property(aiMaterial const& mat, A&& a, B&& b, C&& c, Dest& dest, Converter&& convert)
{
	Type t;
	if (AI_SUCCESS == mat.Get(a, b, c, t))
		convert(dest, t);
}

namespace {
	struct PrintReflected {
		template <class T>
		void operator ()(T const& v, char const* t) const {
			std::cout << t << ": " << v << std::endl;
		}
	};
}

void write_meshes(scene::Scene& outScene, aiScene const& inScene)
{
	// Allow for append usage
	size_t baseVertexCount = outScene.positions.size();
	size_t baseIndexCount = outScene.indices.size();
	size_t baseMaterialCount = outScene.materials.size();
	size_t baseMeshCount = outScene.meshes.size();
	size_t baseTextureCount = outScene.textures.size();
	size_t baseInstanceCount = outScene.instances.size();

	// Set up texture table
	std::map<std::string, unsigned> textureIdcs;
	size_t textureChars = baseTextureCount;
	auto&& lookupTexture = [&](aiString const& path) -> unsigned
	{
		auto ins = textureIdcs.insert( std::make_pair(std::string(path.C_Str()), unsigned(textureChars)) );
		if (ins.second) textureChars += ins.first->first.size() + 1; // // include null-termination
		return ins.first->second;
	};
	auto&& textureConvert = [&](unsigned& dest, aiString const& path) { dest = lookupTexture(path); };

	// null dummy (textureIdx == 0)
	if (textureChars == 0)
		lookupTexture(aiString("no:tex"));

	// Count & check
	{
		size_t vertexCount = 0;
		size_t normalCount = 0;
		size_t colorCount = 0;
		size_t texcoordCount = 0;
		size_t tangentCount = 0;
		size_t indexCount = 0;
		size_t meshCount = 0;

		for (unsigned i = 0, ie = inScene.mNumMeshes; i < ie; ++i)
		{
			auto& mesh = *inScene.mMeshes[i];
			if (!mesh.HasPositions()) continue;

			++meshCount;
			vertexCount += mesh.mNumVertices;
			indexCount += mesh.mNumFaces * 3;

			if (mesh.HasNormals()) normalCount = vertexCount;
			if (mesh.HasVertexColors(0)) colorCount = vertexCount;
			if (mesh.HasTextureCoords(0)) texcoordCount = vertexCount;
			if (mesh.HasTangentsAndBitangents()) tangentCount = vertexCount;
		}

		outScene.positions.resize(baseVertexCount + vertexCount);
		outScene.normals.resize(baseVertexCount + normalCount);
		outScene.colors.resize(baseVertexCount + colorCount);
		outScene.texcoords.resize(baseVertexCount + texcoordCount);
		outScene.tangents.resize(baseVertexCount + tangentCount);
		outScene.bitangents.resize(baseVertexCount + tangentCount);
		outScene.indices.resize(baseIndexCount + indexCount);

		outScene.materials.resize(baseMaterialCount + inScene.mNumMaterials);
		outScene.meshes.resize(baseMeshCount + meshCount);

		size_t instanceCount = 0;

		std::function<void (aiNode const&)> addNodeMeshes = [&](aiNode const& node)
		{
			instanceCount += node.mNumMeshes;

			if (node.mChildren)
				for (unsigned i = 0, ie = node.mNumChildren; i < ie; ++i)
					addNodeMeshes(*node.mChildren[i]);
		};
		addNodeMeshes(*inScene.mRootNode);
		
		outScene.instances.resize(baseInstanceCount + instanceCount);
	}

	// Geometry & meshes
	{
		
		unsigned vertexCount = unsigned(baseVertexCount);
		unsigned indexCount = unsigned(baseIndexCount);
		unsigned meshCount = unsigned(baseMeshCount);

		for (unsigned i = 0, ie = inScene.mNumMeshes; i < ie; ++i)
		{
			auto& mesh = *inScene.mMeshes[i];
			if (!mesh.HasPositions()) continue;

			fastcpyn(outScene.positions.data() + vertexCount, mesh.mVertices, mesh.mNumVertices);
			
			if (mesh.HasNormals()) fastcpyn(outScene.normals.data() + vertexCount, mesh.mNormals, mesh.mNumVertices);
			if (mesh.HasTangentsAndBitangents()) {
				fastcpyn(outScene.tangents.data() + vertexCount, mesh.mTangents, mesh.mNumVertices);
				fastcpyn(outScene.bitangents.data() + vertexCount, mesh.mBitangents, mesh.mNumVertices);
			}
			
			if (mesh.HasTextureCoords(0))
				cnvtcpyn(outScene.texcoords.data() + vertexCount, mesh.mTextureCoords[0], mesh.mNumVertices, binary_converter());

			if (mesh.HasVertexColors(0))
				castcpyn(outScene.colors.data() + vertexCount, mesh.mColors[0], mesh.mNumVertices, color_cast);

			auto indexEnd = indexCount;

			for (unsigned i = 0, ie = mesh.mNumFaces; i < ie; ++i)
				for (int j = 0; j < 3; ++j)
					outScene.indices.data()[indexEnd++] = vertexCount + mesh.mFaces[i].mIndices[j];

			auto& outMesh = outScene.meshes[meshCount];

			outMesh.primitives.first = indexCount;
			outMesh.primitives.last = indexEnd;
			// todo: outMesh.bounds;
			outMesh.material = unsigned(baseMaterialCount) + mesh.mMaterialIndex;

			++meshCount;
			vertexCount += mesh.mNumVertices;
			indexCount = indexEnd;
		}
	}
	
	// Materials
	{
		for (unsigned i = 0, ie = inScene.mNumMaterials; i < ie; ++i)
		{
			auto& mat = *inScene.mMaterials[i];
			auto& outMat = outScene.materials[baseMaterialCount + i];

			outMat.reset_default();
			
			// Properties
			get_material_property<aiColor3D>(mat, AI_MATKEY_COLOR_AMBIENT, outMat.diffuse, binary_converter());
			get_material_property<aiColor3D>(mat, AI_MATKEY_COLOR_DIFFUSE, outMat.diffuse, binary_converter());
			get_material_property<aiColor3D>(mat, AI_MATKEY_COLOR_EMISSIVE, outMat.emissive, binary_converter());
			
			get_material_property<aiColor3D>(mat, AI_MATKEY_COLOR_SPECULAR, outMat.specular, binary_converter());
			outMat.reflectivity = outMat.specular;
			get_material_property<aiColor3D>(mat, AI_MATKEY_COLOR_REFLECTIVE, outMat.reflectivity, binary_converter());
			get_material_property<float>(mat, AI_MATKEY_SHININESS_STRENGTH, [&](float pow) { for (auto& c : outMat.specular.c) c *= pow; });
			get_material_property<float>(mat, AI_MATKEY_SHININESS, outMat.shininess, binary_duplicator());
			
			get_material_property<aiColor3D>(mat, AI_MATKEY_COLOR_TRANSPARENT, outMat.filter, binary_converter());
			get_material_property<float>(mat, AI_MATKEY_OPACITY, [&](float opac) { 
				bool color_opaque = [&]() { for (auto& c : outMat.filter.c) if (c != 0.0f) return false; return true; }();
				for (auto& c : outMat.filter.c) if (color_opaque) c = 1.0f - opac; else c *= 1.0f - opac;
			});
			get_material_property<float>(mat, AI_MATKEY_REFRACTI, outMat.refract, binary_duplicator());

			// Textures
			get_material_property<aiString>(mat, AI_MATKEY_TEXTURE_AMBIENT(0), outMat.tex.diffuse, textureConvert);
			get_material_property<aiString>(mat, AI_MATKEY_TEXTURE_DIFFUSE(0), outMat.tex.diffuse, textureConvert);
			get_material_property<aiString>(mat, AI_MATKEY_TEXTURE_EMISSIVE(0), outMat.tex.emissive, textureConvert);
			get_material_property<aiString>(mat, AI_MATKEY_TEXTURE_SPECULAR(0), outMat.tex.specular, textureConvert);
			get_material_property<aiString>(mat, AI_MATKEY_TEXTURE_SHININESS(0), outMat.tex.shininess, textureConvert);
			get_material_property<aiString>(mat, AI_MATKEY_TEXTURE_REFLECTION(0), outMat.tex.reflectivity, textureConvert);

			get_material_property<aiString>(mat, AI_MATKEY_TEXTURE_OPACITY(0), outMat.tex.filter, textureConvert);

			get_material_property<aiString>(mat, AI_MATKEY_TEXTURE_NORMALS(0), outMat.tex.normal, textureConvert);
			get_material_property<aiString>(mat, AI_MATKEY_TEXTURE_DISPLACEMENT(0), outMat.tex.bump, textureConvert);
			get_material_property<aiString>(mat, AI_MATKEY_TEXTURE_HEIGHT(0), outMat.tex.bump, textureConvert);
			get_material_property<float>(mat, AI_MATKEY_BUMPSCALING, outMat.tex.bumpScale, binary_duplicator());

			std::cout << "Material: " << std::endl;
			outMat.reflect(outMat, PrintReflected());
		}
	}

	// Textures
	{
		outScene.texturePaths.resize(textureChars);
		auto textureData = outScene.texturePaths.data();

		for (auto&& texture : textureIdcs)
			strcpy(textureData + texture.second, texture.first.c_str());
	}

	// Instances
	{
		size_t instanceCount = baseInstanceCount;

		std::function<void (aiNode const&, aiMatrix4x4 const&)> addNodeMeshes = [&](aiNode const& node, aiMatrix4x4 const& transform)
		{
			math::mat4x3 instanceTransform;
			for (int r = 0; r < 4; ++r)
				for (int c = 0; c < 3; ++c)
					instanceTransform.cls[r].c[c] = transform[c][r];

			for (unsigned j = 0, je = node.mNumMeshes; j < je; ++j)
			{
				auto& instance = outScene.instances[instanceCount++];

				instance.mesh = unsigned(baseMeshCount) + node.mMeshes[j];
				instance.transform = instanceTransform;
				// todo: bounds?
			}

			if (node.mChildren)
				for (unsigned j = 0, je = node.mNumChildren; j < je; ++j)
				{
					auto& child = *node.mChildren[j];
					addNodeMeshes(child, transform * child.mTransformation);
				}
		};
		addNodeMeshes(*inScene.mRootNode, inScene.mRootNode->mTransformation);
	}
}

} // namespace

int scene_tool(char const* tool, char const* const* args, char const* const* args_end)
{
	if (args_end - args < 2 || stdx::strieq(*args, "help"))
	{
		scene_help();
		return 0;
	}

	auto output = *--args_end;
	auto input = *--args_end;
	// allow for multiple inputs
	auto allInputsBegin = args_end;
	auto allInputsEnd = allInputsBegin + 1;

	Assimp::DefaultLogger::create("assimp.log", Assimp::Logger::NORMAL, aiDefaultLogStream_STDOUT);
	Assimp::Importer importer;

	// Discard colors & tangents by default
	unsigned inputDiscardFlags = aiComponent_COLORS | aiComponent_TANGENTS_AND_BITANGENTS;
	unsigned inputKeepFlags = 0;
	
	// Keep materials by default
	bool geometryOnly = false;

	float scaleFactor = 1.0f;
	bool forceUV = false;

	unsigned processFlags = 0;
	unsigned processMask = 0;

	std::string exportFormat; // if s.th. else than binary scene
	
	// Polygons only
	processFlags |= aiProcess_FindDegenerates | aiProcess_SortByPType;
	importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_POINT | aiPrimitiveType_LINE);

	// Indexed triangles only
	processFlags |= aiProcess_JoinIdenticalVertices;
	processFlags |= aiProcess_Triangulate;

	// Re-generate missing normals
	processFlags |= aiProcess_GenSmoothNormals;
	importer.SetPropertyFloat(AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE, 45.0f);

	// UVs only
	processFlags |= aiProcess_GenUVCoords | aiProcess_TransformUVCoords;

	// Reduce mesh & material count, flatten hierarchy
	processFlags |= aiProcess_OptimizeMeshes | aiProcess_OptimizeGraph;

	for (auto arg = args; arg < args_end; ++arg)
	{
		if (stdx::check_flag(*arg, "VDt")) {
			inputDiscardFlags |= aiComponent_TEXCOORDS;
		} else if (stdx::check_flag(*arg, "VFt")) {
			forceUV = true;
		}
		else if (stdx::check_flag(*arg, "Vc")) {
			inputKeepFlags |= aiComponent_COLORS;
		}
		else if (stdx::check_flag(*arg, "VDn")) {
			inputDiscardFlags |= aiComponent_NORMALS;
			processMask |= aiProcess_GenSmoothNormals;
		} else if (stdx::check_flag(*arg, "Vsn")) {
			inputDiscardFlags |= aiComponent_NORMALS | aiComponent_TANGENTS_AND_BITANGENTS;
		} else if (stdx::check_flag(*arg, "Vsna")) {
			float smoothingAngle;
			if (arg + 1 < args_end && sscanf(arg[1], "%f", &smoothingAngle) == 1) {
				importer.SetPropertyFloat(AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE, smoothingAngle);
				++arg;
			} else
				std::cout << "Argument requires number, consult 'mesh help' for help: " << *arg << std::endl;
		}
		else if (stdx::check_flag(*arg, "Vtan")) {
			processFlags |= aiProcess_CalcTangentSpace;
		}
		else if (stdx::check_flag(*arg, "Mo")) {
			processFlags |= aiProcess_ImproveCacheLocality;
			importer.SetPropertyInteger(AI_CONFIG_PP_ICL_PTCACHE_SIZE, 64);
			std::cout << "Mesh optimization enabled, this might take a while." << std::endl;
		}
		else if (stdx::check_flag(*arg, "Sg")) {
			geometryOnly = true;
		} 
		else if (stdx::check_flag(*arg, "Sm")) {
			processFlags |= aiProcess_RemoveRedundantMaterials;
		} else if (stdx::check_flag(*arg, "Sp")) {
			processFlags |= aiProcess_PreTransformVertices;
			processMask |= aiProcess_OptimizeGraph; // incompatible
		} else if (stdx::check_flag(*arg, "Ssf")) {
			if (arg + 1 < args_end && sscanf(arg[1], "%f", &scaleFactor) == 1)
				(void) scaleFactor;
			else
				std::cout << "Argument requires number, consult 'mesh help' for help: " << *arg << std::endl;
		} else if (stdx::check_flag(*arg, "S+")) {
			allInputsBegin = args_end = arg + 1;
		} else if (stdx::check_flag(*arg, "E")) {
			if (arg + 1 < args_end) {
				exportFormat = *(arg + 1);
				++arg;
			} else
				std::cout << "Argument requires format, consult 'mesh help' for help: " << *arg << std::endl;
		}
		else
			std::cout << "Unrecognized argument, consult 'mesh help' for help: " << *arg << std::endl;
	}

	processFlags &= ~processMask;
	inputDiscardFlags &= ~inputKeepFlags;

	// Remove unwanted mesh components
	importer.SetPropertyInteger(AI_CONFIG_PP_RVC_FLAGS, inputDiscardFlags);
	if (inputDiscardFlags != 0)
		processFlags |= aiProcess_RemoveComponent;

	scene::Scene outScene;

	for (auto addInput = allInputsEnd; addInput-- > allInputsBegin; )
	{
		auto scene = importer.ReadFile(*addInput, 0);
		if (!scene)
		{
			std::cout << "Error loading " << *addInput << std::endl;
			throwx( std::runtime_error("Assimp Loading") );
		}

		if (scaleFactor != 1.0f)
		{
			aiMatrix4x4 scaling;
			aiMatrix4x4::Scaling(aiVector3D(scaleFactor), scaling);
			const_cast<aiMatrix4x4&>(scene->mRootNode->mTransformation) = scaling * scene->mRootNode->mTransformation;
		}

		if (geometryOnly)
		{
			for (unsigned i = 0, ie = scene->mNumMeshes; i < ie; ++i)
				scene->mMeshes[i]->mMaterialIndex = 0;
		}

		if (forceUV)
		{
			for (unsigned i = 0, ie = scene->mNumMaterials; i < ie; ++i)
			{
				auto& material = *scene->mMaterials[i];
				// Ensure that each material has some kind of texture mapping that results in UV coords being generated
				int mapping = aiTextureMapping_BOX;
				if (AI_SUCCESS != material.Get(_AI_MATKEY_MAPPING_BASE, UINT_MAX, UINT_MAX, mapping))
				{
					scene->mMaterials[i]->Get(AI_MATKEY_MAPPING_DIFFUSE(0), mapping);
					scene->mMaterials[i]->AddProperty(&mapping, 1, AI_MATKEY_MAPPING_DIFFUSE(0));
				}
			}
		}

		scene = importer.ApplyPostProcessing(processFlags);
		if (!scene)
		{
			std::cout << "Error processing " << *addInput << std::endl;
			throwx( std::runtime_error("Assimp Post-processing") );
		}

		if (!exportFormat.empty()) {
			std::string outputFile = output;
			if (allInputsEnd - allInputsBegin > 1) {
				outputFile = *addInput;
				outputFile += '.';
				outputFile += exportFormat;
			}
			auto r = Assimp::Exporter().Export(scene, exportFormat.c_str(), output, 0);
			if (r != AI_SUCCESS)
				throwx( std::runtime_error("Assimp Export") );
		}
		else
			write_meshes(outScene, *scene);
	}

	// done exporting
	if (!exportFormat.empty())
		return 0;

	{
		auto bytes = scene::dump_scene(outScene);
		auto file = stdx::write_binary_file(output, std::ios_base::trunc);
		file.write(bytes.data(), bytes.size());
	}

	{
		std::vector<char const*> replayArgs(args_end - args + (allInputsEnd - allInputsBegin) + 1);
		auto cmdIt = std::copy(args, args_end, replayArgs.data());
		
		std::vector<std::string> replayInPaths(allInputsEnd - allInputsBegin);

		for (auto addInput = allInputsBegin; addInput < allInputsEnd; ++addInput)
		{
			auto& replayInPath = replayInPaths[addInput - allInputsBegin];
			replayInPath = stdx::basename(*addInput);
			replayInPath.insert(replayInPath.begin(), '@');

			*cmdIt++ = replayInPath.data();
		}
		
		*cmdIt++ = output;

		record_command(tool, input, replayArgs.data(), replayArgs.size());
	}

	return 0;
}
