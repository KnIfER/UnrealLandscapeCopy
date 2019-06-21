// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CyLandEditorObject.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Modules/ModuleManager.h"
#include "UObject/ConstructorHelpers.h"
#include "CyLandEditorModule.h"
#include "CyLandRender.h"
#include "CyLandMaterialInstanceConstant.h"
#include "Misc/ConfigCacheIni.h"
#include "EngineUtils.h"

//#define LOCTEXT_NAMESPACE "CyLandEditor"

UCyLandEditorObject::UCyLandEditorObject(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)

	// Tool Settings:
	, ToolStrength(0.3f)
	, bUseWeightTargetValue(false)
	, WeightTargetValue(1.0f)
	, MaximumValueRadius(10000.0f)

	, FlattenMode(ECyLandToolFlattenMode::Both)
	, bUseSlopeFlatten(false)
	, bPickValuePerApply(false)
	, bUseFlattenTarget(false)
	, FlattenTarget(0)
	, bShowFlattenTargetPreview(true)

	, RampWidth(2000)
	, RampSideFalloff(0.4f)

	, SmoothFilterKernelSize(4)
	, bDetailSmooth(false)
	, DetailScale(0.3f)

	, ErodeThresh(64)
	, ErodeSurfaceThickness(256)
	, ErodeIterationNum(28)
	, ErosionNoiseMode(ECyLandToolErosionMode::Lower)
	, ErosionNoiseScale(60.0f)

	, RainAmount(128)
	, SedimentCapacity(0.3f)
	, HErodeIterationNum(75)
	, RainDistMode(ECyLandToolHydroErosionMode::Both)
	, RainDistScale(60.0f)
	, bHErosionDetailSmooth(true)
	, HErosionDetailScale(0.01f)

	, NoiseMode(ECyLandToolNoiseMode::Both)
	, NoiseScale(128.0f)

	, bUseSelectedRegion(true)
	, bUseNegativeMask(true)

	, PasteMode(ECyLandToolPasteMode::Both)
	, bApplyToAllTargets(true)
	, bSnapGizmo(false)
	, bSmoothGizmoBrush(true)

	, MirrorPoint(FVector::ZeroVector)
	, MirrorOp(ECyLandMirrorOperation::MinusXToPlusX)

	, ResizeCyLand_QuadsPerSection(0)
	, ResizeCyLand_SectionsPerComponent(0)
	, ResizeCyLand_ComponentCount(0, 0)
	, ResizeCyLand_ConvertMode(ECyLandConvertMode::Expand)

	, NewCyLand_Material(NULL)
	, NewCyLand_QuadsPerSection(63)
	, NewCyLand_SectionsPerComponent(1)
	, NewCyLand_ComponentCount(8, 8)
	, NewCyLand_Location(0, 0, 100)
	, NewCyLand_Rotation(0, 0, 0)
	, NewCyLand_Scale(100, 100, 100)
	, ImportCyLand_Width(0)
	, ImportCyLand_Height(0)
	, ImportCyLand_AlphamapType(ECyLandImportAlphamapType::Additive)

	// Brush Settings:
	, BrushRadius(2048.0f)
	, BrushFalloff(0.5f)
	, bUseClayBrush(false)

	, AlphaBrushScale(0.5f)
	, bAlphaBrushAutoRotate(true)
	, AlphaBrushRotation(0.0f)
	, AlphaBrushPanU(0.5f)
	, AlphaBrushPanV(0.5f)
	, bUseWorldSpacePatternBrush(false)
	, WorldSpacePatternBrushSettings(FCyLandPatternBrushWorldSpaceSettings{FVector2D::ZeroVector, 0.0f, false, 3200})
	, AlphaTexture(NULL)
	, AlphaTextureChannel(ECyColorChannel::Red)
	, AlphaTextureSizeX(1)
	, AlphaTextureSizeY(1)

	, BrushComponentSize(1)
	, TargetDisplayOrder(ECyLandLayerDisplayMode::Default)
	, ShowUnusedLayers(true)
{
	// Structure to hold one-time initialization
	struct FConstructorStatics
	{
		ConstructorHelpers::FObjectFinder<UTexture2D> AlphaTexture;

		FConstructorStatics()
			: AlphaTexture(TEXT("/Engine/EditorLandscapeResources/DefaultAlphaTexture"))
		{
		}
	};
	static FConstructorStatics ConstructorStatics;

	SetAlphaTexture(ConstructorStatics.AlphaTexture.Object, AlphaTextureChannel);
}

