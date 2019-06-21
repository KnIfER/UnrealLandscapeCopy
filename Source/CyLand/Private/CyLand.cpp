// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
CyLand.cpp: Terrain rendering
=============================================================================*/

#include "CyLand.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"
#include "UObject/RenderingObjectVersion.h"
#include "UObject/UObjectIterator.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/LinkerLoad.h"
#include "CyLandStreamingProxy.h"
#include "CyLandInfo.h"
#include "LightMap.h"
#include "Engine/MapBuildDataRegistry.h"
#include "ShadowMap.h"
#include "CyLandComponent.h"
#include "CyLandLayerInfoObject.h"
#include "CyLandInfoMap.h"
#include "EditorSupportDelegates.h"
#include "CyLandMeshProxyComponent.h"
#include "CyLandRender.h"
#include "CyLandRenderMobile.h"
#include "Logging/TokenizedMessage.h"
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
#include "Misc/MapErrors.h"
#include "DerivedDataCacheInterface.h"
#include "Interfaces/ITargetPlatform.h"
#include "CyLandMeshCollisionComponent.h"
#include "Materials/Material.h"
#include "CyLandMaterialInstanceConstant.h"
#include "Engine/CollisionProfile.h"
#include "CyLandMeshProxyActor.h"
#include "Materials/MaterialExpressionCyLandLayerWeight.h"
#include "Materials/MaterialExpressionCyLandLayerSwitch.h"
#include "Materials/MaterialExpressionCyLandLayerSample.h"
#include "Materials/MaterialExpressionCyLandLayerBlend.h"
#include "Materials/MaterialExpressionCyLandVisibilityMask.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProfilingDebugging/CookStats.h"
#include "CyLandSplinesComponent.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "ComponentRecreateRenderStateContext.h"


#include "MUtils.h"

#if WITH_EDITOR
#include "MaterialUtilities.h"
#include "Settings/EditorExperimentalSettings.h"
#include "Editor.h"
#endif
#include "CyLandVersion.h"
#include "UObject/FortniteMainBranchObjectVersion.h"

/** CyLand stats */

DEFINE_STAT(STAT_CyLandDynamicDrawTime);
DEFINE_STAT(STAT_CyLandStaticDrawLODTime);
DEFINE_STAT(STAT_CyLandVFDrawTimeVS);
DEFINE_STAT(STAT_CyLandInitViewCustomData);
DEFINE_STAT(STAT_CyLandPostInitViewCustomData);
DEFINE_STAT(STAT_CyLandComputeCustomMeshBatchLOD);
DEFINE_STAT(STAT_CyLandComputeCustomShadowMeshBatchLOD);
DEFINE_STAT(STAT_CyLandVFDrawTimePS);
DEFINE_STAT(STAT_CyLandComponentRenderPasses);
DEFINE_STAT(STAT_CyLandTessellatedShadowCascade);
DEFINE_STAT(STAT_CyLandTessellatedComponents);
DEFINE_STAT(STAT_CyLandComponentUsingSubSectionDrawCalls);
DEFINE_STAT(STAT_CyLandDrawCalls);
DEFINE_STAT(STAT_CyLandTriangles);

DEFINE_STAT(STAT_CyLandRegenerateProceduralHeightmaps);
DEFINE_STAT(STAT_CyLandRegenerateProceduralHeightmaps_RenderThread);
DEFINE_STAT(STAT_CyLandResolveProceduralHeightmap);
DEFINE_STAT(STAT_CyLandRegenerateProceduralHeightmapsDrawCalls);

DEFINE_STAT(STAT_CyLandVertexMem);
DEFINE_STAT(STAT_CyLandOccluderMem);
DEFINE_STAT(STAT_CyLandComponentMem);

#if ENABLE_COOK_STATS
namespace CyLandCookStats
{
	static FCookStats::FDDCResourceUsageStats UsageStats;
	static FCookStatsManager::FAutoRegisterCallback RegisterCookStats([](FCookStatsManager::AddStatFuncRef AddStat)
	{
		UsageStats.LogStats(AddStat, TEXT("CyLand.Usage"), TEXT(""));
	});
}
#endif

// Set this to 0 to disable landscape cooking and thus disable it on device.
#define ENABLE_LANDSCAPE_COOKING 1

// If mobile landscape data data needs to be rebuilt (new format, serialization
// differences, etc.) replace the version GUID below with a new one.
// In case of merge conflicts with DDC versions, you MUST generate a new GUID
// and set this new GUID as the version.                                       
#define LANDSCAPE_MOBILE_COOK_VERSION TEXT("A048A0D4A24644BA9948FB08068AE8D7")

#define LOCTEXT_NAMESPACE "CyLand"

static void PrintNumCyLandShadows()
{
	int32 NumComponents = 0;
	int32 NumShadowCasters = 0;
	for (TObjectIterator<UCyLandComponent> It; It; ++It)
	{
		UCyLandComponent* LC = *It;
		NumComponents++;
		if (LC->CastShadow && LC->bCastDynamicShadow)
		{
			NumShadowCasters++;
		}
	}
	UE_LOG(LogConsoleResponse, Display, TEXT("%d/%d landscape components cast shadows"), NumShadowCasters, NumComponents);
}

FAutoConsoleCommand CmdPrintNumCyLandShadows(
	TEXT("ls.PrintNumCyLandShadows"),
	TEXT("Prints the number of landscape components that cast shadows."),
	FConsoleCommandDelegate::CreateStatic(PrintNumCyLandShadows)
	);

UCyLandComponent::UCyLandComponent(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
, GrassData(MakeShareable(new FCyLandComponentGrassData()))
, ChangeTag(0)
{
	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SetGenerateOverlapEvents(false);
	CastShadow = true;
	// by default we want to see the CyLand shadows even in the far shadow cascades
	bCastFarShadow = true;
	bAffectDistanceFieldLighting = true;
	bUseAsOccluder = true;
	bAllowCullDistanceVolume = false;
	CollisionMipLevel = 0;
	StaticLightingResolution = 0.f; // Default value 0 means no overriding

	MaterialInstances.AddDefaulted(); // make sure we always have a MaterialInstances[0]	
	LODIndexToMaterialIndex.AddDefaulted(); // make sure we always have a MaterialInstances[0]	

	HeightmapScaleBias = FVector4(0.0f, 0.0f, 0.0f, 1.0f);
	WeightmapScaleBias = FVector4(0.0f, 0.0f, 0.0f, 1.0f);

	bBoundsChangeTriggersStreamingDataRebuild = true;
	ForcedLOD = -1;
	LODBias = 0;
#if WITH_EDITORONLY_DATA
	LightingLODBias = -1; // -1 Means automatic LOD calculation based on ForcedLOD + LODBias
#endif

	Mobility = EComponentMobility::Static;

#if WITH_EDITORONLY_DATA
	EditToolRenderData = FCyLandEditToolRenderData();
#endif

	LpvBiasMultiplier = 0.0f; // Bias is 0 for landscape, since it's single sided

	// We don't want to load this on the server, this component is for graphical purposes only
	AlwaysLoadOnServer = false;
}

int32 UCyLandComponent::GetMaterialInstanceCount(bool InDynamic) const
{
	ACyLandProxy* Actor = GetCyLandProxy();

	if (Actor != nullptr && Actor->bUseDynamicMaterialInstance && InDynamic)
	{
		return MaterialInstancesDynamic.Num();
	}

	return MaterialInstances.Num();
}

UMaterialInstance* UCyLandComponent::GetMaterialInstance(int32 InIndex, bool InDynamic) const
{
	ACyLandProxy* Actor = GetCyLandProxy();

	if (Actor != nullptr && Actor->bUseDynamicMaterialInstance && InDynamic)
	{
		check(MaterialInstancesDynamic.IsValidIndex(InIndex));
		return MaterialInstancesDynamic[InIndex];
	}

	check(MaterialInstances.IsValidIndex(InIndex));
	return MaterialInstances[InIndex];
}

UMaterialInstanceDynamic* UCyLandComponent::GetMaterialInstanceDynamic(int32 InIndex) const
{
	ACyLandProxy* Actor = GetCyLandProxy();

	if (Actor != nullptr && Actor->bUseDynamicMaterialInstance)
	{
		if (MaterialInstancesDynamic.IsValidIndex(InIndex))
		{
			return MaterialInstancesDynamic[InIndex];
		}
	}

	return nullptr;
}

void UCyLandComponent::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UCyLandComponent* This = CastChecked<UCyLandComponent>(InThis);
	Super::AddReferencedObjects(This, Collector);
}

#if WITH_EDITOR
void UCyLandComponent::BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform)
{
	Super::BeginCacheForCookedPlatformData(TargetPlatform);

	if (TargetPlatform->SupportsFeature(ETargetPlatformFeatures::MobileRendering) && !HasAnyFlags(RF_ClassDefaultObject))
	{
		CheckGenerateCyLandPlatformData(true, TargetPlatform);
	}
}

void ACyLandProxy::CheckGenerateCyLandPlatformData(bool bIsCooking, const ITargetPlatform* TargetPlatform)
{
	for (UCyLandComponent* Component : CyLandComponents)
	{
		Component->CheckGenerateCyLandPlatformData(bIsCooking, TargetPlatform);
	}
}

void UCyLandComponent::CheckGenerateCyLandPlatformData(bool bIsCooking, const ITargetPlatform* TargetPlatform)
{
#if ENABLE_LANDSCAPE_COOKING
	
	// Regenerate platform data only when it's missing or there is a valid hash-mismatch.

	FBufferArchive ComponentStateAr;
	SerializeStateHashes(ComponentStateAr);

	// Serialize the version guid as part of the hash so we can invalidate DDC data if needed
	FString Version(LANDSCAPE_MOBILE_COOK_VERSION);
	ComponentStateAr << Version;
	
	uint32 Hash[5];
	FSHA1::HashBuffer(ComponentStateAr.GetData(), ComponentStateAr.Num(), (uint8*)Hash);
	FGuid NewSourceHash = FGuid(Hash[0] ^ Hash[4], Hash[1], Hash[2], Hash[3]);

	bool bHashMismatch = MobileDataSourceHash != NewSourceHash;
	bool bMissingVertexData = !PlatformData.HasValidPlatformData();
	bool bMissingPixelData = MobileMaterialInterfaces.Num() == 0 || MobileWeightmapTextures.Num() == 0 || MaterialPerLOD.Num() == 0;

	bool bRegenerateVertexData = bMissingVertexData || bMissingPixelData || bHashMismatch;
	
	if (bRegenerateVertexData)
	{
		if (bIsCooking)
		{
			// The DDC is only useful when cooking (see else).

			COOK_STAT(auto Timer = CyLandCookStats::UsageStats.TimeSyncWork());
			if (PlatformData.LoadFromDDC(NewSourceHash))
			{
				COOK_STAT(Timer.AddHit(PlatformData.GetPlatformDataSize()));
			}
			else
			{
				GeneratePlatformVertexData(TargetPlatform);
				PlatformData.SaveToDDC(NewSourceHash);
				COOK_STAT(Timer.AddMiss(PlatformData.GetPlatformDataSize()));
			}
		}
		else
		{
			// When not cooking (e.g. mobile preview) DDC data isn't sufficient to 
			// display correctly, so the platform vertex data must be regenerated.

			GeneratePlatformVertexData(TargetPlatform);
		}
	}

	bool bRegeneratePixelData = bMissingPixelData || bHashMismatch;

	if (bRegeneratePixelData)
	{
		GeneratePlatformPixelData();
	}

	MobileDataSourceHash = NewSourceHash;

#endif
}
#endif

void UCyLandComponent::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FRenderingObjectVersion::GUID);
	Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);

#if WITH_EDITOR
	if (Ar.IsCooking() && !HasAnyFlags(RF_ClassDefaultObject) && Ar.CookingTarget()->SupportsFeature(ETargetPlatformFeatures::MobileRendering))
	{
		// for -oldcook:
		// the old cooker calls BeginCacheForCookedPlatformData after the package export set is tagged, so the mobile material doesn't get saved, so we have to do CheckGenerateCyLandPlatformData in serialize
		// the new cooker clears the texture source data before calling serialize, causing GeneratePlatformVertexData to crash, so we have to do CheckGenerateCyLandPlatformData in BeginCacheForCookedPlatformData
		CheckGenerateCyLandPlatformData(true, Ar.CookingTarget());
	}

	if (Ar.IsCooking() && !HasAnyFlags(RF_ClassDefaultObject) && !Ar.CookingTarget()->SupportsFeature(ETargetPlatformFeatures::DeferredRendering))
	{
		// These properties are only used for SM4+ so we back them up and clear them before serializing them.
		UTexture2D* BackupHeightmapTexture = nullptr;
		UTexture2D* BackupXYOffsetmapTexture = nullptr;
		TArray<UMaterialInstanceConstant*> BackupMaterialInstances;
		TArray<UTexture2D*> BackupWeightmapTextures;

		Exchange(HeightmapTexture, BackupHeightmapTexture);
		Exchange(BackupXYOffsetmapTexture, XYOffsetmapTexture);
		Exchange(BackupMaterialInstances, MaterialInstances);
		Exchange(BackupWeightmapTextures, WeightmapTextures);

		Super::Serialize(Ar);

		Exchange(HeightmapTexture, BackupHeightmapTexture);
		Exchange(BackupXYOffsetmapTexture, XYOffsetmapTexture);
		Exchange(BackupMaterialInstances, MaterialInstances);
		Exchange(BackupWeightmapTextures, WeightmapTextures);
	}
	else
	if (Ar.IsCooking() && !HasAnyFlags(RF_ClassDefaultObject) && !Ar.CookingTarget()->SupportsFeature(ETargetPlatformFeatures::MobileRendering))
	{
		// These properties are only used for mobile so we back them up and clear them before serializing them.
		TArray<UMaterialInterface*> BackupMobileMaterialInterfaces;
		TArray<UTexture2D*> BackupMobileWeightmapTextures;

		Exchange(MobileMaterialInterfaces, BackupMobileMaterialInterfaces);
		Exchange(MobileWeightmapTextures, BackupMobileWeightmapTextures);

		Super::Serialize(Ar);

		Exchange(MobileMaterialInterfaces, BackupMobileMaterialInterfaces);
		Exchange(MobileWeightmapTextures, BackupMobileWeightmapTextures);
	}
	else
