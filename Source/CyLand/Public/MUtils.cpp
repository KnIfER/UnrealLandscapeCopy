


#include "MUtils.h"
#include "Engine/TextureRenderTarget2D.h"
//#include "Engine/Public/FLegacyScreenPercentageDriver.h"
#include "RendererInterface.h"
#include "CanvasTypes.h"
#include "SceneView.h"

#include "CyLandProxy.h"
#include "CyLandComponent.h"



#include "EngineDefines.h"
#include "ShowFlags.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Engine/Texture2D.h"
#include "Misc/App.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Modules/ModuleManager.h"
#include "Misc/PackageName.h"
#include "LegacyScreenPercentageDriver.h"

#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Engine/TextureCube.h"
#include "SceneView.h"
#include "RendererInterface.h"
#include "EngineModule.h"
#include "ImageUtils.h"
#include "CanvasTypes.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "MaterialCompiler.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "Materials/MaterialParameterCollection.h"
#include "LandscapeProxy.h"
#include "LandscapeComponent.h"
#include "Engine/MeshMerging.h"
#include "Engine/StaticMesh.h"
#include "MeshUtilities.h"
#include "MeshMergeData.h"
#include "Templates/UniquePtr.h"


#include "MeshDescription.h"
#include "MeshAttributes.h"
#include "MeshAttributeArray.h"
#include "MeshDescriptionOperations.h"


#if WITH_EDITOR
#include "DeviceProfiles/DeviceProfile.h"
#include "Tests/AutomationEditorCommon.h"
#endif // WITH_EDITOR

static void RenderSceneToTexture(
	FSceneInterface* Scene,
	const FName& VisualizationMode,
	const FVector& ViewOrigin,
	const FMatrix& ViewRotationMatrix,
	const FMatrix& ProjectionMatrix,
	const TSet<FPrimitiveComponentId>& HiddenPrimitives,
	FIntPoint TargetSize,
	float TargetGamma,
	TArray<FColor>& OutSamples)
{
	auto RenderTargetTexture = NewObject<UTextureRenderTarget2D>();
	check(RenderTargetTexture);
	RenderTargetTexture->AddToRoot();
	RenderTargetTexture->ClearColor = FLinearColor::Transparent;
	RenderTargetTexture->TargetGamma = TargetGamma;
	RenderTargetTexture->InitCustomFormat(TargetSize.X, TargetSize.Y, PF_FloatRGBA, false);
	FTextureRenderTargetResource* RenderTargetResource = RenderTargetTexture->GameThread_GetRenderTargetResource();

	FSceneViewFamilyContext ViewFamily(
		FSceneViewFamily::ConstructionValues(RenderTargetResource, Scene, FEngineShowFlags(ESFIM_Game))
		.SetWorldTimes(FApp::GetCurrentTime() - GStartTime, FApp::GetDeltaTime(), FApp::GetCurrentTime() - GStartTime)
	);

	// To enable visualization mode
	ViewFamily.EngineShowFlags.SetPostProcessing(true);
	ViewFamily.EngineShowFlags.SetVisualizeBuffer(true);
	ViewFamily.EngineShowFlags.SetTonemapper(false);
	ViewFamily.EngineShowFlags.SetScreenPercentage(false);

	FSceneViewInitOptions ViewInitOptions;
	ViewInitOptions.SetViewRectangle(FIntRect(0, 0, TargetSize.X, TargetSize.Y));
	ViewInitOptions.ViewFamily = &ViewFamily;
	ViewInitOptions.HiddenPrimitives = HiddenPrimitives;
	ViewInitOptions.ViewOrigin = ViewOrigin;
	ViewInitOptions.ViewRotationMatrix = ViewRotationMatrix;
	ViewInitOptions.ProjectionMatrix = ProjectionMatrix;

	FSceneView* NewView = new FSceneView(ViewInitOptions);
	NewView->CurrentBufferVisualizationMode = VisualizationMode;
	ViewFamily.Views.Add(NewView);

	ViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(
		ViewFamily, /* GlobalResolutionFraction = */ 1.0f, /* AllowPostProcessSettingsScreenPercentage = */ false));

	FCanvas Canvas(RenderTargetResource, NULL, FApp::GetCurrentTime() - GStartTime, FApp::GetDeltaTime(), FApp::GetCurrentTime() - GStartTime, Scene->GetFeatureLevel());
	Canvas.Clear(FLinearColor::Transparent);
	GetRendererModule().BeginRenderingViewFamily(&Canvas, &ViewFamily);

	// Copy the contents of the remote texture to system memory
	OutSamples.SetNumUninitialized(TargetSize.X*TargetSize.Y);
	FReadSurfaceDataFlags ReadSurfaceDataFlags;
	ReadSurfaceDataFlags.SetLinearToGamma(false);
	RenderTargetResource->ReadPixelsPtr(OutSamples.GetData(), ReadSurfaceDataFlags, FIntRect(0, 0, TargetSize.X, TargetSize.Y));
	FlushRenderingCommands();

	RenderTargetTexture->RemoveFromRoot();
	RenderTargetTexture = nullptr;
}