void UCyLandEditorObject::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	SetbUseSelectedRegion(bUseSelectedRegion);
	SetbUseNegativeMask(bUseNegativeMask);
	SetPasteMode(PasteMode);
	SetbSnapGizmo(bSnapGizmo);

	if (PropertyChangedEvent.MemberProperty == nullptr ||
		PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, AlphaTexture) ||
		PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, AlphaTextureChannel))
	{
		SetAlphaTexture(AlphaTexture, AlphaTextureChannel);
	}


	if (PropertyChangedEvent.MemberProperty == nullptr ||
		PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, NewCyLand_QuadsPerSection) ||
		PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, NewCyLand_SectionsPerComponent) ||
		PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, NewCyLand_ComponentCount))
	{
		NewCyLand_ClampSize();
	}

	if (PropertyChangedEvent.MemberProperty == nullptr ||
		PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, ResizeCyLand_QuadsPerSection) ||
		PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, ResizeCyLand_SectionsPerComponent) ||
		PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, ResizeCyLand_ConvertMode))
	{
		UpdateComponentCount();
	}

	if (PropertyChangedEvent.MemberProperty == nullptr ||
		PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, NewCyLand_Material) ||
		PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, ImportCyLand_HeightmapFilename))
	{
		RefreshImportLayersList();
	}

	if (PropertyChangedEvent.MemberProperty == nullptr ||
		PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, PaintingRestriction))
	{
		UpdateComponentLayerWhitelist();
	}

	if (PropertyChangedEvent.MemberProperty == nullptr ||
		PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, TargetDisplayOrder))
	{
		UpdateTargetLayerDisplayOrder();
	}

	if (PropertyChangedEvent.MemberProperty == nullptr ||
		PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UCyLandEditorObject, ShowUnusedLayers))
	{
		UpdateShowUnusedLayers();
	}
}

