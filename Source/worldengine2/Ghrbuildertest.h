// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Ghrbuildertest.generated.h"


class ULandscapeGhrassType;
struct FAsyncGhrBuilder;
class ULandscapeGrassType;
class UHierarchicalInstancedStaticMeshComponent;

struct FCachedMyFoliage
{
	struct FGhrCompKey
	{
		TWeakObjectPtr<USceneComponent> BasedOn;
		TWeakObjectPtr<ULandscapeGrassType> GhrType;
		int32 SqrtSubsections;
		int32 CachedMaxInstancesPerComponent;
		int32 SubsectionX;
		int32 SubsectionY;
		int32 NumVarieties;
		int32 VarietyIndex;

		FGhrCompKey()
			: SqrtSubsections(0)
			, CachedMaxInstancesPerComponent(0)
			, SubsectionX(0)
			, SubsectionY(0)
			, NumVarieties(0)
			, VarietyIndex(-1)
		{
		}
		inline bool operator==(const FGhrCompKey& Other) const
		{
			return
				SqrtSubsections == Other.SqrtSubsections &&
				CachedMaxInstancesPerComponent == Other.CachedMaxInstancesPerComponent &&
				SubsectionX == Other.SubsectionX &&
				SubsectionY == Other.SubsectionY &&
				BasedOn == Other.BasedOn &&
				GhrType == Other.GhrType &&
				NumVarieties == Other.NumVarieties &&
				VarietyIndex == Other.VarietyIndex;
		}

		friend uint32 GetTypeHash(const FGhrCompKey& Key)
		{
			return GetTypeHash(Key.BasedOn) ^ GetTypeHash(Key.GhrType) ^ Key.SqrtSubsections ^ Key.CachedMaxInstancesPerComponent ^ (Key.SubsectionX << 16) ^ (Key.SubsectionY << 24) ^ (Key.NumVarieties << 3) ^ (Key.VarietyIndex << 13);
		}

	};

	struct FGhrComp
	{
		FGhrCompKey Key;
		TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent> Foliage;
		TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent> PreviousFoliage;
		TArray<FBox> ExcludedBoxes;
		uint32 LastUsedFrameNumber;
		uint32 ExclusionChangeTag;
		double LastUsedTime;
		bool Pending;
		bool PendingRemovalRebuild;

		FGhrComp()
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

	struct FGhrCompKeyFuncs : BaseKeyFuncs<FGhrComp, FGhrCompKey>
	{
		static KeyInitType GetSetKey(const FGhrComp& Element)
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

	typedef TSet<FGhrComp, FGhrCompKeyFuncs> TGSet;
	TSet<FGhrComp, FGhrCompKeyFuncs> CachedGhrComps;

	void ClearCache()
	{
		CachedGhrComps.Empty();
	}
};

class FMyAsyncGhrTask : public FNonAbandonableTask
{
public:
	FAsyncGhrBuilder* Builder;
	FCachedMyFoliage::FGhrCompKey Key;
	TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent> Foliage;

	FMyAsyncGhrTask(FAsyncGhrBuilder* InBuilder, const FCachedMyFoliage::FGhrCompKey& InKey, UHierarchicalInstancedStaticMeshComponent* InFoliage);
	void DoWork();

	FORCEINLINE TStatId GetStatId() const
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(FCyAsyncGTask, STATGROUP_ThreadPoolAsyncTasks);
	}

	~FMyAsyncGhrTask();
};


/**
 * Gbuilder World actor class
 */
UCLASS()
class WORLDENGINE2_API AGhrbuildertest : public AActor
{
	GENERATED_BODY()

public:

	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "Rendering")
	void dotest();

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ULandscapeGrassType* mGrassType;

	/** A transient data structure for tracking the grass */
	FCachedMyFoliage FoliageCache;

	UPROPERTY(transient, duplicatetransient)
	TArray<UHierarchicalInstancedStaticMeshComponent*> FoliageComponents;
};