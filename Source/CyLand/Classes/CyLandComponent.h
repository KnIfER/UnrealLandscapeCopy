// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Guid.h"
#include "Engine/TextureStreamingTypes.h"
#include "Components/PrimitiveComponent.h"
#include "PerPlatformProperties.h"

#include "CyLandComponent.generated.h"

class ACyLand;
class ACyLandProxy;
class FLightingBuildOptions;
class FMaterialUpdateContext;
class FMeshMapBuildData;
class FPrimitiveSceneProxy;
class ITargetPlatform;
class UCyLandComponent;
class UCyLandGrassType;
class UCyLandHeightfieldCollisionComponent;
class UCyLandInfo;
class UCyLandLayerInfoObject;
class ULightComponent;
class UMaterialInstanceConstant;
class UMaterialInterface;
class UTexture2D;
struct FConvexVolume;
struct FEngineShowFlags;
struct FCyLandEditDataInterface;
struct FCyLandTextureDataInfo;
struct FStaticLightingPrimitiveInfo;

struct FCyLandEditDataInterface;
struct FCyLandMobileRenderData;

//
// FCyLandEditToolRenderData
//
USTRUCT()
struct FCyLandEditToolRenderData
{
public:
	GENERATED_USTRUCT_BODY()

	enum SelectionType
	{
		ST_NONE = 0,
		ST_COMPONENT = 1,
		ST_REGION = 2,
		// = 4...
	};

	FCyLandEditToolRenderData()
		: ToolMaterial(NULL),
		GizmoMaterial(NULL),
		SelectedType(ST_NONE),
		DebugChannelR(INDEX_NONE),
		DebugChannelG(INDEX_NONE),
		DebugChannelB(INDEX_NONE),
		DataTexture(NULL)
	{}

	// Material used to render the tool.
	UPROPERTY(NonTransactional)
	UMaterialInterface* ToolMaterial;

	// Material used to render the gizmo selection region...
	UPROPERTY(NonTransactional)
	UMaterialInterface* GizmoMaterial;

	// Component is selected
	UPROPERTY(NonTransactional)
	int32 SelectedType;

	UPROPERTY(NonTransactional)
	int32 DebugChannelR;

	UPROPERTY(NonTransactional)
	int32 DebugChannelG;

	UPROPERTY(NonTransactional)
	int32 DebugChannelB;

	UPROPERTY(NonTransactional)
	UTexture2D* DataTexture; // Data texture other than height/weight

#if WITH_EDITOR
	void UpdateDebugColorMaterial(const UCyLandComponent* const Component);
	void UpdateSelectionMaterial(int32 InSelectedType, const UCyLandComponent* const Component);
#endif
};

class FCyLandComponentDerivedData
{
	/** The compressed CyLand component data for mobile rendering. Serialized to disk. 
	    On device, freed once it has been decompressed. */
	TArray<uint8> CompressedCyLandData;
	
	/** Cached render data. Only valid on device. */
	TSharedPtr<FCyLandMobileRenderData, ESPMode::ThreadSafe > CachedRenderData;

public:
	/** Returns true if there is any valid platform data */
	bool HasValidPlatformData() const
	{
		return CompressedCyLandData.Num() != 0;
	}

	/** Returns true if there is any valid platform data */
	bool HasValidRuntimeData() const
	{
		return CompressedCyLandData.Num() != 0 || CachedRenderData.IsValid();
	}

	/** Returns the size of the platform data if there is any. */
	int32 GetPlatformDataSize() const
	{
		return CompressedCyLandData.Num();
	}

	/** Initializes the compressed data from an uncompressed source. */
	void InitializeFromUncompressedData(const TArray<uint8>& UncompressedData);

	/** Decompresses data if necessary and returns the render data object. 
     *  On device, this frees the compressed data and keeps a reference to the render data. */
	TSharedPtr<FCyLandMobileRenderData, ESPMode::ThreadSafe> GetRenderData();

	/** Constructs a key string for the DDC that uniquely identifies a the CyLand component's derived data. */
	static FString GetDDCKeyString(const FGuid& StateId);

