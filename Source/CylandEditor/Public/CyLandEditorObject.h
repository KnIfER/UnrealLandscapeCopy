// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "CyLandProxy.h"
#include "Private/CyLandEdMode.h"
#include "CyLandFileFormatInterface.h"
#include "CyLandBPCustomBrush.h"

#include "CyLandEditorObject.generated.h"

class UCyLandMaterialInstanceConstant;
class UTexture2D;

UENUM()
enum class ECyLandToolFlattenMode : int8
{
	Invalid = -1 UMETA(Hidden),

	/** Flatten may both raise and lower values */
	Both = 0,

	/** Flatten may only raise values, values above the clicked point will be left unchanged */
	Raise = 1,

	/** Flatten may only lower values, values below the clicked point will be left unchanged */
	Lower = 2,

	/** Flatten to specific terrace height intervals */
	Terrace = 3
};

UENUM()
enum class ECyLandToolErosionMode : int8
{
	Invalid = -1 UMETA(Hidden),

	/** Apply all erosion effects, both raising and lowering the heightmap */
	Both = 0,

	/** Only applies erosion effects that result in raising the heightmap */
	Raise = 1,

	/** Only applies erosion effects that result in lowering the heightmap */
	Lower = 2,
};

UENUM()
enum class ECyLandToolHydroErosionMode : int8
{
	Invalid = -1 UMETA(Hidden),

	/** Rains in some places and not others, randomly */
	Both = 0,

	/** Rain is applied to the entire area */
	Positive = 1,
};

UENUM()
enum class ECyLandToolNoiseMode : int8
{
	Invalid = -1 UMETA(Hidden),

	/** Noise will both raise and lower the heightmap */
	Both = 0,

	/** Noise will only raise the heightmap */
	Add = 1,

	/** Noise will only lower the heightmap */
	Sub = 2,
};

#if CPP
inline float NoiseModeConversion(ECyLandToolNoiseMode Mode, float NoiseAmount, float OriginalValue)
{
	switch (Mode)
	{
	case ECyLandToolNoiseMode::Add: // always +
		OriginalValue += NoiseAmount;
		break;
	case ECyLandToolNoiseMode::Sub: // always -
		OriginalValue -= NoiseAmount;
		break;
	case ECyLandToolNoiseMode::Both:
		break;
	}
	return OriginalValue;
}
#endif

UENUM()
enum class ECyLandToolPasteMode : int8
{
	Invalid = -1 UMETA(Hidden),

	/** Paste may both raise and lower values */
	Both = 0,

	/** Paste may only raise values, places where the pasted data would be below the heightmap are left unchanged. Good for copy/pasting mountains */
	Raise = 1,

	/** Paste may only lower values, places where the pasted data would be above the heightmap are left unchanged. Good for copy/pasting valleys or pits */
	Lower = 2,
};

UENUM()
enum class ECyLandConvertMode : int8
{
	Invalid = -1 UMETA(Hidden),

	/** Will round up the number of components for the new world size, which might expand the world size compared to previous settings*/
	Expand = 0,

	/** Will floor the number of components for the new world size, which might reduce the world size compared to previous settings*/
	Clip = 1,

	/** The CyLand will have the same overall size in the world, and have the same number of components. Existing CyLand geometry and layer data will be resampled to match the new resolution. */
	Resample = 2,
};

UENUM()
namespace ECyColorChannel
{
	enum Type
	{
		Red,
		Green,
		Blue,
		Alpha,
	};
}

UENUM()
enum class ECyLandMirrorOperation : uint8
{
	MinusXToPlusX UMETA(DisplayName="-X to +X"),
	PlusXToMinusX UMETA(DisplayName="+X to -X"),
	MinusYToPlusY UMETA(DisplayName="-Y to +Y"),
	PlusYToMinusY UMETA(DisplayName="+Y to -Y"),
	RotateMinusXToPlusX UMETA(DisplayName="Rotate -X to +X"),
	RotatePlusXToMinusX UMETA(DisplayName="Rotate +X to -X"),
	RotateMinusYToPlusY UMETA(DisplayName="Rotate -Y to +Y"),
	RotatePlusYToMinusY UMETA(DisplayName="Rotate +Y to -Y"),
};

