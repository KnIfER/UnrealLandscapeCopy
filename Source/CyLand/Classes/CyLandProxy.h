// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Guid.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Async/AsyncWork.h"
#include "Engine/Texture.h"
#include "PerPlatformProperties.h"
#include "CyLandBPCustomBrush.h"
#include "CyLandProxy.generated.h"

class ACyLand;
class ACyLandProxy;
class UHierarchicalInstancedStaticMeshComponent;
class UCyLandComponent;
class UCyLandGrassType;
class UCyLandHeightfieldCollisionComponent;
class UCyLandInfo;
class UCyLandLayerInfoObject;
class UCyLandMaterialInstanceConstant;
class UCyLandSplinesComponent;
class UMaterialInstanceConstant;
class UMaterialInterface;
class UPhysicalMaterial;
class USplineComponent;
class UTexture2D;
struct FAsyncGrassBuilder;
struct FCyLandInfoLayerSettings;
struct FMeshDescription;
enum class ENavDataGatheringMode : uint8;

/** Structure storing channel usage for weightmap textures */
USTRUCT()
struct FCyLandWeightmapUsage
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	UCyLandComponent* ChannelUsage[4];

	FCyLandWeightmapUsage()
	{
		ChannelUsage[0] = nullptr;
		ChannelUsage[1] = nullptr;
		ChannelUsage[2] = nullptr;
		ChannelUsage[3] = nullptr;
	}
	friend FArchive& operator<<( FArchive& Ar, FCyLandWeightmapUsage& U );
	int32 CyFreeChannelCount() const
	{
		return	((ChannelUsage[0] == nullptr) ? 1 : 0) +
				((ChannelUsage[1] == nullptr) ? 1 : 0) +
				((ChannelUsage[2] == nullptr) ? 1 : 0) +
				((ChannelUsage[3] == nullptr) ? 1 : 0);
	}
};

USTRUCT()
struct FCyLandEditorLayerSettings
{
	GENERATED_USTRUCT_BODY()

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	UCyLandLayerInfoObject* LayerInfoObj;

	UPROPERTY()
	FString ReimportLayerFilePath;

	FCyLandEditorLayerSettings()
		: LayerInfoObj(nullptr)
		, ReimportLayerFilePath()
	{
	}

	explicit FCyLandEditorLayerSettings(UCyLandLayerInfoObject* InLayerInfo, const FString& InFilePath = FString())
		: LayerInfoObj(InLayerInfo)
		, ReimportLayerFilePath(InFilePath)
	{
	}

	// To allow FindByKey etc
	bool operator==(const UCyLandLayerInfoObject* LayerInfo) const
	{
		return LayerInfoObj == LayerInfo;
	}
#endif // WITH_EDITORONLY_DATA
};

USTRUCT()
struct FCyLandLayerStruct
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	UCyLandLayerInfoObject* LayerInfoObj;

#if WITH_EDITORONLY_DATA
	UPROPERTY(transient)
	UCyLandMaterialInstanceConstant* ThumbnailMIC;

	UPROPERTY()
	ACyLandProxy* Owner;

	UPROPERTY(transient)
	int32 DebugColorChannel;

	UPROPERTY(transient)
	uint32 bSelected:1;

	UPROPERTY()
	FString SourceFilePath;
#endif // WITH_EDITORONLY_DATA

	FCyLandLayerStruct()
		: LayerInfoObj(nullptr)
#if WITH_EDITORONLY_DATA
		, ThumbnailMIC(nullptr)
		, Owner(nullptr)
		, DebugColorChannel(0)
		, bSelected(false)
		, SourceFilePath()
#endif // WITH_EDITORONLY_DATA
	{
	}
};

UENUM()
enum class ECyLandImportAlphamapType : uint8
{
	// Three layers blended 50/30/20 represented as 0.5, 0.3, and 0.2 in the alpha maps
	// All alpha maps for blended layers total to 1.0
	// This is the style used by UE4 internally for blended layers
	Additive,

	// Three layers blended 50/30/20 represented as 0.5, 0.6, and 1.0 in the alpha maps
	// Each alpha map only specifies the remainder from previous layers, so the last layer used will always be 1.0
	// Some other tools use this format
	Layered,
};

/** Structure storing Layer Data for import */
USTRUCT()
struct FCyLandImportLayerInfo
{
	GENERATED_USTRUCT_BODY()

#if WITH_EDITORONLY_DATA
	UPROPERTY(Category="Import", VisibleAnywhere)
	FName LayerName;

	UPROPERTY(Category="Import", EditAnywhere)
	UCyLandLayerInfoObject* LayerInfo;

	UPROPERTY(Category="Import", EditAnywhere)
	FString SourceFilePath; // Optional
	
	// Raw weightmap data
	TArray<uint8> LayerData;
#endif

#if WITH_EDITOR
	FCyLandImportLayerInfo(FName InLayerName = NAME_None)
	:	LayerName(InLayerName)
	,	LayerInfo(nullptr)
	,	SourceFilePath("")
	{
	}

	CYLAND_API FCyLandImportLayerInfo(const FCyLandInfoLayerSettings& InLayerSettings);
#endif
};

// this is only here because putting it in CyLandEditorObject.h (where it belongs)
// results in Engine being dependent on CyLandEditor, as the actual landscape editing
// code (e.g. CyLandEdit.h) is in /Engine/ for some reason...
UENUM()
enum class ECyLandLayerPaintingRestriction : uint8
{
	/** No restriction, can paint anywhere (default). */
	None         UMETA(DisplayName="None"),