#endif
	{
		Super::Serialize(Ar);
	}

	if (Ar.IsLoading() && Ar.CustomVer(FRenderingObjectVersion::GUID) < FRenderingObjectVersion::MapBuildDataSeparatePackage)
	{
		FMeshMapBuildData* LegacyMapBuildData = new FMeshMapBuildData();
		Ar << LegacyMapBuildData->LightMap;
		Ar << LegacyMapBuildData->ShadowMap;
		LegacyMapBuildData->IrrelevantLights = IrrelevantLights_DEPRECATED;

		FMeshMapBuildLegacyData LegacyComponentData;
		LegacyComponentData.Data.Emplace(MapBuildDataId, LegacyMapBuildData);
		GComponentsWithLegacyLightmaps.AddAnnotation(this, LegacyComponentData);
	}

	if (Ar.IsLoading() && Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::NewLandscapeMaterialPerLOD)
	{
		if (MobileMaterialInterface_DEPRECATED != nullptr)
		{
			MobileMaterialInterfaces.AddUnique(MobileMaterialInterface_DEPRECATED);
		}

#if WITH_EDITORONLY_DATA
		if (MobileCombinationMaterialInstance_DEPRECATED != nullptr)
		{
			MobileCombinationMaterialInstances.AddUnique(MobileCombinationMaterialInstance_DEPRECATED);
		}
#endif
	}

	if (Ar.UE4Ver() >= VER_UE4_SERIALIZE_LANDSCAPE_GRASS_DATA)
	{
		// Share the shared ref so PIE can share this data
		if (Ar.GetPortFlags() & PPF_DuplicateForPIE)
		{
			if (Ar.IsSaving())
			{
				PTRINT GrassDataPointer = (PTRINT)&GrassData;
				Ar << GrassDataPointer;
			}
			else
			{
				PTRINT GrassDataPointer;
				Ar << GrassDataPointer;
				// Duplicate shared reference
				GrassData = *(TSharedRef<FCyLandComponentGrassData, ESPMode::ThreadSafe>*)GrassDataPointer;
			}
		}
		else
		{
			Ar << GrassData.Get();
		}
	}

#if WITH_EDITOR
	if (Ar.IsTransacting())
	{
		Ar << EditToolRenderData.SelectedType;
	}
#endif

	bool bCooked = false;

	if (Ar.UE4Ver() >= VER_UE4_LANDSCAPE_PLATFORMDATA_COOKING && !HasAnyFlags(RF_ClassDefaultObject))
	{
		bCooked = Ar.IsCooking() || (FPlatformProperties::RequiresCookedData() && Ar.IsSaving());
		// This is needed when loading cooked data, to know to serialize differently
		Ar << bCooked;
	}

	if (FPlatformProperties::RequiresCookedData() && !bCooked && Ar.IsLoading())
	{
		UE_LOG(LogCyLand, Fatal, TEXT("This platform requires cooked packages, and this landscape does not contain cooked data %s."), *GetName());
	}

#if ENABLE_LANDSCAPE_COOKING
	if (bCooked)
	{
		bool bCookedMobileData = Ar.IsCooking() && Ar.CookingTarget()->SupportsFeature(ETargetPlatformFeatures::MobileRendering);
		Ar << bCookedMobileData;

		// Saving for cooking path
		if (bCookedMobileData)
		{
			if (Ar.IsCooking())
			{
				check(PlatformData.HasValidPlatformData());
			}
			Ar << PlatformData;
		}
	}
#endif

#if WITH_EDITOR
	if (Ar.GetPortFlags() & PPF_DuplicateForPIE)
	{
		Ar << PlatformData;
	}
#endif
}

void UCyLandComponent::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);

	CumulativeResourceSize.AddDedicatedSystemMemoryBytes(GrassData->GetAllocatedSize());
}

#if WITH_EDITOR
UMaterialInterface* UCyLandComponent::GetCyLandMaterial(int8 InLODIndex) const
{
	if (InLODIndex != INDEX_NONE)
	{		
		UWorld* World = GetWorld();

		if (World != nullptr)
		{
			for (const FCyLandComponentMaterialOverride& Material : OverrideMaterials)
			{
				if (Material.LODIndex.GetValueForFeatureLevel(World->FeatureLevel) == InLODIndex)
				{
					if (Material.Material != nullptr)
					{
						return Material.Material;
					}

					break;
				}
			}
		}
	}
	
	if (OverrideMaterial != nullptr)
	{
		return OverrideMaterial;
	}

	ACyLandProxy* Proxy = GetCyLandProxy();
	if (Proxy)
	{
		return Proxy->GetCyLandMaterial(InLODIndex);
	}
	
	return UMaterial::GetDefaultMaterial(MD_Surface);
}

UMaterialInterface* UCyLandComponent::GetCyLandHoleMaterial() const
{
	if (OverrideHoleMaterial)
	{
		return OverrideHoleMaterial;
	}
	ACyLandProxy* Proxy = GetCyLandProxy();
	if (Proxy)
	{
		return Proxy->GetCyLandHoleMaterial();
	}
	return nullptr;
}

bool UCyLandComponent::ComponentHasVisibilityPainted() const
{
	for (const FCyWeightmapLayerAllocationInfo& Allocation : WeightmapLayerAllocations)
	{
		if (Allocation.LayerInfo == ACyLandProxy::VisibilityLayer)
		{
			return true;
		}
	}

	return false;
}

void UCyLandComponent::GetLayerDebugColorKey(int32& R, int32& G, int32& B) const
{
	UCyLandInfo* Info = GetCyLandInfo();
	if (Info != nullptr)
	{
		R = INDEX_NONE, G = INDEX_NONE, B = INDEX_NONE;

		for (auto It = Info->Layers.CreateConstIterator(); It; It++)
		{
			const FCyLandInfoLayerSettings& LayerStruct = *It;
			if (LayerStruct.DebugColorChannel > 0
				&& LayerStruct.LayerInfoObj)
			{
				for (int32 LayerIdx = 0; LayerIdx < WeightmapLayerAllocations.Num(); LayerIdx++)
				{
					if (WeightmapLayerAllocations[LayerIdx].LayerInfo == LayerStruct.LayerInfoObj)
					{
						if (LayerStruct.DebugColorChannel & 1) // R
						{
							R = (WeightmapLayerAllocations[LayerIdx].WeightmapTextureIndex * 4 + WeightmapLayerAllocations[LayerIdx].WeightmapTextureChannel);
						}
						if (LayerStruct.DebugColorChannel & 2) // G
						{
							G = (WeightmapLayerAllocations[LayerIdx].WeightmapTextureIndex * 4 + WeightmapLayerAllocations[LayerIdx].WeightmapTextureChannel);
						}
						if (LayerStruct.DebugColorChannel & 4) // B
						{
							B = (WeightmapLayerAllocations[LayerIdx].WeightmapTextureIndex * 4 + WeightmapLayerAllocations[LayerIdx].WeightmapTextureChannel);
						}
						break;
					}
				}
			}
		}
	}
}
#endif	//WITH_EDITOR

UCyLandMeshCollisionComponent::UCyLandMeshCollisionComponent(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
	// make landscape always create? 
	bAlwaysCreatePhysicsState = true;
}

UCyLandInfo::UCyLandInfo(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
}

#if WITH_EDITOR
void UCyLandInfo::UpdateDebugColorMaterial()
{
	FlushRenderingCommands();
	//GWarn->BeginSlowTask( *FString::Printf(TEXT("Compiling layer color combinations for %s"), *GetName()), true);

	for (auto It = XYtoComponentMap.CreateIterator(); It; ++It)
	{
		UCyLandComponent* Comp = It.Value();
		if (Comp)
		{
			Comp->EditToolRenderData.UpdateDebugColorMaterial(Comp);
			Comp->UpdateEditToolRenderData();
		}
	}
	FlushRenderingCommands();
	//GWarn->EndSlowTask();
}

void UCyLandComponent::UpdatedSharedPropertiesFromActor()
{
	ACyLandProxy* CyLandProxy = GetCyLandProxy();

	bCastStaticShadow = CyLandProxy->bCastStaticShadow;
	bCastShadowAsTwoSided = CyLandProxy->bCastShadowAsTwoSided;
	bCastFarShadow = CyLandProxy->bCastFarShadow;
	bAffectDistanceFieldLighting = CyLandProxy->bAffectDistanceFieldLighting;
	bRenderCustomDepth = CyLandProxy->bRenderCustomDepth;
	LDMaxDrawDistance = CyLandProxy->LDMaxDrawDistance;
	CustomDepthStencilValue = CyLandProxy->CustomDepthStencilValue;
	LightingChannels = CyLandProxy->LightingChannels;
}

void UCyLandComponent::PostLoad()
{
	Super::PostLoad();

	ACyLandProxy* CyLandProxy = GetCyLandProxy();
	if (ensure(CyLandProxy))
	{
		// Ensure that the component's lighting settings matches the actor's.
		UpdatedSharedPropertiesFromActor();	

		// check SectionBaseX/Y are correct
		int32 CheckSectionBaseX = FMath::RoundToInt(RelativeLocation.X) + CyLandProxy->CyLandSectionOffset.X;
		int32 CheckSectionBaseY = FMath::RoundToInt(RelativeLocation.Y) + CyLandProxy->CyLandSectionOffset.Y;
		if (CheckSectionBaseX != SectionBaseX ||
			CheckSectionBaseY != SectionBaseY)
		{
			UE_LOG(LogCyLand, Warning, TEXT("CyLandComponent SectionBaseX disagrees with its location, attempted automated fix: '%s', %d,%d vs %d,%d."),
				*GetFullName(), SectionBaseX, SectionBaseY, CheckSectionBaseX, CheckSectionBaseY);
			SectionBaseX = CheckSectionBaseX;
			SectionBaseY = CheckSectionBaseY;
		}
	}

#if WITH_EDITOR
	if (GIsEditor && !HasAnyFlags(RF_ClassDefaultObject))
	{
		// This is to ensure that component relative location is exact section base offset value
		float CheckRelativeLocationX = float(SectionBaseX - CyLandProxy->CyLandSectionOffset.X);
		float CheckRelativeLocationY = float(SectionBaseY - CyLandProxy->CyLandSectionOffset.Y);
		if (CheckRelativeLocationX != RelativeLocation.X || 
			CheckRelativeLocationY != RelativeLocation.Y)
		{
			UE_LOG(LogCyLand, Warning, TEXT("CyLandComponent RelativeLocation disagrees with its section base, attempted automated fix: '%s', %f,%f vs %f,%f."),
				*GetFullName(), RelativeLocation.X, RelativeLocation.Y, CheckRelativeLocationX, CheckRelativeLocationY);
			RelativeLocation.X = CheckRelativeLocationX;
			RelativeLocation.Y = CheckRelativeLocationY;
		}

		// Remove standalone flags from data textures to ensure data is unloaded in the editor when reverting an unsaved level.
		// Previous version of landscape set these flags on creation.
		if (HeightmapTexture && HeightmapTexture->HasAnyFlags(RF_Standalone))
		{
			HeightmapTexture->ClearFlags(RF_Standalone);
		}
		for (int32 Idx = 0; Idx < WeightmapTextures.Num(); Idx++)
		{
			if (WeightmapTextures[Idx] && WeightmapTextures[Idx]->HasAnyFlags(RF_Standalone))
			{
				WeightmapTextures[Idx]->ClearFlags(RF_Standalone);
			}
		}

		if (GIBakedBaseColorTexture)
		{
			if (GIBakedBaseColorTexture->GetOutermost() != GetOutermost())
			{
				// The GIBakedBaseColorTexture property was never intended to be reassigned, but it was previously editable so we need to null any invalid values
				// it will get recreated by ACyLandProxy::UpdateBakedTextures()
				GIBakedBaseColorTexture = nullptr;
				BakedTextureMaterialGuid = FGuid();
			}
			else
			{
				// Remove public flag from GI textures to stop them being visible in the content browser.
				// Previous version of landscape set these flags on creation.
				if (GIBakedBaseColorTexture->HasAnyFlags(RF_Public))
				{
					GIBakedBaseColorTexture->ClearFlags(RF_Public);
				}
			}
		}
	}
#endif

#if WITH_EDITORONLY_DATA
	// Handle old MaterialInstance
	if (MaterialInstance_DEPRECATED)
	{
		MaterialInstances.Empty(1);
		MaterialInstances.Add(MaterialInstance_DEPRECATED);
		MaterialInstance_DEPRECATED = nullptr;

#if WITH_EDITOR
		if (GIsEditor && MaterialInstances.Num() > 0 && MaterialInstances[0] != nullptr)
		{
			MaterialInstances[0]->ConditionalPostLoad();
			UpdateMaterialInstances();
		}
#endif // WITH_EDITOR
	}
#endif

#if !UE_BUILD_SHIPPING
	// This will fix the data in case there is mismatch between save of asset/maps
	int8 MaxLOD = FMath::CeilLogTwo(SubsectionSizeQuads + 1) - 1;

	TArray<UCyLandMaterialInstanceConstant*> ResolvedMaterials;

	if (MaterialIndexToDisabledTessellationMaterial.Num() < MaxLOD)
	{
		MaterialIndexToDisabledTessellationMaterial.Init(INDEX_NONE, MaxLOD+1);
	}

	// Be sure we have the appropriate material count
	for (int32 i = 0; i < MaterialInstances.Num(); ++i)
	{
		UCyLandMaterialInstanceConstant* CyLandMIC = Cast<UCyLandMaterialInstanceConstant>(MaterialInstances[i]);

		if (CyLandMIC == nullptr || CyLandMIC->Parent == nullptr || ResolvedMaterials.Contains(CyLandMIC))
		{
			continue;
		}

		UMaterial* Material = CyLandMIC->GetMaterial();
		bool FoundMatchingDisablingMaterial = false;

		// If we have tessellation, find the equivalent with disable tessellation set
		if (Material->D3D11TessellationMode != EMaterialTessellationMode::MTM_NoTessellation)
		{
			for (int32 j = i + 1; j < MaterialInstances.Num(); ++j)
			{
				UCyLandMaterialInstanceConstant* OtherCyLandMIC = Cast<UCyLandMaterialInstanceConstant>(MaterialInstances[j]);

				if (OtherCyLandMIC == nullptr || OtherCyLandMIC->Parent == nullptr)
				{
					continue;
				}

				UMaterial* OtherMaterial = OtherCyLandMIC->GetMaterial();

				if (OtherMaterial == Material && OtherCyLandMIC->bDisableTessellation)  // we have a matching material
				{			
					FoundMatchingDisablingMaterial = true;
					ResolvedMaterials.Add(CyLandMIC);
					ResolvedMaterials.Add(OtherCyLandMIC);
					MaterialIndexToDisabledTessellationMaterial[i] = j;
					break;
				}
			}

			if (!FoundMatchingDisablingMaterial)
			{
				if (GIsEditor)
				{
					UpdateMaterialInstances();
					break;
				}
				else
				{
					UE_LOG(LogCyLand, Error, TEXT("CyLand component (%d, %d) have a material with Tessellation enabled but we do not have the corresponding disabling one. To correct this issue, open the map in the editor and resave the map."), SectionBaseX, SectionBaseY);
				}
			}
		}
	}	

	if (LODIndexToMaterialIndex.Num() != MaxLOD+1)
	{
		if (GIsEditor)
		{
			UpdateMaterialInstances();
		}
		else
		{
			// Correct in-place differences by applying the highest LOD value we have to the newly added items as most case will be missing items added at the end
			LODIndexToMaterialIndex.SetNumZeroed(MaxLOD + 1);

			int8 LastLODIndex = 0;

			for (int32 i = 0; i < LODIndexToMaterialIndex.Num(); ++i)
			{
				if (LODIndexToMaterialIndex[i] > LastLODIndex)
				{
					LastLODIndex = LODIndexToMaterialIndex[i];
				}

				if (LODIndexToMaterialIndex[i] == 0 && LastLODIndex != 0)
				{
					LODIndexToMaterialIndex[i] = LastLODIndex;
				}
			}
		}
	}
#endif // UE_BUILD_SHIPPING

#if WITH_EDITOR
	if (GIsEditor && !HasAnyFlags(RF_ClassDefaultObject))
	{
		// Move the MICs and Textures back to the Package if they're currently in the level
		// Moving them into the level caused them to be duplicated when running PIE, which is *very very slow*, so we've reverted that change
		// Also clear the public flag to avoid various issues, e.g. generating and saving thumbnails that can never be seen
		ULevel* Level = GetLevel();
		if (ensure(Level))
		{
			TArray<UObject*> ObjectsToMoveFromLevelToPackage;
			GetGeneratedTexturesAndMaterialInstances(ObjectsToMoveFromLevelToPackage);

			UPackage* MyPackage = GetOutermost();
			for (auto* Obj : ObjectsToMoveFromLevelToPackage)
			{
				Obj->ClearFlags(RF_Public);
				if (Obj->GetOuter() == Level)
				{
					Obj->Rename(nullptr, MyPackage, REN_DoNotDirty | REN_DontCreateRedirectors | REN_ForceNoResetLoaders | REN_NonTransactional);
				}
			}
		}
	}
#endif

#if !UE_BUILD_SHIPPING
	if (MobileCombinationMaterialInstances.Num() == 0)
	{
		if (GIsEditor)
		{
			UpdateMaterialInstances();
		}
		else
		{
			if(GMaxRHIFeatureLevel <= ERHIFeatureLevel::ES3_1)
			{
				UE_LOG(LogCyLand, Error, TEXT("CyLand component (%d, %d) Does not have a valid mobile combination material. To correct this issue, open the map in the editor and resave the map."), SectionBaseX, SectionBaseY);
			}
		}
	}
#endif // UE_BUILD_SHIPPING

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		// If we're loading on a platform that doesn't require cooked data, but *only* supports OpenGL ES, generate or preload data from the DDC
		if (!FPlatformProperties::RequiresCookedData() && GMaxRHIFeatureLevel <= ERHIFeatureLevel::ES3_1)
		{
			CheckGenerateCyLandPlatformData(false, nullptr);
		}
	}

	GrassData->ConditionalDiscardDataOnLoad();
}

