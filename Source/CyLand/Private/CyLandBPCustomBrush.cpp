// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CyLandBPCustomBrush.h"
#include "CoreMinimal.h"
#include "CyLandProxy.h"
#include "CyLand.h"

#define LOCTEXT_NAMESPACE "CyLand"

ACyLandBlueprintCustomBrush::ACyLandBlueprintCustomBrush(const FObjectInitializer& ObjectInitializer)
#if WITH_EDITORONLY_DATA
	: OwningCyLand(nullptr)
	, bIsCommited(false)
	, bIsInitialized(false)
#endif
{
	USceneComponent* SceneComp = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
	RootComponent = SceneComp;

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_DuringPhysics;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.SetTickFunctionEnable(true);
	bIsEditorOnlyActor = true;
}

void ACyLandBlueprintCustomBrush::Tick(float DeltaSeconds)
{
	// Forward the Tick to the instances class of this BP
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint))
	{
		TGuardValue<bool> AutoRestore(GAllowActorScriptExecutionInEditor, true);
		ReceiveTick(DeltaSeconds);
	}

	Super::Tick(DeltaSeconds);
}

bool ACyLandBlueprintCustomBrush::ShouldTickIfViewportsOnly() const
{
	return true;
}

#if WITH_EDITOR

void ACyLandBlueprintCustomBrush::SetCommitState(bool InCommited)
{
#if WITH_EDITORONLY_DATA
	bListedInSceneOutliner = !InCommited;
	bEditable = !InCommited;
	bIsCommited = InCommited;
#endif
}

void ACyLandBlueprintCustomBrush::SetOwningCyLand(ACyLand* InOwningCyLand)
{
	OwningCyLand = InOwningCyLand;
}

ACyLand* ACyLandBlueprintCustomBrush::GetOwningCyLand() const
{ 
	return OwningCyLand; 
}

void ACyLandBlueprintCustomBrush::SetIsInitialized(bool InIsInitialized)
{
	bIsInitialized = InIsInitialized;
}

void ACyLandBlueprintCustomBrush::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	if (OwningCyLand != nullptr)
	{
		OwningCyLand->RequestProceduralContentUpdate(bFinished ? EProceduralContentUpdateFlag::All : EProceduralContentUpdateFlag::All_Render);
	}
}

void ACyLandBlueprintCustomBrush::PreEditChange(UProperty* PropertyThatWillChange)
{
	const FName PropertyName = PropertyThatWillChange != nullptr ? PropertyThatWillChange->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(ACyLandBlueprintCustomBrush, AffectHeightmap)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(ACyLandBlueprintCustomBrush, AffectWeightmap))
	{
		PreviousAffectHeightmap = AffectHeightmap;
		PreviousAffectWeightmap = AffectWeightmap;
	}
}

void ACyLandBlueprintCustomBrush::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(ACyLandBlueprintCustomBrush, AffectHeightmap)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(ACyLandBlueprintCustomBrush, AffectWeightmap))
	{
		for (FCyProceduralLayer& Layer : OwningCyLand->ProceduralLayers)
		{
			for (int32 i = 0; i < Layer.Brushes.Num(); ++i)
			{
				if (Layer.Brushes[i].BPCustomBrush == this)
				{
					if (AffectHeightmap && !PreviousAffectHeightmap) // changed to affect
					{
						Layer.HeightmapBrushOrderIndices.Add(i); // simply add the brush as the last one
					}
					else if (!AffectHeightmap && PreviousAffectHeightmap) // changed to no longer affect
					{
						for (int32 j = 0; j < Layer.HeightmapBrushOrderIndices.Num(); ++j)
						{
							if (Layer.HeightmapBrushOrderIndices[j] == i)
							{
								Layer.HeightmapBrushOrderIndices.RemoveAt(j);
								break;
							}
						}
					}

					if (AffectWeightmap && !PreviousAffectWeightmap) // changed to affect
					{
						Layer.WeightmapBrushOrderIndices.Add(i); // simply add the brush as the last one
					}
					else if (!AffectWeightmap && PreviousAffectWeightmap) // changed to no longer affect
					{
						for (int32 j = 0; j < Layer.WeightmapBrushOrderIndices.Num(); ++j)
						{
							if (Layer.WeightmapBrushOrderIndices[j] == i)
							{
								Layer.WeightmapBrushOrderIndices.RemoveAt(j);
								break;
							}
						}
					}

					PreviousAffectHeightmap = AffectHeightmap;
					PreviousAffectWeightmap = AffectWeightmap;

					break;
				}
			}			
		}		

		// Should trigger a rebuild of the UI so the visual is updated with changes made to actor
		//TODO: find a way to trigger the update of the UI
		//FEdModeCyLand* EdMode = (FEdModeCyLand*)GLevelEditorModeTools().GetActiveMode(EM_Landscape_Mimic);
		//EdMode->RefreshDetailPanel();
	}

	if (OwningCyLand != nullptr)
	{
		OwningCyLand->RequestProceduralContentUpdate(EProceduralContentUpdateFlag::All);
	}
}
#endif

ACyLandBlueprintCustomSimulationBrush::ACyLandBlueprintCustomSimulationBrush(const FObjectInitializer& ObjectInitializer)
{
}

#undef LOCTEXT_NAMESPACE
