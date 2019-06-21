// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
CyLandRenderMobile.cpp: CyLand Rendering without using vertex texture fetch
=============================================================================*/

#include "CyLandRenderMobile.h"
#include "ShaderParameterUtils.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"
#include "PrimitiveSceneInfo.h"
#include "CyLandLayerInfoObject.h"
#include "MeshMaterialShader.h"

void FCyLandVertexFactoryMobile::InitRHI()
{
	// list of declaration items
	FVertexDeclarationElementList Elements;

	// position decls
	Elements.Add(AccessStreamComponent(MobileData.PositionComponent,0));

	if (MobileData.LODHeightsComponent.Num())
	{
		const int32 BaseAttribute = 1;
		for(int32 Index = 0;Index < MobileData.LODHeightsComponent.Num();Index++)
		{
			Elements.Add(AccessStreamComponent(MobileData.LODHeightsComponent[Index], BaseAttribute + Index));
		}
	}

	// create the actual device decls
	InitDeclaration(Elements);
}

/** Shader parameters for use with FCyLandVertexFactory */
class FCyLandVertexFactoryMobileVertexShaderParameters : public FVertexFactoryShaderParameters
{
public:
	/**
	* Bind shader constants by name
	* @param	ParameterMap - mapping of named shader constants to indices
	*/
	virtual void Bind(const FShaderParameterMap& ParameterMap) override
	{
		LodValuesParameter.Bind(ParameterMap,TEXT("LodValues"));
		LodTessellationParameter.Bind(ParameterMap, TEXT("LodTessellationParams"));
		NeighborSectionLodParameter.Bind(ParameterMap,TEXT("NeighborSectionLod"));
		LodBiasParameter.Bind(ParameterMap,TEXT("LodBias"));
		SectionLodsParameter.Bind(ParameterMap,TEXT("SectionLods"));
	}

	/**
	* Serialize shader params to an archive
	* @param	Ar - archive to serialize to
	*/
	virtual void Serialize(FArchive& Ar) override
	{
		Ar << LodValuesParameter;
		Ar << LodTessellationParameter;
		Ar << NeighborSectionLodParameter;
		Ar << LodBiasParameter;
		Ar << SectionLodsParameter;
	}
	
