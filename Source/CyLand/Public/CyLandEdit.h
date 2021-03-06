// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
CyLandEdit.h: Classes for the editor to access to CyLand data
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "CyLandProxy.h"
#include "Engine/Texture2D.h"
#include "AI/NavigationSystemBase.h"
#include "Components/ActorComponent.h"
#include "CyLandInfo.h"
#include "CyLandComponent.h"
#include "CyLandHeightfieldCollisionComponent.h"
#include "InstancedFoliageActor.h"
#include "CyLandLayerInfoObject.h"

#if WITH_EDITOR
	#include "Containers/ArrayView.h"
#endif

class UCyLandComponent;
class UCyLandInfo;
class UCyLandLayerInfoObject;

#define MAX_LANDSCAPE_LOD_DISTANCE_FACTOR 10.f

#if WITH_EDITOR

struct FCyLandTextureDataInfo
{
	struct FMipInfo
	{
		void* MipData;
		TArray<FUpdateTextureRegion2D> MipUpdateRegions;
	};

	FCyLandTextureDataInfo(UTexture2D* InTexture);
	virtual ~FCyLandTextureDataInfo();

	// returns true if we need to block on the render thread before unlocking the mip data
	bool UpdateTextureData();

	int32 NumMips() { return MipInfo.Num(); }

	void AddMipUpdateRegion(int32 MipNum, int32 InX1, int32 InY1, int32 InX2, int32 InY2)
	{
		check( MipNum < MipInfo.Num() );
		new(MipInfo[MipNum].MipUpdateRegions) FUpdateTextureRegion2D(InX1, InY1, InX1, InY1, 1+InX2-InX1, 1+InY2-InY1);
	}

	void* GetMipData(int32 MipNum)
	{
		check( MipNum < MipInfo.Num() );
		if( !MipInfo[MipNum].MipData )
		{
			MipInfo[MipNum].MipData = Texture->Source.LockMip(MipNum);
		}
		return MipInfo[MipNum].MipData;
	}

	int32 GetMipSizeX(int32 MipNum)
	{
		return FMath::Max(Texture->Source.GetSizeX() >> MipNum, 1);
	}

	int32 GetMipSizeY(int32 MipNum)
	{
		return FMath::Max(Texture->Source.GetSizeY() >> MipNum, 1);
	}

private:
	UTexture2D* Texture;
	TArray<FMipInfo> MipInfo;
};

struct CYLAND_API FCyLandTextureDataInterface
{
	// tor
	virtual ~FCyLandTextureDataInterface();

	// Texture data access
	FCyLandTextureDataInfo* GetTextureDataInfo(UTexture2D* Texture);

	// Flush texture updates
	void Flush();

	// Texture bulk operations for weightmap reallocation
	void CopyTextureChannel(UTexture2D* Dest, int32 DestChannel, UTexture2D* Src, int32 SrcChannel);
	void ZeroTextureChannel(UTexture2D* Dest, int32 DestChannel);
	void CopyTextureFromHeightmap(UTexture2D* Dest, int32 DestChannel, UCyLandComponent* Comp, int32 SrcChannel);
	void CopyTextureFromWeightmap(UTexture2D* Dest, int32 DestChannel, UCyLandComponent* Comp, UCyLandLayerInfoObject* LayerInfo);

	template<typename TData>
	void SetTextureValueTempl(UTexture2D* Dest, TData Value);
	void ZeroTexture(UTexture2D* Dest);
	void SetTextureValue(UTexture2D* Dest, FColor Value);

	template<typename TData>
	bool EqualTextureValueTempl(UTexture2D* Src, TData Value);
	bool EqualTextureValue(UTexture2D* Src, FColor Value);

private:
	TMap<UTexture2D*, FCyLandTextureDataInfo*> TextureDataMap;
};


struct CYLAND_API FCyLandEditDataInterface : public FCyLandTextureDataInterface
{
	// tor
	FCyLandEditDataInterface(UCyLandInfo* InCyLand);

	// Misc
	bool GetComponentsInRegion(int32 X1, int32 Y1, int32 X2, int32 Y2, TSet<UCyLandComponent*>* OutComponents = NULL);