USTRUCT()
struct FCyGizmoImportLayer
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(Category="Import", EditAnywhere)
	FString LayerFilename;

	UPROPERTY(Category="Import", EditAnywhere)
	FString LayerName;

	UPROPERTY(Category="Import", EditAnywhere)
	bool bNoImport;

	FCyGizmoImportLayer()
		: LayerFilename("")
		, LayerName("")
		, bNoImport(false)
	{
	}
};

UENUM()
enum class ECyLandImportHeightmapError
{
	None,
	FileNotFound,
	InvalidSize,
	CorruptFile,
	ColorPng,
	LowBitDepth,
};

UENUM()
enum class ECyLandImportLayerError : uint8
{
	None,
	MissingLayerInfo,
	FileNotFound,
	FileSizeMismatch,
	CorruptFile,
	ColorPng,
};

USTRUCT()
struct FCyLandImportLayer : public FCyLandImportLayerInfo
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(Category="Import", VisibleAnywhere)
	UCyLandMaterialInstanceConstant* ThumbnailMIC;

	UPROPERTY(Category="Import", VisibleAnywhere)
	ECyLandImportResult ImportResult;

	UPROPERTY(Category="Import", VisibleAnywhere)
	FText ErrorMessage;

	FCyLandImportLayer()
		: FCyLandImportLayerInfo()
		, ThumbnailMIC(nullptr)
		, ImportResult(ECyLandImportResult::Success)
		, ErrorMessage(FText())
	{
	}
};

USTRUCT()
struct FCyLandPatternBrushWorldSpaceSettings
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(Category="World-Space", EditAnywhere)
	FVector2D Origin;

	UPROPERTY(Category = "World-Space", EditAnywhere, Meta = (ClampMin = "-360", ClampMax = "360", UIMin = "-180", UIMax = "180"))
	float Rotation;

	// if true, the texture used for the pattern is centered on the PatternOrigin.
	// if false, the corner of the texture is placed at the PatternOrigin
	UPROPERTY(Category = "World-Space", EditAnywhere)
	bool bCenterTextureOnOrigin;

	UPROPERTY(Category = "World-Space", EditAnywhere)
	float RepeatSize;

	FCyLandPatternBrushWorldSpaceSettings() = default;
};

UCLASS(MinimalAPI)
class UCyLandEditorObject : public UObject
{
	GENERATED_UCLASS_BODY()
public:
	FEdModeCyLand* ParentMode;


	// Common Tool Settings:

	// Strength of the tool. If you're using a pen/tablet with pressure-sensing, the pressure used affects the strength of the tool.
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(ShowForTools="Paint,Sculpt,Smooth,Flatten,Erosion,HydraErosion,Noise,Mask,CopyPaste", ClampMin="0", ClampMax="10", UIMin="0", UIMax="1"))
	float ToolStrength;

	// Enable to make tools blend towards a target value
	UPROPERTY(Category = "Tool Settings", NonTransactional, EditAnywhere, meta = (InlineEditConditionToggle))
	bool bUseWeightTargetValue;

	// Enable to make tools blend towards a target value
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Use Target Value", EditCondition="bUseWeightTargetValue", ShowForTools="Paint,Sculpt,Noise", ClampMin="0", ClampMax="10", UIMin="0", UIMax="1"))
	float WeightTargetValue;

	// I have no idea what this is for but it's used by the noise and erosion tools, and isn't exposed to the UI
	UPROPERTY(NonTransactional)
	float MaximumValueRadius;

	// Flatten Tool:

	// Whether to flatten by lowering, raising, both or terracing
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(ShowForTools="Flatten"))
	ECyLandToolFlattenMode FlattenMode;

	// Flattens to the angle of the clicked point, instead of horizontal
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(ShowForTools="Flatten", ShowForTargetTypes="Heightmap"))
	bool bUseSlopeFlatten;

	// Constantly picks new values to flatten towards when dragging around, instead of only using the first clicked point
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(ShowForTools="Flatten", ShowForTargetTypes="Heightmap"))
	bool bPickValuePerApply;

	// Enable to flatten towards a target height
	UPROPERTY(Category = "Tool Settings", NonTransactional, EditAnywhere, meta = (InlineEditConditionToggle))
	bool bUseFlattenTarget;

	// Target height to flatten towards (in Unreal Units)
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(ShowForTools="Flatten", ShowForTargetTypes="Heightmap", EditCondition="bUseFlattenTarget", UIMin="-32768", UIMax="32768"))
	float FlattenTarget;

	// Whether to show the preview grid for the flatten target height
	UPROPERTY(Category = "Tool Settings", EditAnywhere, NonTransactional, AdvancedDisplay, meta = (DisplayName = "Show Preview Grid", ShowForTools = "Flatten", ShowForTargetTypes = "Heightmap", EditCondition = "bUseFlattenTarget", HideEditConditionToggle, UIMin = "-32768", UIMax = "32768"))
	bool bShowFlattenTargetPreview;

	// Height of the terrace intervals in unreal units, for the terrace flatten mode 
	UPROPERTY(Category = "Tool Settings", EditAnywhere, NonTransactional, AdvancedDisplay, meta = (DisplayName = "Terrace Interval", ShowForTools = "Flatten", ShowForTargetTypes = "Heightmap", UIMin = "1", UIMax = "32768"))
	float TerraceInterval;

	// Smoothing value for terrace flatten mode
	UPROPERTY(Category = "Tool Settings", EditAnywhere, NonTransactional, AdvancedDisplay, meta = (DisplayName = "Terrace Smoothing", ShowForTools = "Flatten", ShowForTargetTypes = "Heightmap", UIMin = "0.0001", UIMax = "1.0"))
	float TerraceSmooth;

	// Whether the Eye Dropper mode is activated
	UPROPERTY(NonTransactional, Transient)
	bool bFlattenEyeDropperModeActivated;

	UPROPERTY(NonTransactional, Transient)
	float FlattenEyeDropperModeDesiredTarget;

	// Ramp Tool:

	// Width of ramp
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(ShowForTools="Ramp", ClampMin="1", UIMin="1", UIMax="8192", SliderExponent=3))
	float RampWidth;

	// Falloff on side of ramp
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Side Falloff", ShowForTools="Ramp", ClampMin="0", ClampMax="1", UIMin="0", UIMax="1"))
	float RampSideFalloff;

	// Smooth Tool:

	// The radius smoothing is performed over
	// Higher values smooth out bigger details, lower values only smooth out smaller details
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Filter Kernel Radius", ShowForTools="Smooth", ClampMin="1", ClampMax="31", UIMin="0", UIMax="7"))
	int32 SmoothFilterKernelSize;

	// If checked, performs a detail preserving smooth using the specified detail smoothing value
	UPROPERTY(Category = "Tool Settings", NonTransactional, EditAnywhere, meta = (InlineEditConditionToggle))
	bool bDetailSmooth;

	// Larger detail smoothing values remove more details, while smaller values preserve more details
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Detail Smooth", EditCondition="bDetailSmooth", ShowForTools="Smooth", ClampMin="0", ClampMax="0.99"))
	float DetailScale;

	// Erosion Tool:

	// The minimum height difference necessary for the erosion effects to be applied. Smaller values will result in more erosion being applied
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Threshold", ShowForTools="Erosion", ClampMin="0", ClampMax="256", UIMin="0", UIMax="128"))
	int32 ErodeThresh;

	// The thickness of the surface for the layer weight erosion effect
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Surface Thickness", ShowForTools="Erosion", ClampMin="128", ClampMax="1024", UIMin="128", UIMax="512"))
	int32 ErodeSurfaceThickness;

	// Number of erosion iterations, more means more erosion but is slower
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Iterations", ShowForTools="Erosion", ClampMin="1", ClampMax="300", UIMin="1", UIMax="150"))
	int32 ErodeIterationNum;

	// Whether to erode by lowering, raising, or both
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Noise Mode", ShowForTools="Erosion"))
	ECyLandToolErosionMode ErosionNoiseMode;

	// The size of the perlin noise filter used
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Noise Scale", ShowForTools="Erosion", ClampMin="1", ClampMax="512", UIMin="1.1", UIMax="256"))
	float ErosionNoiseScale;

	// Hydraulic Erosion Tool:

	// The amount of rain to apply to the surface. Larger values will result in more erosion
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(ShowForTools="HydraErosion", ClampMin="1", ClampMax="512", UIMin="1", UIMax="256"))
	int32 RainAmount;

	// The amount of sediment that the water can carry. Larger values will result in more erosion
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Sediment Cap.", ShowForTools="HydraErosion", ClampMin="0.1", ClampMax="1.0"))
	float SedimentCapacity;

	// Number of erosion iterations, more means more erosion but is slower
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Iterations", ShowForTools="HydraErosion", ClampMin="1", ClampMax="300", UIMin="1", UIMax="150"))
	int32 HErodeIterationNum;

	// Initial Rain Distribution
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Initial Rain Distribution", ShowForTools="HydraErosion"))
	ECyLandToolHydroErosionMode RainDistMode;

	// The size of the noise filter for applying initial rain to the surface
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(ShowForTools="HydraErosion", ClampMin="1", ClampMax="512", UIMin="1.1", UIMax="256"))
	float RainDistScale;

	// If checked, performs a detail-preserving smooth to the erosion effect using the specified detail smoothing value
	UPROPERTY(Category = "Tool Settings", NonTransactional, EditAnywhere, meta = (InlineEditConditionToggle))
	bool bHErosionDetailSmooth;

	// Larger detail smoothing values remove more details, while smaller values preserve more details
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Detail Smooth", EditCondition="bHErosionDetailSmooth", ShowForTools="HydraErosion", ClampMin="0", ClampMax="0.99"))
	float HErosionDetailScale;

	// Noise Tool:

	// Whether to apply noise that raises, lowers, or both
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Noise Mode", ShowForTools="Noise"))
	ECyLandToolNoiseMode NoiseMode;

	// The size of the perlin noise filter used
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Noise Scale", ShowForTools="Noise", ClampMin="1", ClampMax="512", UIMin="1.1", UIMax="256"))
	float NoiseScale;

	// Mask Tool:

	// Uses selected region as a mask for other tools
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Use Region as Mask", ShowForTools="Mask", ShowForMask))
	bool bUseSelectedRegion;

	// If enabled, protects the selected region from changes
	// If disabled, only allows changes in the selected region
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Negative Mask", ShowForTools="Mask", ShowForMask))
	bool bUseNegativeMask;

	// Copy/Paste Tool:

	// Whether to paste will only raise, only lower, or both
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(ShowForTools="CopyPaste"))
	ECyLandToolPasteMode PasteMode;

	// If set, copies/pastes all layers, otherwise only copy/pastes the layer selected in the targets panel
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Gizmo copy/paste all layers", ShowForTools="CopyPaste"))
	bool bApplyToAllTargets;

	// Makes sure the gizmo is snapped perfectly to the CyLand so that the sample points line up, which makes copy/paste less blurry. Irrelevant if gizmo is scaled
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Snap Gizmo to CyLand grid", ShowForTools="CopyPaste"))
	bool bSnapGizmo;

	// Smooths the edges of the gizmo data into the CyLand. Without this, the edges of the pasted data will be sharp
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Use Smooth Gizmo Brush", ShowForTools="CopyPaste"))
	bool bSmoothGizmoBrush;

	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, AdvancedDisplay, meta=(DisplayName="Heightmap", ShowForTools="CopyPaste"))
	FString GizmoHeightmapFilenameString;

	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, AdvancedDisplay, meta=(DisplayName="Heightmap Size", ShowForTools="CopyPaste"))
	FIntPoint GizmoImportSize;

	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, AdvancedDisplay, meta=(DisplayName="Layers", ShowForTools="CopyPaste"))
	TArray<FCyGizmoImportLayer> GizmoImportLayers;

	TArray<FGizmoHistory> GizmoHistories;

	// Mirror Tool

	// Location of the mirror plane, defaults to the center of the CyLand. Doesn't normally need to be changed!
	UPROPERTY(Category="Tool Settings", EditAnywhere, Transient, meta=(DisplayName="Mirror Point", ShowForTools="Mirror"))
	FVector2D MirrorPoint;

	// Type of mirroring operation to perform e.g. "Minus X To Plus X" copies and flips the -X half of the CyLand onto the +X half
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Operation", ShowForTools="Mirror"))
	ECyLandMirrorOperation MirrorOp;

	// Number of vertices either side of the mirror plane to smooth over
	UPROPERTY(Category="Tool Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Smoothing Width", ShowForTools="Mirror", ClampMin="0", UIMin="0", UIMax="20"))
	int32 MirrorSmoothingWidth;

	// BP Custom Tool

	UPROPERTY(Category = "Tool Settings", EditAnywhere, Transient, meta = (DisplayName = "Blueprint Brush", ShowForTools = "BPCustom"))
	TSubclassOf<ACyLandBlueprintCustomBrush> BlueprintCustomBrush;

	// Resize CyLand Tool

	// Number of quads per CyLand component section
	UPROPERTY(Category="Change Component Size", EditAnywhere, NonTransactional, meta=(DisplayName="Section Size", ShowForTools="ResizeCyLand"))
	int32 ResizeCyLand_QuadsPerSection;

	// Number of sections per CyLand component
	UPROPERTY(Category="Change Component Size", EditAnywhere, NonTransactional, meta=(DisplayName="Sections Per Component", ShowForTools="ResizeCyLand"))
	int32 ResizeCyLand_SectionsPerComponent;

	// Number of components in resulting CyLand
	UPROPERTY(Category="Change Component Size", EditAnywhere, NonTransactional, meta=(DisplayName="Number of Components", ShowForTools="ResizeCyLand"))
	FIntPoint ResizeCyLand_ComponentCount;

	// Determines how the new component size will be applied to the existing CyLand geometry.
	UPROPERTY(Category="Change Component Size", EditAnywhere, NonTransactional, meta=(DisplayName="Resize Mode", ShowForTools="ResizeCyLand"))
	ECyLandConvertMode ResizeCyLand_ConvertMode;

	int32 ResizeCyLand_Original_QuadsPerSection;
	int32 ResizeCyLand_Original_SectionsPerComponent;
	FIntPoint ResizeCyLand_Original_ComponentCount;

	// New CyLand "Tool"

	// Material initially applied to the CyLand. Setting a material here exposes properties for setting up layer info based on the CyLand blend nodes in the material.
	UPROPERTY(Category="New CyLand", EditAnywhere, meta=(DisplayName="Material", ShowForTools="NewCyLand"))
	TWeakObjectPtr<UMaterialInterface> NewCyLand_Material;

	// The number of quads in a single CyLand section. One section is the unit of LOD transition for CyLand rendering.
	UPROPERTY(Category="New CyLand", EditAnywhere, meta=(DisplayName="Section Size", ShowForTools="NewCyLand"))
	int32 NewCyLand_QuadsPerSection;

	// The number of sections in a single CyLand component. This along with the section size determines the size of each CyLand component. A component is the base unit of rendering and culling.
	UPROPERTY(Category="New CyLand", EditAnywhere, meta=(DisplayName="Sections Per Component", ShowForTools="NewCyLand"))
	int32 NewCyLand_SectionsPerComponent;

	// The number of components in the X and Y direction, determining the overall size of the CyLand.
	UPROPERTY(Category="New CyLand", EditAnywhere, meta=(DisplayName="Number of Components", ShowForTools="NewCyLand"))
	FIntPoint NewCyLand_ComponentCount;

	// The location of the new CyLand
	UPROPERTY(Category="New CyLand", EditAnywhere, meta=(DisplayName="Location", ShowForTools="NewCyLand"))
	FVector NewCyLand_Location;

	// The rotation of the new CyLand
	UPROPERTY(Category="New CyLand", EditAnywhere, meta=(DisplayName="Rotation", ShowForTools="NewCyLand"))
	FRotator NewCyLand_Rotation;

	// The scale of the new CyLand. This is the distance between each vertex on the CyLand, defaulting to 100 units.
	UPROPERTY(Category="New CyLand", EditAnywhere, meta=(DisplayName="Scale", ShowForTools="NewCyLand"))
	FVector NewCyLand_Scale;

	UPROPERTY(Category="New CyLand", VisibleAnywhere, NonTransactional, meta=(ShowForTools="NewCyLand"))
	ECyLandImportResult ImportCyLand_HeightmapImportResult;

	UPROPERTY(Category="New CyLand", VisibleAnywhere, NonTransactional, meta=(ShowForTools="NewCyLand"))
	FText ImportCyLand_HeightmapErrorMessage;

	// Specify a height map file in 16-bit RAW or PNG format
	UPROPERTY(Category="New CyLand", EditAnywhere, NonTransactional, meta=(DisplayName="Heightmap File", ShowForTools="NewCyLand"))
	FString ImportCyLand_HeightmapFilename;
	UPROPERTY(NonTransactional)
	uint32 ImportCyLand_Width;
	UPROPERTY(NonTransactional)
	uint32 ImportCyLand_Height;