	/** Loads the platform data from DDC */
	bool LoadFromDDC(const FGuid& StateId);

	/** Saves the compressed platform data to the DDC */
	void SaveToDDC(const FGuid& StateId);

	/* Serializer */
	friend FArchive& operator<<(FArchive& Ar, FCyLandComponentDerivedData& Data);
};

/* Used to uniquely reference a landscape vertex in a component, and generate a key suitable for a TMap. */
struct FCyLandVertexRef
{
	FCyLandVertexRef(int16 InX, int16 InY, int8 InSubX, int8 InSubY)
	: X(InX)
	, Y(InY)
	, SubX(InSubX)
	, SubY(InSubY)
	{}
	int16 X;
	int16 Y;
	int8 SubX;
	int8 SubY;

	uint64 MakeKey() const
	{
		// this is very bad for TMap
		//return (uint64)X << 32 | (uint64)Y << 16 | (uint64)SubX << 8 | (uint64)SubY;
		return HashCombine((uint32(X) << 8) | uint32(SubY), (uint32(SubX) << 24) | uint32(Y));
	}
};

/** Stores information about which weightmap texture and channel each layer is stored */
USTRUCT()
struct FCyWeightmapLayerAllocationInfo
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	UCyLandLayerInfoObject* LayerInfo;

	UPROPERTY()
	uint8 WeightmapTextureIndex;

	UPROPERTY()
	uint8 WeightmapTextureChannel;

	FCyWeightmapLayerAllocationInfo()
		: LayerInfo(nullptr)
		, WeightmapTextureIndex(0)
		, WeightmapTextureChannel(0)
	{
	}


	FCyWeightmapLayerAllocationInfo(UCyLandLayerInfoObject* InLayerInfo)
		:	LayerInfo(InLayerInfo)
		,	WeightmapTextureIndex(255)	// Indicates an invalid allocation
		,	WeightmapTextureChannel(255)
	{
	}
	
	FName GetLayerName() const;
};

struct FCyLandComponentGrassData
{
#if WITH_EDITORONLY_DATA
	// Variables used to detect when grass data needs to be regenerated:

	// Guid per material instance in the hierarchy between the assigned landscape material (instance) and the root UMaterial
	// used to detect changes to material instance parameters or the root material that could affect the grass maps
	TArray<FGuid, TInlineAllocator<2>> MaterialStateIds;
	// cached component rotation when material world-position-offset is used,
	// as this will affect the direction of world-position-offset deformation (included in the HeightData below)
	FQuat RotationForWPO;
#endif

	TArray<uint16> HeightData;
#if WITH_EDITORONLY_DATA
	// Height data for LODs 1+, keyed on LOD index
	TMap<int32, TArray<uint16>> HeightMipData;
#endif
	TMap<UCyLandGrassType*, TArray<uint8>> WeightData;

	FCyLandComponentGrassData() {}

#if WITH_EDITOR
	FCyLandComponentGrassData(UCyLandComponent* Component);
#endif

	bool HasData()
	{
		return HeightData.Num() > 0 ||
#if WITH_EDITORONLY_DATA
			HeightMipData.Num() > 0 ||
#endif
			WeightData.Num() > 0;
	}

	SIZE_T GetAllocatedSize() const;

	// Check whether we can discard any data not needed with current scalability settings
	void ConditionalDiscardDataOnLoad();

	friend FArchive& operator<<(FArchive& Ar, FCyLandComponentGrassData& Data);
};

USTRUCT(NotBlueprintable)
struct FCyLandComponentMaterialOverride
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = CyLandComponent)
	FPerPlatformInt LODIndex;

	UPROPERTY(EditAnywhere, Category = CyLandComponent)
	UMaterialInterface* Material;
};

UCLASS(hidecategories=(Display, Attachment, Physics, Debug, Collision, Movement, Rendering, PrimitiveComponent, Object, Transform, Mobility), showcategories=("Rendering|Material"), MinimalAPI, Within=CyLandProxy)
class UCyLandComponent : public UPrimitiveComponent
{
	GENERATED_UCLASS_BODY()
	
