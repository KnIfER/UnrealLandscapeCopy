// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Classes/ActorFactoryCyLand.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Components/BillboardComponent.h"
#include "EditorModeManager.h"
#include "CyLandEditorModule.h"
#include "CyLandProxy.h"
#include "CyLandEditorObject.h"
#include "Classes/CyLandPlaceholder.h"

#define LOCTEXT_NAMESPACE "CyLand"

UActorFactoryCyLand::UActorFactoryCyLand(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DisplayName = LOCTEXT("CyLand", "CyLand");
	NewActorClass = ACyLandProxy::StaticClass();
}

AActor* UActorFactoryCyLand::SpawnActor(UObject* Asset, ULevel* InLevel, const FTransform& Transform, EObjectFlags InObjectFlags, const FName Name)
{
	GLevelEditorModeTools().ActivateMode(ICyLandEditorModule::EM_Landscape_Mimic);

	FEdModeCyLand* EdMode = (FEdModeCyLand*)GLevelEditorModeTools().GetActiveMode(ICyLandEditorModule::EM_Landscape_Mimic);

	EdMode->UISettings->NewCyLand_Location = Transform.GetLocation();
	EdMode->UISettings->NewCyLand_Rotation = Transform.GetRotation().Rotator();

	EdMode->SetCurrentTool("NewCyLand");

	FActorSpawnParameters SpawnInfo;
	SpawnInfo.OverrideLevel = InLevel;
	SpawnInfo.ObjectFlags = InObjectFlags;
	SpawnInfo.Name = Name;
	return InLevel->OwningWorld->SpawnActor(ACyLandPlaceholder::StaticClass(), &Transform, SpawnInfo);
}

ACyLandPlaceholder::ACyLandPlaceholder(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		ConstructorHelpers::FObjectFinderOptional<UTexture2D> TerrainTexture;
		FConstructorStatics()
			: TerrainTexture(TEXT("/Engine/EditorResources/S_Terrain"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	USceneComponent* SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent0"));
	RootComponent = SceneComponent;
	RootComponent->Mobility = EComponentMobility::Static;

#if WITH_EDITORONLY_DATA
	UBillboardComponent* SpriteComponent = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (SpriteComponent)
	{
		SpriteComponent->Sprite = ConstructorStatics.TerrainTexture.Get();
		SpriteComponent->RelativeScale3D = FVector(0.5f, 0.5f, 0.5f);
		SpriteComponent->SetupAttachment(RootComponent);
		SpriteComponent->RelativeLocation = FVector(0, 0, 100);
		SpriteComponent->bAbsoluteScale = true;
	}
#endif
}

bool ACyLandPlaceholder::TeleportTo(const FVector& DestLocation, const FRotator& DestRotation, bool bIsATest /*= false*/, bool bNoCheck /*= false*/)
{
	bool bResult = Super::TeleportTo(DestLocation, DestRotation, bIsATest, bNoCheck);

	GLevelEditorModeTools().ActivateMode(ICyLandEditorModule::EM_Landscape_Mimic);

	FEdModeCyLand* EdMode = (FEdModeCyLand*)GLevelEditorModeTools().GetActiveMode(ICyLandEditorModule::EM_Landscape_Mimic);

	EdMode->UISettings->NewCyLand_Location = GetActorLocation();
	EdMode->UISettings->NewCyLand_Rotation = GetActorRotation();

	EdMode->SetCurrentTool("NewCyLand");

	return bResult;
}

void ACyLandPlaceholder::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (!HasAnyFlags(RF_Transient))
	{
		Destroy();
	}
}

#undef LOCTEXT_NAMESPACE
