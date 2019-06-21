// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
  CyLandLight.h: Static lighting for CyLandComponents
  =============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "StaticLighting.h"
#include "RenderUtils.h"
#include "UnrealEd/Private/Lightmass/Lightmass.h"

//class FLightmassExporter;
class FShadowMapData2D;
class UCyLandComponent;
class ULevel;
class ULightComponent;
struct FQuantizedLightmapData;

/** A texture mapping for landscapes */
class FCyLandStaticLightingTextureMapping : public FStaticLightingTextureMapping
{
public:
	/** Initialization constructor. */
	FCyLandStaticLightingTextureMapping(UCyLandComponent* InPrimitive, FStaticLightingMesh* InMesh, int32 InLightMapWidth, int32 InLightMapHeight, bool bPerformFullQualityRebuild);

	// FStaticLightingTextureMapping interface
	virtual void Apply(FQuantizedLightmapData* QuantizedData, const TMap<ULightComponent*, FShadowMapData2D*>& ShadowMapData, ULevel* LightingScenario);

#if WITH_EDITOR
	/**
	* Export static lighting mapping instance data to an exporter
	* @param Exporter - export interface to process static lighting data
	*/
	//UNREALED_API virtual void ExportMapping(FLightmassExporter* Exporter);
	CYLAND_API virtual void ExportMapping(FLightmassExporter* Exporter)
	{
		//Exporter->LandscapeTextureMappings.AddUnique(this);
		//Exporter->add
	}
#endif	//WITH_EDITOR

	virtual FString GetDescription() const
	{
		return FString(TEXT("CyLandMapping"));
	}
private:

	/** The primitive this mapping represents. */
	UCyLandComponent* const CyLandComponent;
};



/** Represents the triangles of a CyLand component to the static lighting system. */
class FCyLandStaticLightingMesh : public FStaticLightingMesh
{
public:
	// tors
	FCyLandStaticLightingMesh(UCyLandComponent* InComponent, const TArray<ULightComponent*>& InRelevantLights, int32 InExpandQuadsX, int32 InExpandQuadsY, float LightMapRatio, int32 InLOD);
	virtual ~FCyLandStaticLightingMesh();

	// FStaticLightingMesh interface
	virtual void GetTriangle(int32 TriangleIndex,FStaticLightingVertex& OutV0,FStaticLightingVertex& OutV1,FStaticLightingVertex& OutV2) const;
	virtual void GetTriangleIndices(int32 TriangleIndex,int32& OutI0,int32& OutI1,int32& OutI2) const;
	virtual FLightRayIntersection IntersectLightRay(const FVector& Start,const FVector& End,bool bFindNearestIntersection) const;

#if WITH_EDITOR
	/** 
	* Export static lighting mesh instance data to an exporter 
	* @param Exporter - export interface to process static lighting data
	**/
	//UNREALED_API virtual void ExportMeshInstance(FLightmassExporter* Exporter) const;
	CYLAND_API virtual void ExportMeshInstance(FLightmassExporter* Exporter) const
	{
		//Exporter->StaticMeshLightingMeshes.AddUnique(this);
		//
		//for (int32 LightIdx = 0; LightIdx < RelevantLights.Num(); LightIdx++)
		//{
		//	ULightComponent* Light = RelevantLights[LightIdx];
		//	if (Light)
		//	{
		//		Exporter->AddLight(Light);
		//	}
		//}
		//
		//// Add the UStaticMesh and materials to the exporter...
		//if (StaticMesh && StaticMesh->RenderData)
		//{
		//	Exporter->StaticMeshes.AddUnique(StaticMesh);
		//	if (Primitive)
		//	{
		//		for (int32 ResourceIndex = 0; ResourceIndex < StaticMesh->RenderData->LODResources.Num(); ++ResourceIndex)
		//		{
		//			const FStaticMeshLODResources& LODResourceData = StaticMesh->RenderData->LODResources[ResourceIndex];
		//			for (int32 SectionIndex = 0; SectionIndex < LODResourceData.Sections.Num(); ++SectionIndex)
		//			{
		//				const FStaticMeshSection& Section = LODResourceData.Sections[SectionIndex];
		//				UMaterialInterface* Material = Primitive->GetMaterial(Section.MaterialIndex);
		//				Exporter->AddMaterial(Material);
		//			}
		//		}
		//	}
		//}
	}
#endif	//WITH_EDITOR

protected:
	void GetHeightmapData(int32 InLOD, int32 GeometryLOD);