	//
	// Heightmap access
	//
	void SetHeightData(int32 X1, int32 Y1, int32 X2, int32 Y2, const uint16* InData, int32 InStride, bool InCalcNormals, const uint16* InNormalData = nullptr, bool InCreateComponents = false, UTexture2D* InHeightmap = nullptr, UTexture2D* InXYOffsetmapTexture = nullptr,
					   bool InUpdateBounds = true, bool InUpdateCollision = true, bool InGenerateMips = true);

	// Helper accessor
	FORCEINLINE uint16 GetHeightMapData(const UCyLandComponent* Component, int32 TexU, int32 TexV, FColor* TextureData = NULL);
	// Generic
	template<typename TStoreData>
	void GetHeightDataTempl(int32& X1, int32& Y1, int32& X2, int32& Y2, TStoreData& StoreData);
	// Without data interpolation, able to get normal data
	template<typename TStoreData>
	void GetHeightDataTemplFast(const int32 X1, const int32 Y1, const int32 X2, const int32 Y2, TStoreData& StoreData, UTexture2D* InHeightmap = nullptr, TStoreData* NormalData = NULL);
	// Implementation for fixed array
	void GetHeightData(int32& X1, int32& Y1, int32& X2, int32& Y2, uint16* Data, int32 Stride);
	void GetHeightDataFast(const int32 X1, const int32 Y1, const int32 X2, const int32 Y2, uint16* Data, int32 Stride, uint16* NormalData = NULL, UTexture2D* InHeightmap = nullptr);
	// Implementation for sparse array
	void GetHeightData(int32& X1, int32& Y1, int32& X2, int32& Y2, TMap<FIntPoint, uint16>& SparseData);
	void GetHeightDataFast(const int32 X1, const int32 Y1, const int32 X2, const int32 Y2, TMap<FIntPoint, uint16>& SparseData, TMap<FIntPoint, uint16>* NormalData = NULL, UTexture2D* InHeightmap = nullptr);

	// Recaclulate normals for the entire landscape.
	void RecalculateNormals();

	//
	// Weightmap access
	//
	// Helper accessor
	FORCEINLINE uint8 GetWeightMapData(const UCyLandComponent* Component, UCyLandLayerInfoObject* LayerInfo, int32 TexU, int32 TexV, uint8 Offset = 0, UTexture2D* Texture = NULL, uint8* TextureData = NULL);
	template<typename TStoreData>
	void GetWeightDataTempl(UCyLandLayerInfoObject* LayerInfo, int32& X1, int32& Y1, int32& X2, int32& Y2, TStoreData& StoreData);
	// Without data interpolation
	template<typename TStoreData>
	void GetWeightDataTemplFast(UCyLandLayerInfoObject* LayerInfo, const int32 X1, const int32 Y1, const int32 X2, const int32 Y2, TStoreData& StoreData);
	// Implementation for fixed array
	void GetWeightData(UCyLandLayerInfoObject* LayerInfo, int32& X1, int32& Y1, int32& X2, int32& Y2, uint8* Data, int32 Stride);
	//void GetWeightData(FName LayerName, int32& X1, int32& Y1, int32& X2, int32& Y2, TArray<uint8>* Data, int32 Stride);
	void GetWeightDataFast(UCyLandLayerInfoObject* LayerInfo, const int32 X1, const int32 Y1, const int32 X2, const int32 Y2, uint8* Data, int32 Stride);
	void GetWeightDataFast(UCyLandLayerInfoObject* LayerInfo, const int32 X1, const int32 Y1, const int32 X2, const int32 Y2, TArray<uint8>* Data, int32 Stride);
	// Implementation for sparse array
	void GetWeightData(UCyLandLayerInfoObject* LayerInfo, int32& X1, int32& Y1, int32& X2, int32& Y2, TMap<FIntPoint, uint8>& SparseData);
	//void GetWeightData(FName LayerName, int32& X1, int32& Y1, int32& X2, int32& Y2, TMap<uint64, TArray<uint8>>& SparseData);
	void GetWeightDataFast(UCyLandLayerInfoObject* LayerInfo, const int32 X1, const int32 Y1, const int32 X2, const int32 Y2, TMap<FIntPoint, uint8>& SparseData);
	void GetWeightDataFast(UCyLandLayerInfoObject* LayerInfo, const int32 X1, const int32 Y1, const int32 X2, const int32 Y2, TMap<FIntPoint, TArray<uint8>>& SparseData);
	// Updates weightmap for LayerInfo, optionally adjusting all other weightmaps.
	void SetAlphaData(UCyLandLayerInfoObject* LayerInfo, const int32 X1, const int32 Y1, const int32 X2, const int32 Y2, const uint8* Data, int32 Stride, ECyLandLayerPaintingRestriction PaintingRestriction = ECyLandLayerPaintingRestriction::None, bool bWeightAdjust = true, bool bTotalWeightAdjust = false);
	// Updates weightmaps for all layers. Data points to packed data for all layers in the landscape info
	void SetAlphaData(const TSet<UCyLandLayerInfoObject*>& DirtyLayerInfos, const int32 X1, const int32 Y1, const int32 X2, const int32 Y2, const uint8* Data, int32 Stride, ECyLandLayerPaintingRestriction PaintingRestriction = ECyLandLayerPaintingRestriction::None);
	// Delete a layer and re-normalize other layers
	void DeleteLayer(UCyLandLayerInfoObject* LayerInfo);
	// Fill a layer and re-normalize other layers
	void FillLayer(UCyLandLayerInfoObject* LayerInfo);
	// Fill all empty layers and re-normalize layers
	void FillEmptyLayers(UCyLandLayerInfoObject* LayerInfo);
	// Replace/merge a layer
	void ReplaceLayer(UCyLandLayerInfoObject* FromLayerInfo, UCyLandLayerInfoObject* ToLayerInfo);

