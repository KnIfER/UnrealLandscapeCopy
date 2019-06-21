// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"


CYLAND_API DECLARE_LOG_CATEGORY_EXTERN(LogCyLand, Warning, All);
CYLAND_API DECLARE_LOG_CATEGORY_EXTERN(LogCyLandBP, Display, All);

/**
 * CyLand stats
 */
DECLARE_CYCLE_STAT_EXTERN(TEXT("Dynamic Draw Time"), STAT_CyLandDynamicDrawTime, STATGROUP_Landscape, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Static Draw LOD Time"), STAT_CyLandStaticDrawLODTime, STATGROUP_Landscape, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Render SetMesh Draw Time VS"), STAT_CyLandVFDrawTimeVS, STATGROUP_Landscape, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Render SetMesh Draw Time PS"), STAT_CyLandVFDrawTimePS, STATGROUP_Landscape, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Init View Custom Data"), STAT_CyLandInitViewCustomData, STATGROUP_Landscape, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("PostInit View Custom Data"), STAT_CyLandPostInitViewCustomData, STATGROUP_Landscape, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Compute Custom Mesh Batch LOD"), STAT_CyLandComputeCustomMeshBatchLOD, STATGROUP_Landscape, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Compute Custom Shadow Mesh Batch LOD"), STAT_CyLandComputeCustomShadowMeshBatchLOD, STATGROUP_Landscape, );

DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Components Using SubSection DrawCall"), STAT_CyLandComponentUsingSubSectionDrawCalls, STATGROUP_Landscape, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Tessellated Shadow Cascade"), STAT_CyLandTessellatedShadowCascade, STATGROUP_Landscape, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Tessellated Components"), STAT_CyLandTessellatedComponents, STATGROUP_Landscape, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Processed Triangles"), STAT_CyLandTriangles, STATGROUP_Landscape, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Render Passes"), STAT_CyLandComponentRenderPasses, STATGROUP_Landscape, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("DrawCalls"), STAT_CyLandDrawCalls, STATGROUP_Landscape, );

DECLARE_CYCLE_STAT_EXTERN(TEXT("Regenerate Procedural Heightmap (GameThread)"), STAT_CyLandRegenerateProceduralHeightmaps, STATGROUP_Landscape, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Regenerate Procedural Heightmap (RenderThread)"), STAT_CyLandRegenerateProceduralHeightmaps_RenderThread, STATGROUP_Landscape, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Resolve Procedural Heightmap"), STAT_CyLandResolveProceduralHeightmap, STATGROUP_Landscape, );

DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Regenerate Procedural Heightmap DrawCalls"), STAT_CyLandRegenerateProceduralHeightmapsDrawCalls, STATGROUP_Landscape, );

DECLARE_MEMORY_STAT_EXTERN(TEXT("Vertex Mem"), STAT_CyLandVertexMem, STATGROUP_Landscape, );
DECLARE_MEMORY_STAT_EXTERN(TEXT("Occluder Mem"), STAT_CyLandOccluderMem, STATGROUP_Landscape, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Component Mem"), STAT_CyLandComponentMem, STATGROUP_Landscape, );