private:
	UPROPERTY(NonTransactional)
	TArray<uint16> ImportCyLand_Data;
public:

	// Whether the imported alpha maps are to be interpreted as "layered" or "additive" (UE4 uses additive internally)
	UPROPERTY(Category="New CyLand", EditAnywhere, NonTransactional, meta=(DisplayName="Layer Alphamap Type", ShowForTools="NewCyLand"))
	ECyLandImportAlphamapType ImportCyLand_AlphamapType;

	// The CyLand layers that will be created. Only layer names referenced in the material assigned above are shown here. Modify the material to add more layers.
	UPROPERTY(Category="New CyLand", EditAnywhere, NonTransactional, EditFixedSize, meta=(DisplayName="Layers", ShowForTools="NewCyLand"))
	TArray<FCyLandImportLayer> ImportCyLand_Layers;

	// Common Brush Settings:

	// The radius of the brush, in unreal units
	UPROPERTY(Category="Brush Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Brush Size", ShowForBrushes="BrushSet_Circle,BrushSet_Alpha,BrushSet_Pattern", ClampMin="1", ClampMax="65536", UIMin="1", UIMax="8192", SliderExponent="3"))
	float BrushRadius;

	// The falloff at the edge of the brush, as a fraction of the brush's size. 0 = no falloff, 1 = all falloff
	UPROPERTY(Category="Brush Settings", EditAnywhere, NonTransactional, meta=(ShowForBrushes="BrushSet_Circle,BrushSet_Gizmo,BrushSet_Pattern", ClampMin="0", ClampMax="1"))
	float BrushFalloff;

	// Selects the Clay Brush painting mode
	UPROPERTY(Category="Brush Settings", EditAnywhere, NonTransactional, meta=(ShowForTools="Sculpt", ShowForBrushes="BrushSet_Circle,BrushSet_Alpha,BrushSet_Pattern"))
	bool bUseClayBrush;

	// Alpha/Pattern Brush:

	// Scale of the brush texture. A scale of 1.000 maps the brush texture to the CyLand at a 1 pixel = 1 vertex size
	UPROPERTY(Category="Brush Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Texture Scale", ShowForBrushes="BrushSet_Pattern", ClampMin="0.005", ClampMax="5", SliderExponent="3"))
	float AlphaBrushScale;

	// Rotate brush to follow mouse
	UPROPERTY(Category = "Brush Settings", EditAnywhere, NonTransactional, meta = (DisplayName = "Auto-Rotate", ShowForBrushes = "BrushSet_Alpha"))
	bool bAlphaBrushAutoRotate;

	// Rotates the brush mask texture
	UPROPERTY(Category="Brush Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Texture Rotation", ShowForBrushes="BrushSet_Alpha,BrushSet_Pattern", ClampMin="-360", ClampMax="360", UIMin="-180", UIMax="180"))
	float AlphaBrushRotation;

	// Horizontally offsets the brush mask texture
	UPROPERTY(Category="Brush Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Texture Pan U", ShowForBrushes="BrushSet_Pattern", ClampMin="0", ClampMax="1"))
	float AlphaBrushPanU;

	// Vertically offsets the brush mask texture
	UPROPERTY(Category="Brush Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Texture Pan V", ShowForBrushes="BrushSet_Pattern", ClampMin="0", ClampMax="1"))
	float AlphaBrushPanV;

	UPROPERTY(Category = "Brush Settings", EditAnywhere, NonTransactional, meta = (DisplayName = "Use World-Space", ShowForBrushes = "BrushSet_Pattern"))
	bool bUseWorldSpacePatternBrush;

	UPROPERTY(Category = "Brush Settings", EditAnywhere, NonTransactional, meta = (DisplayName = "World-Space Settings", ShowForBrushes = "BrushSet_Pattern", EditCondition = "bUseWorldSpacePatternBrush"))
	FCyLandPatternBrushWorldSpaceSettings WorldSpacePatternBrushSettings;

	// Mask texture to use
	UPROPERTY(Category="Brush Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Texture", ShowForBrushes="BrushSet_Alpha,BrushSet_Pattern"))
	UTexture2D* AlphaTexture;

	// Channel of Mask Texture to use
	UPROPERTY(Category="Brush Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Texture Channel", ShowForBrushes="BrushSet_Alpha,BrushSet_Pattern"))
	TEnumAsByte<ECyColorChannel::Type> AlphaTextureChannel;

	UPROPERTY(NonTransactional)
	int32 AlphaTextureSizeX;
	UPROPERTY(NonTransactional)
	int32 AlphaTextureSizeY;
	UPROPERTY(NonTransactional)
	TArray<uint8> AlphaTextureData;

	// Component Brush:

	// Number of components X/Y to affect at once. 1 means 1x1, 2 means 2x2, etc
	UPROPERTY(Category="Brush Settings", EditAnywhere, NonTransactional, meta=(DisplayName="Brush Size", ShowForBrushes="BrushSet_Component", ClampMin="1", ClampMax="128", UIMin="1", UIMax="64", SliderExponent="3"))
	int32 BrushComponentSize;


	// Target Layer Settings:

	// Limits painting to only the components that already have the selected layer
	UPROPERTY(Category="Target Layers", EditAnywhere, NonTransactional, meta=(ShowForTargetTypes="Weightmap,Visibility"))
	ECyLandLayerPaintingRestriction PaintingRestriction;

	// Display order of the targets
	UPROPERTY(Category = "Target Layers", EditAnywhere)
	ECyLandLayerDisplayMode TargetDisplayOrder;

	UPROPERTY(Category = "Target Layers", EditAnywhere)
	bool ShowUnusedLayers;	