	// Without data interpolation, Select Data 
	template<typename TStoreData>
	void GetSelectDataTempl(const int32 X1, const int32 Y1, const int32 X2, const int32 Y2, TStoreData& StoreData);
	void GetSelectData(const int32 X1, const int32 Y1, const int32 X2, const int32 Y2, uint8* Data, int32 Stride);
	void GetSelectData(const int32 X1, const int32 Y1, const int32 X2, const int32 Y2, TMap<FIntPoint, uint8>& SparseData);
	void SetSelectData(const int32 X1, const int32 Y1, const int32 X2, const int32 Y2, const uint8* Data, int32 Stride);

	//
	// XYOffsetmap access
	//
	template<typename T>
	void SetXYOffsetDataTempl(int32 X1, int32 Y1, int32 X2, int32 Y2, const T* Data, int32 Stride);
	void SetXYOffsetData(int32 X1, int32 Y1, int32 X2, int32 Y2, const FVector2D* Data, int32 Stride);
	void SetXYOffsetData(int32 X1, int32 Y1, int32 X2, int32 Y2, const FVector* Data, int32 Stride);
	// Helper accessor
	FORCEINLINE FVector2D GetXYOffsetmapData(const UCyLandComponent* Component, int32 TexU, int32 TexV, FColor* TextureData = NULL);

	template<typename TStoreData>
	void GetXYOffsetDataTempl(int32& X1, int32& Y1, int32& X2, int32& Y2, TStoreData& StoreData);
	void GetXYOffsetData(int32& X1, int32& Y1, int32& X2, int32& Y2, FVector2D* Data, int32 Stride);
	void GetXYOffsetData(int32& X1, int32& Y1, int32& X2, int32& Y2, TMap<FIntPoint, FVector2D>& SparseData);
	void GetXYOffsetData(int32& X1, int32& Y1, int32& X2, int32& Y2, FVector* Data, int32 Stride);
	void GetXYOffsetData(int32& X1, int32& Y1, int32& X2, int32& Y2, TMap<FIntPoint, FVector>& SparseData);
	// Without data interpolation
	template<typename TStoreData>
	void GetXYOffsetDataTemplFast(const int32 X1, const int32 Y1, const int32 X2, const int32 Y2, TStoreData& StoreData);
	// Without data interpolation, able to get normal data
	void GetXYOffsetDataFast(const int32 X1, const int32 Y1, const int32 X2, const int32 Y2, FVector2D* Data, int32 Stride);
	void GetXYOffsetDataFast(const int32 X1, const int32 Y1, const int32 X2, const int32 Y2, TMap<FIntPoint, FVector2D>& SparseData);
	void GetXYOffsetDataFast(const int32 X1, const int32 Y1, const int32 X2, const int32 Y2, FVector* Data, int32 Stride);
	void GetXYOffsetDataFast(const int32 X1, const int32 Y1, const int32 X2, const int32 Y2, TMap<FIntPoint, FVector>& SparseData);