	/** Uses the MaxPaintedLayersPerComponent setting from the CyLandProxy. */
	UseMaxLayers UMETA(DisplayName="Limit Layer Count"),

	/** Restricts painting to only components that already have this layer. */
	ExistingOnly UMETA(DisplayName="Existing Layers Only"),

	/** Restricts painting to only components that have this layer in their whitelist. */
	UseComponentWhitelist UMETA(DisplayName="Component Whitelist"),
};

UENUM()
enum class ECyLandLayerDisplayMode : uint8
{
	/** Material sorting display mode */
	Default,

	/** Alphabetical sorting display mode */
	Alphabetical,

	/** User specific sorting display mode */
	UserSpecific,
};

UENUM()
namespace ECyLandLODFalloff
{
	enum Type
	{
		/** Default mode. */
		Linear			UMETA(DisplayName = "Linear"),
		/** Square Root give more natural transition, and also keep the same LOD. */
		SquareRoot		UMETA(DisplayName = "Square Root"),
	};
}

struct FCachedCyLandFoliage
{
	struct FGrassCompKey
	{
		TWeakObjectPtr<UCyLandComponent> BasedOn;
		TWeakObjectPtr<UCyLandGrassType> GrassType;
		int32 SqrtSubsections;
		int32 CachedMaxInstancesPerComponent;
		int32 SubsectionX;
		int32 SubsectionY;
		int32 NumVarieties;
		int32 VarietyIndex;

		FGrassCompKey()
			: SqrtSubsections(0)
			, CachedMaxInstancesPerComponent(0)
			, SubsectionX(0)
			, SubsectionY(0)
			, NumVarieties(0)
			, VarietyIndex(-1)
		{
		}
		inline bool operator==(const FGrassCompKey& Other) const
		{
			return 
				SqrtSubsections == Other.SqrtSubsections &&
				CachedMaxInstancesPerComponent == Other.CachedMaxInstancesPerComponent &&
				SubsectionX == Other.SubsectionX &&
				SubsectionY == Other.SubsectionY &&
				BasedOn == Other.BasedOn &&
				GrassType == Other.GrassType &&
				NumVarieties == Other.NumVarieties &&
				VarietyIndex == Other.VarietyIndex;
		}

		friend uint32 GetTypeHash(const FGrassCompKey& Key)
		{
			return GetTypeHash(Key.BasedOn) ^ GetTypeHash(Key.GrassType) ^ Key.SqrtSubsections ^ Key.CachedMaxInstancesPerComponent ^ (Key.SubsectionX << 16) ^ (Key.SubsectionY << 24) ^ (Key.NumVarieties << 3) ^ (Key.VarietyIndex << 13);
		}

	};

	struct FGrassComp
	{
		FGrassCompKey Key;
		TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent> Foliage;
		TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent> PreviousFoliage;
		TArray<FBox> ExcludedBoxes;
		uint32 LastUsedFrameNumber;
		uint32 ExclusionChangeTag;
		double LastUsedTime;
		bool Pending;
		bool PendingRemovalRebuild;

		FGrassComp()
			: ExclusionChangeTag(0)
			, Pending(true)
			, PendingRemovalRebuild(false)
		{
			Touch();
		}
		void Touch()
		{
			LastUsedFrameNumber = GFrameNumber;
			LastUsedTime = FPlatformTime::Seconds();
		}
	};

	struct FGrassCompKeyFuncs : BaseKeyFuncs<FGrassComp,FGrassCompKey>
	{
		static KeyInitType GetSetKey(const FGrassComp& Element)
		{
			return Element.Key;
		}

		static bool Matches(KeyInitType A, KeyInitType B)
		{
			return A == B;
		}

		static uint32 GetKeyHash(KeyInitType Key)
		{
			return GetTypeHash(Key);
		}
	};

	typedef TSet<FGrassComp, FGrassCompKeyFuncs> TGrassSet;
	TSet<FGrassComp, FGrassCompKeyFuncs> CachedGrassComps;

	void ClearCache()
	{
		CachedGrassComps.Empty();
	}
};

class FCyAsyncGrassTask : public FNonAbandonableTask
{
public:
	FAsyncGrassBuilder* Builder;
	FCachedCyLandFoliage::FGrassCompKey Key;
	TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent> Foliage;

	FCyAsyncGrassTask(FAsyncGrassBuilder* InBuilder, const FCachedCyLandFoliage::FGrassCompKey& InKey, UHierarchicalInstancedStaticMeshComponent* InFoliage);
	void DoWork();

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FCyAsyncGrassTask, STATGROUP_ThreadPoolAsyncTasks);
	}

	~FCyAsyncGrassTask();
};

USTRUCT()
struct FCyLandProxyMaterialOverride
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = CyLand)
	FPerPlatformInt LODIndex;

	UPROPERTY(EditAnywhere, Category = CyLand)
	UMaterialInterface* Material;
};

class FCyLandProceduralTexture2DCPUReadBackResource : public FTextureResource
{
public:
	FCyLandProceduralTexture2DCPUReadBackResource(uint32 InSizeX, uint32 InSizeY, EPixelFormat InFormat, uint32 InNumMips)
		: SizeX(InSizeX)
		, SizeY(InSizeY)
		, Format(InFormat)
		, NumMips(InNumMips)
	{}

