// Fill out your copyright notice in the Description page of Project Settings.

#include "CyLandProc.h"
#include "CoreMinimal.h"
#include "CyLand/Classes/CyLand.h"
#include "CyLand/Classes/CyLandComponent.h"
#include "CyLand/Classes/CyLandInfo.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInstanceConstant.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/Private/InstancedStaticMesh.h"
#include "Landscape/Classes/LandscapeGrassType.h"

//DEFINE_LOG_CATEGORY_STATIC(LogProcLand, Warning, All);



UProceuduralGameLandUtils::UProceuduralGameLandUtils(const FObjectInitializer & initilizer) :
Super(initilizer)
{

}

ACyLand* UProceuduralGameLandUtils::SpawnGameLand(AActor* context, UMaterialInterface* mat)
{
	UWorld* GameWorld = context->GetWorld();
	if (GameWorld && GameWorld->GetCurrentLevel()->bIsVisible)
	{
		const int32 SectionsPerComponent = 1;
		const int32 ComponentCountX = 8;
		const int32 ComponentCountY = 8;
		const int32 QuadsPerComponent = 127;
		const int32 SizeX = ComponentCountX * QuadsPerComponent + 1;
		const int32 SizeY = ComponentCountY * QuadsPerComponent + 1;


		TArray<FCyLandImportLayerInfo> ImportLayers;

		//TOptional< TArray< FCyLandImportLayerInfo > > ImportLayers = FNewCyLandUtils::CreateImportLayersInfo(CyLandEdMode->UISettings, CyLandEdMode->NewCyLandPreviewMode);


		//TArray<uint16> Data = FNewCyLandUtils::ComputeHeightData(CyLandEdMode->UISettings, ImportLayers, CyLandEdMode->NewCyLandPreviewMode);
			// Initialize heightmap data
			TArray<uint16> Data;
			Data.AddUninitialized(SizeX * SizeY);
			uint16* WordData = Data.GetData();

			// Initialize blank heightmap data
			for(int32 i = 0; i < SizeX * SizeY; i++)
			{
				WordData[i] = 32768;
			}


		FVector Offset = FVector(-ComponentCountX * QuadsPerComponent / 2, -ComponentCountY * QuadsPerComponent / 2, 0);

		ACyLand* CyLand = GameWorld->SpawnActor<ACyLand>(Offset, FRotator(0,0,0));
		CyLand->SetActorRelativeScale3D(FVector(100,100,100));
		if (mat)
			CyLand->CyLandMaterial = mat;
		CyLand->Imports(FGuid::NewGuid(), 0, 0, SizeX - 1, SizeY - 1, SectionsPerComponent, QuadsPerComponent, Data.GetData(),
			nullptr, ImportLayers, ECyLandImportAlphamapType::Additive);

		CyLand->GetRootComponent()->SetMobility(EComponentMobility::Movable);
		// automatically calculate a lighting LOD that won't crash lightmass (hopefully)
		// < 2048x2048 -> LOD0
		// >=2048x2048 -> LOD1
		// >= 4096x4096 -> LOD2
		// >= 8192x8192 -> LOD3
		CyLand->StaticLightingLOD = FMath::DivideAndRoundUp(FMath::CeilLogTwo((SizeX * SizeY) / (2048 * 2048) + 1), (uint32)2);

		UCyLandInfo* CyLandInfo = CyLand->CreateCyLandInfo();
		CyLandInfo->UpdateLayerInfoMap(CyLand);
		return CyLand;
	}

	return NULL;
}

void UProceuduralGameLandUtils::NotifyMaterialUpdated(ACyLand* CyLand)
{
	auto property = FPropertyChangedEvent(ACyLand::StaticClass()->FindPropertyByName("CyLandMaterial"));
	CyLand->PostEditChangeProperty(property);
	for (int i = 0; i < CyLand->CyLandComponents.Num(); i++)
	{
		UCyLandComponent* C = CyLand->CyLandComponents[i];

		if (C->IsRenderStateCreated())
		{
			C->MarkRenderStateDirty();
			FlushRenderingCommands();
		}
		for (int j = 0; j < C->MaterialInstances.Num(); j++)
		{
			if (!C->MaterialInstances[j]->IsA(UMaterialInstanceDynamic::StaticClass()))
			{
				C->MaterialInstances[j] = (UMaterialInstanceConstant*)UMaterialInstanceDynamic::Create(C->MaterialInstances[j], GetTransientPackage());// HACKY CAST!
			}
			UMaterialInstanceDynamic* MID = (UMaterialInstanceDynamic*)C->MaterialInstances[j];

			//for (int k = 0; k < Env->Textures.Num(); k++)
			//{
			//	MID->SetTextureParameterValue(pn[k], Env->Textures[k]);
			//}
		}
		C->RecreateRenderState_Concurrent();
	}
}