#if WITH_EDITOR
	//~ Begin UObject Interface
	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject Interface
#endif // WITH_EDITOR

	void Load();
	void Save();

	// Region
	void SetbUseSelectedRegion(bool InbUseSelectedRegion);
	void SetbUseNegativeMask(bool InbUseNegativeMask);

	// Copy/Paste
	void SetPasteMode(ECyLandToolPasteMode InPasteMode);

	// Alpha/Pattern Brush
	bool SetAlphaTexture(UTexture2D* InTexture, ECyColorChannel::Type InTextureChannel);

	// New CyLand
	FString LastImportPath;

	const TArray<uint16>& GetImportCyLandData() const { return ImportCyLand_Data; }
	void ClearImportCyLandData() { ImportCyLand_Data.Empty(); }

	void ImportCyLandData();
	void RefreshImportLayersList();
	
	void UpdateComponentLayerWhitelist();

	int32 ClampCyLandSize(int32 InComponentsCount) const
	{
		// Max size is either whole components below 8192 verts, or 32 components
		return FMath::Clamp(InComponentsCount, 1, FMath::Min(32, FMath::FloorToInt(8191 / (NewCyLand_SectionsPerComponent * NewCyLand_QuadsPerSection))));
	}
	
	int32 CalcComponentsCount(int32 InResolution) const
	{
		return ClampCyLandSize(InResolution / (NewCyLand_SectionsPerComponent * NewCyLand_QuadsPerSection));
	}

	void NewCyLand_ClampSize()
	{
		NewCyLand_ComponentCount.X = ClampCyLandSize(NewCyLand_ComponentCount.X);
		NewCyLand_ComponentCount.Y = ClampCyLandSize(NewCyLand_ComponentCount.Y);
	}

	void UpdateComponentCount()
	{
		// ignore invalid cases
		if (ResizeCyLand_QuadsPerSection == 0 || ResizeCyLand_SectionsPerComponent == 0 ||ResizeCyLand_ComponentCount.X == 0 || ResizeCyLand_ComponentCount.Y == 0) 
		{
			return;
		}
		const int32 ComponentSizeQuads = ResizeCyLand_QuadsPerSection * ResizeCyLand_SectionsPerComponent;
		const int32 Original_ComponentSizeQuads = ResizeCyLand_Original_QuadsPerSection * ResizeCyLand_Original_SectionsPerComponent;
		const FIntPoint OriginalResolution = ResizeCyLand_Original_ComponentCount * Original_ComponentSizeQuads;
		switch (ResizeCyLand_ConvertMode)
		{
		case ECyLandConvertMode::Expand:
			ResizeCyLand_ComponentCount.X = FMath::DivideAndRoundUp(OriginalResolution.X, ComponentSizeQuads);
			ResizeCyLand_ComponentCount.Y = FMath::DivideAndRoundUp(OriginalResolution.Y, ComponentSizeQuads);
			break;
		case ECyLandConvertMode::Clip:
			ResizeCyLand_ComponentCount.X = FMath::Max(1, OriginalResolution.X / ComponentSizeQuads);
			ResizeCyLand_ComponentCount.Y = FMath::Max(1, OriginalResolution.Y / ComponentSizeQuads);
			break;
		case ECyLandConvertMode::Resample:
			ResizeCyLand_ComponentCount = ResizeCyLand_Original_ComponentCount;
			break;
		default:
			check(0);
		}
	}

	void SetbSnapGizmo(bool InbSnapGizmo);

	void SetParent(FEdModeCyLand* CyLandParent)
	{
		ParentMode = CyLandParent;
	}

	void UpdateTargetLayerDisplayOrder();
	void UpdateShowUnusedLayers();
};