	virtual uint32 GetSizeX() const override
	{
		return SizeX;
	}

	virtual uint32 GetSizeY() const override
	{
		return SizeY;
	}

	/** Called when the resource is initialized. This is only called by the rendering thread. */
	virtual void InitRHI() override
	{
		FTextureResource::InitRHI();

		FRHIResourceCreateInfo CreateInfo;
		TextureRHI = RHICreateTexture2D(SizeX, SizeY, Format, NumMips, 1, TexCreate_CPUReadback, CreateInfo);
	}

private:
	uint32 SizeX;
	uint32 SizeY;
	EPixelFormat Format;
	uint32 NumMips;
};

USTRUCT()
struct FCyRenderDataPerHeightmap
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(Transient)
	UTexture2D* OriginalHeightmap;

	FCyLandProceduralTexture2DCPUReadBackResource* HeightmapsCPUReadBack;

	UPROPERTY(Transient)
	TArray<UCyLandComponent*> Components;

	UPROPERTY(Transient)
	FIntPoint TopLeftSectionBase;
};

USTRUCT()
struct FCyProceduralLayerData
{
	GENERATED_USTRUCT_BODY()

	FCyProceduralLayerData()
	{}

	UPROPERTY()
	TMap<UTexture2D*, UTexture2D*> Heightmaps;

	// TODO: add weightmap data
};

UCLASS(Abstract, MinimalAPI, Blueprintable, hidecategories=(Display, Attachment, Physics, Debug, Lighting, LOD), showcategories=(Lighting, Rendering, "Utilities|Transformation"), hidecategories=(Mobility))
class ACyLandProxy : public AActor
{
	GENERATED_BODY()

public:
	ACyLandProxy(const FObjectInitializer& ObjectInitializer);

	virtual ~ACyLandProxy();

	UPROPERTY()
	UCyLandSplinesComponent* SplineComponent;

protected:
	/** Guid for CyLandEditorInfo **/
	UPROPERTY()
	FGuid CyLandGuid;

public:
	/** Offset in quads from global components grid origin (in quads) **/
	UPROPERTY()
	FIntPoint CyLandSectionOffset;

	/** Max LOD level to use when rendering, -1 means the max available */
	UPROPERTY(EditAnywhere, Category=LOD)
	int32 MaxLODLevel;

	UPROPERTY()
	float LODDistanceFactor_DEPRECATED;

	UPROPERTY()
	TEnumAsByte<ECyLandLODFalloff::Type> LODFalloff_DEPRECATED;

	/** Component screen size (0.0 - 1.0) at which we should keep sub sections. This is mostly pertinent if you have large component of > 64 and component are close to the camera. The goal is to reduce draw call, so if a component is smaller than the value, we merge all subsections into 1 drawcall. */
	UPROPERTY(EditAnywhere, Category = LOD, meta=(ClampMin = "0.01", ClampMax = "1.0", UIMin = "0.01", UIMax = "1.0", DisplayName= "SubSection Min Component ScreenSize"))
	float ComponentScreenSizeToUseSubSections;

	/** The distribution setting used to change the LOD 0 generation, 1.75 is the normal distribution, numbers influence directly the LOD0 proportion on screen. */
	UPROPERTY(EditAnywhere, Category = "LOD Distribution", meta = (DisplayName = "LOD 0", ClampMin = "1.0", ClampMax = "10.0", UIMin = "1.0", UIMax = "10.0"))
	float LOD0DistributionSetting;

	/** The distribution setting used to change the LOD generation, 2 is the normal distribution, small number mean you want your last LODs to take more screen space and big number mean you want your first LODs to take more screen space. */
	UPROPERTY(EditAnywhere, Category = "LOD Distribution", meta = (DisplayName = "Other LODs", ClampMin = "1.0", ClampMax = "10.0", UIMin = "1.0", UIMax = "10.0"))
	float LODDistributionSetting;

	/** Component screen size (0.0 - 1.0) at which we should enable tessellation. */
	UPROPERTY(EditAnywhere, Category = Tessellation, meta = (ClampMin = "0.01", ClampMax = "1.0", UIMin = "0.01", UIMax = "1.0"))
	float TessellationComponentScreenSize;

	/** Tell if we should enable tessellation falloff. It will ramp down the Tessellation Multiplier from the material linearly. It should be disabled if you plan on using a custom implementation in material/shaders. */
	UPROPERTY(EditAnywhere, Category = Tessellation, meta=(DisplayName = "Use Default Falloff"))
	bool UseTessellationComponentScreenSizeFalloff;

	/** Component screen size (0.0 - 1.0) at which we start the tessellation falloff. */
	UPROPERTY(EditAnywhere, Category = Tessellation, meta=(editcondition= UseTessellationComponentScreenSizeFalloff, ClampMin = "0.01", ClampMax = "1.0", UIMin = "0.01", UIMax = "1.0", DisplayName = "Tessellation Component Screen Size Falloff"))
	float TessellationComponentScreenSizeFalloff;

	/** CyLand LOD to use as an occluder geometry for software occlusion */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category=LOD)
	int32 OccluderGeometryLOD;

#if WITH_EDITORONLY_DATA
	/** LOD level to use when exporting the landscape to obj or FBX */
	UPROPERTY(EditAnywhere, Category=LOD, AdvancedDisplay)
	int32 ExportLOD;

	/** Display Order of the targets */
	UPROPERTY(NonTransactional)
	TArray<FName> TargetDisplayOrderList;

	/** Display Order mode for the targets */
	UPROPERTY(NonTransactional)
	ECyLandLayerDisplayMode TargetDisplayOrder;