	template<typename T>
	static void ShrinkData(TArray<T>& Data, int32 OldMinX, int32 OldMinY, int32 OldMaxX, int32 OldMaxY, int32 NewMinX, int32 NewMinY, int32 NewMaxX, int32 NewMaxY);

private:
	int32 ComponentSizeQuads;
	int32 SubsectionSizeQuads;
	int32 ComponentNumSubsections;
	FVector DrawScale;

	UCyLandInfo* CyLandInfo;

	// Only for Missing Data interpolation... only internal usage
	template<typename TData, typename TStoreData, typename FType>
	FORCEINLINE void CalcMissingValues(const int32& X1, const int32& X2, const int32& Y1, const int32& Y2,
		const int32& ComponentIndexX1, const int32& ComponentIndexX2, const int32& ComponentIndexY1, const int32& ComponentIndexY2,
		const int32& ComponentSizeX, const int32& ComponentSizeY, TData* CornerValues,
		TArray<bool>& NoBorderY1, TArray<bool>& NoBorderY2, TArray<bool>& ComponentDataExist, TStoreData& StoreData);

	// test if layer is whitelisted for a given texel
	inline bool IsWhitelisted(const UCyLandLayerInfoObject* LayerInfo,
	                          int32 ComponentIndexX, int32 SubIndexX, int32 SubX,
	                          int32 ComponentIndexY, int32 SubIndexY, int32 SubY);

	// counts the total influence of each weight-blended layer on this component
	inline TMap<const UCyLandLayerInfoObject*, uint32> CountWeightBlendedLayerInfluence(int32 ComponentIndexX, int32 ComponentIndexY, TOptional<TArrayView<const uint8* const>> LayerDataPtrs);

	// chooses a replacement layer to use when erasing from 100% influence on a texel
	const UCyLandLayerInfoObject* ChooseReplacementLayer(const UCyLandLayerInfoObject* LayerInfo, int32 ComponentIndexX, int32 SubIndexX, int32 SubX, int32 ComponentIndexY, int32 SubIndexY, int32 SubY, TMap<FIntPoint, TMap<const UCyLandLayerInfoObject*, uint32>>& LayerInfluenceCache, TArrayView<const uint8* const> LayerDataPtrs);
};

template<typename T>
void FCyLandEditDataInterface::ShrinkData(TArray<T>& Data, int32 OldMinX, int32 OldMinY, int32 OldMaxX, int32 OldMaxY, int32 NewMinX, int32 NewMinY, int32 NewMaxX, int32 NewMaxY)
{
	checkSlow(OldMinX <= OldMaxX && OldMinY <= OldMaxY);
	checkSlow(NewMinX >= OldMinX && NewMaxX <= OldMaxX);
	checkSlow(NewMinY >= OldMinY && NewMaxY <= OldMaxY);

	if (NewMinX != OldMinX || NewMinY != OldMinY ||
		NewMaxX != OldMaxX || NewMaxY != OldMaxY)
	{
		// if only the MaxY changes we don't need to do the moving, only the truncate
		if (NewMinX != OldMinX || NewMinY != OldMinY || NewMaxX != OldMaxX)
		{
			for (int32 DestY = 0, SrcY = NewMinY - OldMinY; DestY <= NewMaxY - NewMinY; DestY++, SrcY++)
			{
//				UE_LOG(LogCyLand, Warning, TEXT("Dest: %d, %d = %d Src: %d, %d = %d Width = %d"), 0, DestY, DestY * (1 + NewMaxX - NewMinX), NewMinX - OldMinX, SrcY, SrcY * (1 + OldMaxX - OldMinX) + NewMinX - OldMinX, (1 + NewMaxX - NewMinX));
				T* DestData = &Data[DestY * (1 + NewMaxX - NewMinX)];
				const T* SrcData = &Data[SrcY * (1 + OldMaxX - OldMinX) + NewMinX - OldMinX];
				FMemory::Memmove(DestData, SrcData, (1 + NewMaxX - NewMinX) * sizeof(T));
			}
		}

		const int32 NewSize = (1 + NewMaxY - NewMinY) * (1 + NewMaxX - NewMinX);
		Data.RemoveAt(NewSize, Data.Num() - NewSize);
	}
}