/** Load UI settings from ini file */
void UCyLandEditorObject::Load()
{
	GConfig->GetFloat(TEXT("CyLandEdit"), TEXT("ToolStrength"), ToolStrength, GEditorPerProjectIni);
	GConfig->GetFloat(TEXT("CyLandEdit"), TEXT("WeightTargetValue"), WeightTargetValue, GEditorPerProjectIni);
	bool InbUseWeightTargetValue = bUseWeightTargetValue;
	GConfig->GetBool(TEXT("CyLandEdit"), TEXT("bUseWeightTargetValue"), InbUseWeightTargetValue, GEditorPerProjectIni);
	bUseWeightTargetValue = InbUseWeightTargetValue;

	GConfig->GetFloat(TEXT("CyLandEdit"), TEXT("BrushRadius"), BrushRadius, GEditorPerProjectIni);
	GConfig->GetInt(TEXT("CyLandEdit"), TEXT("BrushComponentSize"), BrushComponentSize, GEditorPerProjectIni);
	GConfig->GetFloat(TEXT("CyLandEdit"), TEXT("BrushFalloff"), BrushFalloff, GEditorPerProjectIni);
	bool InbUseClayBrush = bUseClayBrush;
	GConfig->GetBool(TEXT("CyLandEdit"), TEXT("bUseClayBrush"), InbUseClayBrush, GEditorPerProjectIni);
	bUseClayBrush = InbUseClayBrush;
	GConfig->GetFloat(TEXT("CyLandEdit"), TEXT("AlphaBrushScale"), AlphaBrushScale, GEditorPerProjectIni);
	GConfig->GetBool(TEXT("CyLandEdit"), TEXT("AlphaBrushAutoRotate"), bAlphaBrushAutoRotate, GEditorPerProjectIni);
	GConfig->GetFloat(TEXT("CyLandEdit"), TEXT("AlphaBrushRotation"), AlphaBrushRotation, GEditorPerProjectIni);
	GConfig->GetFloat(TEXT("CyLandEdit"), TEXT("AlphaBrushPanU"), AlphaBrushPanU, GEditorPerProjectIni);
	GConfig->GetFloat(TEXT("CyLandEdit"), TEXT("AlphaBrushPanV"), AlphaBrushPanV, GEditorPerProjectIni);
	GConfig->GetBool(TEXT("CyLandEdit"), TEXT("bUseWorldSpacePatternBrush"), bUseWorldSpacePatternBrush, GEditorPerProjectIni);
	GConfig->GetVector2D(TEXT("CyLandEdit"), TEXT("WorldSpacePatternBrushSettings.Origin"), WorldSpacePatternBrushSettings.Origin, GEditorPerProjectIni);
	GConfig->GetBool(TEXT("CyLandEdit"), TEXT("WorldSpacePatternBrushSettings.bCenterTextureOnOrigin"), WorldSpacePatternBrushSettings.bCenterTextureOnOrigin, GEditorPerProjectIni);
	GConfig->GetFloat(TEXT("CyLandEdit"), TEXT("WorldSpacePatternBrushSettings.RepeatSize"), WorldSpacePatternBrushSettings.RepeatSize, GEditorPerProjectIni);
	FString AlphaTextureName = (AlphaTexture != NULL) ? AlphaTexture->GetPathName() : FString();
	int32 InAlphaTextureChannel = AlphaTextureChannel;
	GConfig->GetString(TEXT("CyLandEdit"), TEXT("AlphaTextureName"), AlphaTextureName, GEditorPerProjectIni);
	GConfig->GetInt(TEXT("CyLandEdit"), TEXT("AlphaTextureChannel"), InAlphaTextureChannel, GEditorPerProjectIni);
	AlphaTextureChannel = (ECyColorChannel::Type)InAlphaTextureChannel;
	SetAlphaTexture(LoadObject<UTexture2D>(NULL, *AlphaTextureName, NULL, LOAD_NoWarn), AlphaTextureChannel);

	int32 InFlattenMode = (int32)ECyLandToolFlattenMode::Both;
	GConfig->GetInt(TEXT("CyLandEdit"), TEXT("FlattenMode"), InFlattenMode, GEditorPerProjectIni);
	FlattenMode = (ECyLandToolFlattenMode)InFlattenMode;

	bool InbUseSlopeFlatten = bUseSlopeFlatten;
	GConfig->GetBool(TEXT("CyLandEdit"), TEXT("bUseSlopeFlatten"), InbUseSlopeFlatten, GEditorPerProjectIni);
	bUseSlopeFlatten = InbUseSlopeFlatten;

	bool InbPickValuePerApply = bPickValuePerApply;
	GConfig->GetBool(TEXT("CyLandEdit"), TEXT("bPickValuePerApply"), InbPickValuePerApply, GEditorPerProjectIni);
	bPickValuePerApply = InbPickValuePerApply;

	bool InbUseFlattenTarget = bUseFlattenTarget;
	GConfig->GetBool(TEXT("CyLandEdit"), TEXT("bUseFlattenTarget"), InbUseFlattenTarget, GEditorPerProjectIni);
	bUseFlattenTarget = InbUseFlattenTarget;
	GConfig->GetFloat(TEXT("CyLandEdit"), TEXT("FlattenTarget"), FlattenTarget, GEditorPerProjectIni);

	GConfig->GetFloat(TEXT("CyLandEdit"), TEXT("RampWidth"), RampWidth, GEditorPerProjectIni);
	GConfig->GetFloat(TEXT("CyLandEdit"), TEXT("RampSideFalloff"), RampSideFalloff, GEditorPerProjectIni);

	GConfig->GetInt(TEXT("CyLandEdit"), TEXT("ErodeThresh"), ErodeThresh, GEditorPerProjectIni);
	GConfig->GetInt(TEXT("CyLandEdit"), TEXT("ErodeIterationNum"), ErodeIterationNum, GEditorPerProjectIni);
	GConfig->GetInt(TEXT("CyLandEdit"), TEXT("ErodeSurfaceThickness"), ErodeSurfaceThickness, GEditorPerProjectIni);
	int32 InErosionNoiseMode = (int32)ErosionNoiseMode;
	GConfig->GetInt(TEXT("CyLandEdit"), TEXT("ErosionNoiseMode"), InErosionNoiseMode, GEditorPerProjectIni);
	ErosionNoiseMode = (ECyLandToolErosionMode)InErosionNoiseMode;
	GConfig->GetFloat(TEXT("CyLandEdit"), TEXT("ErosionNoiseScale"), ErosionNoiseScale, GEditorPerProjectIni);

	GConfig->GetInt(TEXT("CyLandEdit"), TEXT("RainAmount"), RainAmount, GEditorPerProjectIni);
	GConfig->GetFloat(TEXT("CyLandEdit"), TEXT("SedimentCapacity"), SedimentCapacity, GEditorPerProjectIni);
	GConfig->GetInt(TEXT("CyLandEdit"), TEXT("HErodeIterationNum"), HErodeIterationNum, GEditorPerProjectIni);
	int32 InRainDistMode = (int32)RainDistMode;
	GConfig->GetInt(TEXT("CyLandEdit"), TEXT("RainDistNoiseMode"), InRainDistMode, GEditorPerProjectIni);
	RainDistMode = (ECyLandToolHydroErosionMode)InRainDistMode;
	GConfig->GetFloat(TEXT("CyLandEdit"), TEXT("RainDistScale"), RainDistScale, GEditorPerProjectIni);
	GConfig->GetFloat(TEXT("CyLandEdit"), TEXT("HErosionDetailScale"), HErosionDetailScale, GEditorPerProjectIni);
	bool InbHErosionDetailSmooth = bHErosionDetailSmooth;
	GConfig->GetBool(TEXT("CyLandEdit"), TEXT("bHErosionDetailSmooth"), InbHErosionDetailSmooth, GEditorPerProjectIni);
	bHErosionDetailSmooth = InbHErosionDetailSmooth;

	int32 InNoiseMode = (int32)NoiseMode;
	GConfig->GetInt(TEXT("CyLandEdit"), TEXT("NoiseMode"), InNoiseMode, GEditorPerProjectIni);
	NoiseMode = (ECyLandToolNoiseMode)InNoiseMode;
	GConfig->GetFloat(TEXT("CyLandEdit"), TEXT("NoiseScale"), NoiseScale, GEditorPerProjectIni);

	GConfig->GetInt(TEXT("CyLandEdit"), TEXT("SmoothFilterKernelSize"), SmoothFilterKernelSize, GEditorPerProjectIni);
	GConfig->GetFloat(TEXT("CyLandEdit"), TEXT("DetailScale"), DetailScale, GEditorPerProjectIni);
	bool InbDetailSmooth = bDetailSmooth;
	GConfig->GetBool(TEXT("CyLandEdit"), TEXT("bDetailSmooth"), InbDetailSmooth, GEditorPerProjectIni);
	bDetailSmooth = InbDetailSmooth;

	GConfig->GetFloat(TEXT("CyLandEdit"), TEXT("MaximumValueRadius"), MaximumValueRadius, GEditorPerProjectIni);

	bool InbSmoothGizmoBrush = bSmoothGizmoBrush;
	GConfig->GetBool(TEXT("CyLandEdit"), TEXT("bSmoothGizmoBrush"), InbSmoothGizmoBrush, GEditorPerProjectIni);
	bSmoothGizmoBrush = InbSmoothGizmoBrush;

	int32 InPasteMode = (int32)ECyLandToolPasteMode::Both;
	GConfig->GetInt(TEXT("CyLandEdit"), TEXT("PasteMode"), InPasteMode, GEditorPerProjectIni);
	//PasteMode = (ECyLandToolPasteMode)InPasteMode;
	SetPasteMode((ECyLandToolPasteMode)InPasteMode);

	int32 InMirrorOp = (int32)ECyLandMirrorOperation::MinusXToPlusX;
	GConfig->GetInt(TEXT("CyLandEdit"), TEXT("MirrorOp"), InMirrorOp, GEditorPerProjectIni);
	MirrorOp = (ECyLandMirrorOperation)InMirrorOp;

	int32 InConvertMode = (int32)ResizeCyLand_ConvertMode;
	GConfig->GetInt(TEXT("CyLandEdit"), TEXT("ConvertMode"), InConvertMode, GEditorPerProjectIni);
	ResizeCyLand_ConvertMode = (ECyLandConvertMode)InConvertMode;

	// Region
	//GConfig->GetBool(TEXT("CyLandEdit"), TEXT("bUseSelectedRegion"), bUseSelectedRegion, GEditorPerProjectIni);
	//GConfig->GetBool(TEXT("CyLandEdit"), TEXT("bUseNegativeMask"), bUseNegativeMask, GEditorPerProjectIni);
	bool InbApplyToAllTargets = bApplyToAllTargets;
	GConfig->GetBool(TEXT("CyLandEdit"), TEXT("bApplyToAllTargets"), InbApplyToAllTargets, GEditorPerProjectIni);
	bApplyToAllTargets = InbApplyToAllTargets;

	GConfig->GetBool(TEXT("CyLandEdit"), TEXT("ShowUnusedLayers"), ShowUnusedLayers, GEditorPerProjectIni);

	// Set EditRenderMode
	SetbUseSelectedRegion(bUseSelectedRegion);
	SetbUseNegativeMask(bUseNegativeMask);

	// Gizmo History (not saved!)
	GizmoHistories.Empty();
	for (TActorIterator<ACyLandGizmoActor> It(ParentMode->GetWorld()); It; ++It)
	{
		ACyLandGizmoActor* Gizmo = *It;
		if (!Gizmo->IsEditable())
		{
			new(GizmoHistories) FGizmoHistory(Gizmo);
		}
	}

	FString NewCyLandMaterialName = (NewCyLand_Material != NULL) ? NewCyLand_Material->GetPathName() : FString();
	GConfig->GetString(TEXT("CyLandEdit"), TEXT("NewCyLandMaterialName"), NewCyLandMaterialName, GEditorPerProjectIni);
	if(NewCyLandMaterialName != TEXT(""))
	{
		NewCyLand_Material = LoadObject<UMaterialInterface>(NULL, *NewCyLandMaterialName, NULL, LOAD_NoWarn);
	}
	
	int32 AlphamapType = (uint8)ImportCyLand_AlphamapType;
	GConfig->GetInt(TEXT("CyLandEdit"), TEXT("ImportCyLand_AlphamapType"), AlphamapType, GEditorPerProjectIni);
	ImportCyLand_AlphamapType = (ECyLandImportAlphamapType)AlphamapType;

	RefreshImportLayersList();
}