#endif

	/** LOD level to use when running lightmass (increase to 1 or 2 for large landscapes to stop lightmass crashing) */
	UPROPERTY(EditAnywhere, Category=Lighting)
	int32 StaticLightingLOD;

	/** Default physical material, used when no per-layer values physical materials */
	UPROPERTY(EditAnywhere, Category=CyLand)
	UPhysicalMaterial* DefaultPhysMaterial;

	/**
	 * Allows artists to adjust the distance where textures using UV 0 are streamed in/out.
	 * 1.0 is the default, whereas a higher value increases the streamed-in resolution.
	 * Value can be < 0 (from legcay content, or code changes)
	 */
	UPROPERTY(EditAnywhere, Category=CyLand)
	float StreamingDistanceMultiplier;

	/** Combined material used to render the landscape */
	UPROPERTY(EditAnywhere, BlueprintSetter=EditorSetCyLandMaterial, Category=CyLand)
	UMaterialInterface* CyLandMaterial;

	/** Material used to render landscape components with holes. If not set, CyLandMaterial will be used (blend mode will be overridden to Masked if it is set to Opaque) */
	UPROPERTY(EditAnywhere, Category=CyLand, AdvancedDisplay)
	UMaterialInterface* CyLandHoleMaterial;

	UPROPERTY(EditAnywhere, Category = CyLand)
	TArray<FCyLandProxyMaterialOverride> CyLandMaterialsOverride;

	/** Allows overriding the landscape bounds. This is useful if you distort the landscape with world-position-offset, for example
	 *  Extension value in the negative Z axis, positive value increases bound size
	 *  Note that this can also be overridden per-component when the component is selected with the component select tool */
	UPROPERTY(EditAnywhere, Category=CyLand)
	float NegativeZBoundsExtension;

	/** Allows overriding the landscape bounds. This is useful if you distort the landscape with world-position-offset, for example
	 *  Extension value in the positive Z axis, positive value increases bound size
	 *  Note that this can also be overridden per-component when the component is selected with the component select tool */
	UPROPERTY(EditAnywhere, Category=CyLand)
	float PositiveZBoundsExtension;

	/** The array of CyLandComponent that are used by the landscape */
	UPROPERTY()
	TArray<UCyLandComponent*> CyLandComponents;

	/** Array of CyLandHeightfieldCollisionComponent */
	UPROPERTY()
	TArray<UCyLandHeightfieldCollisionComponent*> CollisionComponents;

	UPROPERTY(transient, duplicatetransient)
	TArray<UHierarchicalInstancedStaticMeshComponent*> FoliageComponents;

	/** A transient data structure for tracking the grass */
	FCachedCyLandFoliage FoliageCache;
	/** A transient data structure for tracking the grass tasks*/
	TArray<FAsyncTask<FCyAsyncGrassTask>* > AsyncFoliageTasks;
	/** Frame offset for tick interval*/
	uint32 FrameOffsetForTickInterval;

	// Only used outside of the editor (e.g. in cooked builds)
	// Disables landscape grass processing entirely if no landscape components have landscape grass configured
	UPROPERTY()
	bool bHasCyLandGrass;

	/**
	 *	The resolution to cache lighting at, in texels/quad in one axis
	 *  Total resolution would be changed by StaticLightingResolution*StaticLightingResolution
	 *	Automatically calculate proper value for removing seams
	 */
	UPROPERTY(EditAnywhere, Category=Lighting)
	float StaticLightingResolution;

	UPROPERTY(EditAnywhere, Category=Lighting, meta=(DisplayName = "Static Shadow"))
	uint32 bCastStaticShadow:1;

	/** Whether this primitive should cast dynamic shadows as if it were a two sided material. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category=Lighting, meta=(DisplayName = "Shadow Two Sided"))
	uint32 bCastShadowAsTwoSided:1;

	/** Whether this primitive should cast shadows in the far shadow cascades. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category=Lighting, meta=(DisplayName = "Far Shadow"))
	uint32 bCastFarShadow:1;
	
	/** Controls whether the landscape should affect dynamic distance field lighting methods. **/
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Lighting, AdvancedDisplay)
	uint8 bAffectDistanceFieldLighting:1;

	/**
	* Channels that this CyLand should be in.  Lights with matching channels will affect the CyLand.
	* These channels only apply to opaque materials, direct lighting, and dynamic lighting and shadowing.
	*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category = Lighting)
	FLightingChannels LightingChannels;

	/** Whether to use the landscape material's vertical world position offset when calculating static lighting.
		Note: Only z (vertical) offset is supported. XY offsets are ignored.
		Does not work correctly with an XY offset map (mesh collision) */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category=Lighting)
	uint32 bUseMaterialPositionOffsetInStaticLighting:1;

	/** If true, the CyLand will be rendered in the CustomDepth pass (usually used for outlines) */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=Rendering, meta=(DisplayName = "Render CustomDepth Pass"))
	uint32 bRenderCustomDepth:1;

	/** Optionally write this 0-255 value to the stencil buffer in CustomDepth pass (Requires project setting or r.CustomDepth == 3) */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category=Rendering,  meta=(UIMin = "0", UIMax = "255", editcondition = "bRenderCustomDepth", DisplayName = "CustomDepth Stencil Value"))
	int32 CustomDepthStencilValue;

	/**  Max draw distance exposed to LDs. The real max draw distance is the min (disregarding 0) of this and volumes affecting this object. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, BlueprintReadOnly, Category = LOD, meta = (DisplayName = "Desired Max Draw Distance"))
	float LDMaxDrawDistance;

#if WITH_EDITORONLY_DATA
	UPROPERTY(transient)
	uint32 bIsMovingToLevel:1;    // Check for the Move to Current Level case
#endif // WITH_EDITORONLY_DATA

	/** The Lightmass settings for this object. */
	UPROPERTY(EditAnywhere, Category=Lightmass)
	FLightmassPrimitiveSettings LightmassSettings;

	// CyLand LOD to use for collision tests. Higher numbers use less memory and process faster, but are much less accurate
	UPROPERTY(EditAnywhere, Category=Collision)
	int32 CollisionMipLevel;

	// If set higher than the "Collision Mip Level", this specifies the CyLand LOD to use for "simple collision" tests, otherwise the "Collision Mip Level" is used for both simple and complex collision.
	// Does not work with an XY offset map (mesh collision)
	UPROPERTY(EditAnywhere, Category=Collision)
	int32 SimpleCollisionMipLevel;

	/** Thickness of the collision surface, in unreal units */
	UPROPERTY(EditAnywhere, Category=Collision)
	float CollisionThickness;

	/** Collision profile settings for this landscape */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Collision, meta=(ShowOnlyInnerProperties))
	FBodyInstance BodyInstance;

	/**
	 * If true, CyLand will generate overlap events when other components are overlapping it (eg Begin Overlap).
	 * Both the CyLand and the other component must have this flag enabled for overlap events to occur.
	 *
	 * @see [Overlap Events](https://docs.unrealengine.com/latest/INT/Engine/Physics/Collision/index.html#overlapandgenerateoverlapevents)
	 * @see UpdateOverlaps(), BeginComponentOverlap(), EndComponentOverlap()
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Collision)
	uint32 bGenerateOverlapEvents : 1;

	/** Whether to bake the landscape material's vertical world position offset into the collision heightfield.
		Note: Only z (vertical) offset is supported. XY offsets are ignored.
		Does not work with an XY offset map (mesh collision) */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category=Collision)
	uint32 bBakeMaterialPositionOffsetIntoCollision:1;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TArray<UCyLandLayerInfoObject*> EditorCachedLayerInfos_DEPRECATED;

	UPROPERTY()
	FString ReimportHeightmapFilePath;

	UPROPERTY()
	TArray<FCyLandEditorLayerSettings> EditorLayerSettings;

	UPROPERTY(TextExportTransient)
	TMap<FName, FCyProceduralLayerData> ProceduralLayersData;

	UPROPERTY()
	bool HasProceduralContent;

	UPROPERTY(Transient)
	TMap<UTexture2D*, FCyRenderDataPerHeightmap> RenderDataPerHeightmap; // Mapping between Original heightmap and general render data

	FRenderCommandFence ReleaseResourceFence;