	/** X offset from global components grid origin (in quads) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=CyLandComponent)
	int32 SectionBaseX;

	/** Y offset from global components grid origin (in quads) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=CyLandComponent)
	int32 SectionBaseY;

	/** Total number of quads for this component, has to be >0 */
	UPROPERTY()
	int32 ComponentSizeQuads;

	/** Number of quads for a subsection of the component. SubsectionSizeQuads+1 must be a power of two. */
	UPROPERTY()
	int32 SubsectionSizeQuads;

	/** Number of subsections in X or Y axis */
	UPROPERTY()
	int32 NumSubsections;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=CyLandComponent)
	UMaterialInterface* OverrideMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=CyLandComponent, AdvancedDisplay)
	UMaterialInterface* OverrideHoleMaterial;

	UPROPERTY(EditAnywhere, Category = CyLandComponent)
	TArray<FCyLandComponentMaterialOverride> OverrideMaterials;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	UMaterialInstanceConstant* MaterialInstance_DEPRECATED;
#endif

	UPROPERTY(TextExportTransient)
	TArray<UMaterialInstanceConstant*> MaterialInstances;

	UPROPERTY(Transient, TextExportTransient)
	TArray<UMaterialInstanceDynamic*> MaterialInstancesDynamic;

	/** Mapping between LOD and Material Index*/
	UPROPERTY(TextExportTransient)
	TArray<int8> LODIndexToMaterialIndex;

	/** Mapping between Material Index to associated generated disabled Tessellation Material*/
	UPROPERTY(TextExportTransient)
	TArray<int8> MaterialIndexToDisabledTessellationMaterial;

	/** List of layers, and the weightmap and channel they are stored */
	UPROPERTY()
	TArray<FCyWeightmapLayerAllocationInfo> WeightmapLayerAllocations;

	/** Weightmap texture reference */
	UPROPERTY(TextExportTransient)
	TArray<UTexture2D*> WeightmapTextures;

	/** XYOffsetmap texture reference */
	UPROPERTY(TextExportTransient)
	UTexture2D* XYOffsetmapTexture;

	/** UV offset to component's weightmap data from component local coordinates*/
	UPROPERTY()
	FVector4 WeightmapScaleBias;

	/** U or V offset into the weightmap for the first subsection, in texture UV space */
	UPROPERTY()
	float WeightmapSubsectionOffset;

	/** UV offset to Heightmap data from component local coordinates */
	UPROPERTY()
	FVector4 HeightmapScaleBias;

	/** Cached local-space bounding box, created at heightmap update time */
	UPROPERTY()
	FBox CachedLocalBox;

	/** Reference to associated collision component */
	UPROPERTY()
	TLazyObjectPtr<UCyLandHeightfieldCollisionComponent> CollisionComponent;

private:
#if WITH_EDITORONLY_DATA
	/** Unique ID for this component, used for caching during distributed lighting */
	UPROPERTY()
	FGuid LightingGuid;

	/** Heightmap texture reference */
	UPROPERTY(Transient, TextExportTransient)
	UTexture2D* CurrentEditingHeightmapTexture;
#endif // WITH_EDITORONLY_DATA

	/** Heightmap texture reference */
	UPROPERTY(TextExportTransient)
	UTexture2D* HeightmapTexture;