#endif // WITH_EDITOR

ACyLandProxy::ACyLandProxy(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITORONLY_DATA
	, TargetDisplayOrder(ECyLandLayerDisplayMode::Default)
#endif // WITH_EDITORONLY_DATA
	, bHasCyLandGrass(true)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickGroup = TG_DuringPhysics;
	bAllowTickBeforeBeginPlay = true;

	bReplicates = false;
	NetUpdateFrequency = 10.0f;
	bHidden = false;
	bReplicateMovement = false;
	bCanBeDamaged = false;
	// by default we want to see the CyLand shadows even in the far shadow cascades
	bCastFarShadow = true;
	bAffectDistanceFieldLighting = true;

	USceneComponent* SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent0"));
	RootComponent = SceneComponent;
	RootComponent->RelativeScale3D = FVector(128.0f, 128.0f, 256.0f); // Old default scale, preserved for compatibility. See UCyLandEditorObject::NewCyLand_Scale
	RootComponent->Mobility = EComponentMobility::Static;
	CyLandSectionOffset = FIntPoint::ZeroValue;

	StaticLightingResolution = 1.0f;
	StreamingDistanceMultiplier = 1.0f;
	MaxLODLevel = -1;
	bUseDynamicMaterialInstance = false;
	OccluderGeometryLOD = 1; // 1 - usually is a good default
#if WITH_EDITORONLY_DATA
	bLockLocation = true;
	bIsMovingToLevel = false;
#endif // WITH_EDITORONLY_DATA
	TessellationComponentScreenSize = 0.8f;
	ComponentScreenSizeToUseSubSections = 0.65f;
	UseTessellationComponentScreenSizeFalloff = true;
	TessellationComponentScreenSizeFalloff = 0.75f;
	LOD0DistributionSetting = 1.75f;
	LODDistributionSetting = 2.0f;
	bCastStaticShadow = true;
	bCastShadowAsTwoSided = false;
	bUsedForNavigation = true;
	CollisionThickness = 16;
	BodyInstance.SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	bGenerateOverlapEvents = false;
#if WITH_EDITORONLY_DATA
	MaxPaintedLayersPerComponent = 0;
	HasProceduralContent = false;
#endif

#if WITH_EDITOR
	if (VisibilityLayer == nullptr)
	{
		// Structure to hold one-time initialization
		if (false) {
			struct FConstructorStatics
			{
				ConstructorHelpers::FObjectFinderOptional<UCyLandLayerInfoObject> DataLayer;
				FConstructorStatics()
					: DataLayer(TEXT("LandscapeLayerInfoObject'/Engine/EditorLandscapeResources/DataLayer.DataLayer'"))
				{
				}
			};
			static FConstructorStatics ConstructorStatics;

			VisibilityLayer = ConstructorStatics.DataLayer.Get();
		}
		if (!VisibilityLayer)
			VisibilityLayer = CreateDefaultSubobject<UCyLandLayerInfoObject>(TEXT("DataLayer"));// UCyLandLayerInfoObject(FObjectInitializer::Get());
		VisibilityLayer->Hardness = 0.5;
		VisibilityLayer->LayerName = FName("DataLayer__");
		VisibilityLayer->bNoWeightBlend = true;
		check(VisibilityLayer);
	#if WITH_EDITORONLY_DATA
		// This layer should be no weight blending
		VisibilityLayer->bNoWeightBlend = true;
	#endif
		VisibilityLayer->LayerUsageDebugColor = FLinearColor(0, 0, 0, 0);
		VisibilityLayer->AddToRoot();
	}

	if (!HasAnyFlags(RF_ArchetypeObject | RF_ClassDefaultObject) && GetWorld() != nullptr)
	{
		FOnFeatureLevelChanged::FDelegate FeatureLevelChangedDelegate = FOnFeatureLevelChanged::FDelegate::CreateUObject(this, &ACyLandProxy::OnFeatureLevelChanged);
		FeatureLevelChangedDelegateHandle = GetWorld()->AddOnFeatureLevelChangedHandler(FeatureLevelChangedDelegate);
	}
#endif

	static uint32 FrameOffsetForTickIntervalInc = 0;
	FrameOffsetForTickInterval = FrameOffsetForTickIntervalInc++;
}

ACyLand::ACyLand(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	bLockLocation = false;
	PreviousExperimentalCyLandProcedural = false;
	ProceduralContentUpdateFlags = 0;
#endif // WITH_EDITORONLY_DATA
}

ACyLandStreamingProxy::ACyLandStreamingProxy(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	bLockLocation = true;
#endif // WITH_EDITORONLY_DATA
}

ACyLand* ACyLand::GetCyLandActor()
{
	return this;
}

ACyLand* ACyLandStreamingProxy::GetCyLandActor()
{
	return CyLandActor.Get();
}

//#if WITH_EDITOR
UCyLandInfo* ACyLandProxy::CreateCyLandInfo()
{
	UE_LOG(LogCyLand, Display, TEXT("Creating CyLandInfo !!!"));
	UCyLandInfo* CyLandInfo = nullptr;

	//check(GIsEditor);
	check(CyLandGuid.IsValid());
	UWorld* OwningWorld = GetWorld();
	check(OwningWorld);
	//check(!OwningWorld->IsGameWorld());
	
	auto& CyLandInfoMap = UCyLandInfoMap::GetCyLandInfoMap(OwningWorld);
	CyLandInfo = CyLandInfoMap.Map.FindRef(CyLandGuid);

	if (!CyLandInfo)
	{
		check(!HasAnyFlags(RF_BeginDestroyed));
		CyLandInfo = NewObject<UCyLandInfo>(GetTransientPackage(), NAME_None, RF_Transactional | RF_Transient);
		CyLandInfoMap.Modify(false);
		CyLandInfoMap.Map.Add(CyLandGuid, CyLandInfo);
	}
	check(CyLandInfo);
	CyLandInfo->RegisterActor(this);

	return CyLandInfo;
}

UCyLandInfo* ACyLandProxy::GetCyLandInfo() const
{
	UCyLandInfo* CyLandInfo = nullptr;

	//check(GIsEditor);

	//UE_LOG(LogCyLand, Warning, TEXT("why fatal ACyLand PostLoad"));
	//fatal
	check(CyLandGuid.IsValid());
	UWorld* OwningWorld = GetWorld();
	//if(CyLandGuid.IsValid())
	if (OwningWorld != nullptr)// && !OwningWorld->IsGameWorld()
	{
		auto& CyLandInfoMap = UCyLandInfoMap::GetCyLandInfoMap(OwningWorld);
		CyLandInfo = CyLandInfoMap.Map.FindRef(CyLandGuid);
	}
	return CyLandInfo;
}
//#endif

ACyLand* UCyLandComponent::GetCyLandActor() const
{
	ACyLandProxy* CyLand = GetCyLandProxy();
	if (CyLand)
	{
		return CyLand->GetCyLandActor();
	}
	return nullptr;
}

ULevel* UCyLandComponent::GetLevel() const
{
	AActor* MyOwner = GetOwner();
	return MyOwner ? MyOwner->GetLevel() : nullptr;
}

#if WITH_EDITOR
void UCyLandComponent::GetGeneratedTexturesAndMaterialInstances(TArray<UObject*>& OutTexturesAndMaterials) const
{
	if (HeightmapTexture)
	{
		OutTexturesAndMaterials.Add(HeightmapTexture);
	}

	if (CurrentEditingHeightmapTexture != nullptr)
	{
		OutTexturesAndMaterials.Add(CurrentEditingHeightmapTexture);
	}

	for (auto* Tex : WeightmapTextures)
	{
		OutTexturesAndMaterials.Add(Tex);
	}

	if (XYOffsetmapTexture)
	{
		OutTexturesAndMaterials.Add(XYOffsetmapTexture);
	}

	for (UMaterialInstance* MaterialInstance : MaterialInstances)
	{
		for (UCyLandMaterialInstanceConstant* CurrentMIC = Cast<UCyLandMaterialInstanceConstant>(MaterialInstance); CurrentMIC; CurrentMIC = Cast<UCyLandMaterialInstanceConstant>(CurrentMIC->Parent))
		{
			OutTexturesAndMaterials.Add(CurrentMIC);

			// Sometimes weight map is not registered in the WeightmapTextures, so
			// we need to get it from here.
			auto* WeightmapPtr = CurrentMIC->TextureParameterValues.FindByPredicate(
				[](const FTextureParameterValue& ParamValue)
			{
				static const FName WeightmapParamName("Weightmap0");
				return ParamValue.ParameterInfo.Name == WeightmapParamName;
			});

			if (WeightmapPtr != nullptr &&
				!OutTexturesAndMaterials.Contains(WeightmapPtr->ParameterValue))
			{
				OutTexturesAndMaterials.Add(WeightmapPtr->ParameterValue);
			}
		}
	}

	for (UMaterialInstanceConstant* MaterialInstance : MobileCombinationMaterialInstances)
	{
		for (UCyLandMaterialInstanceConstant* CurrentMIC = Cast<UCyLandMaterialInstanceConstant>(MaterialInstance); CurrentMIC; CurrentMIC = Cast<UCyLandMaterialInstanceConstant>(CurrentMIC->Parent))
		{
			OutTexturesAndMaterials.Add(CurrentMIC);
		}
	}	
}
#endif

ACyLandProxy* UCyLandComponent::GetCyLandProxy() const
{
	return CastChecked<ACyLandProxy>(GetOuter());
}

FIntPoint UCyLandComponent::GetSectionBase() const
{
	return FIntPoint(SectionBaseX, SectionBaseY);
}

void UCyLandComponent::SetSectionBase(FIntPoint InSectionBase)
{
	SectionBaseX = InSectionBase.X;
	SectionBaseY = InSectionBase.Y;
}

const FMeshMapBuildData* UCyLandComponent::GetMeshMapBuildData() const
{
	AActor* Owner = GetOwner();

	if (Owner)
	{
		ULevel* OwnerLevel = Owner->GetLevel();

		if (OwnerLevel && OwnerLevel->OwningWorld)
		{
			ULevel* ActiveLightingScenario = OwnerLevel->OwningWorld->GetActiveLightingScenario();
			UMapBuildDataRegistry* MapBuildData = NULL;

			if (ActiveLightingScenario && ActiveLightingScenario->MapBuildData)
			{
				MapBuildData = ActiveLightingScenario->MapBuildData;
			}
			else if (OwnerLevel->MapBuildData)
			{
				MapBuildData = OwnerLevel->MapBuildData;
			}

			if (MapBuildData)
			{
				return MapBuildData->GetMeshBuildData(MapBuildDataId);
			}
		}
	}
	
	return NULL;
}

bool UCyLandComponent::IsPrecomputedLightingValid() const
{
	return GetMeshMapBuildData() != NULL;
}

void UCyLandComponent::PropagateLightingScenarioChange()
{
	FComponentRecreateRenderStateContext Context(this);
}

#if WITH_EDITOR
UCyLandInfo* UCyLandComponent::GetCyLandInfo() const
{
	return GetCyLandProxy()->GetCyLandInfo();
}
#endif