#endif

	/** Data set at creation time */
	UPROPERTY()
	int32 ComponentSizeQuads;    // Total number of quads in each component

	UPROPERTY()
	int32 SubsectionSizeQuads;    // Number of quads for a subsection of a component. SubsectionSizeQuads+1 must be a power of two.

	UPROPERTY()
	int32 NumSubsections;    // Number of subsections in X and Y axis

	/** Hints navigation system whether this landscape will ever be navigated on. true by default, but make sure to set it to false for faraway, background landscapes */
	UPROPERTY(EditAnywhere, Category=CyLand)
	uint32 bUsedForNavigation:1;

	/** When set to true it will generate MaterialInstanceDynamic for each components, so material can be changed at runtime */
	UPROPERTY(EditAnywhere, Category = CyLand)
	bool bUseDynamicMaterialInstance;

	UPROPERTY(EditAnywhere, Category = CyLand, AdvancedDisplay)
	ENavDataGatheringMode NavigationGeometryGatheringMode;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category=CyLand)
	int32 MaxPaintedLayersPerComponent; // 0 = disabled
#endif

	/** Flag whether or not this CyLand's surface can be used for culling hidden triangles **/
	UPROPERTY(EditAnywhere, Category = HierarchicalLOD)
	bool bUseCyLandForCullingInvisibleHLODVertices;

public:

#if WITH_EDITOR
	CYLAND_API static UCyLandLayerInfoObject* VisibilityLayer;
#endif

#if WITH_EDITORONLY_DATA
	/** Map of material instance constants used to for the components. Key is generated with UCyLandComponent::GetLayerAllocationKey() */
	TMap<FString, UMaterialInstanceConstant*> MaterialInstanceConstantMap;