/** Save UI settings to ini file */
void UCyLandEditorObject::Save()
{
	GConfig->SetFloat(TEXT("CyLandEdit"), TEXT("ToolStrength"), ToolStrength, GEditorPerProjectIni);
	GConfig->SetFloat(TEXT("CyLandEdit"), TEXT("WeightTargetValue"), WeightTargetValue, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("CyLandEdit"), TEXT("bUseWeightTargetValue"), bUseWeightTargetValue, GEditorPerProjectIni);

	GConfig->SetFloat(TEXT("CyLandEdit"), TEXT("BrushRadius"), BrushRadius, GEditorPerProjectIni);
	GConfig->SetInt(TEXT("CyLandEdit"), TEXT("BrushComponentSize"), BrushComponentSize, GEditorPerProjectIni);
	GConfig->SetFloat(TEXT("CyLandEdit"), TEXT("BrushFalloff"), BrushFalloff, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("CyLandEdit"), TEXT("bUseClayBrush"), bUseClayBrush, GEditorPerProjectIni);
	GConfig->SetFloat(TEXT("CyLandEdit"), TEXT("AlphaBrushScale"), AlphaBrushScale, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("CyLandEdit"), TEXT("AlphaBrushAutoRotate"), bAlphaBrushAutoRotate, GEditorPerProjectIni);
	GConfig->SetFloat(TEXT("CyLandEdit"), TEXT("AlphaBrushRotation"), AlphaBrushRotation, GEditorPerProjectIni);
	GConfig->SetFloat(TEXT("CyLandEdit"), TEXT("AlphaBrushPanU"), AlphaBrushPanU, GEditorPerProjectIni);
	GConfig->SetFloat(TEXT("CyLandEdit"), TEXT("AlphaBrushPanV"), AlphaBrushPanV, GEditorPerProjectIni);
	GConfig->SetVector2D(TEXT("CyLandEdit"), TEXT("WorldSpacePatternBrushSettings.Origin"), WorldSpacePatternBrushSettings.Origin, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("CyLandEdit"), TEXT("WorldSpacePatternBrushSettings.bCenterTextureOnOrigin"), WorldSpacePatternBrushSettings.bCenterTextureOnOrigin, GEditorPerProjectIni);
	GConfig->SetFloat(TEXT("CyLandEdit"), TEXT("WorldSpacePatternBrushSettings.RepeatSize"), WorldSpacePatternBrushSettings.RepeatSize, GEditorPerProjectIni);
	const FString AlphaTextureName = (AlphaTexture != NULL) ? AlphaTexture->GetPathName() : FString();
	GConfig->SetString(TEXT("CyLandEdit"), TEXT("AlphaTextureName"), *AlphaTextureName, GEditorPerProjectIni);
	GConfig->SetInt(TEXT("CyLandEdit"), TEXT("AlphaTextureChannel"), (int32)AlphaTextureChannel, GEditorPerProjectIni);

	GConfig->SetInt(TEXT("CyLandEdit"), TEXT("FlattenMode"), (int32)FlattenMode, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("CyLandEdit"), TEXT("bUseSlopeFlatten"), bUseSlopeFlatten, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("CyLandEdit"), TEXT("bPickValuePerApply"), bPickValuePerApply, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("CyLandEdit"), TEXT("bUseFlattenTarget"), bUseFlattenTarget, GEditorPerProjectIni);
	GConfig->SetFloat(TEXT("CyLandEdit"), TEXT("FlattenTarget"), FlattenTarget, GEditorPerProjectIni);

	GConfig->SetFloat(TEXT("CyLandEdit"), TEXT("RampWidth"), RampWidth, GEditorPerProjectIni);
	GConfig->SetFloat(TEXT("CyLandEdit"), TEXT("RampSideFalloff"), RampSideFalloff, GEditorPerProjectIni);

	GConfig->SetInt(TEXT("CyLandEdit"), TEXT("ErodeThresh"), ErodeThresh, GEditorPerProjectIni);
	GConfig->SetInt(TEXT("CyLandEdit"), TEXT("ErodeIterationNum"), ErodeIterationNum, GEditorPerProjectIni);
	GConfig->SetInt(TEXT("CyLandEdit"), TEXT("ErodeSurfaceThickness"), ErodeSurfaceThickness, GEditorPerProjectIni);
	GConfig->SetInt(TEXT("CyLandEdit"), TEXT("ErosionNoiseMode"), (int32)ErosionNoiseMode, GEditorPerProjectIni);
	GConfig->SetFloat(TEXT("CyLandEdit"), TEXT("ErosionNoiseScale"), ErosionNoiseScale, GEditorPerProjectIni);

	GConfig->SetInt(TEXT("CyLandEdit"), TEXT("RainAmount"), RainAmount, GEditorPerProjectIni);
	GConfig->SetFloat(TEXT("CyLandEdit"), TEXT("SedimentCapacity"), SedimentCapacity, GEditorPerProjectIni);
	GConfig->SetInt(TEXT("CyLandEdit"), TEXT("HErodeIterationNum"), ErodeIterationNum, GEditorPerProjectIni);
	GConfig->SetInt(TEXT("CyLandEdit"), TEXT("RainDistMode"), (int32)RainDistMode, GEditorPerProjectIni);
	GConfig->SetFloat(TEXT("CyLandEdit"), TEXT("RainDistScale"), RainDistScale, GEditorPerProjectIni);
	GConfig->SetFloat(TEXT("CyLandEdit"), TEXT("HErosionDetailScale"), HErosionDetailScale, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("CyLandEdit"), TEXT("bHErosionDetailSmooth"), bHErosionDetailSmooth, GEditorPerProjectIni);

	GConfig->SetInt(TEXT("CyLandEdit"), TEXT("NoiseMode"), (int32)NoiseMode, GEditorPerProjectIni);
	GConfig->SetFloat(TEXT("CyLandEdit"), TEXT("NoiseScale"), NoiseScale, GEditorPerProjectIni);
	GConfig->SetInt(TEXT("CyLandEdit"), TEXT("SmoothFilterKernelSize"), SmoothFilterKernelSize, GEditorPerProjectIni);
	GConfig->SetFloat(TEXT("CyLandEdit"), TEXT("DetailScale"), DetailScale, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("CyLandEdit"), TEXT("bDetailSmooth"), bDetailSmooth, GEditorPerProjectIni);

	GConfig->SetFloat(TEXT("CyLandEdit"), TEXT("MaximumValueRadius"), MaximumValueRadius, GEditorPerProjectIni);

	GConfig->SetBool(TEXT("CyLandEdit"), TEXT("bSmoothGizmoBrush"), bSmoothGizmoBrush, GEditorPerProjectIni);
	GConfig->SetInt(TEXT("CyLandEdit"), TEXT("PasteMode"), (int32)PasteMode, GEditorPerProjectIni);

	GConfig->SetInt(TEXT("CyLandEdit"), TEXT("MirrorOp"), (int32)MirrorOp, GEditorPerProjectIni);

	GConfig->SetInt(TEXT("CyLandEdit"), TEXT("ConvertMode"), (int32)ResizeCyLand_ConvertMode, GEditorPerProjectIni);
	//GConfig->SetBool(TEXT("CyLandEdit"), TEXT("bUseSelectedRegion"), bUseSelectedRegion, GEditorPerProjectIni);
	//GConfig->SetBool(TEXT("CyLandEdit"), TEXT("bUseNegativeMask"), bUseNegativeMask, GEditorPerProjectIni);
	GConfig->SetBool(TEXT("CyLandEdit"), TEXT("bApplyToAllTargets"), bApplyToAllTargets, GEditorPerProjectIni);

	const FString NewCyLandMaterialName = (NewCyLand_Material != NULL) ? NewCyLand_Material->GetPathName() : FString();
	GConfig->SetString(TEXT("CyLandEdit"), TEXT("NewCyLandMaterialName"), *NewCyLandMaterialName, GEditorPerProjectIni);

	GConfig->SetInt(TEXT("CyLandEdit"), TEXT("ImportCyLand_AlphamapType"), (uint8)ImportCyLand_AlphamapType, GEditorPerProjectIni);

	GConfig->SetBool(TEXT("CyLandEdit"), TEXT("ShowUnusedLayers"), ShowUnusedLayers, GEditorPerProjectIni);
}