	virtual void GetElementShaderBindings(
		const class FSceneInterface* Scene,
		const FSceneView* InView,
		const class FMeshMaterialShader* Shader,
		bool bShaderRequiresPositionOnlyStream,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams
	) const override
	{
		SCOPE_CYCLE_COUNTER(STAT_CyLandVFDrawTimeVS);

		const FCyLandBatchElementParams* BatchElementParams = (const FCyLandBatchElementParams*)BatchElement.UserData;
		check(BatchElementParams);

		const FCyLandComponentSceneProxyMobile* SceneProxy = (const FCyLandComponentSceneProxyMobile*)BatchElementParams->SceneProxy;
		ShaderBindings.Add(Shader->GetUniformBufferParameter<FCyLandCyUniformShaderParameters>(),*BatchElementParams->CyLandCyUniformShaderParametersResource);

		if (LodValuesParameter.IsBound())
		{
			FVector4 LodValues(
				0.0f, // this is the mesh's LOD, ES2 always uses the LOD0 mesh
				0.0f, // unused
				(float)SceneProxy->SubsectionSizeQuads,
				1.f / (float)SceneProxy->SubsectionSizeQuads);

			ShaderBindings.Add(LodValuesParameter, LodValues);
		}

		if (LodBiasParameter.IsBound())
		{
			FVector CameraLocalPos3D = SceneProxy->WorldToLocal.TransformPosition(InView->ViewMatrices.GetViewOrigin());

			FVector4 LodBias(
				0.0f, // unused
				0.0f, // unused
				CameraLocalPos3D.X + SceneProxy->SectionBase.X,
				CameraLocalPos3D.Y + SceneProxy->SectionBase.Y
			);
			ShaderBindings.Add(LodBiasParameter, LodBias);
		}

		FCyLandComponentSceneProxy::FViewCustomDataLOD* LODData = (FCyLandComponentSceneProxy::FViewCustomDataLOD*)InView->GetCustomData(SceneProxy->GetPrimitiveSceneInfo()->GetIndex());
		int32 SubSectionIndex = BatchElementParams->SubX + BatchElementParams->SubY * SceneProxy->NumSubsections;

		if (LODData != nullptr)
		{
			SceneProxy->PostInitViewCustomData(*InView, LODData);

			if (LodTessellationParameter.IsBound())
			{
				ShaderBindings.Add(LodTessellationParameter, LODData->LodTessellationParams);
			}

			if (SectionLodsParameter.IsBound())
			{
				if (LODData->UseCombinedMeshBatch)
				{
					ShaderBindings.Add(SectionLodsParameter, LODData->ShaderCurrentLOD);
				}
				else // in non combined, only the one representing us as we'll be called 4 times (once per sub section)
				{
					check(SubSectionIndex >= 0);
					FVector4 ShaderCurrentLOD(ForceInitToZero);
					ShaderCurrentLOD.Component(SubSectionIndex) = LODData->ShaderCurrentLOD.Component(SubSectionIndex);

					ShaderBindings.Add(SectionLodsParameter, ShaderCurrentLOD);
				}
			}

			if (NeighborSectionLodParameter.IsBound())
			{
				FVector4 ShaderCurrentNeighborLOD[FCyLandComponentSceneProxy::NEIGHBOR_COUNT] = { FVector4(ForceInitToZero), FVector4(ForceInitToZero), FVector4(ForceInitToZero), FVector4(ForceInitToZero) };

				if (LODData->UseCombinedMeshBatch)
				{
					int32 SubSectionCount = SceneProxy->NumSubsections == 1 ? 1 : FCyLandComponentSceneProxy::MAX_SUBSECTION_COUNT;

					for (int32 NeighborSubSectionIndex = 0; NeighborSubSectionIndex < SubSectionCount; ++NeighborSubSectionIndex)
					{
						ShaderCurrentNeighborLOD[NeighborSubSectionIndex] = LODData->SubSections[NeighborSubSectionIndex].ShaderCurrentNeighborLOD;
						check(ShaderCurrentNeighborLOD[NeighborSubSectionIndex].X != -1.0f); // they should all match so only check the 1st one for simplicity
					}

					ShaderBindings.Add(NeighborSectionLodParameter, ShaderCurrentNeighborLOD);
				}
				else // in non combined, only the one representing us as we'll be called 4 times (once per sub section)
				{
					check(SubSectionIndex >= 0);
					ShaderCurrentNeighborLOD[SubSectionIndex] = LODData->SubSections[SubSectionIndex].ShaderCurrentNeighborLOD;
					check(ShaderCurrentNeighborLOD[SubSectionIndex].X != -1.0f); // they should all match so only check the 1st one for simplicity

					ShaderBindings.Add(NeighborSectionLodParameter, ShaderCurrentNeighborLOD);
				}
			}
		}
	}
protected:
	FShaderParameter LodValuesParameter;
	FShaderParameter LodTessellationParameter;
	FShaderParameter NeighborSectionLodParameter;
	FShaderParameter LodBiasParameter;
	FShaderParameter SectionLodsParameter;
	TShaderUniformBufferParameter<FCyLandCyUniformShaderParameters> CyLandShaderParameters;
};

/** Shader parameters for use with FCyLandVertexFactory */
class FCyLandVertexFactoryMobilePixelShaderParameters : public FCyLandVertexFactoryPixelShaderParameters
{
public:
	/**
	* Bind shader constants by name
	* @param	ParameterMap - mapping of named shader constants to indices
	*/
	virtual void Bind(const FShaderParameterMap& ParameterMap) override
	{
		FCyLandVertexFactoryPixelShaderParameters::Bind(ParameterMap);
		BlendableLayerMaskParameter.Bind(ParameterMap, TEXT("BlendableLayerMask"));
	}

