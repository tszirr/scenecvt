#include "pch.h"

#include "stdx" 
#include "mathx"

#include <iostream>
#include <string>
#include <vector>

#include <assimp/Importer.hpp>
#include <assimp/mesh.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

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
	std::cout << "  /Sp            Pretransform and merge all nodes and instances"  << std::endl;
	std::cout << "  /Ssf <float>   Set scale factor to <float> (default 1.0)"  << std::endl;
	std::cout << "  <input>        Input mesh file path"  << std::endl;
	std::cout << "  <output>       Output mesh file path"  << std::endl;
}

namespace
{

void write_scene(const char* outName, aiScene const& inScene)
{
	scene::Scene outScene;



	auto bytes = scene::dump_scene(outScene);
	auto file = stdx::write_binary_file(outName, std::ios_base::trunc);
	file.write(bytes.data(), bytes.size());
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
	
	// Polygons only
	processFlags |= aiProcess_FindDegenerates | aiProcess_SortByPType;
	importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_POINT | aiPrimitiveType_LINE);

	// Indexed triangles only
	processFlags |= aiProcess_JoinIdenticalVertices;
	processFlags |= aiProcess_Triangulate;

	// Re-generate missing normals
	processFlags |= aiProcess_GenSmoothNormals;

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
		} else if (stdx::check_flag(*arg, "Sp")) {
			processFlags |= aiProcess_PreTransformVertices;
			processMask |= aiProcess_OptimizeGraph; // incompatible
		} else if (stdx::check_flag(*arg, "Ssf")) {
			if (arg + 1 < args_end && sscanf(arg[1], "%f", &scaleFactor) == 1)
				(void) scaleFactor;
			else
				std::cout << "Argument requires number, consult 'mesh help' for help: " << *arg << std::endl;
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

	auto scene = importer.ReadFile(input, 0);
	if (!scene)
		throwx( std::runtime_error("Assimp Loading") );

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
			int mapping = aiTextureMapping_BOX;
			scene->mMaterials[i]->Get(AI_MATKEY_MAPPING_DIFFUSE(0), mapping);
			scene->mMaterials[i]->AddProperty(&mapping, 1, AI_MATKEY_MAPPING_DIFFUSE(0));
		}
	}

	scene = importer.ApplyPostProcessing(processFlags);
	if (!scene)
		throwx( std::runtime_error("Assimp Post-processing") );

	write_scene(output, *scene);

	auto replayInPath = stdx::basename(input);
	replayInPath.insert(replayInPath.begin(), '@');

	std::vector<char const*> replayArgs(args_end - args + 2);
	auto cmdIt = std::copy(args, args_end, replayArgs.data());
	*cmdIt++ = replayInPath.data();
	*cmdIt++ = output;

	record_command(tool, input, replayArgs.data(), replayArgs.size());

	return 0;
}
