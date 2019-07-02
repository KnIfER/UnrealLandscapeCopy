// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CyLand/Classes/CyLand.h"
#include "CyLandProc.generated.h"


UCLASS()
class CYLAND_API UProceuduralGameLandUtils : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

	UFUNCTION(BlueprintCallable, Category = "Procedural Rendering")
	static ACyLand* SpawnGameLand(AActor* context, UMaterialInterface* mat, int32 SectionsPerComponent=1, int32 ComponentCountX = 8, int32 ComponentCountY = 8, int32 QuadsPerComponent = 127);

	UFUNCTION(BlueprintCallable, Category = "Procedural Rendering")
	static void NotifyMaterialUpdated(ACyLand* CyLand);

};