#endif

	/** Map of weightmap usage */
	TMap<UTexture2D*, FCyLandWeightmapUsage> WeightmapUsageMap;

	// Blueprint functions

	/** Change the Level of Detail distance factor */
	UFUNCTION(BlueprintCallable, Category = "Rendering", meta=(DeprecatedFunction, DeprecationMessage = "This value can't be changed anymore, you should edit the property LODDistributionSetting of the CyLand"))
	virtual void ChangeLODDistanceFactor(float InLODDistanceFactor);

	/** Change TessellationComponentScreenSize value on the render proxy.*/
	UFUNCTION(BlueprintCallable, Category = "Rendering")
	virtual void ChangeTessellationComponentScreenSize(float InTessellationComponentScreenSize);

	/** Change ComponentScreenSizeToUseSubSections value on the render proxy.*/
	UFUNCTION(BlueprintCallable, Category = "Rendering")
	virtual void ChangeComponentScreenSizeToUseSubSections(float InComponentScreenSizeToUseSubSections);

	/** Change UseTessellationComponentScreenSizeFalloff value on the render proxy.*/
	UFUNCTION(BlueprintCallable, Category = "Rendering")
	virtual void ChangeUseTessellationComponentScreenSizeFalloff(bool InComponentScreenSizeToUseSubSections);

	/** Change TessellationComponentScreenSizeFalloff value on the render proxy.*/
	UFUNCTION(BlueprintCallable, Category = "Rendering")
	virtual void ChangeTessellationComponentScreenSizeFalloff(float InUseTessellationComponentScreenSizeFalloff);

	/* Setter for CyLandMaterial. Has no effect outside the editor. */
	UFUNCTION(BlueprintSetter)
	void EditorSetCyLandMaterial(UMaterialInterface* NewCyLandMaterial);

	// Editor-time blueprint functions

	/** Deform landscape using a given spline
	 * @param StartWidth - Width of the spline at the start node, in Spline Component local space
	 * @param EndWidth   - Width of the spline at the end node, in Spline Component local space
	 * @param StartSideFalloff - Width of the falloff at either side of the spline at the start node, in Spline Component local space
	 * @param EndSideFalloff - Width of the falloff at either side of the spline at the end node, in Spline Component local space
	 * @param StartRoll - Roll applied to the spline at the start node, in degrees. 0 is flat
	 * @param EndRoll - Roll applied to the spline at the end node, in degrees. 0 is flat
	 * @param NumSubdivisions - Number of triangles to place along the spline when applying it to the landscape. Higher numbers give better results, but setting it too high will be slow and may cause artifacts
	 * @param bRaiseHeights - Allow the landscape to be raised up to the level of the spline. If both bRaiseHeights and bLowerHeights are false, no height modification of the landscape will be performed
	 * @param bLowerHeights - Allow the landscape to be lowered down to the level of the spline. If both bRaiseHeights and bLowerHeights are false, no height modification of the landscape will be performed
	 * @param PaintLayer - LayerInfo to paint, or none to skip painting. The landscape must be configured with the same layer info in one of its layers or this will do nothing!
	 */
	UFUNCTION(BlueprintCallable, Category = "CyLand|Editor")
	void EditorApplySpline(USplineComponent* InSplineComponent, float StartWidth = 200, float EndWidth = 200, float StartSideFalloff = 200, float EndSideFalloff = 200, float StartRoll = 0, float EndRoll = 0, int32 NumSubdivisions = 20, bool bRaiseHeights = true, bool bLowerHeights = true, UCyLandLayerInfoObject* PaintLayer = nullptr);

	/** Set an MID texture parameter value for all landscape components. */
	UFUNCTION(BlueprintCallable, Category = "CyLand|Runtime|Material")
	void SetCyLandMaterialTextureParameterValue(FName ParameterName, class UTexture* Value);

	/** Set an MID vector parameter value for all landscape components. */
	UFUNCTION(BlueprintCallable, meta = (Keywords = "SetColorParameterValue"), Category = "CyLand|Runtime|Material")
	void SetCyLandMaterialVectorParameterValue(FName ParameterName, FLinearColor Value);

	/** Set a MID scalar (float) parameter value for all landscape components. */
	UFUNCTION(BlueprintCallable, meta = (Keywords = "SetFloatParameterValue"), Category = "CyLand|Runtime|Material")
	void SetCyLandMaterialScalarParameterValue(FName ParameterName, float Value);

	// End blueprint functions

	//~ Begin AActor Interface
	virtual void PostRegisterAllComponents() override;
	virtual void UnregisterAllComponents(bool bForReregister = false) override;
	virtual void RerunConstructionScripts() override {}
	virtual bool IsLevelBoundsRelevant() const override { return true; }

#if WITH_EDITOR
	virtual void Destroyed() override;
	virtual void EditorApplyScale(const FVector& DeltaScale, const FVector* PivotLocation, bool bAltDown, bool bShiftDown, bool bCtrlDown) override;
	virtual void EditorApplyMirror(const FVector& MirrorScale, const FVector& PivotLocation) override;
	virtual void PostEditMove(bool bFinished) override;
	virtual bool ShouldImport(FString* ActorPropString, bool IsMovingLevel) override;
	virtual bool ShouldExport() override;
	//~ End AActor Interface
#endif	//WITH_EDITOR

	FGuid GetCyLandGuid() const { return CyLandGuid; }
	void SetCyLandGuid(const FGuid& Guid) { CyLandGuid = Guid; }
	virtual ACyLand* GetCyLandActor() PURE_VIRTUAL(GetCyLandActor, return nullptr;)

	/* Per-frame call to update dynamic grass placement and render grassmaps */
	void TickGrass();

	/** Flush the grass cache */
	CYLAND_API void FlushGrassComponents(const TSet<UCyLandComponent*>* OnlyForComponents = nullptr, bool bFlushGrassMaps = true);

	/**
		Update Grass 
		* @param Cameras to use for culling, if empty, then NO culling
		* @param bForceSync if true, block and finish all work
	*/
	CYLAND_API void UpdateGrass(const TArray<FVector>& Cameras, bool bForceSync = false);

	CYLAND_API static void AddExclusionBox(FWeakObjectPtr Owner, const FBox& BoxToRemove);
	CYLAND_API static void RemoveExclusionBox(FWeakObjectPtr Owner);
	CYLAND_API static void RemoveAllExclusionBoxes();


	/* Get the list of grass types on this landscape */
	TArray<UCyLandGrassType*> GetGrassTypes() const;

	/* Invalidate the precomputed grass and baked texture data for the specified components */
	CYLAND_API static void InvalidateGeneratedComponentData(const TSet<UCyLandComponent*>& Components);

