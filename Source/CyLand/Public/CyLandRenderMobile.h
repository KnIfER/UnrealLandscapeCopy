// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
CyLandRenderMobile.h: Mobile landscape rendering
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "RenderResource.h"
#include "VertexFactory.h"
#include "CyLandRender.h"
#include "CyLandPrivate.h"

#define LANDSCAPE_MAX_ES_LOD_COMP	2
#define LANDSCAPE_MAX_ES_LOD		6

struct FCyLandMobileVertex
{
	uint8 Position[4]; // Pos + LOD 0 Height
	uint8 LODHeights[LANDSCAPE_MAX_ES_LOD_COMP*4];
};

/** vertex factory for VTF-heightmap terrain  */
class FCyLandVertexFactoryMobile : public FCyLandVertexFactory
{
	DECLARE_VERTEX_FACTORY_TYPE(FCyLandVertexFactoryMobile);

	typedef FCyLandVertexFactory Super;
public:

	struct FDataType : FCyLandVertexFactory::FDataType
	{
		/** stream which has heights of each LOD levels */
		TArray<FVertexStreamComponent,TFixedAllocator<LANDSCAPE_MAX_ES_LOD_COMP> > LODHeightsComponent;
	};

	FCyLandVertexFactoryMobile(ERHIFeatureLevel::Type InFeatureLevel)
		: FCyLandVertexFactory(InFeatureLevel)
	{
	}

	virtual ~FCyLandVertexFactoryMobile()
	{
		ReleaseResource();
	}

	static FVertexFactoryShaderParameters* ConstructShaderParameters(EShaderFrequency ShaderFrequency);

	/**
	* Should we cache the material's shadertype on this platform with this vertex factory? 
	*/
	static bool ShouldCompilePermutation(EShaderPlatform Platform, const class FMaterial* Material, const class FShaderType* ShaderType)
	{
		auto FeatureLevel = GetMaxSupportedFeatureLevel(Platform);
		return (FeatureLevel == ERHIFeatureLevel::ES2 || FeatureLevel == ERHIFeatureLevel::ES3_1) &&
			(Material->IsUsedWithLandscape() || Material->IsSpecialEngineMaterial());
	}

	static void ModifyCompilationEnvironment( const FVertexFactoryType* Type, EShaderPlatform Platform, const FMaterial* Material, FShaderCompilerEnvironment& OutEnvironment )
	{
		FVertexFactory::ModifyCompilationEnvironment(Type, Platform, Material, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("NUM_VF_PACKED_INTERPOLANTS"), TEXT("1"));
	}

	// FRenderResource interface.
	virtual void InitRHI() override;

	/**
	 * An implementation of the interface used by TSynchronizedResource to update the resource with new data from the game thread.
	 */
	void SetData(const FDataType& InData)
	{
		MobileData = InData;
		UpdateRHI();
	}

private:
	/** stream component data bound to this vertex factory */
	FDataType MobileData; 

	friend class FCyLandComponentSceneProxyMobile;
};

//
// FCyLandVertexBuffer
//
class FCyLandVertexBufferMobile : public FVertexBuffer
{
	TArray<uint8> VertexData;
	int32 DataSize;
public:

	/** Constructor. */
	FCyLandVertexBufferMobile(TArray<uint8> InVertexData)
	:	VertexData(InVertexData)
	,	DataSize(InVertexData.Num())
	{
		INC_DWORD_STAT_BY(STAT_CyLandVertexMem, DataSize);
	}

	/** Destructor. */
	virtual ~FCyLandVertexBufferMobile()
	{
		ReleaseResource();
		DEC_DWORD_STAT_BY(STAT_CyLandVertexMem, DataSize);
	}

	/** 
	* Initialize the RHI for this rendering resource 
	*/
	virtual void InitRHI() override;
};

//
// FCyLandComponentSceneProxy
//
class FCyLandComponentSceneProxyMobile final : public FCyLandComponentSceneProxy
{
	TSharedPtr<FCyLandMobileRenderData, ESPMode::ThreadSafe> MobileRenderData;

	virtual ~FCyLandComponentSceneProxyMobile();

public:
	SIZE_T GetTypeHash() const override;

	FCyLandComponentSceneProxyMobile(UCyLandComponent* InComponent);

	virtual void CreateRenderThreadResources() override;
	virtual bool CollectOccluderElements(FOccluderElementsCollector& Collector) const override;

	uint8 BlendableLayerMask;

	friend class FCyLandVertexBufferMobile;
};