// Region
void UCyLandEditorObject::SetbUseSelectedRegion(bool InbUseSelectedRegion)
{ 
	bUseSelectedRegion = InbUseSelectedRegion;
	if (bUseSelectedRegion)
	{
		GCyLandEditRenderMode |= ECyLandEditRenderMode::Mask;
	}
	else
	{
		GCyLandEditRenderMode &= ~(ECyLandEditRenderMode::Mask);
	}
}
void UCyLandEditorObject::SetbUseNegativeMask(bool InbUseNegativeMask) 
{ 
	bUseNegativeMask = InbUseNegativeMask; 
	if (bUseNegativeMask)
	{
		GCyLandEditRenderMode |= ECyLandEditRenderMode::InvertedMask;
	}
	else
	{
		GCyLandEditRenderMode &= ~(ECyLandEditRenderMode::InvertedMask);
	}
}

void UCyLandEditorObject::SetPasteMode(ECyLandToolPasteMode InPasteMode)
{
	PasteMode = InPasteMode;
}

void UCyLandEditorObject::SetbSnapGizmo(bool InbSnapGizmo)
{
	bSnapGizmo = InbSnapGizmo;

	if (ParentMode->CurrentGizmoActor.IsValid())
	{
		ParentMode->CurrentGizmoActor->bSnapToCyLandGrid = bSnapGizmo;
	}

	if (bSnapGizmo)
	{
		if (ParentMode->CurrentGizmoActor.IsValid())
		{
			check(ParentMode->CurrentGizmoActor->TargetCyLandInfo);

			const FVector WidgetLocation = ParentMode->CurrentGizmoActor->GetActorLocation();
			const FRotator WidgetRotation = ParentMode->CurrentGizmoActor->GetActorRotation();

			const FVector SnappedWidgetLocation = ParentMode->CurrentGizmoActor->SnapToCyLandGrid(WidgetLocation);
			const FRotator SnappedWidgetRotation = ParentMode->CurrentGizmoActor->SnapToCyLandGrid(WidgetRotation);

			ParentMode->CurrentGizmoActor->SetActorLocation(SnappedWidgetLocation, false);
			ParentMode->CurrentGizmoActor->SetActorRotation(SnappedWidgetRotation);
		}
	}
}