void UCyLandComponent::BeginDestroy()
{
	Super::BeginDestroy();

#if WITH_EDITOR
	// Ask render thread to destroy EditToolRenderData
	EditToolRenderData = FCyLandEditToolRenderData();
	UpdateEditToolRenderData();

	if (GIsEditor && !HasAnyFlags(RF_ClassDefaultObject))
	{
		ACyLandProxy* Proxy = GetCyLandProxy();

		// Remove any weightmap allocations from the CyLand Actor's map
		for (int32 LayerIdx = 0; LayerIdx < WeightmapLayerAllocations.Num(); LayerIdx++)
		{
			int32 WeightmapIndex = WeightmapLayerAllocations[LayerIdx].WeightmapTextureIndex;
			if (WeightmapTextures.IsValidIndex(WeightmapIndex))
			{
				UTexture2D* WeightmapTexture = WeightmapTextures[WeightmapIndex];
				FCyLandWeightmapUsage* Usage = Proxy->WeightmapUsageMap.Find(WeightmapTexture);
				if (Usage != nullptr)
				{
					Usage->ChannelUsage[WeightmapLayerAllocations[LayerIdx].WeightmapTextureChannel] = nullptr;

					if (Usage->CyFreeChannelCount() == 4)
					{
						Proxy->WeightmapUsageMap.Remove(WeightmapTexture);
					}
				}
			}
		}
	}
#endif
}

FPrimitiveSceneProxy* UCyLandComponent::CreateSceneProxy()
{
	const auto FeatureLevel = GetWorld()->FeatureLevel;
	FPrimitiveSceneProxy* Proxy = nullptr;
	if (FeatureLevel >= ERHIFeatureLevel::SM4)
	{
		Proxy = new FCyLandComponentSceneProxy(this);
	}
	else // i.e. (FeatureLevel <= ERHIFeatureLevel::ES3_1)
	{
		if (PlatformData.HasValidRuntimeData())
		{
			Proxy = new FCyLandComponentSceneProxyMobile(this);
		}
	}

	return Proxy;
}

void UCyLandComponent::DestroyComponent(bool bPromoteChildren/*= false*/)
{
	ACyLandProxy* Proxy = GetCyLandProxy();
	if (Proxy)
	{
		Proxy->CyLandComponents.Remove(this);
	}

	Super::DestroyComponent(bPromoteChildren);
}

FBoxSphereBounds UCyLandComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	FBox MyBounds = CachedLocalBox.TransformBy(LocalToWorld);
	MyBounds = MyBounds.ExpandBy({0, 0, NegativeZBoundsExtension}, {0, 0, PositiveZBoundsExtension});

	ACyLandProxy* Proxy = GetCyLandProxy();
	if (Proxy)
	{
		MyBounds = MyBounds.ExpandBy({0, 0, Proxy->NegativeZBoundsExtension}, {0, 0, Proxy->PositiveZBoundsExtension});
	}

	return FBoxSphereBounds(MyBounds);
}

void UCyLandComponent::OnRegister()
{
	Super::OnRegister();

	if (GetCyLandProxy())
	{
		// Generate MID representing the MIC
		if (GetCyLandProxy()->bUseDynamicMaterialInstance)
		{
			MaterialInstancesDynamic.Reserve(MaterialInstances.Num());

			for (int32 i = 0; i < MaterialInstances.Num(); ++i)
			{
				MaterialInstancesDynamic.Add(UMaterialInstanceDynamic::Create(MaterialInstances[i], this));
			}
		}

#if WITH_EDITOR
		// AActor::GetWorld checks for Unreachable and BeginDestroyed
		UWorld* World = GetCyLandProxy()->GetWorld();
		if (World && !World->IsGameWorld())
		{
			UCyLandInfo* Info = GetCyLandInfo();
			if (Info)
			{
				Info->RegisterActorComponent(this);
			}
		}
#endif
	}
}

void UCyLandComponent::OnUnregister()
{
	Super::OnUnregister();

	if (GetCyLandProxy())
	{
		// Generate MID representing the MIC
		if (GetCyLandProxy()->bUseDynamicMaterialInstance)
		{
			MaterialInstancesDynamic.Empty();
		}

#if WITH_EDITOR
		// AActor::GetWorld checks for Unreachable and BeginDestroyed
		UWorld* World = GetCyLandProxy()->GetWorld();

		// Game worlds don't have landscape infos
		if (World && !World->IsGameWorld())
		{
			UE_LOG(LogCyLand, Warning, TEXT("UCyLandComponent OnUnregister"));
			UCyLandInfo* Info = GetCyLandInfo();
			if (Info)
			{
				Info->UnregisterActorComponent(this);
			}
		}
#endif
	}
}

UTexture2D* UCyLandComponent::GetHeightmap(bool InReturnCurrentEditingHeightmap) const
{
#if WITH_EDITORONLY_DATA
	if (InReturnCurrentEditingHeightmap && GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
	{
		if (CurrentEditingHeightmapTexture != nullptr)
		{
			return CurrentEditingHeightmapTexture;
		}
	}
#endif

	return HeightmapTexture;
}

#if WITH_EDITOR
void UCyLandComponent::SetCurrentEditingHeightmap(UTexture2D* InNewHeightmap)
{
#if WITH_EDITORONLY_DATA
	CurrentEditingHeightmapTexture = InNewHeightmap;
#endif
}
#endif

void UCyLandComponent::SetHeightmap(UTexture2D* NewHeightmap)
{
	check(NewHeightmap != nullptr);
	HeightmapTexture = NewHeightmap;
}

void ACyLandProxy::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();

#if WITH_EDITOR
	// Game worlds don't have landscape infos
	if (!GetWorld()->IsGameWorld())
	{
		// Duplicated CyLands don't have a valid guid until PostEditImport is called, we'll register then
		if (CyLandGuid.IsValid())
		{
			UCyLandInfo* CyLandInfo = CreateCyLandInfo();

			CyLandInfo->FixupProxiesTransform();
		}
	}
#endif
}

void ACyLandProxy::UnregisterAllComponents(const bool bForReregister)
{
#if WITH_EDITOR
	// Game worlds don't have landscape infos
	if (GetWorld() && !GetWorld()->IsGameWorld()
		// On shutdown the world will be unreachable
		&& !GetWorld()->IsPendingKillOrUnreachable() &&
		// When redoing the creation of a landscape we may get UnregisterAllComponents called when
		// we are in a "pre-initialized" state (empty guid, etc)
		CyLandGuid.IsValid())
	{
		UCyLandInfo* CyLandInfo = GetCyLandInfo();
		if (CyLandInfo)
		{
			CyLandInfo->UnregisterActor(this);
		}
	}
#endif

	Super::UnregisterAllComponents(bForReregister);
}

// FCyLandWeightmapUsage serializer
FArchive& operator<<(FArchive& Ar, FCyLandWeightmapUsage& U)
{
	return Ar << U.ChannelUsage[0] << U.ChannelUsage[1] << U.ChannelUsage[2] << U.ChannelUsage[3];
}

#if WITH_EDITORONLY_DATA
FArchive& operator<<(FArchive& Ar, FCyLandAddCollision& U)
{
	return Ar << U.Corners[0] << U.Corners[1] << U.Corners[2] << U.Corners[3];
}
#endif // WITH_EDITORONLY_DATA

FArchive& operator<<(FArchive& Ar, FCyLandLayerStruct*& L)
{
	if (L)
	{
		Ar << L->LayerInfoObj;
#if WITH_EDITORONLY_DATA
		return Ar << L->ThumbnailMIC;
#else
		return Ar;
#endif // WITH_EDITORONLY_DATA
	}
	return Ar;
}

void UCyLandInfo::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.IsTransacting())
	{
		Ar << XYtoComponentMap;
#if WITH_EDITORONLY_DATA
		Ar << XYtoAddCollisionMap;
#endif
		Ar << SelectedComponents;
		Ar << SelectedRegion;
		Ar << SelectedRegionComponents;
	}
}

void ACyLand::PostLoad()
{
	UE_LOG(LogCyLand, Warning, TEXT("ACyLand PostLoad"));
	if (!GetCyLandGuid().IsValid())
	{
		CyLandGuid = FGuid::NewGuid();
	}
	else
	{
#if WITH_EDITOR
		UWorld* CurrentWorld = GetWorld();
		for (ACyLand* CyLand : TObjectRange<ACyLand>(RF_ClassDefaultObject | RF_BeginDestroyed))
		{
			if (CyLand && CyLand != this && CyLand->CyLandGuid == CyLandGuid && CyLand->GetWorld() == CurrentWorld)
			{
				// Duplicated landscape level, need to generate new GUID
				Modify();
				CyLandGuid = FGuid::NewGuid();


				// Show MapCheck window

				FFormatNamedArguments Arguments;
				Arguments.Add(TEXT("ProxyName1"), FText::FromString(CyLand->GetName()));
				Arguments.Add(TEXT("LevelName1"), FText::FromString(CyLand->GetLevel()->GetOutermost()->GetName()));
				Arguments.Add(TEXT("ProxyName2"), FText::FromString(this->GetName()));
				Arguments.Add(TEXT("LevelName2"), FText::FromString(this->GetLevel()->GetOutermost()->GetName()));
				FMessageLog("LoadErrors").Warning()
					->AddToken(FUObjectToken::Create(this))
					->AddToken(FTextToken::Create(FText::Format(LOCTEXT("LoadError_DuplicateCyLandGuid", "CyLand {ProxyName1} of {LevelName1} has the same guid as {ProxyName2} of {LevelName2}. {LevelName2}.{ProxyName2} has had its guid automatically changed, please save {LevelName2}!"), Arguments)));

				// Show MapCheck window
				FMessageLog("LoadErrors").Open();
				break;
			}
		}

		if (GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
		{
			FEditorDelegates::PreSaveWorld.AddUObject(this, &ACyLand::OnPreSaveWorld);
			FEditorDelegates::PostSaveWorld.AddUObject(this, &ACyLand::OnPostSaveWorld);
		}
#endif
	}

	Super::PostLoad();
}

void ACyLand::BeginDestroy()
{
#if WITH_EDITOR
	if (GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
	{
		FEditorDelegates::PreSaveWorld.RemoveAll(this);
		FEditorDelegates::PostSaveWorld.RemoveAll(this);
	}
#endif

	Super::BeginDestroy();
}

#if WITH_EDITOR
void ACyLandProxy::OnFeatureLevelChanged(ERHIFeatureLevel::Type NewFeatureLevel)
{
	FlushGrassComponents();

	UpdateAllComponentMaterialInstances();

	if (NewFeatureLevel <= ERHIFeatureLevel::ES3_1)
	{
		for (UCyLandComponent * Component : CyLandComponents)
		{
			if (Component != nullptr)
			{
				Component->CheckGenerateCyLandPlatformData(false, nullptr);
			}
		}
	}
}
#endif

void ACyLandProxy::PreSave(const class ITargetPlatform* TargetPlatform)
{
	Super::PreSave(TargetPlatform);

#if WITH_EDITOR
	// Work out whether we have grass or not for the next game run
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		bHasCyLandGrass = CyLandComponents.ContainsByPredicate([](UCyLandComponent* Component) { return Component->MaterialHasGrass(); });
	}

	if (GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
	{
		HasProceduralContent = true;
	}
#endif
}

void ACyLandProxy::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FCyLandCustomVersion::GUID);

	if (Ar.IsLoading() && Ar.CustomVer(FCyLandCustomVersion::GUID) < FCyLandCustomVersion::MigrateOldPropertiesToNewRenderingProperties)
	{
		if (LODDistanceFactor_DEPRECATED > 0)
		{
			const float LOD0LinearDistributionSettingMigrationTable[11] = { 1.75f, 1.75f, 1.75f, 1.75f, 1.75f, 1.68f, 1.55f, 1.4f, 1.25f, 1.25f, 1.25f };
			const float LODDLinearDistributionSettingMigrationTable[11] = { 2.0f, 2.0f, 2.0f, 1.65f, 1.35f, 1.25f, 1.25f, 1.25f, 1.25f, 1.25f, 1.25f };
			const float LOD0SquareRootDistributionSettingMigrationTable[11] = { 1.75f, 1.6f, 1.25f, 1.25f, 1.25f, 1.25f, 1.25f, 1.25f, 1.25f, 1.25f, 1.25f };
			const float LODDSquareRootDistributionSettingMigrationTable[11] = { 2.0f, 1.8f, 1.55f, 1.3f, 1.25f, 1.25f, 1.25f, 1.25f, 1.25f, 1.25f, 1.25f };

			if (LODFalloff_DEPRECATED == ECyLandLODFalloff::Type::Linear)
			{
				LOD0DistributionSetting = LOD0LinearDistributionSettingMigrationTable[FMath::RoundToInt(LODDistanceFactor_DEPRECATED)];
				LODDistributionSetting = LODDLinearDistributionSettingMigrationTable[FMath::RoundToInt(LODDistanceFactor_DEPRECATED)];
			}
			else if (LODFalloff_DEPRECATED == ECyLandLODFalloff::Type::SquareRoot)
			{
				LOD0DistributionSetting = LOD0SquareRootDistributionSettingMigrationTable[FMath::RoundToInt(LODDistanceFactor_DEPRECATED)];
				LODDistributionSetting = LODDSquareRootDistributionSettingMigrationTable[FMath::RoundToInt(LODDistanceFactor_DEPRECATED)];
			}
		}
	}

#if WITH_EDITOR
	if (Ar.IsTransacting())
	{
		Ar << WeightmapUsageMap;
	}
#endif
}

void ACyLandProxy::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	ACyLandProxy* This = CastChecked<ACyLandProxy>(InThis);

	Super::AddReferencedObjects(InThis, Collector);

#if WITH_EDITORONLY_DATA
	Collector.AddReferencedObjects(This->MaterialInstanceConstantMap, This);
#endif

	for (auto It = This->WeightmapUsageMap.CreateIterator(); It; ++It)
	{
		Collector.AddReferencedObject(It.Key(), This);
		Collector.AddReferencedObject(It.Value().ChannelUsage[0], This);
		Collector.AddReferencedObject(It.Value().ChannelUsage[1], This);
		Collector.AddReferencedObject(It.Value().ChannelUsage[2], This);
		Collector.AddReferencedObject(It.Value().ChannelUsage[3], This);
	}
}

#if WITH_EDITOR
FName FCyLandInfoLayerSettings::GetLayerName() const
{
	checkSlow(LayerInfoObj == nullptr || LayerInfoObj->LayerName == LayerName);

	return LayerName;
}

FCyLandEditorLayerSettings& FCyLandInfoLayerSettings::GetEditorSettings() const
{
	check(LayerInfoObj);

	UCyLandInfo* CyLandInfo = Owner->GetCyLandInfo();
	return CyLandInfo->GetLayerEditorSettings(LayerInfoObj);
}

