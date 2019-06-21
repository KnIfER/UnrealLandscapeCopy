// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Misc/Guid.h"
#include "UObject/LazyObjectPtr.h"
#include "CyLandInfo.generated.h"

class ACyLand;
class ACyLandProxy;
class ACyLandStreamingProxy;
class UCyLandComponent;
class UCyLandLayerInfoObject;
class ULevel;
class UMaterialInstanceConstant;
struct FCyLandEditorLayerSettings;

/** Structure storing Collision for CyLandComponent Add */
#if WITH_EDITORONLY_DATA
struct FCyLandAddCollision
{
	FVector Corners[4];

	FCyLandAddCollision()
	{
		Corners[0] = Corners[1] = Corners[2] = Corners[3] = FVector::ZeroVector;
	}
};
#endif // WITH_EDITORONLY_DATA

USTRUCT()
struct FCyLandInfoLayerSettings
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	UCyLandLayerInfoObject* LayerInfoObj;

	UPROPERTY()
	FName LayerName;

#if WITH_EDITORONLY_DATA
	UPROPERTY(transient)
	UMaterialInstanceConstant* ThumbnailMIC;

	UPROPERTY()
	ACyLandProxy* Owner;

	UPROPERTY(transient)
	int32 DebugColorChannel;

	UPROPERTY(transient)
	uint32 bValid:1;
#endif // WITH_EDITORONLY_DATA

	FCyLandInfoLayerSettings()
		: LayerInfoObj(nullptr)
		, LayerName(NAME_None)
#if WITH_EDITORONLY_DATA
		, ThumbnailMIC(nullptr)
		, Owner(nullptr)
		, DebugColorChannel(0)
		, bValid(false)
#endif // WITH_EDITORONLY_DATA
	{
	}

	CYLAND_API FCyLandInfoLayerSettings(UCyLandLayerInfoObject* InLayerInfo, ACyLandProxy* InProxy);

	FCyLandInfoLayerSettings(FName InPlaceholderLayerName, ACyLandProxy* InProxy)
		: LayerInfoObj(nullptr)
		, LayerName(InPlaceholderLayerName)
#if WITH_EDITORONLY_DATA
		, ThumbnailMIC(nullptr)
		, Owner(InProxy)
		, DebugColorChannel(0)
		, bValid(false)
#endif
	{
	}

	CYLAND_API FName GetLayerName() const;

#if WITH_EDITORONLY_DATA
	CYLAND_API FCyLandEditorLayerSettings& GetEditorSettings() const;
#endif
};

UCLASS(Transient)
class UCyLandInfo : public UObject
{
	GENERATED_UCLASS_BODY()

	UPROPERTY()
	TLazyObjectPtr<ACyLand> CyLandActor;

	UPROPERTY()
	FGuid CyLandGuid;

	UPROPERTY()
	int32 ComponentSizeQuads;
	
	UPROPERTY()
	int32 SubsectionSizeQuads;

	UPROPERTY()
	int32 ComponentNumSubsections;
	
	UPROPERTY()
	FVector DrawScale;
	
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TArray<FCyLandInfoLayerSettings> Layers;
#endif // WITH_EDITORONLY_DATA

public:
	/** Map of the offsets (in component space) to the component. Valid in editor only. */
	TMap<FIntPoint, UCyLandComponent*> XYtoComponentMap;

#if WITH_EDITORONLY_DATA
	/** Lookup map used by the "add component" tool. Only available near valid CyLandComponents.
	    only for use by the "add component" tool. Todo - move into the tool? */
	TMap<FIntPoint, FCyLandAddCollision> XYtoAddCollisionMap;
#endif

	UPROPERTY()
	TSet<ACyLandStreamingProxy*> Proxies;

private:
	TSet<UCyLandComponent*> SelectedComponents;

	TSet<UCyLandComponent*> SelectedRegionComponents;

public:
	TMap<FIntPoint,float> SelectedRegion;

	//~ Begin UObject Interface.
	virtual void Serialize(FArchive& Ar) override;
	//~ End UObject Interface

#if WITH_EDITOR
	// @todo document 
	// all below.
	CYLAND_API void GetComponentsInRegion(int32 X1, int32 Y1, int32 X2, int32 Y2, TSet<UCyLandComponent*>& OutComponents, bool bOverlap = true) const;
	CYLAND_API bool GetCyLandExtent(int32& MinX, int32& MinY, int32& MaxX, int32& MaxY) const;
	CYLAND_API void ExportHeightmap(const FString& Filename);
	CYLAND_API void ExportLayer(UCyLandLayerInfoObject* LayerInfo, const FString& Filename);
	CYLAND_API bool ApplySplines(bool bOnlySelected);
	bool ApplySplinesInternal(bool bOnlySelected, ACyLandProxy* CyLand);