bool UCyLandEditorObject::SetAlphaTexture(UTexture2D* InTexture, ECyColorChannel::Type InTextureChannel)
{
	bool Result = true;

	TArray<uint8> NewTextureData;
	UTexture2D* NewAlphaTexture = InTexture;

	// No texture or no source art, try to use the previous texture.
	if (NewAlphaTexture == NULL || !NewAlphaTexture->Source.IsValid())
	{
		NewAlphaTexture = AlphaTexture;
		Result = false;
	}

	if (NewAlphaTexture != NULL && NewAlphaTexture->Source.IsValid())
	{
		NewAlphaTexture->Source.GetMipData(NewTextureData, 0);
	}

	// Load fallback if there's no texture or data
	if (NewAlphaTexture == NULL || (NewTextureData.Num() != 4 * NewAlphaTexture->Source.GetSizeX() * NewAlphaTexture->Source.GetSizeY()))
	{
		NewAlphaTexture = GetClass()->GetDefaultObject<UCyLandEditorObject>()->AlphaTexture;
		NewAlphaTexture->Source.GetMipData(NewTextureData, 0);
		Result = false;
	}

	check(NewAlphaTexture);
	AlphaTexture = NewAlphaTexture;
	AlphaTextureSizeX = NewAlphaTexture->Source.GetSizeX();
	AlphaTextureSizeY = NewAlphaTexture->Source.GetSizeY();
	AlphaTextureChannel = InTextureChannel;
	AlphaTextureData.Empty(AlphaTextureSizeX*AlphaTextureSizeY);

	if (NewTextureData.Num() != 4 *AlphaTextureSizeX*AlphaTextureSizeY)
	{
		// Don't crash if for some reason we couldn't load any source art
		AlphaTextureData.AddZeroed(AlphaTextureSizeX*AlphaTextureSizeY);
	}
	else
	{
		uint8* SrcPtr;
		switch(AlphaTextureChannel)
		{
		case 1:
			SrcPtr = &((FColor*)NewTextureData.GetData())->G;
			break;
		case 2:
			SrcPtr = &((FColor*)NewTextureData.GetData())->B;
			break;
		case 3:
			SrcPtr = &((FColor*)NewTextureData.GetData())->A;
			break;
		default:
			SrcPtr = &((FColor*)NewTextureData.GetData())->R;
			break;
		}

		for (int32 i=0;i<AlphaTextureSizeX*AlphaTextureSizeY;i++)
		{
			AlphaTextureData.Add(*SrcPtr);
			SrcPtr += 4;
		}
	}

	return Result;
}