FCyLandEditorLayerSettings& UCyLandInfo::GetLayerEditorSettings(UCyLandLayerInfoObject* LayerInfo) const
{
	ACyLandProxy* Proxy = GetCyLandProxy();
	FCyLandEditorLayerSettings* EditorLayerSettings = Proxy->EditorLayerSettings.FindByKey(LayerInfo);
	if (EditorLayerSettings)
	{
		return *EditorLayerSettings;
	}
	else
	{
		int32 Index = Proxy->EditorLayerSettings.Add(FCyLandEditorLayerSettings(LayerInfo));
		return Proxy->EditorLayerSettings[Index];
	}
}

void UCyLandInfo::CreateLayerEditorSettingsFor(UCyLandLayerInfoObject* LayerInfo)
{
	ForAllCyLandProxies([LayerInfo](ACyLandProxy* Proxy)
		{
		FCyLandEditorLayerSettings* EditorLayerSettings = Proxy->EditorLayerSettings.FindByKey(LayerInfo);
		if (!EditorLayerSettings)
		{
			Proxy->Modify();
			Proxy->EditorLayerSettings.Add(FCyLandEditorLayerSettings(LayerInfo));
	}
	});
}

UCyLandLayerInfoObject* UCyLandInfo::GetLayerInfoByName(FName LayerName, ACyLandProxy* Owner /*= nullptr*/) const
{
	UCyLandLayerInfoObject* LayerInfo = nullptr;
	for (int32 j = 0; j < Layers.Num(); j++)
	{
		if (Layers[j].LayerInfoObj && Layers[j].LayerInfoObj->LayerName == LayerName
			&& (Owner == nullptr || Layers[j].Owner == Owner))
		{
			LayerInfo = Layers[j].LayerInfoObj;
		}
	}
	return LayerInfo;
}

int32 UCyLandInfo::GetLayerInfoIndex(UCyLandLayerInfoObject* LayerInfo, ACyLandProxy* Owner /*= nullptr*/) const
{
	for (int32 j = 0; j < Layers.Num(); j++)
	{
		if (Layers[j].LayerInfoObj && Layers[j].LayerInfoObj == LayerInfo
			&& (Owner == nullptr || Layers[j].Owner == Owner))
		{
			return j;
		}
	}

	return INDEX_NONE;
}

int32 UCyLandInfo::GetLayerInfoIndex(FName LayerName, ACyLandProxy* Owner /*= nullptr*/) const
{
	for (int32 j = 0; j < Layers.Num(); j++)
	{
		if (Layers[j].GetLayerName() == LayerName
			&& (Owner == nullptr || Layers[j].Owner == Owner))
		{
			return j;
		}
	}

	return INDEX_NONE;
}


bool UCyLandInfo::UpdateLayerInfoMap(ACyLandProxy* Proxy /*= nullptr*/, bool bInvalidate /*= false*/)
{
	bool bHasCollision = false;
	if (GIsEditor)
	{
		if (Proxy)
		{
			if (bInvalidate)
			{
				// this is a horribly dangerous combination of parameters...

				for (int32 i = 0; i < Layers.Num(); i++)
				{
					if (Layers[i].Owner == Proxy)
					{
						Layers.RemoveAt(i--);
					}
				}
			}
			else // Proxy && !bInvalidate
			{
				TArray<FName> LayerNames = Proxy->GetLayersFromMaterial();

				// Validate any existing layer infos owned by this proxy
				for (int32 i = 0; i < Layers.Num(); i++)
				{
					if (Layers[i].Owner == Proxy)
					{
						Layers[i].bValid = LayerNames.Contains(Layers[i].GetLayerName());
					}
				}

				// Add placeholders for any unused material layers
				for (int32 i = 0; i < LayerNames.Num(); i++)
				{
					int32 LayerInfoIndex = GetLayerInfoIndex(LayerNames[i]);
					if (LayerInfoIndex == INDEX_NONE)
					{
						FCyLandInfoLayerSettings LayerSettings(LayerNames[i], Proxy);
						LayerSettings.bValid = true;
						Layers.Add(LayerSettings);
					}
				}

				// Populate from layers used in components
				for (int32 ComponentIndex = 0; ComponentIndex < Proxy->CyLandComponents.Num(); ComponentIndex++)
				{
					UCyLandComponent* Component = Proxy->CyLandComponents[ComponentIndex];

					// Add layers from per-component override materials
					if (Component->OverrideMaterial != nullptr)
					{
						TArray<FName> ComponentLayerNames = Proxy->GetLayersFromMaterial(Component->OverrideMaterial);
						for (int32 i = 0; i < ComponentLayerNames.Num(); i++)
						{
							int32 LayerInfoIndex = GetLayerInfoIndex(ComponentLayerNames[i]);
							if (LayerInfoIndex == INDEX_NONE)
							{
								FCyLandInfoLayerSettings LayerSettings(ComponentLayerNames[i], Proxy);
								LayerSettings.bValid = true;
								Layers.Add(LayerSettings);
							}
						}
					}

					for (int32 AllocationIndex = 0; AllocationIndex < Component->WeightmapLayerAllocations.Num(); AllocationIndex++)
					{
						UCyLandLayerInfoObject* LayerInfo = Component->WeightmapLayerAllocations[AllocationIndex].LayerInfo;
						if (LayerInfo)
						{
							int32 LayerInfoIndex = GetLayerInfoIndex(LayerInfo);
							bool bValid = LayerNames.Contains(LayerInfo->LayerName);

							#if WITH_EDITORONLY_DATA
							if (bValid)
							{
								//LayerInfo->IsReferencedFromLoadedData = true;
							}
							#endif

							if (LayerInfoIndex != INDEX_NONE)
							{
								FCyLandInfoLayerSettings& LayerSettings = Layers[LayerInfoIndex];

								// Valid layer infos take precedence over invalid ones
								// CyLand Actors take precedence over Proxies
								if ((bValid && !LayerSettings.bValid)
									|| (bValid == LayerSettings.bValid && Proxy->IsA<ACyLand>()))
								{
									LayerSettings.Owner = Proxy;
									LayerSettings.bValid = bValid;
									LayerSettings.ThumbnailMIC = nullptr;
								}
							}
							else
							{
								// handle existing placeholder layers
								LayerInfoIndex = GetLayerInfoIndex(LayerInfo->LayerName);
								if (LayerInfoIndex != INDEX_NONE)
								{
									FCyLandInfoLayerSettings& LayerSettings = Layers[LayerInfoIndex];

									//if (LayerSettings.Owner == Proxy)
									{
										LayerSettings.Owner = Proxy;
										LayerSettings.LayerInfoObj = LayerInfo;
										LayerSettings.bValid = bValid;
										LayerSettings.ThumbnailMIC = nullptr;
									}
								}
								else
								{
									FCyLandInfoLayerSettings LayerSettings(LayerInfo, Proxy);
									LayerSettings.bValid = bValid;
									Layers.Add(LayerSettings);
								}
							}
						}
					}
				}

				// Add any layer infos cached in the actor
				Proxy->EditorLayerSettings.RemoveAll([](const FCyLandEditorLayerSettings& Settings) { return Settings.LayerInfoObj == nullptr; });
				for (int32 i = 0; i < Proxy->EditorLayerSettings.Num(); i++)
				{
					FCyLandEditorLayerSettings& EditorLayerSettings = Proxy->EditorLayerSettings[i];
					if (LayerNames.Contains(EditorLayerSettings.LayerInfoObj->LayerName))
					{
						// intentionally using the layer name here so we don't add layer infos from
						// the cache that have the same name as an actual assignment from a component above
						int32 LayerInfoIndex = GetLayerInfoIndex(EditorLayerSettings.LayerInfoObj->LayerName);
						if (LayerInfoIndex != INDEX_NONE)
						{
							FCyLandInfoLayerSettings& LayerSettings = Layers[LayerInfoIndex];
							if (LayerSettings.LayerInfoObj == nullptr)
							{
								LayerSettings.Owner = Proxy;
								LayerSettings.LayerInfoObj = EditorLayerSettings.LayerInfoObj;
								LayerSettings.bValid = true;
							}
						}
					}
					else
					{
						Proxy->Modify();
						Proxy->EditorLayerSettings.RemoveAt(i--);
					}
				}
			}
		}
		else // !Proxy
		{
			Layers.Empty();

			if (!bInvalidate)
			{
				ForAllCyLandProxies([this](ACyLandProxy* EachProxy)
				{
					if (!EachProxy->IsPendingKillPending())
				{
						checkSlow(EachProxy->GetCyLandInfo() == this);
						UpdateLayerInfoMap(EachProxy, false);
				}
				});
			}
		}

		//if (GCallbackEvent)
		//{
		//	GCallbackEvent->Send( CALLBACK_EditorPostModal );
		//}
	}
	return bHasCollision;
}
#endif