	CYLAND_API bool GetSelectedExtent(int32& MinX, int32& MinY, int32& MaxX, int32& MaxY) const;
	FVector GetCyLandCenterPos(float& LengthZ, int32 MinX = MAX_int32, int32 MinY = MAX_int32, int32 MaxX = MIN_int32, int32 MaxY = MIN_int32);
	CYLAND_API bool IsValidPosition(int32 X, int32 Y);
	CYLAND_API void DeleteLayer(UCyLandLayerInfoObject* LayerInfo, const FName& LayerName);
	CYLAND_API void ReplaceLayer(UCyLandLayerInfoObject* FromLayerInfo, UCyLandLayerInfoObject* ToLayerInfo);

	CYLAND_API void UpdateDebugColorMaterial();

	CYLAND_API TSet<UCyLandComponent*> GetSelectedComponents() const;
	CYLAND_API TSet<UCyLandComponent*> GetSelectedRegionComponents() const;
	CYLAND_API void UpdateSelectedComponents(TSet<UCyLandComponent*>& NewComponents, bool bIsComponentwise = true);
	CYLAND_API void SortSelectedComponents();
	CYLAND_API void ClearSelectedRegion(bool bIsComponentwise = true);

	// only for use by the "add component" tool. Todo - move into the tool?
	CYLAND_API void UpdateAllAddCollisions();
	CYLAND_API void UpdateAddCollision(FIntPoint CyLandKey);

	CYLAND_API FCyLandEditorLayerSettings& GetLayerEditorSettings(UCyLandLayerInfoObject* LayerInfo) const;
	CYLAND_API void CreateLayerEditorSettingsFor(UCyLandLayerInfoObject* LayerInfo);

	CYLAND_API UCyLandLayerInfoObject* GetLayerInfoByName(FName LayerName, ACyLandProxy* Owner = nullptr) const;
	CYLAND_API int32 GetLayerInfoIndex(FName LayerName, ACyLandProxy* Owner = nullptr) const;
	CYLAND_API int32 GetLayerInfoIndex(UCyLandLayerInfoObject* LayerInfo, ACyLandProxy* Owner = nullptr) const;
	CYLAND_API bool UpdateLayerInfoMap(ACyLandProxy* Proxy = nullptr, bool bInvalidate = false);

	/**
	 *  Returns the landscape proxy of this landscape info in the given level (if it exists)
	 *  @param  Level  Level to look in
	 *	@return        CyLand or landscape proxy found in the given level, or null if none
	 */
	CYLAND_API ACyLandProxy* GetCyLandProxyForLevel(ULevel* Level) const;

	/**
	 *  Returns landscape which is spawned in the current level that was previously added to this landscape info object
	 *  @param	bRegistered		Whether to consider only registered(visible) landscapes
	 *	@return					CyLand or landscape proxy found in the current level 
	 */
	CYLAND_API ACyLandProxy* GetCurrentLevelCyLandProxy(bool bRegistered) const;
	
	/** 
	 *	returns shared landscape or landscape proxy, mostly for transformations
	 *	@todo: should be removed
	 */
	CYLAND_API ACyLandProxy* GetCyLandProxy() const;

	/**
	 * Runs the given function on the root landscape actor and all streaming proxies
	 * Most easily used with a lambda as follows:
	 * ForAllCyLandProxies([](ACyLandProxy* Proxy)
	 * {
	 *     // Code
	 * });
	 */
	CYLAND_API void ForAllCyLandProxies(TFunctionRef<void(ACyLandProxy*)> Fn) const;

	/** Associates passed actor with this info object
 	 *  @param	Proxy		CyLand actor to register
	 *  @param  bMapCheck	Whether to warn about landscape errors
	 */
	CYLAND_API void RegisterActor(ACyLandProxy* Proxy, bool bMapCheck = false);
	
	/** Deassociates passed actor with this info object*/
	CYLAND_API void UnregisterActor(ACyLandProxy* Proxy);

	/** Associates passed landscape component with this info object
	 *  @param	Component	CyLand component to register
	 *  @param  bMapCheck	Whether to warn about landscape errors
	 */
	CYLAND_API void RegisterActorComponent(UCyLandComponent* Component, bool bMapCheck = false);
	
	/** Deassociates passed landscape component with this info object*/
	CYLAND_API void UnregisterActorComponent(UCyLandComponent* Component);

	/** Resets all actors, proxies, components registrations */
	CYLAND_API void Reset();

	/** Recreate all CyLandInfo objects in given world
	 *  @param  bMapCheck	Whether to warn about landscape errors
	 */
	CYLAND_API static void RecreateCyLandInfo(UWorld* InWorld, bool bMapCheck);

	/** 
	 *  Fixes up proxies relative position to landscape actor
	 *  basically makes sure that each CyLandProxy RootComponent transform reflects CyLandSectionOffset value
	 *  requires CyLandActor to be loaded
	 *  Does not work in World composition mode!
	 */
	CYLAND_API void FixupProxiesTransform();
	
	// Update per-component layer whitelists to include the currently painted layers
	CYLAND_API void UpdateComponentLayerWhitelist();

	CYLAND_API void RecreateCollisionComponents();

	CYLAND_API void RemoveXYOffsets();

	/** Postpones landscape textures baking, usually used during landscape painting to avoid hitches */
	CYLAND_API void PostponeTextureBaking();
#endif
};