void UCyLandEditorObject::ImportCyLandData()
{
	ICyLandEditorModule& CyLandEditorModule = FModuleManager::GetModuleChecked<ICyLandEditorModule>("CyLandEditor");
	const ICyLandHeightmapFileFormat* HeightmapFormat = CyLandEditorModule.GetHeightmapFormatByExtension(*FPaths::GetExtension(ImportCyLand_HeightmapFilename, true));

	if (HeightmapFormat)
	{
		FCyLandHeightmapImportData HeightmapImportData = HeightmapFormat->Import(*ImportCyLand_HeightmapFilename, {ImportCyLand_Width, ImportCyLand_Height});
		ImportCyLand_HeightmapImportResult = HeightmapImportData.ResultCode;
		ImportCyLand_HeightmapErrorMessage = HeightmapImportData.ErrorMessage;
		ImportCyLand_Data = MoveTemp(HeightmapImportData.Data);
	}
	else
	{
		ImportCyLand_HeightmapImportResult = ECyLandImportResult::Error;
		ImportCyLand_HeightmapErrorMessage = NSLOCTEXT("CyLandEditor.NewCyLand", "Import_UnknownFileType", "File type not recognised");
	}

	if (ImportCyLand_HeightmapImportResult == ECyLandImportResult::Error)
	{
		ImportCyLand_Data.Empty();
	}
}