public:

	/** Uniquely identifies this component's built map data. */
	UPROPERTY()
	FGuid MapBuildDataId;

	/**	Legacy irrelevant lights */
	UPROPERTY()
	TArray<FGuid> IrrelevantLights_DEPRECATED;

	/** Heightfield mipmap used to generate collision */
	UPROPERTY(EditAnywhere, Category=CyLandComponent)
	int32 CollisionMipLevel;

	/** Heightfield mipmap used to generate simple collision */
	UPROPERTY(EditAnywhere, Category=CyLandComponent)
	int32 SimpleCollisionMipLevel;

	/** Allows overriding the landscape bounds. This is useful if you distort the landscape with world-position-offset, for example
	 *  Extension value in the negative Z axis, positive value increases bound size */
	UPROPERTY(EditAnywhere, Category=CyLandComponent, meta=(EditCondition="bOverrideBounds"))
	float NegativeZBoundsExtension;

	/** Allows overriding the landscape bounds. This is useful if you distort the landscape with world-position-offset, for example
	 *  Extension value in the positive Z axis, positive value increases bound size */
	UPROPERTY(EditAnywhere, Category=CyLandComponent, meta=(EditCondition="bOverrideBounds"))
	float PositiveZBoundsExtension;

	/** StaticLightingResolution overriding per component, default value 0 means no overriding */
	UPROPERTY(EditAnywhere, Category=CyLandComponent, meta=(ClampMax = 4096))
	float StaticLightingResolution;

	/** Forced LOD level to use when rendering */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=CyLandComponent)
	int32 ForcedLOD;

	/** LOD level Bias to use when rendering */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=CyLandComponent)
	int32 LODBias;

	UPROPERTY()
	FGuid StateId;

	/** The Material Guid that used when baking, to detect material recompilations */
	UPROPERTY()
	FGuid BakedTextureMaterialGuid;

	/** Pre-baked Base Color texture for use by distance field GI */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = BakedTextures)
	UTexture2D* GIBakedBaseColorTexture;

#if WITH_EDITORONLY_DATA
	/** LOD level Bias to use when lighting buidling via lightmass, -1 Means automatic LOD calculation based on ForcedLOD + LODBias */
	UPROPERTY(EditAnywhere, Category=CyLandComponent)
	int32 LightingLODBias;

	// List of layers allowed to be painted on this component
	UPROPERTY(EditAnywhere, Category=CyLandComponent)
	TArray<UCyLandLayerInfoObject*> LayerWhitelist;

	/** Pointer to data shared with the render thread, used by the editor tools */
	UPROPERTY(Transient, DuplicateTransient, NonTransactional)
	FCyLandEditToolRenderData EditToolRenderData;

	/** Hash of source for ES2 generated data. Used determine if we need to re-generate ES2 pixel data. */
	UPROPERTY(DuplicateTransient)
	FGuid MobileDataSourceHash;

	/** Represent the chosen material for each LOD */
	UPROPERTY(DuplicateTransient)
	TMap<UMaterialInterface*, int8> MaterialPerLOD;
#endif

	/** For ES2 */
	UPROPERTY()
	uint8 MobileBlendableLayerMask;

	UPROPERTY(NonPIEDuplicateTransient)
	UMaterialInterface* MobileMaterialInterface_DEPRECATED;

	/** Material interfaces used for mobile */
	UPROPERTY(NonPIEDuplicateTransient)
	TArray<UMaterialInterface*> MobileMaterialInterfaces;

	/** Generated weightmap textures used for ES2. The first entry is also used for the normal map. 
	  * Serialized only when cooking or loading cooked builds. */
	UPROPERTY(NonPIEDuplicateTransient)
	TArray<UTexture2D*> MobileWeightmapTextures;

#if WITH_EDITORONLY_DATA
	/** Layer allocations used by mobile. Cached value here used only in the editor for usage visualization. */
	TArray<FCyWeightmapLayerAllocationInfo> MobileWeightmapLayerAllocations;

	/** The editor needs to save out the combination MIC we'll use for mobile, 
	  because we cannot generate it at runtime for standalone PIE games */
	UPROPERTY(NonPIEDuplicateTransient)
	TArray<UMaterialInstanceConstant*> MobileCombinationMaterialInstances;

	UPROPERTY(NonPIEDuplicateTransient)
	UMaterialInstanceConstant* MobileCombinationMaterialInstance_DEPRECATED;
#endif