UTexture2D* FMUtils::CreateTexture(UPackage* Outer, const FString& AssetLongName, FIntPoint Size, const TArray<FColor>& Samples, TextureCompressionSettings CompressionSettings, TextureGroup LODGroup, EObjectFlags Flags, bool bSRGB, const FGuid& SourceGuidHash)
{
	FCreateTexture2DParameters TexParams;
	TexParams.bUseAlpha = false;
	TexParams.CompressionSettings = CompressionSettings;
	TexParams.bDeferCompression = true;
	TexParams.bSRGB = bSRGB;
	TexParams.SourceGuidHash = SourceGuidHash;

	if (Outer == nullptr)
	{
		Outer = CreatePackage(NULL, *AssetLongName);
		Outer->FullyLoad();
		Outer->Modify();
	}

	UTexture2D* Texture = FImageUtils::CreateTexture2D(Size.X, Size.Y, Samples, Outer, FPackageName::GetShortName(AssetLongName), Flags, TexParams);
	Texture->LODGroup = LODGroup;
	Texture->PostEditChange();

	return Texture;
}


bool FMUtils::ExportBaseColor(UCyLandComponent* CyLandComponent, int32 TextureSize, TArray<FColor>& OutSamples)
{
	ACyLandProxy* CyLandProxy = CyLandComponent->GetCyLandProxy();

	FIntPoint ComponentOrigin = CyLandComponent->GetSectionBase() - CyLandProxy->CyLandSectionOffset;
	FIntPoint ComponentSize(CyLandComponent->ComponentSizeQuads, CyLandComponent->ComponentSizeQuads);
	FVector MidPoint = FVector(ComponentOrigin, 0.f) + FVector(ComponentSize, 0.f)*0.5f;

	FVector CyLandCenter = CyLandProxy->GetTransform().TransformPosition(MidPoint);
	FVector CyLandExtent = FVector(ComponentSize, 0.f)*CyLandProxy->GetActorScale()*0.5f;

	FVector ViewOrigin = CyLandCenter;
	FMatrix ViewRotationMatrix = FInverseRotationMatrix(CyLandProxy->GetActorRotation());
	ViewRotationMatrix *= FMatrix(FPlane(1, 0, 0, 0),
		FPlane(0, -1, 0, 0),
		FPlane(0, 0, -1, 0),
		FPlane(0, 0, 0, 1));

	const float ZOffset = WORLD_MAX;
	FMatrix ProjectionMatrix = FReversedZOrthoMatrix(
		CyLandExtent.X,
		CyLandExtent.Y,
		0.5f / ZOffset,
		ZOffset);

	FSceneInterface* Scene = CyLandProxy->GetWorld()->Scene;

	// Hide all but the component
	TSet<FPrimitiveComponentId> HiddenPrimitives;
	for (auto PrimitiveComponentId : Scene->GetScenePrimitiveComponentIds())
	{
		HiddenPrimitives.Add(PrimitiveComponentId);
	}
	HiddenPrimitives.Remove(CyLandComponent->SceneProxy->GetPrimitiveComponentId());

	FIntPoint TargetSize(TextureSize, TextureSize);

	// Render diffuse texture using BufferVisualizationMode=BaseColor
	static const FName BaseColorName("BaseColor");
	const float BaseColorGamma = 2.2f;
	RenderSceneToTexture(Scene, BaseColorName, ViewOrigin, ViewRotationMatrix, ProjectionMatrix, HiddenPrimitives, TargetSize, BaseColorGamma, OutSamples);
	return true;
}