//
// FHeightmapAccessor
//
template<bool bInUseInterp>
struct FHeightmapAccessor
{
	enum { bUseInterp = bInUseInterp };
	FHeightmapAccessor(UCyLandInfo* InCyLandInfo)
	{
		CyLandInfo = InCyLandInfo;
		CyLandEdit = new FCyLandEditDataInterface(InCyLandInfo);
	}

	// accessors
	void GetData(int32& X1, int32& Y1, int32& X2, int32& Y2, TMap<FIntPoint, uint16>& Data)
	{
		CyLandEdit->GetHeightData(X1, Y1, X2, Y2, Data);
	}

	void GetDataFast(int32 X1, int32 Y1, int32 X2, int32 Y2, TMap<FIntPoint, uint16>& Data)
	{
		CyLandEdit->GetHeightDataFast(X1, Y1, X2, Y2, Data);
	}
	void SetData(const ACyLand& land, int32 X1, int32 Y1, int32 X2, int32 Y2, const uint16* Data, ECyLandLayerPaintingRestriction PaintingRestriction = ECyLandLayerPaintingRestriction::None);

	void SetData(int32 X1, int32 Y1, int32 X2, int32 Y2, const uint16* Data, ECyLandLayerPaintingRestriction PaintingRestriction = ECyLandLayerPaintingRestriction::None)
	{
		TSet<UCyLandComponent*> Components;
		if (CyLandInfo && CyLandEdit->GetComponentsInRegion(X1, Y1, X2, Y2, &Components))
		{
			// Update data
			ChangedComponents.Append(Components);

			for (UCyLandComponent* Component : Components)
			{
				Component->InvalidateLightingCache();
			}

			// Flush dynamic foliage (grass)
			ACyLandProxy::InvalidateGeneratedComponentData(Components);

			// Notify foliage to move any attached instances
			bool bUpdateFoliage = false;
			for (UCyLandComponent* Component : Components)
			{
				UCyLandHeightfieldCollisionComponent* CollisionComponent = Component->CollisionComponent.Get();
				if (CollisionComponent && AInstancedFoliageActor::HasFoliageAttached(CollisionComponent))
				{
					bUpdateFoliage = true;
					break;
				}
			}

			if (bUpdateFoliage)
			{
				// Calculate landscape local-space bounding box of old data, to look for foliage instances.
				TArray<UCyLandHeightfieldCollisionComponent*> CollisionComponents;
				CollisionComponents.Empty(Components.Num());
				TArray<FBox> PreUpdateLocalBoxes;
				PreUpdateLocalBoxes.Empty(Components.Num());

				for (UCyLandComponent* Component : Components)
				{
					UCyLandHeightfieldCollisionComponent* CollisionComponent = Component->CollisionComponent.Get();
					if (CollisionComponent)
					{
						CollisionComponents.Add(CollisionComponent);
						PreUpdateLocalBoxes.Add(FBox(FVector((float)X1, (float)Y1, Component->CachedLocalBox.Min.Z), FVector((float)X2, (float)Y2, Component->CachedLocalBox.Max.Z)));
					}
				}

				// Update landscape.
				CyLandEdit->SetHeightData(X1, Y1, X2, Y2, Data, 0, true);

				// Snap foliage for each component.
				for (int32 Index = 0; Index < CollisionComponents.Num(); ++Index)
				{
					UCyLandHeightfieldCollisionComponent* CollisionComponent = CollisionComponents[Index];
					CollisionComponent->SnapFoliageInstances(PreUpdateLocalBoxes[Index].TransformBy(CyLandInfo->GetCyLandProxy()->CyLandActorToWorld().ToMatrixWithScale()).ExpandBy(1.0f));
				}
			}
			else
			{
				// No foliage, just update landscape.
				CyLandEdit->SetHeightData(X1, Y1, X2, Y2, Data, 0, true);
			}
		}
	}

