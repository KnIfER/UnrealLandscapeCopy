// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Materials/MaterialExpression.h"
#include "MaterialExpressionCyLandLayerCoords.generated.h"

UENUM()
enum ECyTerrainCoordMappingType
{
	CyTCMT_Auto,
	CyTCMT_XY,
	CyTCMT_XZ,
	CyTCMT_YZ,
	CyTCMT_MAX,
};

UENUM()
enum ECyLandCustomizedCoordType
{
	/** Don't use customized UV, just use original UV. */
	CyLCCT_None,
	CyLCCT_CustomUV0,
	CyLCCT_CustomUV1,
	CyLCCT_CustomUV2,
	/** Use original WeightMapUV, which could not be customized. */
	CyLCCT_WeightMapUV,
	CyLCCT_MAX,
};

UCLASS(collapsecategories, hidecategories=Object)
class CYLAND_API UMaterialExpressionCyLandLayerCoords : public UMaterialExpression
{
	GENERATED_UCLASS_BODY()

	/** Determines the mapping place to use on the terrain. */
	UPROPERTY(EditAnywhere, Category=MaterialExpressionCyLandLayerCoords)
	TEnumAsByte<enum ECyTerrainCoordMappingType> MappingType;

	/** Determines the mapping place to use on the terrain. */
	UPROPERTY(EditAnywhere, Category=MaterialExpressionCyLandLayerCoords)
	TEnumAsByte<enum ECyLandCustomizedCoordType> CustomUVType;

	/** CyUniform scale to apply to the mapping. */
	UPROPERTY(EditAnywhere, Category=MaterialExpressionCyLandLayerCoords)
	float MappingScale;

	/** Rotation to apply to the mapping. */
	UPROPERTY(EditAnywhere, Category=MaterialExpressionCyLandLayerCoords)
	float MappingRotation;

	/** Offset to apply to the mapping along U. */
	UPROPERTY(EditAnywhere, Category=MaterialExpressionCyLandLayerCoords)
	float MappingPanU;

	/** Offset to apply to the mapping along V. */
	UPROPERTY(EditAnywhere, Category=MaterialExpressionCyLandLayerCoords)
	float MappingPanV;

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
#endif
	//~ End UMaterialExpression Interface

	//~ Begin UObject Interface
	virtual bool NeedsLoadForClient() const override;
	//~ End UObject Interface
};