void ACyLandProxy::PostLoad()
{
	Super::PostLoad();

	// disable ticking if we have no grass to tick
	if (!GIsEditor && !bHasCyLandGrass)
	{
		SetActorTickEnabled(false);
		PrimaryActorTick.bCanEverTick = false;
	}

	// Temporary
	if (ComponentSizeQuads == 0 && CyLandComponents.Num() > 0)
	{
		UCyLandComponent* Comp = CyLandComponents[0];
		if (Comp)
		{
			ComponentSizeQuads = Comp->ComponentSizeQuads;
			SubsectionSizeQuads = Comp->SubsectionSizeQuads;
			NumSubsections = Comp->NumSubsections;
		}
	}

	if (IsTemplate() == false)
	{
		BodyInstance.FixupData(this);
	}
	//fatal
	if(true){//GetWorld()
#if WITH_EDITOR
		if (GIsEditor && !GetWorld()->IsGameWorld())
		{
			if ((GetLinker() && (GetLinker()->UE4Ver() < VER_UE4_LANDSCAPE_COMPONENT_LAZY_REFERENCES)) ||
				CyLandComponents.Num() != CollisionComponents.Num() ||
				CyLandComponents.ContainsByPredicate([](UCyLandComponent* Comp) { return ((Comp != nullptr) && !Comp->CollisionComponent.IsValid()); }))
			{
				// Need to clean up invalid collision components
				CreateCyLandInfo();
				RecreateCollisionComponents();
			}
		}

		EditorLayerSettings.RemoveAll([](const FCyLandEditorLayerSettings& Settings) { return Settings.LayerInfoObj == nullptr; });

		if (EditorCachedLayerInfos_DEPRECATED.Num() > 0)
		{
			for (int32 i = 0; i < EditorCachedLayerInfos_DEPRECATED.Num(); i++)
			{
				EditorLayerSettings.Add(FCyLandEditorLayerSettings(EditorCachedLayerInfos_DEPRECATED[i]));
			}
			EditorCachedLayerInfos_DEPRECATED.Empty();
		}

		if (GIsEditor && !GetWorld()->IsGameWorld())
		{
			UCyLandInfo* CyLandInfo = CreateCyLandInfo();
			CyLandInfo->RegisterActor(this, true);

			FixupWeightmaps();
		}

		// track feature level change to flush grass cache
		FOnFeatureLevelChanged::FDelegate FeatureLevelChangedDelegate = FOnFeatureLevelChanged::FDelegate::CreateUObject(this, &ACyLandProxy::OnFeatureLevelChanged);
		FeatureLevelChangedDelegateHandle = GetWorld()->AddOnFeatureLevelChangedHandler(FeatureLevelChangedDelegate);

		if (GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
		{
			ACyLand* CyLand = GetCyLandActor();

			if (CyLand != nullptr)
			{
				CyLand->RequestProceduralContentUpdate(EProceduralContentUpdateFlag::All_Setup);
			}
		}
#endif
	}
}

#if WITH_EDITOR
void ACyLandProxy::Destroyed()
{
	Super::Destroyed();

	UWorld* World = GetWorld();

	if (GIsEditor && !World->IsGameWorld())
	{
		UCyLandInfo::RecreateCyLandInfo(World, false);

		if (SplineComponent)
		{
			SplineComponent->ModifySplines();
		}

		TotalComponentsNeedingGrassMapRender -= NumComponentsNeedingGrassMapRender;
		NumComponentsNeedingGrassMapRender = 0;
		TotalTexturesToStreamForVisibleGrassMapRender -= NumTexturesToStreamForVisibleGrassMapRender;
		NumTexturesToStreamForVisibleGrassMapRender = 0;
	}

	// unregister feature level changed handler for grass
	if (FeatureLevelChangedDelegateHandle.IsValid())
	{
		World->RemoveOnFeatureLevelChangedHandler(FeatureLevelChangedDelegateHandle);
		FeatureLevelChangedDelegateHandle.Reset();
	}
}

void ACyLandProxy::GetSharedProperties(ACyLandProxy* CyLand)
{
	if (GIsEditor && CyLand)
	{
		Modify();

		CyLandGuid = CyLand->CyLandGuid;

		//@todo UE4 merge, landscape, this needs work
		RootComponent->SetRelativeScale3D(CyLand->GetRootComponent()->GetComponentToWorld().GetScale3D());

		//PrePivot = CyLand->PrePivot;
		StaticLightingResolution = CyLand->StaticLightingResolution;
		bCastStaticShadow = CyLand->bCastStaticShadow;
		bCastShadowAsTwoSided = CyLand->bCastShadowAsTwoSided;
		LightingChannels = CyLand->LightingChannels;
		bRenderCustomDepth = CyLand->bRenderCustomDepth;
		LDMaxDrawDistance = CyLand->LDMaxDrawDistance;		
		CustomDepthStencilValue = CyLand->CustomDepthStencilValue;
		ComponentSizeQuads = CyLand->ComponentSizeQuads;
		NumSubsections = CyLand->NumSubsections;
		SubsectionSizeQuads = CyLand->SubsectionSizeQuads;
		MaxLODLevel = CyLand->MaxLODLevel;
		LODDistanceFactor_DEPRECATED = CyLand->LODDistanceFactor_DEPRECATED;
		LODFalloff_DEPRECATED = CyLand->LODFalloff_DEPRECATED;
		TessellationComponentScreenSize = CyLand->TessellationComponentScreenSize;
		ComponentScreenSizeToUseSubSections = CyLand->ComponentScreenSizeToUseSubSections;
		UseTessellationComponentScreenSizeFalloff = CyLand->UseTessellationComponentScreenSizeFalloff;
		TessellationComponentScreenSizeFalloff = CyLand->TessellationComponentScreenSizeFalloff;
		LODDistributionSetting = CyLand->LODDistributionSetting;
		LOD0DistributionSetting = CyLand->LOD0DistributionSetting;
		OccluderGeometryLOD = CyLand->OccluderGeometryLOD;
		NegativeZBoundsExtension = CyLand->NegativeZBoundsExtension;
		PositiveZBoundsExtension = CyLand->PositiveZBoundsExtension;
		CollisionMipLevel = CyLand->CollisionMipLevel;
		bBakeMaterialPositionOffsetIntoCollision = CyLand->bBakeMaterialPositionOffsetIntoCollision;
		if (!CyLandMaterial)
		{
			CyLandMaterial = CyLand->CyLandMaterial;
			CyLandMaterialsOverride = CyLand->CyLandMaterialsOverride;
		}
		if (!CyLandHoleMaterial)
		{
			CyLandHoleMaterial = CyLand->CyLandHoleMaterial;
		}
		if (CyLandMaterial == CyLand->CyLandMaterial)
		{
			EditorLayerSettings = CyLand->EditorLayerSettings;
		}
		if (!DefaultPhysMaterial)
		{
			DefaultPhysMaterial = CyLand->DefaultPhysMaterial;
		}
		LightmassSettings = CyLand->LightmassSettings;
	}
}

void ACyLandProxy::ConditionalAssignCommonProperties(ACyLand* CyLand)
{
	if (CyLand == nullptr)
	{
		return;
	}
	
	bool bUpdated = false;
	
	if (MaxLODLevel != CyLand->MaxLODLevel)
	{
		MaxLODLevel = CyLand->MaxLODLevel;
		bUpdated = true;
	}
	
	if (TessellationComponentScreenSize != CyLand->TessellationComponentScreenSize)
	{
		TessellationComponentScreenSize = CyLand->TessellationComponentScreenSize;
		bUpdated = true;
	}

	if (ComponentScreenSizeToUseSubSections != CyLand->ComponentScreenSizeToUseSubSections)
	{
		ComponentScreenSizeToUseSubSections = CyLand->ComponentScreenSizeToUseSubSections;
		bUpdated = true;
	}

	if (UseTessellationComponentScreenSizeFalloff != CyLand->UseTessellationComponentScreenSizeFalloff)
	{
		UseTessellationComponentScreenSizeFalloff = CyLand->UseTessellationComponentScreenSizeFalloff;
		bUpdated = true;
	}

	if (TessellationComponentScreenSizeFalloff != CyLand->TessellationComponentScreenSizeFalloff)
	{
		TessellationComponentScreenSizeFalloff = CyLand->TessellationComponentScreenSizeFalloff;
		bUpdated = true;
	}
	
	if (LODDistributionSetting != CyLand->LODDistributionSetting)
	{
		LODDistributionSetting = CyLand->LODDistributionSetting;
		bUpdated = true;
	}

	if (LOD0DistributionSetting != CyLand->LOD0DistributionSetting)
	{
		LOD0DistributionSetting = CyLand->LOD0DistributionSetting;
		bUpdated = true;
	}

	if (OccluderGeometryLOD != CyLand->OccluderGeometryLOD)
	{
		OccluderGeometryLOD = CyLand->OccluderGeometryLOD;
		bUpdated = true;
	}

	if (TargetDisplayOrder != CyLand->TargetDisplayOrder)
	{
		TargetDisplayOrder = CyLand->TargetDisplayOrder;
		bUpdated = true;
	}

	if (TargetDisplayOrderList != CyLand->TargetDisplayOrderList)
	{
		TargetDisplayOrderList = CyLand->TargetDisplayOrderList;
		bUpdated = true;
	}

	if (bUpdated)
	{
		MarkPackageDirty();
	}
}

FTransform ACyLandProxy::CyLandActorToWorld() const
{
	FTransform TM = ActorToWorld();
	// Add this proxy landscape section offset to obtain landscape actor transform
	TM.AddToTranslation(TM.TransformVector(-FVector(CyLandSectionOffset)));
	return TM;
}

void ACyLandProxy::SetAbsoluteSectionBase(FIntPoint InSectionBase)
{
	FIntPoint Difference = InSectionBase - CyLandSectionOffset;
	CyLandSectionOffset = InSectionBase;

	for (int32 CompIdx = 0; CompIdx < CyLandComponents.Num(); CompIdx++)
	{
		UCyLandComponent* Comp = CyLandComponents[CompIdx];
		if (Comp)
		{
			FIntPoint AbsoluteSectionBase = Comp->GetSectionBase() + Difference;
			Comp->SetSectionBase(AbsoluteSectionBase);
			Comp->RecreateRenderState_Concurrent();
		}
	}

	for (int32 CompIdx = 0; CompIdx < CollisionComponents.Num(); CompIdx++)
	{
		UCyLandHeightfieldCollisionComponent* Comp = CollisionComponents[CompIdx];
		if (Comp)
		{
			FIntPoint AbsoluteSectionBase = Comp->GetSectionBase() + Difference;
			Comp->SetSectionBase(AbsoluteSectionBase);
		}
	}
}

FIntPoint ACyLandProxy::GetSectionBaseOffset() const
{
	return CyLandSectionOffset;
}

void ACyLandProxy::RecreateComponentsState()
{
	for (int32 ComponentIndex = 0; ComponentIndex < CyLandComponents.Num(); ComponentIndex++)
	{
		UCyLandComponent* Comp = CyLandComponents[ComponentIndex];
		if (Comp)
		{
			Comp->UpdateComponentToWorld();
			Comp->UpdateCachedBounds();
			Comp->UpdateBounds();
			Comp->RecreateRenderState_Concurrent(); // @todo UE4 jackp just render state needs update?
		}
	}

	for (int32 ComponentIndex = 0; ComponentIndex < CollisionComponents.Num(); ComponentIndex++)
	{
		UCyLandHeightfieldCollisionComponent* Comp = CollisionComponents[ComponentIndex];
		if (Comp)
		{
			Comp->UpdateComponentToWorld();
			Comp->RecreatePhysicsState();
		}
	}
}

UMaterialInterface* ACyLandProxy::GetCyLandMaterial(int8 InLODIndex) const
{
	if (InLODIndex != INDEX_NONE)
	{
		UWorld* World = GetWorld();

		if (World != nullptr)
		{
			for (const FCyLandProxyMaterialOverride& OverrideMaterial : CyLandMaterialsOverride)
			{
				if (OverrideMaterial.LODIndex.GetValueForFeatureLevel(World->FeatureLevel) == InLODIndex)
				{
					if (OverrideMaterial.Material != nullptr)
					{
						return OverrideMaterial.Material;
					}

					break;
				}
			}
		}
	}
	
	return CyLandMaterial != nullptr ? CyLandMaterial : UMaterial::GetDefaultMaterial(MD_Surface);
}

UMaterialInterface* ACyLandProxy::GetCyLandHoleMaterial() const
{
	if (CyLandHoleMaterial)
	{
		return CyLandHoleMaterial;
	}
	return nullptr;
}

UMaterialInterface* ACyLandStreamingProxy::GetCyLandMaterial(int8 InLODIndex) const
{
	if (InLODIndex != INDEX_NONE)
	{
		UWorld* World = GetWorld();

		if (World != nullptr)
		{
			for (const FCyLandProxyMaterialOverride& OverrideMaterial : CyLandMaterialsOverride)
			{
				if (OverrideMaterial.LODIndex.GetValueForFeatureLevel(World->FeatureLevel) == InLODIndex)
				{
					if (OverrideMaterial.Material != nullptr)
					{
						return OverrideMaterial.Material;
					}

					break;
				}
			}
		}
	}
	
	if (CyLandMaterial != nullptr)
	{
		return CyLandMaterial;
	}

	if (CyLandActor != nullptr)
	{
		return CyLandActor->GetCyLandMaterial(InLODIndex);
	}

	return UMaterial::GetDefaultMaterial(MD_Surface);
}

UMaterialInterface* ACyLandStreamingProxy::GetCyLandHoleMaterial() const
{
	if (CyLandHoleMaterial)
	{
		return CyLandHoleMaterial;
	}
	else if (ACyLand* CyLand = CyLandActor.Get())
	{
		return CyLand->GetCyLandHoleMaterial();
	}
	return nullptr;
}

void ACyLand::PreSave(const class ITargetPlatform* TargetPlatform)
{
	Super::PreSave(TargetPlatform);
	//UCyLandInfo* Info = GetCyLandInfo();
	//if (GIsEditor && Info && !IsRunningCommandlet())
	//{
	//	for (TSet<ACyLandProxy*>::TIterator It(Info->Proxies); It; ++It)
	//	{
	//		ACyLandProxy* Proxy = *It;
	//		if (!ensure(Proxy->CyLandActor == this))
	//		{
	//			Proxy->CyLandActor = this;
	//			Proxy->GetSharedProperties(this);
	//		}
	//	}
	//}
}

ACyLandProxy* UCyLandInfo::GetCyLandProxyForLevel(ULevel* Level) const
{
	ACyLandProxy* CyLandProxy = nullptr;
	ForAllCyLandProxies([&CyLandProxy, Level](ACyLandProxy* Proxy)
	{
		if (Proxy->GetLevel() == Level)
	{
			CyLandProxy = Proxy;
	}
	});
			return CyLandProxy;
		}

ACyLandProxy* UCyLandInfo::GetCurrentLevelCyLandProxy(bool bRegistered) const
{
	ACyLandProxy* CyLandProxy = nullptr;
	ForAllCyLandProxies([&CyLandProxy, bRegistered](ACyLandProxy* Proxy)
	{
		if (!bRegistered || Proxy->GetRootComponent()->IsRegistered())
		{
			UWorld* ProxyWorld = Proxy->GetWorld();
			if (ProxyWorld &&
				ProxyWorld->GetCurrentLevel() == Proxy->GetOuter())
			{
				CyLandProxy = Proxy;
			}
		}
	});
	return CyLandProxy;
}

ACyLandProxy* UCyLandInfo::GetCyLandProxy() const
{
	// Mostly this Proxy used to calculate transformations
	// in Editor all proxies of same landscape actor have root components in same locations
	// so it doesn't really matter which proxy we return here

	// prefer CyLandActor in case it is loaded
	if (CyLandActor && CyLandActor.IsValid())
	{
		ACyLand* CyLand = CyLandActor.Get();
		if (CyLand != nullptr &&
			CyLand->GetRootComponent()->IsRegistered())
		{
			return CyLand;
		}
	}

	// prefer current level proxy 
	ACyLandProxy* Proxy = GetCurrentLevelCyLandProxy(true);
	if (Proxy != nullptr)
	{
		return Proxy;
	}

	// any proxy in the world
	for (auto It = Proxies.CreateConstIterator(); It; ++It)
	{
		Proxy = (*It);
		if (Proxy != nullptr &&
			Proxy->GetRootComponent()->IsRegistered())
		{
			return Proxy;
		}
	}

	return nullptr;
}

void UCyLandInfo::ForAllCyLandProxies(TFunctionRef<void(ACyLandProxy*)> Fn) const
{
	ACyLand* CyLand = CyLandActor.Get();
	if (CyLand)
	{
		Fn(CyLand);
	}

	for (ACyLandProxy* CyLandProxy : Proxies)
	{
		Fn(CyLandProxy);
	}
}

void UCyLandInfo::RegisterActor(ACyLandProxy* Proxy, bool bMapCheck)
{
	// do not pass here invalid actors
	checkSlow(Proxy);
	check(Proxy->GetCyLandGuid().IsValid());
	UWorld* OwningWorld = Proxy->GetWorld();

	// in case this Info object is not initialized yet
	// initialized it with properties from passed actor
	if (CyLandGuid.IsValid() == false ||
		(GetCyLandProxy() == nullptr && ensure(CyLandGuid == Proxy->GetCyLandGuid())))
	{
		CyLandGuid = Proxy->GetCyLandGuid();
		ComponentSizeQuads = Proxy->ComponentSizeQuads;
		ComponentNumSubsections = Proxy->NumSubsections;
		SubsectionSizeQuads = Proxy->SubsectionSizeQuads;
		DrawScale = Proxy->GetRootComponent() != nullptr ? Proxy->GetRootComponent()->RelativeScale3D : FVector(100.0f);
	}

	// check that passed actor matches all shared parameters
	check(CyLandGuid == Proxy->GetCyLandGuid());
	check(ComponentSizeQuads == Proxy->ComponentSizeQuads);
	check(ComponentNumSubsections == Proxy->NumSubsections);
	check(SubsectionSizeQuads == Proxy->SubsectionSizeQuads);

	if (Proxy->GetRootComponent() != nullptr && !DrawScale.Equals(Proxy->GetRootComponent()->RelativeScale3D))
	{
		UE_LOG(LogCyLand, Warning, TEXT("CyLand proxy (%s) scale (%s) does not match to main actor scale (%s)."),
			*Proxy->GetName(), *Proxy->GetRootComponent()->RelativeScale3D.ToCompactString(), *DrawScale.ToCompactString());
	}

	// register
	if (ACyLand* CyLand = Cast<ACyLand>(Proxy))
	{
		checkf(!CyLandActor || CyLandActor == CyLand, TEXT("Multiple landscapes with the same GUID detected: %s vs %s"), *CyLandActor->GetPathName(), *CyLand->GetPathName());
		CyLandActor = CyLand;
		// In world composition user is not allowed to move landscape in editor, only through WorldBrowser 
		CyLandActor->bLockLocation = OwningWorld != nullptr ? OwningWorld->WorldComposition != nullptr : false;

		// update proxies reference actor
		for (ACyLandStreamingProxy* StreamingProxy : Proxies)
		{
			StreamingProxy->CyLandActor = CyLandActor;
			StreamingProxy->ConditionalAssignCommonProperties(CyLand);
		}
	}
	else
	{
		ACyLandStreamingProxy* StreamingProxy = CastChecked<ACyLandStreamingProxy>(Proxy);

		Proxies.Add(StreamingProxy);
		StreamingProxy->CyLandActor = CyLandActor;
		StreamingProxy->ConditionalAssignCommonProperties(CyLandActor.Get());
	}

	UpdateLayerInfoMap(Proxy);
	UpdateAllAddCollisions();

	//
	// add proxy components to the XY map
	//
	for (int32 CompIdx = 0; CompIdx < Proxy->CyLandComponents.Num(); ++CompIdx)
	{
		RegisterActorComponent(Proxy->CyLandComponents[CompIdx], bMapCheck);
	}
}

void UCyLandInfo::UnregisterActor(ACyLandProxy* Proxy)
{
	if (ACyLand* CyLand = Cast<ACyLand>(Proxy))
	{
		// Note: UnregisterActor sometimes gets triggered twice, e.g. it has been observed to happen during redo
		// Note: In some cases CyLandActor could be updated to a new landscape actor before the old landscape is unregistered/destroyed
		// e.g. this has been observed when merging levels in the editor

		if (CyLandActor.Get() == CyLand)
		{
		CyLandActor = nullptr;
		}

		// update proxies reference to landscape actor
		for (ACyLandStreamingProxy* StreamingProxy : Proxies)
		{
			StreamingProxy->CyLandActor = CyLandActor;
		}
	}
	else
	{
		ACyLandStreamingProxy* StreamingProxy = CastChecked<ACyLandStreamingProxy>(Proxy);
		Proxies.Remove(StreamingProxy);
		StreamingProxy->CyLandActor = nullptr;
	}

	// remove proxy components from the XY map
	for (int32 CompIdx = 0; CompIdx < Proxy->CyLandComponents.Num(); ++CompIdx)
	{
		UCyLandComponent* Component = Proxy->CyLandComponents[CompIdx];
		if (Component) // When a landscape actor is being GC'd it's possible the components were already GC'd and are null
		{
			UnregisterActorComponent(Component);
		}
	}
	XYtoComponentMap.Compact();

	UpdateLayerInfoMap();
	UpdateAllAddCollisions();
}

void UCyLandInfo::RegisterActorComponent(UCyLandComponent* Component, bool bMapCheck)
{
	// Do not register components which are not part of the world
	if (Component == nullptr ||
		Component->IsRegistered() == false)
	{
		return;
	}
	UE_LOG(LogCyLand, Warning, TEXT("RegisterActorComponent sec %s"), *Component->GetSectionBase().ToString());

	check(Component);

	FIntPoint ComponentKey = Component->GetSectionBase() / Component->ComponentSizeQuads;
	auto RegisteredComponent = XYtoComponentMap.FindRef(ComponentKey);
	//if (true) return;
	if (RegisteredComponent != Component)
	{
		if (RegisteredComponent == nullptr)
		{
			XYtoComponentMap.Add(ComponentKey, Component);
		}
		else if (bMapCheck)
		{
			ACyLandProxy* OurProxy = Component->GetCyLandProxy();
			ACyLandProxy* ExistingProxy = RegisteredComponent->GetCyLandProxy();
			FFormatNamedArguments Arguments;
			Arguments.Add(TEXT("ProxyName1"), FText::FromString(OurProxy->GetName()));
			Arguments.Add(TEXT("LevelName1"), FText::FromString(OurProxy->GetLevel()->GetOutermost()->GetName()));
			Arguments.Add(TEXT("ProxyName2"), FText::FromString(ExistingProxy->GetName()));
			Arguments.Add(TEXT("LevelName2"), FText::FromString(ExistingProxy->GetLevel()->GetOutermost()->GetName()));
			Arguments.Add(TEXT("XLocation"), Component->GetSectionBase().X);
			Arguments.Add(TEXT("YLocation"), Component->GetSectionBase().Y);
			FMessageLog("MapCheck").Warning()
				->AddToken(FUObjectToken::Create(OurProxy))
				->AddToken(FTextToken::Create(FText::Format(LOCTEXT("MapCheck_Message_LandscapeComponentPostLoad_Warning", "CyLand {ProxyName1} of {LevelName1} has overlapping render components with {ProxyName2} of {LevelName2} at location ({XLocation}, {YLocation})."), Arguments)))
				->AddToken(FActionToken::Create(LOCTEXT("MapCheck_RemoveDuplicateCyLandComponent", "Delete Duplicate"), LOCTEXT("MapCheck_RemoveDuplicateCyLandComponentDesc", "Deletes the duplicate landscape component."), FOnActionTokenExecuted::CreateUObject(OurProxy, &ACyLandProxy::RemoveOverlappingComponent, Component), true))
				->AddToken(FMapErrorToken::Create(FMapErrors::LandscapeComponentPostLoad_Warning));

			// Show MapCheck window
			FMessageLog("MapCheck").Open(EMessageSeverity::Warning);
		}
	}

	// Update Selected Components/Regions
	if (Component->EditToolRenderData.SelectedType)
	{
		if (Component->EditToolRenderData.SelectedType & FCyLandEditToolRenderData::ST_COMPONENT)
		{
			SelectedComponents.Add(Component);
		}
		else if (Component->EditToolRenderData.SelectedType & FCyLandEditToolRenderData::ST_REGION)
		{
			SelectedRegionComponents.Add(Component);
		}
	}
}

void UCyLandInfo::UnregisterActorComponent(UCyLandComponent* Component)
{
	if (ensure(Component))
	{
	FIntPoint ComponentKey = Component->GetSectionBase() / Component->ComponentSizeQuads;
	auto RegisteredComponent = XYtoComponentMap.FindRef(ComponentKey);

	if (RegisteredComponent == Component)
	{
		XYtoComponentMap.Remove(ComponentKey);
	}

	SelectedComponents.Remove(Component);
	SelectedRegionComponents.Remove(Component);
}
}

void UCyLandInfo::Reset()
{
	CyLandActor.Reset();

	Proxies.Empty();
	XYtoComponentMap.Empty();
	XYtoAddCollisionMap.Empty();

	//SelectedComponents.Empty();
	//SelectedRegionComponents.Empty();
	//SelectedRegion.Empty();
}

void UCyLandInfo::FixupProxiesTransform()
{
	ACyLand* CyLand = CyLandActor.Get();

	if (CyLand == nullptr ||
		CyLand->GetRootComponent()->IsRegistered() == false)
	{
		return;
	}

	// Make sure section offset of all proxies is multiple of ACyLandProxy::ComponentSizeQuads
	for (auto It = Proxies.CreateConstIterator(); It; ++It)
	{
		ACyLandProxy* Proxy = *It;
		FIntPoint CyLandSectionOffset = Proxy->CyLandSectionOffset - CyLand->CyLandSectionOffset;
		FIntPoint CyLandSectionOffsetRem(
			CyLandSectionOffset.X % Proxy->ComponentSizeQuads, 
			CyLandSectionOffset.Y % Proxy->ComponentSizeQuads);

		if (CyLandSectionOffsetRem.X != 0 || CyLandSectionOffsetRem.Y != 0)
		{
			FIntPoint NewCyLandSectionOffset = Proxy->CyLandSectionOffset - CyLandSectionOffsetRem;
			
			UE_LOG(LogCyLand, Warning, TEXT("CyLand section base is not multiple of component size, attempted automated fix: '%s', %d,%d vs %d,%d."),
					*Proxy->GetFullName(), Proxy->CyLandSectionOffset.X, Proxy->CyLandSectionOffset.Y, NewCyLandSectionOffset.X, NewCyLandSectionOffset.Y);

			Proxy->SetAbsoluteSectionBase(NewCyLandSectionOffset);
		}
	}

	FTransform CyLandTM = CyLand->CyLandActorToWorld();
	// Update transformations of all linked landscape proxies
	for (auto It = Proxies.CreateConstIterator(); It; ++It)
	{
		ACyLandProxy* Proxy = *It;
		FTransform ProxyRelativeTM(FVector(Proxy->CyLandSectionOffset));
		FTransform ProxyTransform = ProxyRelativeTM*CyLandTM;

		if (!Proxy->GetTransform().Equals(ProxyTransform))
		{
			Proxy->SetActorTransform(ProxyTransform);

			// Let other systems know that an actor was moved
			GEngine->BroadcastOnActorMoved(Proxy);
		}
	}
}

void UCyLandInfo::UpdateComponentLayerWhitelist()
{
	ForAllCyLandProxies([](ACyLandProxy* Proxy)
	{
		for (UCyLandComponent* Comp : Proxy->CyLandComponents)
		{
			Comp->UpdateLayerWhitelistFromPaintedLayers();
		}
	});
}

void UCyLandInfo::RecreateCyLandInfo(UWorld* InWorld, bool bMapCheck)
{
	check(InWorld);

	UCyLandInfoMap& CyLandInfoMap = UCyLandInfoMap::GetCyLandInfoMap(InWorld);
	CyLandInfoMap.Modify();

	// reset all CyLandInfo objects
	for (auto& CyLandInfoPair : CyLandInfoMap.Map)
	{
		UCyLandInfo* CyLandInfo = CyLandInfoPair.Value;

		if (CyLandInfo != nullptr)
		{
			CyLandInfo->Modify();
			CyLandInfo->Reset();
		}
	}

	TMap<FGuid, TArray<ACyLandProxy*>> ValidCyLandsMap;
	// Gather all valid landscapes in the world
	for (ACyLandProxy* Proxy : TActorRange<ACyLandProxy>(InWorld))
	{
		if (Proxy->GetLevel() &&
			Proxy->GetLevel()->bIsVisible &&
			!Proxy->HasAnyFlags(RF_BeginDestroyed) &&
			!Proxy->IsPendingKill() &&
			!Proxy->IsPendingKillPending())
		{
			ValidCyLandsMap.FindOrAdd(Proxy->GetCyLandGuid()).Add(Proxy);
		}
	}

	// Register landscapes in global landscape map
	for (auto& ValidCyLandsPair : ValidCyLandsMap)
	{
		auto& CyLandList = ValidCyLandsPair.Value;
		for (ACyLandProxy* Proxy : CyLandList)
		{
			Proxy->CreateCyLandInfo()->RegisterActor(Proxy, bMapCheck);
		}
	}

	// Remove empty entries from global CyLandInfo map
	for (auto It = CyLandInfoMap.Map.CreateIterator(); It; ++It)
	{
		UCyLandInfo* Info = It.Value();

		if (Info != nullptr && Info->GetCyLandProxy() == nullptr)
		{
			Info->MarkPendingKill();
			It.RemoveCurrent();
		}
		else if (Info == nullptr) // remove invalid entry
		{
			It.RemoveCurrent();
		}
	}

	// We need to inform CyLand editor tools about CyLandInfo updates
	FEditorSupportDelegates::WorldChange.Broadcast();
}


#endif

void UCyLandComponent::PostInitProperties()
{
	Super::PostInitProperties();

	// Create a new guid in case this is a newly created component
	// If not, this guid will be overwritten when serialized
	FPlatformMisc::CreateGuid(StateId);

	// Initialize MapBuildDataId to something unique, in case this is a new UCyLandComponent
	MapBuildDataId = FGuid::NewGuid();
}

void UCyLandComponent::PostDuplicate(bool bDuplicateForPIE)
{
	if (!bDuplicateForPIE)
	{
		// Reset the StateId on duplication since it needs to be unique for each capture.
		// PostDuplicate covers direct calls to StaticDuplicateObject, but not actor duplication (see PostEditImport)
		FPlatformMisc::CreateGuid(StateId);
	}
}

// Generate a new guid to force a recache of all landscape derived data
#define LANDSCAPE_FULL_DERIVEDDATA_VER			TEXT("016D326F3A954BBA9CCDFA00CEFA31E9")

FString FCyLandComponentDerivedData::GetDDCKeyString(const FGuid& StateId)
{
	return FDerivedDataCacheInterface::BuildCacheKey(TEXT("LS_FULL"), LANDSCAPE_FULL_DERIVEDDATA_VER, *StateId.ToString());
}

void FCyLandComponentDerivedData::InitializeFromUncompressedData(const TArray<uint8>& UncompressedData)
{
	int32 UncompressedSize = UncompressedData.Num() * UncompressedData.GetTypeSize();

	TArray<uint8> TempCompressedMemory;
	// Compressed can be slightly larger than uncompressed
	TempCompressedMemory.Empty(UncompressedSize * 4 / 3);
	TempCompressedMemory.AddUninitialized(UncompressedSize * 4 / 3);
	int32 CompressedSize = TempCompressedMemory.Num() * TempCompressedMemory.GetTypeSize();

	verify(FCompression::CompressMemory(
		NAME_Zlib,
		TempCompressedMemory.GetData(),
		CompressedSize,
		UncompressedData.GetData(),
		UncompressedSize,
		COMPRESS_BiasMemory));

	// Note: change LANDSCAPE_FULL_DERIVEDDATA_VER when modifying the serialization layout
	FMemoryWriter FinalArchive(CompressedCyLandData, true);
	FinalArchive << UncompressedSize;
	FinalArchive << CompressedSize;
	FinalArchive.Serialize(TempCompressedMemory.GetData(), CompressedSize);
}

FArchive& operator<<(FArchive& Ar, FCyLandComponentDerivedData& Data)
{
	return Ar << Data.CompressedCyLandData;
}

bool FCyLandComponentDerivedData::LoadFromDDC(const FGuid& StateId)
{
	return GetDerivedDataCacheRef().GetSynchronous(*GetDDCKeyString(StateId), CompressedCyLandData);
}

void FCyLandComponentDerivedData::SaveToDDC(const FGuid& StateId)
{
	check(CompressedCyLandData.Num() > 0);
	GetDerivedDataCacheRef().Put(*GetDDCKeyString(StateId), CompressedCyLandData);
}

void CyLandMaterialsParameterValuesGetter(FStaticParameterSet& OutStaticParameterSet, UMaterialInstance* Material)
{
	if (Material->Parent)
	{
		UMaterial* ParentMaterial = Material->Parent->GetMaterial();

		TArray<FMaterialParameterInfo> OutParameterInfo;
		TArray<FGuid> Guids;
		Material->GetAllParameterInfo<UMaterialExpressionCyLandLayerWeight>(OutParameterInfo, Guids);
		Material->GetAllParameterInfo<UMaterialExpressionCyLandLayerSwitch>(OutParameterInfo, Guids);
		Material->GetAllParameterInfo<UMaterialExpressionCyLandLayerSample>(OutParameterInfo, Guids);
		Material->GetAllParameterInfo<UMaterialExpressionCyLandLayerBlend>(OutParameterInfo, Guids);
		Material->GetAllParameterInfo<UMaterialExpressionCyLandVisibilityMask>(OutParameterInfo, Guids);

		OutStaticParameterSet.TerrainLayerWeightParameters.AddZeroed(OutParameterInfo.Num());
		for (int32 ParameterIdx = 0; ParameterIdx < OutParameterInfo.Num(); ParameterIdx++)
		{
			FStaticTerrainLayerWeightParameter& ParentParameter = OutStaticParameterSet.TerrainLayerWeightParameters[ParameterIdx];
			const FMaterialParameterInfo& ParameterInfo = OutParameterInfo[ParameterIdx];
			FGuid ExpressionId = Guids[ParameterIdx];
			int32 WeightmapIndex = INDEX_NONE;

			ParentParameter.bOverride = false;
			ParentParameter.ParameterInfo = ParameterInfo;
			// Get the settings from the parent in the MIC chain
			Material->Parent->GetTerrainLayerWeightParameterValue(ParameterInfo, ParentParameter.WeightmapIndex, ExpressionId);
			ParentParameter.ExpressionGUID = ExpressionId;

			// If the SourceInstance is overriding this parameter, use its settings
			for (int32 WeightParamIdx = 0; WeightParamIdx < Material->GetStaticParameters().TerrainLayerWeightParameters.Num(); WeightParamIdx++)
			{
				const FStaticTerrainLayerWeightParameter &TerrainLayerWeightParam = Material->GetStaticParameters().TerrainLayerWeightParameters[WeightParamIdx];

				if (ParameterInfo == TerrainLayerWeightParam.ParameterInfo)
				{
					ParentParameter.bOverride = TerrainLayerWeightParam.bOverride;
					if (TerrainLayerWeightParam.bOverride)
					{
						ParentParameter.WeightmapIndex = TerrainLayerWeightParam.WeightmapIndex;
						ParentParameter.bWeightBasedBlend = TerrainLayerWeightParam.bWeightBasedBlend;
					}
				}
			}
		}
	}
}

bool CyLandMaterialsParameterSetUpdater(FStaticParameterSet& StaticParameterSet, UMaterial* ParentMaterial)
{
	return UpdateParameterSet<FStaticTerrainLayerWeightParameter, UMaterialExpressionCyLandLayerWeight>(StaticParameterSet.TerrainLayerWeightParameters, ParentMaterial);
}

bool ACyLandProxy::ShouldTickIfViewportsOnly() const
{
	return true;
}

void ACyLand::TickActor(float DeltaTime, ELevelTick TickType, FActorTickFunction& ThisTickFunction)
{
	Super::TickActor(DeltaTime, TickType, ThisTickFunction);

#if WITH_EDITOR
	UWorld* World = GetWorld();
	if (GIsEditor && World && !World->IsPlayInEditor())
	{
		if (GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
		{
			if (PreviousExperimentalCyLandProcedural != GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
			{
				PreviousExperimentalCyLandProcedural = GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape;

				RequestProceduralContentUpdate(EProceduralContentUpdateFlag::All_Setup);
			}

			RegenerateProceduralContent();
		}
		else
		{
			if (PreviousExperimentalCyLandProcedural != GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
			{
				PreviousExperimentalCyLandProcedural = GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape;

				for (auto& ItPair : RenderDataPerHeightmap)
				{
					FCyRenderDataPerHeightmap& HeightmapRenderData = ItPair.Value;

					if (HeightmapRenderData.HeightmapsCPUReadBack != nullptr)
					{
						BeginReleaseResource(HeightmapRenderData.HeightmapsCPUReadBack);
					}
				}

				FlushRenderingCommands();

				for (auto& ItPair : RenderDataPerHeightmap)
				{
					FCyRenderDataPerHeightmap& HeightmapRenderData = ItPair.Value;

					delete HeightmapRenderData.HeightmapsCPUReadBack;
					HeightmapRenderData.HeightmapsCPUReadBack = nullptr;
				}
			}
		}
	}
#endif
}

void ACyLandProxy::TickActor(float DeltaTime, ELevelTick TickType, FActorTickFunction& ThisTickFunction)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(CyLand);
#if WITH_EDITOR
	// editor-only
	UWorld* World = GetWorld();
	if (GIsEditor && World && !World->IsPlayInEditor())
	{
		UpdateBakedTextures();
	}
#endif

	// Tick grass even while paused or in the editor
	if (GIsEditor || bHasCyLandGrass)
	{
		TickGrass();
	}

	Super::TickActor(DeltaTime, TickType, ThisTickFunction);
}

ACyLandProxy::~ACyLandProxy()
{
	for (int32 Index = 0; Index < AsyncFoliageTasks.Num(); Index++)
	{
		FAsyncTask<FCyAsyncGrassTask>* Task = AsyncFoliageTasks[Index];
		Task->EnsureCompletion(true);
		FCyAsyncGrassTask& Inner = Task->GetTask();
		delete Task;
	}
	AsyncFoliageTasks.Empty();

#if WITH_EDITOR
	TotalComponentsNeedingGrassMapRender -= NumComponentsNeedingGrassMapRender;
	NumComponentsNeedingGrassMapRender = 0;
	TotalTexturesToStreamForVisibleGrassMapRender -= NumTexturesToStreamForVisibleGrassMapRender;
	NumTexturesToStreamForVisibleGrassMapRender = 0;
#endif
}

//
// ACyLandMeshProxyActor
//
ACyLandMeshProxyActor::ACyLandMeshProxyActor(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
	bCanBeDamaged = false;

	CyLandMeshProxyComponent = CreateDefaultSubobject<UCyLandMeshProxyComponent>(TEXT("CyLandMeshProxyComponent0"));
	CyLandMeshProxyComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	CyLandMeshProxyComponent->Mobility = EComponentMobility::Static;
	CyLandMeshProxyComponent->SetGenerateOverlapEvents(false);

	RootComponent = CyLandMeshProxyComponent;
}

//
// UCyLandMeshProxyComponent
//
UCyLandMeshProxyComponent::UCyLandMeshProxyComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UCyLandMeshProxyComponent::InitializeForCyLand(ACyLandProxy* CyLand, int8 InProxyLOD)
{
	CyLandGuid = CyLand->GetCyLandGuid();

	for (UCyLandComponent* Component : CyLand->CyLandComponents)
	{
		if (Component)
		{
			ProxyComponentBases.Add(Component->GetSectionBase() / Component->ComponentSizeQuads);
		}
	}

	if (InProxyLOD != INDEX_NONE)
	{
		ProxyLOD = FMath::Clamp<int32>(InProxyLOD, 0, FMath::CeilLogTwo(CyLand->SubsectionSizeQuads + 1) - 1);
	}
}

#if WITH_EDITOR
void UCyLandComponent::SerializeStateHashes(FArchive& Ar)
{
	FGuid HeightmapGuid = HeightmapTexture->Source.GetId();
	Ar << HeightmapGuid;
	for (auto WeightmapTexture : WeightmapTextures)
	{
		FGuid WeightmapGuid = WeightmapTexture->Source.GetId();
		Ar << WeightmapGuid;
	}

	int32 OccluderGeometryLOD = GetCyLandProxy()->OccluderGeometryLOD;
	Ar << OccluderGeometryLOD;

	// Take into account the Heightmap offset per component
	Ar << HeightmapScaleBias.Z;
	Ar << HeightmapScaleBias.W;

	if (OverrideMaterial != nullptr)
	{
		UMaterialInterface::TMicRecursionGuard RecursionGuard;
		FGuid LocalStateId = OverrideMaterial->GetMaterial_Concurrent(RecursionGuard)->StateId;
		Ar << LocalStateId;
	}

	for (FCyLandComponentMaterialOverride& MaterialOverride : OverrideMaterials)
	{
		if (MaterialOverride.Material != nullptr)
		{
			UMaterialInterface::TMicRecursionGuard RecursionGuard;
			FGuid LocalStateId = MaterialOverride.Material->GetMaterial_Concurrent(RecursionGuard)->StateId;
			Ar << LocalStateId;
			Ar << MaterialOverride.LODIndex;
		}
	}	

	ACyLandProxy* Proxy = GetCyLandProxy();

	if (Proxy->CyLandMaterial != nullptr)
	{
		UMaterialInterface::TMicRecursionGuard RecursionGuard;
		FGuid LocalStateId = Proxy->CyLandMaterial->GetMaterial_Concurrent(RecursionGuard)->StateId;
		Ar << LocalStateId;
	}

	for (FCyLandProxyMaterialOverride& MaterialOverride : Proxy->CyLandMaterialsOverride)
	{
		if (MaterialOverride.Material != nullptr)
		{
			UMaterialInterface::TMicRecursionGuard RecursionGuard;
			FGuid LocalStateId = MaterialOverride.Material->GetMaterial_Concurrent(RecursionGuard)->StateId;
			Ar << LocalStateId;
			Ar << MaterialOverride.LODIndex;
		}
	}
}

void ACyLandProxy::UpdateBakedTextures()
{
	// See if we can render
	UWorld* World = GetWorld();
	if (!GIsEditor || GUsingNullRHI || !World || World->IsGameWorld() || World->FeatureLevel < ERHIFeatureLevel::SM4)
	{
		return;
	}

	if (UpdateBakedTexturesCountdown-- > 0)
	{
		return;
	}

	// Check if we can want to generate landscape GI data
	static const auto DistanceFieldCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.GenerateMeshDistanceFields"));
	static const auto CyLandGICVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.GenerateCyLandGIData"));
	if (DistanceFieldCVar->GetValueOnGameThread() == 0 || CyLandGICVar->GetValueOnGameThread() == 0)
	{
		// Clear out any existing GI textures
		for (UCyLandComponent* Component : CyLandComponents)
		{
			if (Component != nullptr && Component->GIBakedBaseColorTexture != nullptr)
			{
				Component->BakedTextureMaterialGuid.Invalidate();
				Component->GIBakedBaseColorTexture = nullptr;
				Component->MarkRenderStateDirty();
			}
		}

		// Don't check if we need to update anything for another 60 frames
		UpdateBakedTexturesCountdown = 60;

		return;
	}
	
	// Stores the components and their state hash data for a single atlas
	struct FBakedTextureSourceInfo
	{
		// pointer as FMemoryWriter caches the address of the FBufferArchive, and this struct could be relocated on a realloc.
		TUniquePtr<FBufferArchive> ComponentStateAr;
		TArray<UCyLandComponent*> Components;

		FBakedTextureSourceInfo()
		{
			ComponentStateAr = MakeUnique<FBufferArchive>();
		}
	};

	// Group components by heightmap texture
	TMap<UTexture2D*, FBakedTextureSourceInfo> ComponentsByHeightmap;
	for (UCyLandComponent* Component : CyLandComponents)
	{
		if (Component == nullptr)
		{
			continue;
		}

		FBakedTextureSourceInfo& Info = ComponentsByHeightmap.FindOrAdd(Component->GetHeightmap());
		Info.Components.Add(Component);
		Component->SerializeStateHashes(*Info.ComponentStateAr);
	}

	TotalComponentsNeedingTextureBaking -= NumComponentsNeedingTextureBaking;
	NumComponentsNeedingTextureBaking = 0;
	int32 NumGenerated = 0;

	for (auto It = ComponentsByHeightmap.CreateConstIterator(); It; ++It)
	{
		const FBakedTextureSourceInfo& Info = It.Value();

		bool bCanBake = true;
		for (UCyLandComponent* Component : Info.Components)
		{
			// not registered; ignore this component
			if (!Component->SceneProxy)
			{
				continue;
			}

			// Check we can render the material
			UMaterialInstance* MaterialInstance = Component->GetMaterialInstance(0, false);
			if (!MaterialInstance)
			{
				// Cannot render this component yet as it doesn't have a material; abandon the atlas for this heightmap
				bCanBake = false;
				break;
			}

			FMaterialResource* MaterialResource = MaterialInstance->GetMaterialResource(World->FeatureLevel);
			if (!MaterialResource || !MaterialResource->HasValidGameThreadShaderMap())
			{
				// Cannot render this component yet as its shaders aren't compiled; abandon the atlas for this heightmap
				bCanBake = false;
				break;
			}
		}

		if (bCanBake)
		{
			// Calculate a combined Guid-like ID we can use for this component
			uint32 Hash[5];
			FSHA1::HashBuffer(Info.ComponentStateAr->GetData(), Info.ComponentStateAr->Num(), (uint8*)Hash);
			FGuid CombinedStateId = FGuid(Hash[0] ^ Hash[4], Hash[1], Hash[2], Hash[3]);

			bool bNeedsBake = false;
			for (UCyLandComponent* Component : Info.Components)
			{
				if (Component->BakedTextureMaterialGuid != CombinedStateId)
				{
					bNeedsBake = true;
					break;
				}
			}
			
			if (bNeedsBake)
			{
				// We throttle, baking only one atlas per frame
				if (NumGenerated > 0)
				{
					NumComponentsNeedingTextureBaking += Info.Components.Num();
				}
				else
				{
					UTexture2D* HeightmapTexture = It.Key();
					// 1/8 the res of the heightmap
					FIntPoint AtlasSize(HeightmapTexture->GetSizeX() >> 3, HeightmapTexture->GetSizeY() >> 3);

					TArray<FColor> AtlasSamples;
					AtlasSamples.AddZeroed(AtlasSize.X * AtlasSize.Y);

					for (UCyLandComponent* Component : Info.Components)
					{
						// not registered; ignore this component
						if (!Component->SceneProxy)
						{
							continue;
						}

						int32 ComponentSamples = (SubsectionSizeQuads + 1) * NumSubsections;
						check(FMath::IsPowerOfTwo(ComponentSamples));

						int32 BakeSize = ComponentSamples >> 3;
						TArray<FColor> Samples;
						if (FMUtils::ExportBaseColor(Component, BakeSize, Samples))
						{
							int32 AtlasOffsetX = FMath::RoundToInt(Component->HeightmapScaleBias.Z * (float)HeightmapTexture->GetSizeX()) >> 3;
							int32 AtlasOffsetY = FMath::RoundToInt(Component->HeightmapScaleBias.W * (float)HeightmapTexture->GetSizeY()) >> 3;
							for (int32 y = 0; y < BakeSize; y++)
							{
								FMemory::Memcpy(&AtlasSamples[(y + AtlasOffsetY)*AtlasSize.X + AtlasOffsetX], &Samples[y*BakeSize], sizeof(FColor)* BakeSize);
							}
							NumGenerated++;
						}
					}
					UTexture2D* AtlasTexture = FMUtils::CreateTexture(GetOutermost(), HeightmapTexture->GetName() + TEXT("_BaseColor"), AtlasSize, AtlasSamples, TC_Default, TEXTUREGROUP_World, RF_NoFlags, true, CombinedStateId);
					AtlasTexture->MarkPackageDirty();

					for (UCyLandComponent* Component : Info.Components)
					{
						Component->BakedTextureMaterialGuid = CombinedStateId;
						Component->GIBakedBaseColorTexture = AtlasTexture;
						Component->MarkRenderStateDirty();
					}
				}
			}
		}
	}

	TotalComponentsNeedingTextureBaking += NumComponentsNeedingTextureBaking;

	if (NumGenerated == 0)
	{
		// Don't check if we need to update anything for another 60 frames
		UpdateBakedTexturesCountdown = 60;
	}
}
#endif

void ACyLandProxy::InvalidateGeneratedComponentData(const TSet<UCyLandComponent*>& Components)
{
	TMap<ACyLandProxy*, TSet<UCyLandComponent*>> ByProxy;
	for (auto Component : Components)
	{
		Component->BakedTextureMaterialGuid.Invalidate();

		ByProxy.FindOrAdd(Component->GetCyLandProxy()).Add(Component);
	}
	for (auto It = ByProxy.CreateConstIterator(); It; ++It)
	{
		It.Key()->FlushGrassComponents(&It.Value());
	}
}

#undef LOCTEXT_NAMESPACE
