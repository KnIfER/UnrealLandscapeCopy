// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Materials/MaterialExpressionCyLandLayerCoords.h"
#include "CyLandPrivate.h"
#include "MaterialCompiler.h"


#define LOCTEXT_NAMESPACE "CyLand"


///////////////////////////////////////////////////////////////////////////////
// UMaterialExpressionCyLandLayerCoords
///////////////////////////////////////////////////////////////////////////////

UMaterialExpressionCyLandLayerCoords::UMaterialExpressionCyLandLayerCoords(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		FText NAME_CyLand;
		FConstructorStatics()
			: NAME_CyLand(LOCTEXT("CyLand", "CyLand"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

#if WITH_EDITORONLY_DATA
	MenuCategories.Add(ConstructorStatics.NAME_CyLand);

	bCollapsed = false;
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionCyLandLayerCoords::Compile(class FMaterialCompiler* Compiler, int32 OutputIndex)
{
	switch (CustomUVType)
	{
	case CyLCCT_CustomUV0:
		return Compiler->TextureCoordinate(0, false, false);
	case CyLCCT_CustomUV1:
		return Compiler->TextureCoordinate(1, false, false);
	case CyLCCT_CustomUV2:
		return Compiler->TextureCoordinate(2, false, false);
	case CyLCCT_WeightMapUV:
		return Compiler->TextureCoordinate(3, false, false);
	default:
		break;
	}

	int32 BaseUV;

	switch (MappingType)
	{
	case CyTCMT_Auto:
	case CyTCMT_XY: BaseUV = Compiler->TextureCoordinate(0, false, false); break;
	case CyTCMT_XZ: BaseUV = Compiler->TextureCoordinate(1, false, false); break;
	case CyTCMT_YZ: BaseUV = Compiler->TextureCoordinate(2, false, false); break;
	default: UE_LOG(LogCyLand, Fatal, TEXT("Invalid mapping type %u"), (uint8)MappingType); return INDEX_NONE;
	};

	float Scale = (MappingScale == 0.0f) ? 1.0f : 1.0f / MappingScale;
	int32 RealScale = Compiler->Constant(Scale);

	float Cos = FMath::Cos(MappingRotation * PI / 180.0);
	float Sin = FMath::Sin(MappingRotation * PI / 180.0);

	int32 ResultUV = INDEX_NONE;
	int32 TransformedUV = Compiler->Add(
		Compiler->Mul(RealScale,
		Compiler->AppendVector(
		Compiler->Dot(BaseUV, Compiler->Constant2(+Cos, +Sin)),
		Compiler->Dot(BaseUV, Compiler->Constant2(-Sin, +Cos)))
		),
		Compiler->Constant2(MappingPanU, MappingPanV)
		);

	if (Compiler->GetFeatureLevel() != ERHIFeatureLevel::ES2) // No need to localize UV
	{
		ResultUV = TransformedUV;
	}
	else
	{
		int32 Offset = Compiler->TextureCoordinateOffset();
		int32 TransformedOffset =
			Compiler->Floor(
			Compiler->Mul(RealScale,
			Compiler->AppendVector(
			Compiler->Dot(Offset, Compiler->Constant2(+Cos, +Sin)),
			Compiler->Dot(Offset, Compiler->Constant2(-Sin, +Cos)))
			));

		ResultUV = Compiler->Sub(TransformedUV, TransformedOffset);
	}

	return ResultUV;
}


void UMaterialExpressionCyLandLayerCoords::GetCaption(TArray<FString>& OutCaptions) const
{
	OutCaptions.Add(FString(TEXT("CyLandCoords")));
}
#endif // WITH_EDITOR

bool UMaterialExpressionCyLandLayerCoords::NeedsLoadForClient() const
{
	return true;
}

#undef LOCTEXT_NAMESPACE
