// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"

#include "CyLandBPCustomBrush.generated.h"

UCLASS(Blueprintable, hidecategories = (Replication, Input, LOD, Actor, Cooking, Rendering))
class CYLAND_API ACyLandBlueprintCustomBrush : public AActor
{
	GENERATED_UCLASS_BODY()

private:
	UPROPERTY(Category= "Settings", EditAnywhere, NonTransactional)
	bool AffectHeightmap;

	UPROPERTY(Category= "Settings", EditAnywhere, NonTransactional)
	bool AffectWeightmap;

#if WITH_EDITORONLY_DATA
	UPROPERTY(NonTransactional, DuplicateTransient)
	class ACyLand* OwningCyLand;

	UPROPERTY(NonTransactional, DuplicateTransient)
	bool bIsCommited;

	UPROPERTY(Transient)
	bool bIsInitialized;

	UPROPERTY(Transient)
	bool PreviousAffectHeightmap;

	UPROPERTY(Transient)
	bool PreviousAffectWeightmap;
#endif
public:

	virtual bool ShouldTickIfViewportsOnly() const override;
	virtual void Tick(float DeltaSeconds) override;

	bool IsAffectingHeightmap() const { return AffectHeightmap; }
	bool IsAffectingWeightmap() const { return AffectWeightmap; }

	UFUNCTION(BlueprintImplementableEvent)
	UTextureRenderTarget2D* Render(bool InIsHeightmap, UTextureRenderTarget2D* InCombinedResult);

	UFUNCTION(BlueprintImplementableEvent)
	void Initialize(const FIntPoint& InCyLandSize, const FIntPoint& InCyLandRenderTargetSize);

#if WITH_EDITOR
	void SetCommitState(bool InCommited);
	bool IsCommited() const { return bIsCommited; }

	bool IsInitialized() const { return bIsInitialized; }
	void SetIsInitialized(bool InIsInitialized);

	void SetOwningCyLand(class ACyLand* InOwningCyLand);
	class ACyLand* GetOwningCyLand() const;

	virtual void PostEditMove(bool bFinished) override;
	virtual void PreEditChange(UProperty* PropertyThatWillChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};

UCLASS(Blueprintable, hidecategories = (Replication, Input, LOD, Actor, Cooking, Rendering))
class CYLAND_API ACyLandBlueprintCustomSimulationBrush : public ACyLandBlueprintCustomBrush
{
	GENERATED_UCLASS_BODY()

public:
	// TODO: To Implement
};