void UCyLandEditorObject::RefreshImportLayersList()
{
	UTexture2D* ThumbnailWeightmap = LoadObject<UTexture2D>(NULL, TEXT("/Engine/EditorLandscapeResources/LandscapeThumbnailWeightmap.LandscapeThumbnailWeightmap"), NULL, LOAD_None, NULL);
	UTexture2D* ThumbnailHeightmap = LoadObject<UTexture2D>(NULL, TEXT("/Engine/EditorLandscapeResources/LandscapeThumbnailHeightmap.LandscapeThumbnailHeightmap"), NULL, LOAD_None, NULL);


	UMaterialInterface* Material = NewCyLand_Material.Get();
	TArray<FName> LayerNames = ACyLandProxy::GetLayersFromMaterial(Material);

	const TArray<FCyLandImportLayer> OldLayersList = MoveTemp(ImportCyLand_Layers);
	ImportCyLand_Layers.Reset(LayerNames.Num());

	for (int32 i = 0; i < LayerNames.Num(); i++)
	{
		const FName& LayerName = LayerNames[i];

		bool bFound = false;
		FCyLandImportLayer NewImportLayer;
		for (int32 j = 0; j < OldLayersList.Num(); j++)
		{
			if (OldLayersList[j].LayerName == LayerName)
			{
				NewImportLayer = OldLayersList[j];
				bFound = true;
				break;
			}
		}

		if (bFound)
		{
			if (NewImportLayer.ThumbnailMIC->Parent != Material)
			{
				FMaterialUpdateContext Context;
				NewImportLayer.ThumbnailMIC->SetParentEditorOnly(Material);
				Context.AddMaterialInterface(NewImportLayer.ThumbnailMIC);
			}

			NewImportLayer.ImportResult = ECyLandImportResult::Success;
			NewImportLayer.ErrorMessage = FText();

			if (!NewImportLayer.SourceFilePath.IsEmpty())
			{
				if (!NewImportLayer.LayerInfo)
				{
					NewImportLayer.ImportResult = ECyLandImportResult::Error;
					NewImportLayer.ErrorMessage = NSLOCTEXT("CyLandEditor.NewCyLand", "Import_LayerInfoNotSet", "Can't import a layer file without a layer info");
				}
				else
				{
					ICyLandEditorModule& CyLandEditorModule = FModuleManager::GetModuleChecked<ICyLandEditorModule>("CyLandEditor");
					const ICyLandWeightmapFileFormat* WeightmapFormat = CyLandEditorModule.GetWeightmapFormatByExtension(*FPaths::GetExtension(NewImportLayer.SourceFilePath, true));

					if (WeightmapFormat)
					{
						FCyLandWeightmapInfo WeightmapImportInfo = WeightmapFormat->Validate(*NewImportLayer.SourceFilePath, NewImportLayer.LayerName);
						NewImportLayer.ImportResult = WeightmapImportInfo.ResultCode;
						NewImportLayer.ErrorMessage = WeightmapImportInfo.ErrorMessage;

						if (WeightmapImportInfo.ResultCode != ECyLandImportResult::Error &&
							!WeightmapImportInfo.PossibleResolutions.Contains(FCyLandFileResolution{ImportCyLand_Width, ImportCyLand_Height}))
						{
							NewImportLayer.ImportResult = ECyLandImportResult::Error;
							NewImportLayer.ErrorMessage = NSLOCTEXT("CyLandEditor.NewCyLand", "Import_LayerSizeMismatch", "Size of the layer file does not match size of heightmap file");
						}
					}
					else
					{
						NewImportLayer.ImportResult = ECyLandImportResult::Error;
						NewImportLayer.ErrorMessage = NSLOCTEXT("CyLandEditor.NewCyLand", "Import_UnknownFileType", "File type not recognised");
					}
				}
			}
		}
		else
		{
			NewImportLayer.LayerName = LayerName;
			NewImportLayer.ThumbnailMIC = ACyLandProxy::GetLayerThumbnailMIC(Material, LayerName, ThumbnailWeightmap, ThumbnailHeightmap, nullptr);
		}

		ImportCyLand_Layers.Add(MoveTemp(NewImportLayer));
	}
}

void UCyLandEditorObject::UpdateComponentLayerWhitelist()
{
	if (ParentMode->CurrentToolTarget.CyLandInfo.IsValid())
	{
		ParentMode->CurrentToolTarget.CyLandInfo->UpdateComponentLayerWhitelist();
	}
}

void UCyLandEditorObject::UpdateTargetLayerDisplayOrder()
{
	if (ParentMode != nullptr)
	{
		ParentMode->UpdateTargetLayerDisplayOrder(TargetDisplayOrder);
	}
}

void UCyLandEditorObject::UpdateShowUnusedLayers()
{
	if (ParentMode != nullptr)
	{
		ParentMode->UpdateShownLayerList();
	}
}