	/**
	* Serialize shader params to an archive
	* @param	Ar - archive to serialize to
	*/
	virtual void Serialize(FArchive& Ar) override
	{
		FCyLandVertexFactoryPixelShaderParameters::Serialize(Ar);
		Ar << BlendableLayerMaskParameter;
	}

	virtual void GetElementShaderBindings(
		const class FSceneInterface* Scene,
		const FSceneView* InView,
		const class FMeshMaterialShader* Shader,
		bool bShaderRequiresPositionOnlyStream,
		ERHIFeatureLevel::Type FeatureLevel,
		const FVertexFactory* VertexFactory,
		const FMeshBatchElement& BatchElement,
		class FMeshDrawSingleShaderBindings& ShaderBindings,
		FVertexInputStreamArray& VertexStreams
	) const override final
	{
		SCOPE_CYCLE_COUNTER(STAT_CyLandVFDrawTimePS);
		
		FCyLandVertexFactoryPixelShaderParameters::GetElementShaderBindings(Scene, InView, Shader, bShaderRequiresPositionOnlyStream, FeatureLevel, VertexFactory, BatchElement, ShaderBindings, VertexStreams);

		if (BlendableLayerMaskParameter.IsBound())
		{
			const FCyLandBatchElementParams* BatchElementParams = (const FCyLandBatchElementParams*)BatchElement.UserData;
			check(BatchElementParams);
			const FCyLandComponentSceneProxyMobile* SceneProxy = (const FCyLandComponentSceneProxyMobile*)BatchElementParams->SceneProxy;
			
			FVector MaskVector;
			MaskVector[0] = (SceneProxy->BlendableLayerMask & (1 << 0)) ? 1 : 0;
			MaskVector[1] = (SceneProxy->BlendableLayerMask & (1 << 1)) ? 1 : 0;
			MaskVector[2] = (SceneProxy->BlendableLayerMask & (1 << 2)) ? 1 : 0;
			ShaderBindings.Add(BlendableLayerMaskParameter, MaskVector);
		}
	}

protected:
	FShaderParameter BlendableLayerMaskParameter;
};

FVertexFactoryShaderParameters* FCyLandVertexFactoryMobile::ConstructShaderParameters(EShaderFrequency ShaderFrequency)
{
	switch( ShaderFrequency )
	{
	case SF_Vertex:
		return new FCyLandVertexFactoryMobileVertexShaderParameters();
	case SF_Pixel:
		return new FCyLandVertexFactoryMobilePixelShaderParameters();
	default:
		return NULL;
	}
}

IMPLEMENT_VERTEX_FACTORY_TYPE(FCyLandVertexFactoryMobile, "/Project/Private/LandscapeVertexFactory.ush", true, true, true, false, false);

/**
* Initialize the RHI for this rendering resource
*/
void FCyLandVertexBufferMobile::InitRHI()
{
	// create a static vertex buffer
	FRHIResourceCreateInfo CreateInfo;
	void* VertexDataPtr = nullptr;
	VertexBufferRHI = RHICreateAndLockVertexBuffer(VertexData.Num(), BUF_Static, CreateInfo, VertexDataPtr);

	// Copy stored platform data and free CPU copy
	FMemory::Memcpy(VertexDataPtr, VertexData.GetData(), VertexData.Num());
	VertexData.Empty();

	RHIUnlockVertexBuffer(VertexBufferRHI);
}

/**
 * Container for FCyLandVertexBufferMobile that we can reference from a thread-safe shared pointer
 * while ensuring the vertex buffer is always destroyed on the render thread.
 **/
struct FCyLandMobileRenderData
{
	FCyLandVertexBufferMobile* VertexBuffer;
	FOccluderVertexArraySP OccluderVerticesSP;