public:
	/** Platform Data where don't support texture sampling in vertex buffer */
	FCyLandComponentDerivedData PlatformData;

	/** Grass data for generation **/
	TSharedRef<FCyLandComponentGrassData, ESPMode::ThreadSafe> GrassData;
	TArray<FBox> ActiveExcludedBoxes;
	uint32 ChangeTag;


	//~ Begin UObject Interface.	
	virtual void PostInitProperties() override;	
	virtual void Serialize(FArchive& Ar) override;
	virtual void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) override;
	virtual void BeginDestroy() override;
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
#if WITH_EDITOR
	virtual void BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform) override;
	virtual void PostLoad() override;
	virtual void PostEditUndo() override;
	virtual void PreEditChange(UProperty* PropertyThatWillChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject Interface

	CYLAND_API void UpdateEditToolRenderData();

	/** Fix up component layers, weightmaps
	 */
	CYLAND_API void FixupWeightmaps();

	// Update layer whitelist to include the currently painted layers
	CYLAND_API void UpdateLayerWhitelistFromPaintedLayers();
	
	//~ Begin UPrimitiveComponent Interface.
	virtual bool GetLightMapResolution( int32& Width, int32& Height ) const override;
	virtual int32 GetStaticLightMapResolution() const override;
	virtual void GetLightAndShadowMapMemoryUsage( int32& LightMapMemoryUsage, int32& ShadowMapMemoryUsage ) const override;
	virtual void GetStaticLightingInfo(FStaticLightingPrimitiveInfo& OutPrimitiveInfo,const TArray<ULightComponent*>& InRelevantLights,const FLightingBuildOptions& Options) override;
	virtual void AddMapBuildDataGUIDs(TSet<FGuid>& InGUIDs) const override;
#endif
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual ELightMapInteractionType GetStaticLightingType() const override { return LMIT_Texture;	}
	virtual void GetStreamingTextureInfo(FStreamingTextureLevelContext& LevelContext, TArray<FStreamingTexturePrimitiveInfo>& OutStreamingTextures) const override;
	virtual bool IsPrecomputedLightingValid() const override;

	CYLAND_API UTexture2D* GetHeightmap(bool InReturnCurrentEditingHeightmap = false) const;
	CYLAND_API void SetHeightmap(UTexture2D* NewHeightmap);
	CYLAND_API void SetCurrentEditingHeightmap(UTexture2D* InNewHeightmap);

#if WITH_EDITOR
	virtual int32 GetNumMaterials() const override;
	virtual UMaterialInterface* GetMaterial(int32 ElementIndex) const override;
	virtual void SetMaterial(int32 ElementIndex, UMaterialInterface* Material) override;
	virtual bool ComponentIsTouchingSelectionBox(const FBox& InSelBBox, const FEngineShowFlags& ShowFlags, const bool bConsiderOnlyBSP, const bool bMustEncompassEntireComponent) const override;
	virtual bool ComponentIsTouchingSelectionFrustum(const FConvexVolume& InFrustum, const FEngineShowFlags& ShowFlags, const bool bConsiderOnlyBSP, const bool bMustEncompassEntireComponent) const override;
	virtual void PreFeatureLevelChange(ERHIFeatureLevel::Type PendingFeatureLevel) override;
#endif
	//~ End UPrimitiveComponent Interface.

	//~ Begin USceneComponent Interface.
	virtual void DestroyComponent(bool bPromoteChildren = false) override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ End USceneComponent Interface.

	//~ Begin UActorComponent Interface.
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
#if WITH_EDITOR
	virtual void InvalidateLightingCacheDetailed(bool bInvalidateBuildEnqueuedLighting, bool bTranslationOnly) override;
#endif
	virtual void PropagateLightingScenarioChange() override;
	//~ End UActorComponent Interface.


#if WITH_EDITOR
	/** Gets the landscape info object for this landscape */
	CYLAND_API UCyLandInfo* GetCyLandInfo() const;

	/** Deletes a layer from this component, removing all its data */
	CYLAND_API void DeleteLayer(UCyLandLayerInfoObject* LayerInfo, FCyLandEditDataInterface& CyLandEdit);

	/** Fills a layer to 100% on this component, adding it if needed and removing other layers that get painted away */
	CYLAND_API void FillLayer(UCyLandLayerInfoObject* LayerInfo, FCyLandEditDataInterface& CyLandEdit);

	/** Replaces one layerinfo on this component with another */
	CYLAND_API void ReplaceLayer(UCyLandLayerInfoObject* FromLayerInfo, UCyLandLayerInfoObject* ToLayerInfo, FCyLandEditDataInterface& CyLandEdit);

	// true if the component's landscape material supports grass
	bool MaterialHasGrass() const;

	/** Creates and destroys cooked grass data stored in the map */
	void RenderGrassMap();
	void RemoveGrassMap();

	/* Could a grassmap currently be generated, disregarding whether our textures are streamed in? */
	bool CanRenderGrassMap() const;

	/* Are the textures we need to render a grassmap currently streamed in? */
	bool AreTexturesStreamedForGrassMapRender() const;

	/* Is the grassmap data outdated, eg by a material */
	bool IsGrassMapOutdated() const;

	/** Renders the heightmap of this component (including material world-position-offset) at the specified LOD */
	TArray<uint16> RenderWPOHeightmap(int32 LOD);

	/* Serialize all hashes/guids that record the current state of this component */
	void SerializeStateHashes(FArchive& Ar);

	// Generates mobile platform data for this component
	void GenerateMobileWeightmapLayerAllocations();
	void GeneratePlatformVertexData(const ITargetPlatform* TargetPlatform);
	void GeneratePlatformPixelData();

	/** Generate mobile data if it's missing or outdated */
	void CheckGenerateCyLandPlatformData(bool bIsCooking, const ITargetPlatform* TargetPlatform);
#endif

	CYLAND_API int32 GetMaterialInstanceCount(bool InDynamic = true) const;
	CYLAND_API class UMaterialInstance* GetMaterialInstance(int32 InIndex, bool InDynamic = true) const;

	/** Gets the landscape material instance dynamic for this component */
	UFUNCTION(BlueprintCallable, Category = "CyLand|Runtime|Material")
	class UMaterialInstanceDynamic* GetMaterialInstanceDynamic(int32 InIndex) const;

	/** Get the landscape actor associated with this component. */
	ACyLand* GetCyLandActor() const;

	/** Get the level in which the owning actor resides */
	ULevel* GetLevel() const;

#if WITH_EDITOR
	/** Returns all generated textures and material instances used by this component. */
	CYLAND_API void GetGeneratedTexturesAndMaterialInstances(TArray<UObject*>& OutTexturesAndMaterials) const;
#endif

	/** Gets the landscape proxy actor which owns this component */
	CYLAND_API ACyLandProxy* GetCyLandProxy() const;

	/** @return Component section base as FIntPoint */
	CYLAND_API FIntPoint GetSectionBase() const; 

	/** @param InSectionBase new section base for a component */
	CYLAND_API void SetSectionBase(FIntPoint InSectionBase);

	/** @todo document */
	const FGuid& GetLightingGuid() const
	{
#if WITH_EDITORONLY_DATA
		return LightingGuid;
#else
		static const FGuid NullGuid( 0, 0, 0, 0 );
		return NullGuid;
#endif // WITH_EDITORONLY_DATA
	}

	/** @todo document */
	void SetLightingGuid()
	{
#if WITH_EDITORONLY_DATA
		LightingGuid = FGuid::NewGuid();
#endif // WITH_EDITORONLY_DATA
	}

	FGuid GetMapBuildDataId() const
	{
		return MapBuildDataId;
	}

	CYLAND_API const FMeshMapBuildData* GetMeshMapBuildData() const;

#if WITH_EDITOR
	/** Initialize the landscape component */
	CYLAND_API void Init(int32 InBaseX,int32 InBaseY,int32 InComponentSizeQuads, int32 InNumSubsections,int32 InSubsectionSizeQuads);

	/**
	 * Recalculate cached bounds using height values.
	 */
	CYLAND_API void UpdateCachedBounds();

	/**
	 * Update the MaterialInstance parameters to match the layer and weightmaps for this component
	 * Creates the MaterialInstance if it doesn't exist.
	 */
	CYLAND_API void UpdateMaterialInstances();

	// Internal implementation of UpdateMaterialInstances, not safe to call directly
	void UpdateMaterialInstances_Internal(FMaterialUpdateContext& Context);

	/** Helper function for UpdateMaterialInstance to get Material without set parameters */
	UMaterialInstanceConstant* GetCombinationMaterial(FMaterialUpdateContext* InMaterialUpdateContext, const TArray<FCyWeightmapLayerAllocationInfo>& Allocations, int8 InLODIndex, bool bMobile = false) const;
	/**
	 * Generate mipmaps for height and tangent data.
	 * @param HeightmapTextureMipData - array of pointers to the locked mip data.
	 *           This should only include the mips that are generated directly from this component's data
	 *           ie where each subsection has at least 2 vertices.
	* @param ComponentX1 - region of texture to update in component space, MAX_int32 meant end of X component in ACyLand::Import()
	* @param ComponentY1 - region of texture to update in component space, MAX_int32 meant end of Y component in ACyLand::Import()
	* @param ComponentX2 (optional) - region of texture to update in component space
	* @param ComponentY2 (optional) - region of texture to update in component space
	* @param TextureDataInfo - FCyLandTextureDataInfo pointer, to notify of the mip data region updated.
	 */
	void GenerateHeightmapMips(TArray<FColor*>& HeightmapTextureMipData, int32 ComponentX1=0, int32 ComponentY1=0, int32 ComponentX2=MAX_int32, int32 ComponentY2=MAX_int32, FCyLandTextureDataInfo* TextureDataInfo=nullptr);

	/**
	 * Generate empty mipmaps for weightmap
	 */
	CYLAND_API static void CreateEmptyTextureMips(UTexture2D* Texture, bool bClear = false);

	/**
	 * Generate mipmaps for weightmap
	 * Assumes all weightmaps are unique to this component.
	 * @param WeightmapTextureBaseMipData: array of pointers to each of the weightmaps' base locked mip data.
	 */
	template<typename DataType>

	/** @todo document */
	static void GenerateMipsTempl(int32 InNumSubsections, int32 InSubsectionSizeQuads, UTexture2D* WeightmapTexture, DataType* BaseMipData);

	/** @todo document */
	static void GenerateWeightmapMips(int32 InNumSubsections, int32 InSubsectionSizeQuads, UTexture2D* WeightmapTexture, FColor* BaseMipData);

	/**
	 * Update mipmaps for existing weightmap texture
	 */
	template<typename DataType>

	/** @todo document */
	static void UpdateMipsTempl(int32 InNumSubsections, int32 InSubsectionSizeQuads, UTexture2D* WeightmapTexture, TArray<DataType*>& WeightmapTextureMipData, int32 ComponentX1=0, int32 ComponentY1=0, int32 ComponentX2=MAX_int32, int32 ComponentY2=MAX_int32, FCyLandTextureDataInfo* TextureDataInfo=nullptr);

	/** @todo document */
	CYLAND_API static void UpdateWeightmapMips(int32 InNumSubsections, int32 InSubsectionSizeQuads, UTexture2D* WeightmapTexture, TArray<FColor*>& WeightmapTextureMipData, int32 ComponentX1=0, int32 ComponentY1=0, int32 ComponentX2=MAX_int32, int32 ComponentY2=MAX_int32, FCyLandTextureDataInfo* TextureDataInfo=nullptr);

	/** @todo document */
	static void UpdateDataMips(int32 InNumSubsections, int32 InSubsectionSizeQuads, UTexture2D* Texture, TArray<uint8*>& TextureMipData, int32 ComponentX1=0, int32 ComponentY1=0, int32 ComponentX2=MAX_int32, int32 ComponentY2=MAX_int32, FCyLandTextureDataInfo* TextureDataInfo=nullptr);

	/**
	 * Create or updates collision component height data
	 * @param HeightmapTextureMipData: heightmap data
	 * @param ComponentX1, ComponentY1, ComponentX2, ComponentY2: region to update
	 * @param bUpdateBounds: Whether to update bounds from render component.
	 * @param XYOffsetTextureMipData: xy-offset map data
	 */
	void UpdateCollisionHeightData(const FColor* HeightmapTextureMipData, const FColor* SimpleCollisionHeightmapTextureData, int32 ComponentX1=0, int32 ComponentY1=0, int32 ComponentX2=MAX_int32, int32 ComponentY2=MAX_int32, bool bUpdateBounds=false, const FColor* XYOffsetTextureMipData=nullptr);

	/** Updates collision component height data for the entire component, locking and unlocking heightmap textures
	 * @param: bRebuild: If true, recreates the collision component */
	void UpdateCollisionData(bool bRebuild);

	/**
	 * Update collision component dominant layer data
	 * @param WeightmapTextureMipData: weightmap data
	 * @param ComponentX1, ComponentY1, ComponentX2, ComponentY2: region to update
	 * @param Whether to update bounds from render component.
	 */
	void UpdateCollisionLayerData(const FColor* const* WeightmapTextureMipData, const FColor* const* const SimpleCollisionWeightmapTextureMipData, int32 ComponentX1=0, int32 ComponentY1=0, int32 ComponentX2=MAX_int32, int32 ComponentY2=MAX_int32);

	/**
	 * Update collision component dominant layer data for the whole component, locking and unlocking the weightmap textures.
	 */
	CYLAND_API void UpdateCollisionLayerData();

	/**
	 * Create weightmaps for this component for the layers specified in the WeightmapLayerAllocations array
	 */
	void ReallocateWeightmaps(FCyLandEditDataInterface* DataInterface=NULL);

	/** Returns the actor's CyLandMaterial, or the Component's OverrideCyLandMaterial if set */
	CYLAND_API UMaterialInterface* GetCyLandMaterial(int8 InLODIndex = INDEX_NONE) const;

	/** Returns the actor's CyLandHoleMaterial, or the Component's OverrideCyLandHoleMaterial if set */
	CYLAND_API UMaterialInterface* GetCyLandHoleMaterial() const;

	/** Returns true if this component has visibility painted */
	CYLAND_API bool ComponentHasVisibilityPainted() const;

	/**
	 * Generate a key for a component's layer allocations to use with MaterialInstanceConstantMap.
	 */
	static FString GetLayerAllocationKey(const TArray<FCyWeightmapLayerAllocationInfo>& Allocations, UMaterialInterface* CyLandMaterial, bool bMobile = false);

	/** @todo document */
	void GetLayerDebugColorKey(int32& R, int32& G, int32& B) const;

	/** @todo document */
	void RemoveInvalidWeightmaps();

	/** @todo document */
	virtual void ExportCustomProperties(FOutputDevice& Out, uint32 Indent) override;

	/** @todo document */
	virtual void ImportCustomProperties(const TCHAR* SourceText, FFeedbackContext* Warn) override;

	/** @todo document */
	CYLAND_API void InitHeightmapData(TArray<FColor>& Heights, bool bUpdateCollision);

	/** @todo document */
	CYLAND_API void InitWeightmapData(TArray<UCyLandLayerInfoObject*>& LayerInfos, TArray<TArray<uint8> >& Weights);

	/** @todo document */
	CYLAND_API float GetLayerWeightAtLocation( const FVector& InLocation, UCyLandLayerInfoObject* LayerInfo, TArray<uint8>* LayerCache=NULL );

	/** Extends passed region with this component section size */
	CYLAND_API void GetComponentExtent(int32& MinX, int32& MinY, int32& MaxX, int32& MaxY) const;

	/** Updates navigation properties to match landscape's master switch */
	void UpdateNavigationRelevance();
	
	/** Updates the values of component-level properties exposed by the CyLand Actor */
	CYLAND_API void UpdatedSharedPropertiesFromActor();
#endif

	friend class FCyLandComponentSceneProxy;
	friend struct FCyLandComponentDataInterface;

	void SetLOD(bool bForced, int32 InLODValue);

protected:

	/** Whether the component type supports static lighting. */
	virtual bool SupportsStaticLighting() const override
	{
		return true;
	}
};