	void Flush()
	{
		CyLandEdit->Flush();
	}

	virtual ~FHeightmapAccessor()
	{
		delete CyLandEdit;
		CyLandEdit = NULL;

		// Update the bounds and navmesh for the components we edited
		for (TSet<UCyLandComponent*>::TConstIterator It(ChangedComponents); It; ++It)
		{
			(*It)->UpdateCachedBounds();
			(*It)->UpdateComponentToWorld();

			// Recreate collision for modified components to update the physical materials
			UCyLandHeightfieldCollisionComponent* CollisionComponent = (*It)->CollisionComponent.Get();
			if (CollisionComponent)
			{
				CollisionComponent->RecreateCollision();
				FNavigationSystem::UpdateComponentData(*CollisionComponent);
			}
		}
	}

private:
	UCyLandInfo* CyLandInfo;
	FCyLandEditDataInterface* CyLandEdit;
	TSet<UCyLandComponent*> ChangedComponents;
};

//
// FAlphamapAccessor
//
template<bool bInUseInterp, bool bInUseTotalNormalize>
struct FAlphamapAccessor
{
	enum { bUseInterp = bInUseInterp };
	enum { bUseTotalNormalize = bInUseTotalNormalize };
	FAlphamapAccessor(UCyLandInfo* InCyLandInfo, UCyLandLayerInfoObject* InLayerInfo)
		: CyLandInfo(InCyLandInfo)
		, CyLandEdit(InCyLandInfo)
		, LayerInfo(InLayerInfo)
		, bBlendWeight(true)
	{
		// should be no Layer change during FAlphamapAccessor lifetime...
		if (InCyLandInfo && InLayerInfo)
		{
			if (LayerInfo == ACyLandProxy::VisibilityLayer)
			{
				bBlendWeight = false;
			}
			else
			{
				bBlendWeight = !LayerInfo->bNoWeightBlend;
			}
		}
	}

	~FAlphamapAccessor()
	{
		// Recreate collision for modified components to update the physical materials
		for (UCyLandComponent* Component : ModifiedComponents)
		{
			UCyLandHeightfieldCollisionComponent* CollisionComponent = Component->CollisionComponent.Get();
			if (CollisionComponent)
			{
				CollisionComponent->RecreateCollision();

				// We need to trigger navigation mesh build, in case user have painted holes on a landscape
				if (LayerInfo == ACyLandProxy::VisibilityLayer)
				{
					FNavigationSystem::UpdateComponentData(*CollisionComponent);
				}
			}
		}
	}

	void GetData(int32& X1, int32& Y1, int32& X2, int32& Y2, TMap<FIntPoint, uint8>& Data)
	{
		CyLandEdit.GetWeightData(LayerInfo, X1, Y1, X2, Y2, Data);
	}

	void GetDataFast(int32 X1, int32 Y1, int32 X2, int32 Y2, TMap<FIntPoint, uint8>& Data)
	{
		CyLandEdit.GetWeightDataFast(LayerInfo, X1, Y1, X2, Y2, Data);
	}

	void SetData(int32 X1, int32 Y1, int32 X2, int32 Y2, const uint8* Data, ECyLandLayerPaintingRestriction PaintingRestriction)
	{
		TSet<UCyLandComponent*> Components;
		if (CyLandEdit.GetComponentsInRegion(X1, Y1, X2, Y2, &Components))
		{
			// Flush dynamic foliage (grass)
			ACyLandProxy::InvalidateGeneratedComponentData(Components);

			CyLandEdit.SetAlphaData(LayerInfo, X1, Y1, X2, Y2, Data, 0, PaintingRestriction, bBlendWeight, bUseTotalNormalize);
			//LayerInfo->IsReferencedFromLoadedData = true;
			ModifiedComponents.Append(Components);
		}
	}

	void Flush()
	{
		CyLandEdit.Flush();
	}

private:
	UCyLandInfo* CyLandInfo;
	FCyLandEditDataInterface CyLandEdit;
	TSet<UCyLandComponent*> ModifiedComponents;
	UCyLandLayerInfoObject* LayerInfo;
	bool bBlendWeight;
};

#endif