#if WITH_EDITOR
	/** Render grass maps for the specified components */
	void RenderGrassMaps(const TArray<UCyLandComponent*>& CyLandComponents, const TArray<UCyLandGrassType*>& GrassTypes);

	/** Update any textures baked from the landscape as necessary */
	void UpdateBakedTextures();

	/** Frame counter to count down to the next time we check to update baked textures, so we don't check every frame */
	int32 UpdateBakedTexturesCountdown;

	/** Editor notification when changing feature level */
	void OnFeatureLevelChanged(ERHIFeatureLevel::Type NewFeatureLevel);

	/** Handle so we can unregister the delegate */
	FDelegateHandle FeatureLevelChangedDelegateHandle;
#endif

	//~ Begin AActor Interface.
	virtual void TickActor(float DeltaTime, ELevelTick TickType, FActorTickFunction& ThisTickFunction) override;
	virtual bool ShouldTickIfViewportsOnly() const override;
	//~ End AActor Interface

	//~ Begin UObject Interface.
	virtual void PreSave(const class ITargetPlatform* TargetPlatform) override;
	virtual void Serialize(FArchive& Ar) override;
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
	virtual void PostLoad() override;
	virtual void BeginDestroy() override;
	virtual bool IsReadyForFinishDestroy() override;
	virtual void FinishDestroy() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditImport() override;
	//~ End UObject Interface

	CYLAND_API static TArray<FName> GetLayersFromMaterial(UMaterialInterface* Material);
	CYLAND_API TArray<FName> GetLayersFromMaterial() const;
	CYLAND_API static UCyLandLayerInfoObject* CreateLayerInfo(const TCHAR* LayerName, ULevel* Level);
	CYLAND_API UCyLandLayerInfoObject* CreateLayerInfo(const TCHAR* LayerName);

	CYLAND_API UCyLandInfo* CreateCyLandInfo();
	CYLAND_API UCyLandInfo* GetCyLandInfo() const;

	// Get CyLand Material assigned to this CyLand
	virtual UMaterialInterface* GetCyLandMaterial(int8 InLODIndex = INDEX_NONE) const;

	// Get Hole CyLand Material assigned to this CyLand
	virtual UMaterialInterface* GetCyLandHoleMaterial() const;

	// 
	void FixupWeightmaps();

	// Remove Invalid weightmaps
	CYLAND_API void RemoveInvalidWeightmaps();

	// Changed Physical Material
	CYLAND_API void ChangedPhysMaterial();

	// Copy properties from parent CyLand actor
	CYLAND_API void GetSharedProperties(ACyLandProxy* CyLand);
	// Assign only mismatched properties and mark proxy package dirty
	CYLAND_API void ConditionalAssignCommonProperties(ACyLand* CyLand);
	
	/** Get the LandcapeActor-to-world transform with respect to landscape section offset*/
	CYLAND_API FTransform CyLandActorToWorld() const;
	
	/** Set landscape absolute location in section space */
	CYLAND_API void SetAbsoluteSectionBase(FIntPoint SectionOffset);
	
	/** Get landscape position in section space */
	CYLAND_API FIntPoint GetSectionBaseOffset() const;
	
	/** Recreate all components rendering and collision states */
	CYLAND_API void RecreateComponentsState();

	/** Recreate all collision components based on render component */
	CYLAND_API void RecreateCollisionComponents();

	/** Remove all XYOffset values */
	CYLAND_API void RemoveXYOffsets();

	/** Update the material instances for all the landscape components */
	CYLAND_API void UpdateAllComponentMaterialInstances();

	/** Create a thumbnail material for a given layer */
	CYLAND_API static UCyLandMaterialInstanceConstant* GetLayerThumbnailMIC(UMaterialInterface* CyLandMaterial, FName LayerName, UTexture2D* ThumbnailWeightmap, UTexture2D* ThumbnailHeightmap, ACyLandProxy* Proxy);

	/** Import the given Height/Weight data into this landscape */
	CYLAND_API void Imports(FGuid Guid, int32 MinX, int32 MinY, int32 MaxX, int32 MaxY, int32 NumSubsections, int32 SubsectionSizeQuads,
							const uint16* HeightData, const TCHAR* HeightmapFileName,
							const TArray<FCyLandImportLayerInfo>& ImportLayerInfos, ECyLandImportAlphamapType ImportLayerType);

	/**
	 * Exports landscape into raw mesh
	 * 
	 * @param InExportLOD CyLand LOD level to use while exporting, INDEX_NONE will use ALanscapeProxy::ExportLOD settings
	 * @param OutRawMesh - Resulting raw mesh
	 * @return true if successful
	 */
	CYLAND_API bool ExportToRawMesh(int32 InExportLOD, FMeshDescription& OutRawMesh) const;


	/**
	* Exports landscape geometry contained within InBounds into a raw mesh
	*
	* @param InExportLOD CyLand LOD level to use while exporting, INDEX_NONE will use ALanscapeProxy::ExportLOD settings
	* @param OutRawMesh - Resulting raw mesh
	* @param InBounds - Box/Sphere bounds which limit the geometry exported out into OutRawMesh
	* @return true if successful
	*/
	CYLAND_API bool ExportToRawMesh(int32 InExportLOD, FMeshDescription& OutRawMesh, const FBoxSphereBounds& InBounds, bool bIgnoreBounds = false) const;

	/** Generate platform data if it's missing or outdated */
	CYLAND_API void CheckGenerateCyLandPlatformData(bool bIsCooking, const ITargetPlatform* TargetPlatform);
	
	/** @return Current size of bounding rectangle in quads space */
	CYLAND_API FIntRect GetBoundingRect() const;

	/** Creates a Texture2D for use by this landscape proxy or one of it's components. If OptionalOverrideOuter is not specified, the level is used. */
	CYLAND_API UTexture2D* CreateCyLandTexture(int32 InSizeX, int32 InSizeY, TextureGroup InLODGroup, ETextureSourceFormat InFormat, UObject* OptionalOverrideOuter = nullptr, bool bCompress = false) const;

	/* For the grassmap rendering notification */
	int32 NumComponentsNeedingGrassMapRender;
	CYLAND_API static int32 TotalComponentsNeedingGrassMapRender;

	/* To throttle texture streaming when we're trying to render a grassmap */
	int32 NumTexturesToStreamForVisibleGrassMapRender;
	CYLAND_API static int32 TotalTexturesToStreamForVisibleGrassMapRender;

	/* For the texture baking notification */
	int32 NumComponentsNeedingTextureBaking;
	CYLAND_API static int32 TotalComponentsNeedingTextureBaking;

	/** remove an overlapping component. Called from MapCheck. */
	CYLAND_API void RemoveOverlappingComponent(UCyLandComponent* Component);

	/**
	* Samples an array of values from a Texture Render Target 2D. 
	* Only works in the editor
	*/
	CYLAND_API static TArray<FLinearColor> SampleRTData(UTextureRenderTarget2D* InRenderTarget, FLinearColor InRect);

	/**
	* Overwrites a landscape heightmap with render target data
	* @param InRenderTarget - Valid render target with a format of RTF_RGBA16f, RTF_RGBA32f or RTF_RGBA8
	* @param InImportHeightFromRGChannel - Only relevant when using format RTF_RGBA16f or RTF_RGBA32f, and will tell us if we should import the height data from the R channel only of the Render target or from R & G. 
	*									   Note that using RTF_RGBA16f with InImportHeightFromRGChannel == false, could have precision loss
	* Only works in the editor
	*/
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "CyLand Import Heightmap from RenderTarget", Keywords = "Push RenderTarget to CyLand Heightmap", UnsafeDuringActorConstruction = "true"), Category = Rendering)
	bool CyLandImportHeightmapFromRenderTarget(UTextureRenderTarget2D* InRenderTarget, bool InImportHeightFromRGChannel = false);

	/**
	* Output a landscape heightmap to a render target
	* @param InRenderTarget - Valid render target with a format of RTF_RGBA16f, RTF_RGBA32f or RTF_RGBA8
	* @param InExportHeightIntoRGChannel - Tell us if we should export the height that is internally stored as R & G (for 16 bits) to a single R channel of the render target (the format need to be RTF_RGBA16f or RTF_RGBA32f)
	*									   Note that using RTF_RGBA16f with InExportHeightIntoRGChannel == false, could have precision loss.
	*/
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "CyLand Export Heightmap to RenderTarget", Keywords = "Push CyLand Heightmap to RenderTarget", UnsafeDuringActorConstruction = "true"), Category = Rendering)
	bool CyLandExportHeightmapToRenderTarget(UTextureRenderTarget2D* InRenderTarget, bool InExportHeightIntoRGChannel = false);
	
	/**
	* Overwrites a landscape weightmap with render target data
	* Only works in the editor
	*/
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "CyLand Import Weightmap from RenderTarget", Keywords = "Push RenderTarget to CyLand Weightmap", UnsafeDuringActorConstruction = "true"), Category = Rendering)
	bool CyLandImportWeightmapFromRenderTarget(UTextureRenderTarget2D* InRenderTarget, FName InLayerName);

	
	/**
	* Output a landscape weightmap to a render target
	* Only works in the editor
	*/
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "CyLand Export Weightmap to RenderTarget", Keywords = "Push CyLand Weightmap to RenderTarget", UnsafeDuringActorConstruction = "true"), Category = Rendering)
	bool CyLandExportWeightmapToRenderTarget(UTextureRenderTarget2D* InRenderTarget, FName InLayerName);

	DECLARE_EVENT(ACyLand, FCyLandMaterialChangedDelegate);
	FCyLandMaterialChangedDelegate& OnMaterialChangedDelegate() { return CyLandMaterialChangedDelegate; }

protected:
	FCyLandMaterialChangedDelegate CyLandMaterialChangedDelegate;
	
	CYLAND_API void SetupProceduralLayers(int32 InNumComponentsX = INDEX_NONE, int32 InNumComponentsY = INDEX_NONE);
#endif
};