	/** Fills in the static lighting vertex data for the CyLand vertex. */
	void GetStaticLightingVertex(int32 VertexIndex, FStaticLightingVertex& OutVertex) const;

	UCyLandComponent* CyLandComponent;

	// FCyLandStaticLightingMeshData
	FTransform LocalToWorld;
	int32 ComponentSizeQuads;
	float LightMapRatio;
	int32 ExpandQuadsX;
	int32 ExpandQuadsY;

	TArray<FColor> HeightData;
	// Cache
	int32 NumVertices;
	int32 NumQuads;
	float UVFactor;
	bool bReverseWinding;

	friend class FLightmassExporter;

#if WITH_EDITOR
public:
	// Cache data for CyLand upscaling data
	CYLAND_API static TMap<FIntPoint, FColor> CyLandUpscaleHeightDataCache;
	CYLAND_API static TMap<FIntPoint, FColor> CyLandUpscaleXYOffsetDataCache;
#endif
};

namespace
{
	// LightmapRes: Multiplier of lightmap size relative to landscape size
	// X: (Output) PatchExpandCountX (at Lighting LOD)
	// Y: (Output) PatchExpandCountY (at Lighting LOD)
	// ComponentSize: Component size in patches (at LOD 0)
	// LigtmapSize: Size desired for lightmap (texels)
	// DesiredSize: (Output) Recommended lightmap size (texels)
	// return: LightMapRatio
	static float GetTerrainExpandPatchCount(float LightMapRes, int32& X, int32& Y, int32 ComponentSize, int32 LightmapSize, int32& DesiredSize, uint32 LightingLOD)
	{
		if (LightMapRes <= 0) return 0.f;

		// Assuming DXT_1 compression at the moment...
		int32 PixelPaddingX = GPixelFormats[PF_DXT1].BlockSizeX; // "/2" ?
		int32 PixelPaddingY = GPixelFormats[PF_DXT1].BlockSizeY;
		int32 PatchExpandCountX = (LightMapRes >= 1.f) ? (PixelPaddingX) / LightMapRes : (PixelPaddingX);
		int32 PatchExpandCountY = (LightMapRes >= 1.f) ? (PixelPaddingY) / LightMapRes : (PixelPaddingY);

		X = FMath::Max<int32>(1, PatchExpandCountX >> LightingLOD);
		Y = FMath::Max<int32>(1, PatchExpandCountY >> LightingLOD);

		DesiredSize = (LightMapRes >= 1.f) ? FMath::Min<int32>((int32)((ComponentSize + 1) * LightMapRes), 4096) : FMath::Min<int32>((int32)((LightmapSize)* LightMapRes), 4096);
		int32 CurrentSize = (LightMapRes >= 1.f) ? FMath::Min<int32>((int32)((2 * (X << LightingLOD) + ComponentSize + 1) * LightMapRes), 4096) : FMath::Min<int32>((int32)((2 * (X << LightingLOD) + LightmapSize) * LightMapRes), 4096);

		// Find proper Lightmap Size
		if (CurrentSize > DesiredSize)
		{
			// Find maximum bit
			int32 PriorSize = DesiredSize;
			while (DesiredSize > 0)
			{
				PriorSize = DesiredSize;
				DesiredSize = DesiredSize & ~(DesiredSize & ~(DesiredSize - 1));
			}

			DesiredSize = PriorSize << 1; // next bigger size
			if (CurrentSize * CurrentSize <= ((PriorSize * PriorSize) << 1))
			{
				DesiredSize = PriorSize;
			}
		}

		int32 DestSize = (float)DesiredSize / CurrentSize * (ComponentSize*LightMapRes);
		float LightMapRatio = (float)DestSize / (ComponentSize*LightMapRes) * CurrentSize / DesiredSize;
		return LightMapRatio;
	}
}