	FCyLandMobileRenderData(const TArray<uint8>& InPlatformData)
	{
		FMemoryReader MemAr(InPlatformData);
		{
			int32 NumMobileVertices;
			TArray<uint8> MobileVerticesData;

			MemAr << NumMobileVertices;
			MobileVerticesData.SetNumUninitialized(NumMobileVertices*sizeof(FCyLandMobileVertex));
			MemAr.Serialize(MobileVerticesData.GetData(), MobileVerticesData.Num());

			VertexBuffer = new FCyLandVertexBufferMobile(MoveTemp(MobileVerticesData));
		}
		
		int32 NumOccluderVertices;
		MemAr << NumOccluderVertices;
		if (NumOccluderVertices > 0)
		{
			OccluderVerticesSP = MakeShared<FOccluderVertexArray, ESPMode::ThreadSafe>();
			OccluderVerticesSP->SetNumUninitialized(NumOccluderVertices);
			MemAr.Serialize(OccluderVerticesSP->GetData(), NumOccluderVertices*sizeof(FVector));

			INC_DWORD_STAT_BY(STAT_CyLandOccluderMem, OccluderVerticesSP->GetAllocatedSize());
		}
	}

	~FCyLandMobileRenderData()
	{
		// Make sure the vertex buffer is always destroyed from the render thread 
		if (VertexBuffer != nullptr)
		{
			if (IsInRenderingThread())
			{
				delete VertexBuffer;
			}
			else
			{
				FCyLandVertexBufferMobile* InVertexBuffer = VertexBuffer;
				ENQUEUE_RENDER_COMMAND(InitCommand)(
					[InVertexBuffer](FRHICommandListImmediate& RHICmdList)
					{
						delete InVertexBuffer;
					});
			}
		}

		if (OccluderVerticesSP.IsValid())
		{
			DEC_DWORD_STAT_BY(STAT_CyLandOccluderMem, OccluderVerticesSP->GetAllocatedSize());
		}
	}
};

FCyLandComponentSceneProxyMobile::FCyLandComponentSceneProxyMobile(UCyLandComponent* InComponent)
	: FCyLandComponentSceneProxy(InComponent)
	, MobileRenderData(InComponent->PlatformData.GetRenderData())
{
	check(InComponent);
	
	check(InComponent->MobileMaterialInterfaces.Num() > 0);
	check(InComponent->MobileWeightmapTextures.Num() > 0);

	WeightmapTextures = InComponent->MobileWeightmapTextures;
	NormalmapTexture = InComponent->MobileWeightmapTextures[0];

	BlendableLayerMask = InComponent->MobileBlendableLayerMask;

#if WITH_EDITOR
	TArray<FCyWeightmapLayerAllocationInfo>& LayerAllocations = InComponent->MobileWeightmapLayerAllocations.Num() ? InComponent->MobileWeightmapLayerAllocations : InComponent->WeightmapLayerAllocations;
	LayerColors.Empty();
	for (auto& Allocation : LayerAllocations)
	{
		if (Allocation.LayerInfo != nullptr)
		{
			LayerColors.Add(Allocation.LayerInfo->LayerUsageDebugColor);
		}
	}
#endif
}

bool FCyLandComponentSceneProxyMobile::CollectOccluderElements(FOccluderElementsCollector& Collector) const
{
	if (MobileRenderData->OccluderVerticesSP.IsValid() && SharedBuffers->OccluderIndicesSP.IsValid())
	{
		Collector.AddElements(MobileRenderData->OccluderVerticesSP, SharedBuffers->OccluderIndicesSP, GetLocalToWorld());
		return true;
	}

	return false;
}

FCyLandComponentSceneProxyMobile::~FCyLandComponentSceneProxyMobile()
{
	if (VertexFactory)
	{
		delete VertexFactory;
		VertexFactory = NULL;
	}
}

SIZE_T FCyLandComponentSceneProxyMobile::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

void FCyLandComponentSceneProxyMobile::CreateRenderThreadResources()
{
	if (IsComponentLevelVisible())
	{
		RegisterNeighbors();
	}
	
	auto FeatureLevel = GetScene().GetFeatureLevel();
	// Use only Index buffers
	SharedBuffers = FCyLandComponentSceneProxy::SharedBuffersMap.FindRef(SharedBuffersKey);
	if (SharedBuffers == nullptr)
	{
		int32 NumOcclusionVertices = MobileRenderData->OccluderVerticesSP.IsValid() ? MobileRenderData->OccluderVerticesSP->Num() : 0;
				
		SharedBuffers = new FCyLandSharedBuffers(
			SharedBuffersKey, SubsectionSizeQuads, NumSubsections,
			GetScene().GetFeatureLevel(), false, NumOcclusionVertices);

		FCyLandComponentSceneProxy::SharedBuffersMap.Add(SharedBuffersKey, SharedBuffers);
	}

	SharedBuffers->AddRef();

	// Init vertex buffer
	check(MobileRenderData->VertexBuffer);
	MobileRenderData->VertexBuffer->InitResource();

	FCyLandVertexFactoryMobile* CyLandVertexFactory = new FCyLandVertexFactoryMobile(FeatureLevel);
	CyLandVertexFactory->MobileData.PositionComponent = FVertexStreamComponent(MobileRenderData->VertexBuffer, STRUCT_OFFSET(FCyLandMobileVertex,Position), sizeof(FCyLandMobileVertex), VET_UByte4N);
	for( uint32 Index = 0; Index < LANDSCAPE_MAX_ES_LOD_COMP; ++Index )
	{
		CyLandVertexFactory->MobileData.LODHeightsComponent.Add
			(FVertexStreamComponent(MobileRenderData->VertexBuffer, STRUCT_OFFSET(FCyLandMobileVertex,LODHeights) + sizeof(uint8) * 4 * Index, sizeof(FCyLandMobileVertex), VET_UByte4N));
	}

	CyLandVertexFactory->InitResource();
	VertexFactory = CyLandVertexFactory;

	// Assign CyLandCyUniformShaderParameters
	CyLandCyUniformShaderParameters.InitResource();
}

TSharedPtr<FCyLandMobileRenderData, ESPMode::ThreadSafe> FCyLandComponentDerivedData::GetRenderData()
{
	check(IsInGameThread());

	if (FPlatformProperties::RequiresCookedData() && CachedRenderData.IsValid())
	{
		// on device we can re-use the cached data if we are re-registering our component.
		return CachedRenderData;
	}
	else
	{
		check(CompressedCyLandData.Num() > 0);

		FMemoryReader Ar(CompressedCyLandData);

		// Note: change LANDSCAPE_FULL_DERIVEDDATA_VER when modifying the serialization layout
		int32 UncompressedSize;
		Ar << UncompressedSize;

		int32 CompressedSize;
		Ar << CompressedSize;

		TArray<uint8> CompressedData;
		CompressedData.Empty(CompressedSize);
		CompressedData.AddUninitialized(CompressedSize);
		Ar.Serialize(CompressedData.GetData(), CompressedSize);

		TArray<uint8> UncompressedData;
		UncompressedData.Empty(UncompressedSize);
		UncompressedData.AddUninitialized(UncompressedSize);

		verify(FCompression::UncompressMemory(NAME_Zlib, UncompressedData.GetData(), UncompressedSize, CompressedData.GetData(), CompressedSize));

		TSharedPtr<FCyLandMobileRenderData, ESPMode::ThreadSafe> RenderData = MakeShareable(new FCyLandMobileRenderData(MoveTemp(UncompressedData)));

		// if running on device		
		if (FPlatformProperties::RequiresCookedData())
		{
			// free the compressed data now that we have used it to create the render data.
			CompressedCyLandData.Empty();
			// store a reference to the render data so we can use it again should the component be reregistered.
			CachedRenderData = RenderData;
		}

		return RenderData;
	}
}