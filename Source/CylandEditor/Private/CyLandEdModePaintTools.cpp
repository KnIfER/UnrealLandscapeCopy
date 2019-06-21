// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/App.h"
#include "InputCoreTypes.h"
#include "Engine/EngineTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "CyLandToolInterface.h"
#include "CyLandProxy.h"
#include "CyLandEdMode.h"
#include "CyLandEditorObject.h"
#include "CyLandEdit.h"
#include "CyLandDataAccess.h"
#include "CyLandEdModeTools.h"
#include "Settings/EditorExperimentalSettings.h"
#include "CyLand.h"
#include "Logging/TokenizedMessage.h"
#include "Logging/MessageLog.h"
#include "Misc/MapErrors.h"

#define LOCTEXT_NAMESPACE "CyLandTools"

const int32 FNoiseParameter::Permutations[256] =
{
	151, 160, 137, 91, 90, 15,
	131, 13, 201, 95, 96, 53, 194, 233, 7, 225, 140, 36, 103, 30, 69, 142, 8, 99, 37, 240, 21, 10, 23,
	190, 6, 148, 247, 120, 234, 75, 0, 26, 197, 62, 94, 252, 219, 203, 117, 35, 11, 32, 57, 177, 33,
	88, 237, 149, 56, 87, 174, 20, 125, 136, 171, 168, 68, 175, 74, 165, 71, 134, 139, 48, 27, 166,
	77, 146, 158, 231, 83, 111, 229, 122, 60, 211, 133, 230, 220, 105, 92, 41, 55, 46, 245, 40, 244,
	102, 143, 54, 65, 25, 63, 161, 1, 216, 80, 73, 209, 76, 132, 187, 208, 89, 18, 169, 200, 196,
	135, 130, 116, 188, 159, 86, 164, 100, 109, 198, 173, 186, 3, 64, 52, 217, 226, 250, 124, 123,
	5, 202, 38, 147, 118, 126, 255, 82, 85, 212, 207, 206, 59, 227, 47, 16, 58, 17, 182, 189, 28, 42,
	223, 183, 170, 213, 119, 248, 152, 2, 44, 154, 163, 70, 221, 153, 101, 155, 167, 43, 172, 9,
	129, 22, 39, 253, 19, 98, 108, 110, 79, 113, 224, 232, 178, 185, 112, 104, 218, 246, 97, 228,
	251, 34, 242, 193, 238, 210, 144, 12, 191, 179, 162, 241, 81, 51, 145, 235, 249, 14, 239, 107,
	49, 192, 214, 31, 181, 199, 106, 157, 184, 84, 204, 176, 115, 121, 50, 45, 127, 4, 150, 254,
	138, 236, 205, 93, 222, 114, 67, 29, 24, 72, 243, 141, 128, 195, 78, 66, 215, 61, 156, 180
};



// 
// FCyLandToolPaintBase
//


template<class TToolTarget, class TStrokeClass>
class FCyLandToolPaintBase : public FCyLandToolBase<TStrokeClass>
{
public:
	FCyLandToolPaintBase(FEdModeCyLand* InEdMode)
		: FCyLandToolBase<TStrokeClass>(InEdMode)
	{
	}

	virtual ECyLandToolTargetTypeMask::Type GetSupportedTargetTypes() override
	{
		return ECyLandToolTargetTypeMask::FromType(TToolTarget::TargetType);
	}

	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override
	{
		FCyLandToolBase<TStrokeClass>::Tick(ViewportClient, DeltaTime);

		if (GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape && this->IsToolActive())
		{
			ACyLand* CyLand = this->EdMode->CurrentToolTarget.CyLandInfo->CyLandActor.Get();
			if (CyLand != nullptr)
			{
				CyLand->RequestProceduralContentUpdate(this->EdMode->CurrentToolTarget.TargetType == ECyLandToolTargetType::Type::Heightmap ? EProceduralContentUpdateFlag::Heightmap_Render : EProceduralContentUpdateFlag::Weightmap_Render);
			}
		}
	}

	virtual bool BeginTool(FEditorViewportClient* ViewportClient, const FCyLandToolTarget& InTarget, const FVector& InHitLocation) override
	{
		if (GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
		{
			ACyLand* CyLand = this->EdMode->CurrentToolTarget.CyLandInfo->CyLandActor.Get();
			if (CyLand != nullptr)
			{
				CyLand->RequestProceduralContentUpdate(this->EdMode->CurrentToolTarget.TargetType == ECyLandToolTargetType::Type::Heightmap ? EProceduralContentUpdateFlag::Heightmap_Render : EProceduralContentUpdateFlag::Weightmap_Render);
			}

			this->EdMode->ChangeHeightmapsToCurrentProceduralLayerHeightmaps(false);
		}

		return FCyLandToolBase<TStrokeClass>::BeginTool(ViewportClient, InTarget, InHitLocation);
	}

	virtual void EndTool(FEditorViewportClient* ViewportClient) override
	{
		if (GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
		{
			if (this->EdMode->CurrentToolTarget.TargetType == ECyLandToolTargetType::Type::Heightmap)
			{
				this->EdMode->ChangeHeightmapsToCurrentProceduralLayerHeightmaps(true);

				if (this->EdMode->CurrentToolTarget.CyLandInfo->CyLandActor.IsValid())
				{
					this->EdMode->CurrentToolTarget.CyLandInfo->CyLandActor->RequestProceduralContentUpdate(EProceduralContentUpdateFlag::Heightmap_All);
				}
			}
			else
			{
				// TODO: Activate/Deactivate weightmap layers
				if (this->EdMode->CurrentToolTarget.CyLandInfo->CyLandActor.IsValid())
				{
					this->EdMode->CurrentToolTarget.CyLandInfo->CyLandActor->RequestProceduralContentUpdate(EProceduralContentUpdateFlag::Weightmap_All);
				}
			}
		}

		FCyLandToolBase<TStrokeClass>::EndTool(ViewportClient);
	}
};


template<class ToolTarget>
class FCyLandToolStrokePaintBase : public FCyLandToolStrokeBase
{
public:
	FCyLandToolStrokePaintBase(FEdModeCyLand* InEdMode, FEditorViewportClient* InViewportClient, const FCyLandToolTarget& InTarget)
		: FCyLandToolStrokeBase(InEdMode, InViewportClient, InTarget)
		, Cache(InTarget)
	{
	}

protected:
	typename ToolTarget::CacheClass Cache;
};



// 
// FCyLandToolPaint
//
class FCyLandToolStrokePaint : public FCyLandToolStrokePaintBase<FWeightmapToolTarget>
{
	typedef FWeightmapToolTarget ToolTarget;

	TMap<FIntPoint, float> TotalInfluenceMap; // amount of time and weight the brush has spent on each vertex.

	bool bIsWhitelistMode;
	bool bAddToWhitelist;
public:
	// Heightmap sculpt tool will continuously sculpt in the same location, weightmap paint tool doesn't
	enum { UseContinuousApply = false };

	FCyLandToolStrokePaint(FEdModeCyLand* InEdMode, FEditorViewportClient* InViewportClient, const FCyLandToolTarget& InTarget)
		: FCyLandToolStrokePaintBase<FWeightmapToolTarget>(InEdMode, InViewportClient, InTarget)
		, bIsWhitelistMode(EdMode->UISettings->PaintingRestriction == ECyLandLayerPaintingRestriction::UseComponentWhitelist &&
		                   (InViewportClient->Viewport->KeyState(EKeys::Equals) || InViewportClient->Viewport->KeyState(EKeys::Hyphen)))
		, bAddToWhitelist(bIsWhitelistMode && InViewportClient->Viewport->KeyState(EKeys::Equals))
	{
	}

	void Apply(FEditorViewportClient* ViewportClient, FCyLandBrush* Brush, const UCyLandEditorObject* UISettings, const TArray<FCyLandToolInteractorPosition>& InteractorPositions)
	{
		// Invert when holding Shift
		//UE_LOG(LogCyLand, Log, TEXT("bInvert = %d"), bInvert);
		bool bInvert = InteractorPositions.Last().bModifierPressed;

		if (bIsWhitelistMode)
		{
			// Get list of components to delete from brush
			// TODO - only retrieve bounds as we don't need the vert data
			FCyLandBrushData BrushInfo = Brush->ApplyBrush(InteractorPositions);
			if (!BrushInfo)
			{
				return;
			}

			int32 X1, Y1, X2, Y2;
			BrushInfo.GetInclusiveBounds(X1, Y1, X2, Y2);

			// Shrink bounds by 1,1 to avoid GetComponentsInRegion picking up extra components on all sides due to the overlap between components
			TSet<UCyLandComponent*> SelectedComponents;
			CyLandInfo->GetComponentsInRegion(X1 + 1, Y1 + 1, X2 - 1, Y2 - 1, SelectedComponents);

			for (UCyLandComponent* Component : SelectedComponents)
			{
				Component->Modify();
			}

			if (bAddToWhitelist)
			{
				for (UCyLandComponent* Component : SelectedComponents)
				{
					Component->LayerWhitelist.AddUnique(Target.LayerInfo.Get());
				}
			}
			else
			{
				FCyLandEditDataInterface CyLandEdit(CyLandInfo);
				for (UCyLandComponent* Component : SelectedComponents)
				{
					Component->LayerWhitelist.RemoveSingle(Target.LayerInfo.Get());
					Component->DeleteLayer(Target.LayerInfo.Get(), CyLandEdit);
				}
			}

			return;
		}

		// Get list of verts to update
		FCyLandBrushData BrushInfo = Brush->ApplyBrush(InteractorPositions);
		if (!BrushInfo)
		{
			return;
		}

		int32 X1, Y1, X2, Y2;
		BrushInfo.GetInclusiveBounds(X1, Y1, X2, Y2);

		// Tablet pressure
		float Pressure = ViewportClient->Viewport->IsPenActive() ? ViewportClient->Viewport->GetTabletPressure() : 1.0f;

		this->Cache.CacheData(X1, Y1, X2, Y2);

		bool bUseWeightTargetValue = UISettings->bUseWeightTargetValue;

		// The data we'll be writing to
		TArray<ToolTarget::CacheClass::DataType> Data;
		this->Cache.GetCachedData(X1, Y1, X2, Y2, Data);

		// The source data we use for editing. 
		TArray<ToolTarget::CacheClass::DataType>* SourceDataArrayPtr = &Data;
		TArray<ToolTarget::CacheClass::DataType> OriginalData;

		if (!bUseWeightTargetValue)
		{
			// When painting weights (and not using target value mode), we use a source value that tends more
			// to the current value as we paint over the same region multiple times.
			// TODO: Make this frame-rate independent
			this->Cache.GetOriginalData(X1, Y1, X2, Y2, OriginalData);
			SourceDataArrayPtr = &OriginalData;

			for (int32 Y = Y1; Y < Y2; Y++)
			{
				auto* DataScanline = Data.GetData() + (Y - Y1) * (X2 - X1 + 1) + (0 - X1);
				auto* OriginalDataScanline = OriginalData.GetData() + (Y - Y1) * (X2 - X1 + 1) + (0 - X1);
				for (int32 X = X1; X < X2; X++)
				{
					float VertexInfluence = TotalInfluenceMap.FindRef(FIntPoint(X, Y));

					auto& CurrentValue = DataScanline[X];
					auto& SourceValue = OriginalDataScanline[X];

					SourceValue = FMath::Lerp(SourceValue, CurrentValue, FMath::Min<float>(VertexInfluence * 0.05f, 1.0f));
				}
			}
		}

		// Adjust strength based on brush size and drawscale, so strength 1 = one hemisphere
		const float AdjustedStrength = ToolTarget::StrengthMultiplier(this->CyLandInfo, UISettings->BrushRadius);
		FWeightmapToolTarget::CacheClass::DataType DestValue = FWeightmapToolTarget::CacheClass::ClampValue(255.0f * UISettings->WeightTargetValue);

		float PaintStrength = UISettings->ToolStrength * Pressure * AdjustedStrength;

		// TODO: make paint tool framerate independent like the sculpt tool
		// const float DeltaTime = FMath::Min<float>(FApp::GetDeltaTime(), 0.1f); // Under 10 fps slow down paint speed
		// SculptStrength *= DeltaTime * 3.0f; // * 3.0f to partially compensate for impact of DeltaTime on slowing the tools down compared to the old framerate-dependent version

		if (PaintStrength <= 0.0f)
		{
			return;
		}

		if (!bUseWeightTargetValue)
		{
			PaintStrength = FMath::Max(PaintStrength, 1.0f);
		}

		// Apply the brush
		for (int32 Y = BrushInfo.GetBounds().Min.Y; Y < BrushInfo.GetBounds().Max.Y; Y++)
		{
			const float* BrushScanline = BrushInfo.GetDataPtr(FIntPoint(0, Y));
			auto* DataScanline = Data.GetData() + (Y - Y1) * (X2 - X1 + 1) + (0 - X1);
			auto* SourceDataScanline = SourceDataArrayPtr->GetData() + (Y - Y1) * (X2 - X1 + 1) + (0 - X1);

			for (int32 X = BrushInfo.GetBounds().Min.X; X < BrushInfo.GetBounds().Max.X; X++)
			{
				const FIntPoint Key = FIntPoint(X, Y);
				const float BrushValue = BrushScanline[X];

				// Update influence map
				float VertexInfluence = TotalInfluenceMap.FindRef(Key);
				TotalInfluenceMap.Add(Key, VertexInfluence + BrushValue);

				float PaintAmount = BrushValue * PaintStrength;
				auto& CurrentValue = DataScanline[X];
				const auto& SourceValue = SourceDataScanline[X];

				if (bUseWeightTargetValue)
				{
					CurrentValue = FMath::Lerp(CurrentValue, DestValue, PaintAmount / AdjustedStrength);
				}
				else
				{
					if (bInvert)
					{
						CurrentValue = ToolTarget::CacheClass::ClampValue(FMath::Min<int32>(SourceValue - FMath::RoundToInt(PaintAmount), CurrentValue));
					}
					else
					{
						CurrentValue = ToolTarget::CacheClass::ClampValue(FMath::Max<int32>(SourceValue + FMath::RoundToInt(PaintAmount), CurrentValue));
					}
				}
			}
		}

		ACyLand* CyLand = CyLandInfo->CyLandActor.Get();

		if (CyLand != nullptr && CyLand->HasProceduralContent && !GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
		{
			FMessageLog("MapCheck").Warning()->AddToken(FTextToken::Create(LOCTEXT("CyLandProcedural_ChangingDataWithoutSettings", "This map contains CyLand procedural content, modifying the CyLand data will result in data loss when the map is reopened with CyLand Procedural settings on. Please enable CyLand Procedural settings before modifying the data.")));
			FMessageLog("MapCheck").Open(EMessageSeverity::Warning);
		}

		this->Cache.SetCachedData(X1, Y1, X2, Y2, Data, UISettings->PaintingRestriction);
		this->Cache.Flush();
	}
};

class FCyLandToolPaint : public FCyLandToolPaintBase<FWeightmapToolTarget, FCyLandToolStrokePaint>
{
public:
	FCyLandToolPaint(FEdModeCyLand* InEdMode)
		: FCyLandToolPaintBase<FWeightmapToolTarget, FCyLandToolStrokePaint>(InEdMode)
	{
	}

	virtual const TCHAR* GetToolName() override { return TEXT("Paint"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "CyLandMode_Paint", "Paint"); };

	virtual void EnterTool()
	{
		if (EdMode->UISettings->PaintingRestriction == ECyLandLayerPaintingRestriction::UseComponentWhitelist)
		{
			EdMode->UISettings->UpdateComponentLayerWhitelist();
		}

		FCyLandToolPaintBase::EnterTool();
	}
};

//
class FCyLandToolStrokeSculpt : public FCyLandToolStrokePaintBase<FHeightmapToolTarget>
{
	typedef FHeightmapToolTarget ToolTarget;

public:
	// Heightmap sculpt tool will continuously sculpt in the same location, weightmap paint tool doesn't
	enum { UseContinuousApply = true };

	FCyLandToolStrokeSculpt(FEdModeCyLand* InEdMode, FEditorViewportClient* InViewportClient, const FCyLandToolTarget& InTarget)
		: FCyLandToolStrokePaintBase<FHeightmapToolTarget>(InEdMode, InViewportClient, InTarget)
	{
	}

	void Apply(FEditorViewportClient* ViewportClient, FCyLandBrush* Brush, const UCyLandEditorObject* UISettings, const TArray<FCyLandToolInteractorPosition>& InteractorPositions)
	{
		// Invert when holding Shift
		//UE_LOG(LogCyLand, Log, TEXT("bInvert = %d"), bInvert);
		bool bInvert = InteractorPositions.Last().bModifierPressed;

		// Get list of verts to update
		FCyLandBrushData BrushInfo = Brush->ApplyBrush(InteractorPositions);
		if (!BrushInfo)
		{
			return;
		}

		int32 X1, Y1, X2, Y2;
		BrushInfo.GetInclusiveBounds(X1, Y1, X2, Y2);

		// Tablet pressure
		float Pressure = ViewportClient->Viewport->IsPenActive() ? ViewportClient->Viewport->GetTabletPressure() : 1.0f;

		// expand the area by one vertex in each direction to ensure normals are calculated correctly
		X1 -= 1;
		Y1 -= 1;
		X2 += 1;
		Y2 += 1;

		this->Cache.CacheData(X1, Y1, X2, Y2);

		bool bUseClayBrush = UISettings->bUseClayBrush;

		// The data we'll be writing to
		TArray<ToolTarget::CacheClass::DataType> Data;
		this->Cache.GetCachedData(X1, Y1, X2, Y2, Data);

		// The source data we use for editing. 
		TArray<ToolTarget::CacheClass::DataType>* SourceDataArrayPtr = &Data;

		FMatrix ToWorld = ToolTarget::ToWorldMatrix(this->CyLandInfo);
		FMatrix FromWorld = ToolTarget::FromWorldMatrix(this->CyLandInfo);

		// Adjust strength based on brush size and drawscale, so strength 1 = one hemisphere
		const float AdjustedStrength = ToolTarget::StrengthMultiplier(this->CyLandInfo, UISettings->BrushRadius);

		float SculptStrength = UISettings->ToolStrength * Pressure * AdjustedStrength;
		const float DeltaTime = FMath::Min<float>(FApp::GetDeltaTime(), 0.1f); // Under 10 fps slow down paint speed
		SculptStrength *= DeltaTime * 3.0f; // * 3.0f to partially compensate for impact of DeltaTime on slowing the tools down compared to the old framerate-dependent version

		if (SculptStrength <= 0.0f)
		{
			return;
		}

		if (!bUseClayBrush)
		{
			SculptStrength = FMath::Max(SculptStrength, 1.0f);
		}

		FPlane BrushPlane;
		TArray<FVector> Normals;

		if (bUseClayBrush)
		{
			// Calculate normals for brush verts in data space
			Normals.Empty(SourceDataArrayPtr->Num());
			Normals.AddZeroed(SourceDataArrayPtr->Num());

			for (int32 Y = Y1; Y < Y2; Y++)
			{
				auto* SourceDataScanline_0 = SourceDataArrayPtr->GetData() + (Y - Y1) * (X2 - X1 + 1) + (0 - X1);
				auto* SourceDataScanline_1 = SourceDataArrayPtr->GetData() + (Y - Y1) * (X2 - X1 + 1) + (0 - X1);
				auto* NormalsScanline_0 = Normals.GetData() + (Y - Y1) * (X2 - X1 + 1) + (0 - X1);
				auto* NormalsScanline_1 = Normals.GetData() + (Y + 1 - Y1) * (X2 - X1 + 1) + (0 - X1);
				for (int32 X = X1; X < X2; X++)
				{
					FVector Vert00 = ToWorld.TransformPosition(FVector((float)X + 0.0f, (float)Y + 0.0f, SourceDataScanline_0[X + 0]));
					FVector Vert01 = ToWorld.TransformPosition(FVector((float)X + 0.0f, (float)Y + 1.0f, SourceDataScanline_1[X + 0]));
					FVector Vert10 = ToWorld.TransformPosition(FVector((float)X + 1.0f, (float)Y + 0.0f, SourceDataScanline_0[X + 1]));
					FVector Vert11 = ToWorld.TransformPosition(FVector((float)X + 1.0f, (float)Y + 1.0f, SourceDataScanline_1[X + 1]));

					FVector FaceNormal1 = ((Vert00 - Vert10) ^ (Vert10 - Vert11)).GetSafeNormal();
					FVector FaceNormal2 = ((Vert11 - Vert01) ^ (Vert01 - Vert00)).GetSafeNormal();

					// contribute to the vertex normals.
					NormalsScanline_0[X + 1] += FaceNormal1;
					NormalsScanline_1[X + 0] += FaceNormal2;
					NormalsScanline_0[X + 0] += FaceNormal1 + FaceNormal2;
					NormalsScanline_1[X + 1] += FaceNormal1 + FaceNormal2;
				}
			}
			for (int32 Y = Y1; Y <= Y2; Y++)
			{
				auto* NormalsScanline = Normals.GetData() + (Y - Y1) * (X2 - X1 + 1) + (0 - X1);
				for (int32 X = X1; X <= X2; X++)
				{
					NormalsScanline[X] = NormalsScanline[X].GetSafeNormal();
				}
			}

			// Find brush centroid location
			FVector AveragePoint(0.0f, 0.0f, 0.0f);
			FVector AverageNormal(0.0f, 0.0f, 0.0f);
			float TotalWeight = 0.0f;
			for (int32 Y = BrushInfo.GetBounds().Min.Y; Y < BrushInfo.GetBounds().Max.Y; Y++)
			{
				const float* BrushScanline = BrushInfo.GetDataPtr(FIntPoint(0, Y));
				auto* SourceDataScanline = SourceDataArrayPtr->GetData() + (Y - Y1) * (X2 - X1 + 1) + (0 - X1);
				auto* NormalsScanline = Normals.GetData() + (Y - Y1) * (X2 - X1 + 1) + (0 - X1);

				for (int32 X = BrushInfo.GetBounds().Min.X; X < BrushInfo.GetBounds().Max.X; X++)
				{
					const float BrushValue = BrushScanline[X];

					if (BrushValue > 0.0f)
					{
						AveragePoint += FVector((float)X * BrushValue, (float)Y * BrushValue, (float)SourceDataScanline[X] * BrushValue);

						FVector SampleNormal = NormalsScanline[X];
						AverageNormal += SampleNormal * BrushValue;

						TotalWeight += BrushValue;
					}
				}
			}

			if (TotalWeight > 0.0f)
			{
				AveragePoint /= TotalWeight;
				AverageNormal = AverageNormal.GetSafeNormal();
			}

			// Convert to world space
			FVector AverageLocation = ToWorld.TransformPosition(AveragePoint);
			FVector StrengthVector = ToWorld.TransformVector(FVector(0, 0, SculptStrength));

			// Brush pushes out in the normal direction
			FVector OffsetVector = AverageNormal * StrengthVector.Z;
			if (bInvert)
			{
				OffsetVector *= -1;
			}

			// World space brush plane
			BrushPlane = FPlane(AverageLocation + OffsetVector, AverageNormal);
		}

		// Apply the brush
		for (int32 Y = BrushInfo.GetBounds().Min.Y; Y < BrushInfo.GetBounds().Max.Y; Y++)
		{
			const float* BrushScanline = BrushInfo.GetDataPtr(FIntPoint(0, Y));
			auto* DataScanline = Data.GetData() + (Y - Y1) * (X2 - X1 + 1) + (0 - X1);
			auto* SourceDataScanline = SourceDataArrayPtr->GetData() + (Y - Y1) * (X2 - X1 + 1) + (0 - X1);

			for (int32 X = BrushInfo.GetBounds().Min.X; X < BrushInfo.GetBounds().Max.X; X++)
			{
				const FIntPoint Key = FIntPoint(X, Y);
				const float BrushValue = BrushScanline[X];

				float SculptAmount = BrushValue * SculptStrength;
				auto& CurrentValue = DataScanline[X];
				const auto& SourceValue = SourceDataScanline[X];

				if (bUseClayBrush)
				{
					// Brush application starts from original world location at start of stroke
					FVector WorldLoc = ToWorld.TransformPosition(FVector(X, Y, SourceValue));

					// Calculate new location on the brush plane
					WorldLoc.Z = (BrushPlane.W - BrushPlane.X*WorldLoc.X - BrushPlane.Y*WorldLoc.Y) / BrushPlane.Z;

					// Painted amount lerps based on brush falloff.
					float PaintValue = FMath::Lerp<float>((float)SourceValue, FromWorld.TransformPosition(WorldLoc).Z, BrushValue);

					if (bInvert)
					{
						CurrentValue = ToolTarget::CacheClass::ClampValue(FMath::Min<int32>(FMath::RoundToInt(PaintValue), CurrentValue));
					}
					else
					{
						CurrentValue = ToolTarget::CacheClass::ClampValue(FMath::Max<int32>(FMath::RoundToInt(PaintValue), CurrentValue));
					}
				}
				else
				{
					if (bInvert)
					{
						CurrentValue = ToolTarget::CacheClass::ClampValue(FMath::Min<int32>(SourceValue - FMath::RoundToInt(SculptAmount), CurrentValue));
					}
					else
					{
						CurrentValue = ToolTarget::CacheClass::ClampValue(FMath::Max<int32>(SourceValue + FMath::RoundToInt(SculptAmount), CurrentValue));
					}
				}
			}
		}

		ACyLand* CyLand = CyLandInfo->CyLandActor.Get();

		if (CyLand != nullptr && CyLand->HasProceduralContent && !GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
		{
			FMessageLog("MapCheck").Warning()->AddToken(FTextToken::Create(LOCTEXT("CyLandProcedural_ChangingDataWithoutSettings", "This map contains CyLand procedural content, modifying the CyLand data will result in data loss when the map is reopened with CyLand Procedural settings on. Please enable CyLand Procedural settings before modifying the data.")));
			FMessageLog("MapCheck").Open(EMessageSeverity::Warning);
		}

		this->Cache.SetCachedData(X1, Y1, X2, Y2, Data);
		this->Cache.Flush();
	}
};

class FCyLandToolSculpt : public FCyLandToolPaintBase<FHeightmapToolTarget, FCyLandToolStrokeSculpt>
{
public:
	FCyLandToolSculpt(FEdModeCyLand* InEdMode)
		: FCyLandToolPaintBase<FHeightmapToolTarget, FCyLandToolStrokeSculpt>(InEdMode)
	{
	}

	virtual const TCHAR* GetToolName() override { return TEXT("Sculpt"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "CyLandMode_Sculpt", "Sculpt"); };
};

// 
// FCyLandToolSmooth
//

template<class ToolTarget>
class FCyLandToolStrokeSmooth : public FCyLandToolStrokePaintBase<ToolTarget>
{
public:
	FCyLandToolStrokeSmooth(FEdModeCyLand* InEdMode, FEditorViewportClient* InViewportClient, const FCyLandToolTarget& InTarget)
		: FCyLandToolStrokePaintBase<ToolTarget>(InEdMode, InViewportClient, InTarget)
	{
	}

	void Apply(FEditorViewportClient* ViewportClient, FCyLandBrush* Brush, const UCyLandEditorObject* UISettings, const TArray<FCyLandToolInteractorPosition>& InteractorPositions)
	{
		if (!this->CyLandInfo) return;

		// Get list of verts to update
		FCyLandBrushData BrushInfo = Brush->ApplyBrush(InteractorPositions);
		if (!BrushInfo)
		{
			return;
		}

		int32 X1, Y1, X2, Y2;
		BrushInfo.GetInclusiveBounds(X1, Y1, X2, Y2);

		// Tablet pressure
		float Pressure = ViewportClient->Viewport->IsPenActive() ? ViewportClient->Viewport->GetTabletPressure() : 1.0f;

		// expand the area by one vertex in each direction to ensure normals are calculated correctly
		CA_SUPPRESS(6326);
		if (ToolTarget::TargetType == ECyLandToolTargetType::Heightmap)
		{
			X1 -= 1;
			Y1 -= 1;
			X2 += 1;
			Y2 += 1;
		}

		this->Cache.CacheData(X1, Y1, X2, Y2);

		TArray<typename ToolTarget::CacheClass::DataType> Data;
		this->Cache.GetCachedData(X1, Y1, X2, Y2, Data);

		const float ToolStrength = FMath::Clamp<float>(UISettings->ToolStrength * Pressure, 0.0f, 1.0f);

		// Apply the brush
		if (UISettings->bDetailSmooth)
		{
			LowPassFilter<typename ToolTarget::CacheClass::DataType>(X1, Y1, X2, Y2, BrushInfo, Data, UISettings->DetailScale, ToolStrength);
		}
		else
		{
			const int32 FilterRadius = UISettings->SmoothFilterKernelSize;

			for (int32 Y = BrushInfo.GetBounds().Min.Y; Y < BrushInfo.GetBounds().Max.Y; Y++)
			{
				const float* BrushScanline = BrushInfo.GetDataPtr(FIntPoint(0, Y));
				auto* DataScanline = Data.GetData() + (Y - Y1) * (X2 - X1 + 1) + (0 - X1);

				for (int32 X = BrushInfo.GetBounds().Min.X; X < BrushInfo.GetBounds().Max.X; X++)
				{
					const float BrushValue = BrushScanline[X];

					if (BrushValue > 0.0f)
					{
						// needs to be ~12 bits larger than ToolTarget::CacheClass::DataType (for max FilterRadius (31))
						// the editor is 64-bit native so just go the whole hog :)
						int64 FilterValue = 0;
						int32 FilterSamplingNumber = 0;

						const int32 XRadius = FMath::Min3<int32>(FilterRadius, X - BrushInfo.GetBounds().Min.X, BrushInfo.GetBounds().Max.X - X - 1);
						const int32 YRadius = FMath::Min3<int32>(FilterRadius, Y - BrushInfo.GetBounds().Min.Y, BrushInfo.GetBounds().Max.Y - Y - 1);

						const int32 SampleX1 = X - XRadius; checkSlow(SampleX1 >= BrushInfo.GetBounds().Min.X);
						const int32 SampleY1 = Y - YRadius; checkSlow(SampleY1 >= BrushInfo.GetBounds().Min.Y);
						const int32 SampleX2 = X + XRadius; checkSlow(SampleX2 <  BrushInfo.GetBounds().Max.X);
						const int32 SampleY2 = Y + YRadius; checkSlow(SampleY2 <  BrushInfo.GetBounds().Max.Y);
						for (int32 SampleY = SampleY1; SampleY <= SampleY2; SampleY++)
						{
							const float* SampleBrushScanline = BrushInfo.GetDataPtr(FIntPoint(0, SampleY));
							const float* SampleBrushScanline2 = BrushInfo.GetDataPtr(FIntPoint(0, Y + (Y - SampleY)));
							auto* SampleDataScanline = Data.GetData() + (SampleY - Y1) * (X2 - X1 + 1) + (0 - X1);

							for (int32 SampleX = SampleX1; SampleX <= SampleX2; SampleX++)
							{
								// constrain sample to within the brush, symmetrically to prevent flattening bug
								const float SampleBrushValue =
									FMath::Min(
										FMath::Min<float>(SampleBrushScanline [SampleX], SampleBrushScanline [X + (X - SampleX)]),
										FMath::Min<float>(SampleBrushScanline2[SampleX], SampleBrushScanline2[X + (X - SampleX)])
										);
								if (SampleBrushValue > 0.0f)
								{
									FilterValue += SampleDataScanline[SampleX];
									FilterSamplingNumber++;
								}
							}
						}

						FilterValue /= FilterSamplingNumber;

						DataScanline[X] = FMath::Lerp(DataScanline[X], (typename ToolTarget::CacheClass::DataType)FilterValue, BrushValue * ToolStrength);
					}
				}
			}
		}

		ACyLand* CyLand = this->CyLandInfo->CyLandActor.Get();

		if (CyLand != nullptr && CyLand->HasProceduralContent && !GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
		{
			FMessageLog("MapCheck").Warning()->AddToken(FTextToken::Create(LOCTEXT("CyLandProcedural_ChangingDataWithoutSettings", "This map contains CyLand procedural content, modifying the CyLand data will result in data loss when the map is reopened with CyLand Procedural settings on. Please enable CyLand Procedural settings before modifying the data.")));
			FMessageLog("MapCheck").Open(EMessageSeverity::Warning);
		}

		this->Cache.SetCachedData(X1, Y1, X2, Y2, Data, UISettings->PaintingRestriction);
		this->Cache.Flush();
	}
};

template<class ToolTarget>
class FCyLandToolSmooth : public FCyLandToolPaintBase<ToolTarget, FCyLandToolStrokeSmooth<ToolTarget>>
{
public:
	FCyLandToolSmooth(FEdModeCyLand* InEdMode)
		: FCyLandToolPaintBase<ToolTarget, FCyLandToolStrokeSmooth<ToolTarget> >(InEdMode)
	{
	}

	virtual const TCHAR* GetToolName() override { return TEXT("Smooth"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "CyLandMode_Smooth", "Smooth"); };

};

//
// FCyLandToolFlatten
//
template<class ToolTarget>
class FCyLandToolStrokeFlatten : public FCyLandToolStrokePaintBase<ToolTarget>
{
	typename ToolTarget::CacheClass::DataType FlattenHeight;

	FVector FlattenNormal;
	float FlattenPlaneDist;
	bool bInitializedFlattenHeight;
	bool bTargetIsHeightmap;

public:
	FCyLandToolStrokeFlatten(FEdModeCyLand* InEdMode, FEditorViewportClient* InViewportClient, const FCyLandToolTarget& InTarget)
		: FCyLandToolStrokePaintBase<ToolTarget>(InEdMode, InViewportClient, InTarget)
		, bInitializedFlattenHeight(false)
		, bTargetIsHeightmap(InTarget.TargetType == ECyLandToolTargetType::Heightmap)
	{
		if (InEdMode->UISettings->bUseFlattenTarget && bTargetIsHeightmap)
		{
			FTransform LocalToWorld = InTarget.CyLandInfo->GetCyLandProxy()->ActorToWorld();
			float Height = (InEdMode->UISettings->FlattenTarget - LocalToWorld.GetTranslation().Z) / LocalToWorld.GetScale3D().Z;
			FlattenHeight = CyLandDataAccess::GetTexHeight(Height);
			bInitializedFlattenHeight = true;
		}
	}

	void Apply(FEditorViewportClient* ViewportClient, FCyLandBrush* Brush, const UCyLandEditorObject* UISettings, const TArray<FCyLandToolInteractorPosition>& InteractorPositions)
	{
		if (!this->CyLandInfo) return;

		if (!bInitializedFlattenHeight || (UISettings->bPickValuePerApply && bTargetIsHeightmap))
		{
			bInitializedFlattenHeight = false;
			float FlattenX = InteractorPositions[0].Position.X;
			float FlattenY = InteractorPositions[0].Position.Y;
			int32 FlattenHeightX = FMath::FloorToInt(FlattenX);
			int32 FlattenHeightY = FMath::FloorToInt(FlattenY);

			this->Cache.CacheData(FlattenHeightX, FlattenHeightY, FlattenHeightX + 1, FlattenHeightY + 1);
			float HeightValue = this->Cache.GetValue(FlattenX, FlattenY);
			FlattenHeight = HeightValue;

			if (UISettings->bUseSlopeFlatten && bTargetIsHeightmap)
			{
				FlattenNormal = this->Cache.GetNormal(FlattenHeightX, FlattenHeightY);
				FlattenPlaneDist = -(FlattenNormal | FVector(FlattenX, FlattenY, HeightValue));
			}

			bInitializedFlattenHeight = true;
		}


		// Get list of verts to update
		FCyLandBrushData BrushInfo = Brush->ApplyBrush(InteractorPositions);
		if (!BrushInfo)
		{
			return;
		}

		int32 X1, Y1, X2, Y2;
		BrushInfo.GetInclusiveBounds(X1, Y1, X2, Y2);

		// Tablet pressure
		float Pressure = ViewportClient->Viewport->IsPenActive() ? ViewportClient->Viewport->GetTabletPressure() : 1.0f;

		// expand the area by one vertex in each direction to ensure normals are calculated correctly
		CA_SUPPRESS(6326);
		if (ToolTarget::TargetType == ECyLandToolTargetType::Heightmap)
		{
			X1 -= 1;
			Y1 -= 1;
			X2 += 1;
			Y2 += 1;
		}

		this->Cache.CacheData(X1, Y1, X2, Y2);

		TArray<typename ToolTarget::CacheClass::DataType> Data;
		this->Cache.GetCachedData(X1, Y1, X2, Y2, Data);

		// Apply the brush
		for (int32 Y = BrushInfo.GetBounds().Min.Y; Y < BrushInfo.GetBounds().Max.Y; Y++)
		{
			const float* BrushScanline = BrushInfo.GetDataPtr(FIntPoint(0, Y));
			auto* DataScanline = Data.GetData() + (Y - Y1) * (X2 - X1 + 1) + (0 - X1);

			for (int32 X = BrushInfo.GetBounds().Min.X; X < BrushInfo.GetBounds().Max.X; X++)
			{
				const float BrushValue = BrushScanline[X];

				if (BrushValue > 0.0f)
				{
					float Strength = FMath::Clamp<float>(BrushValue * UISettings->ToolStrength * Pressure, 0.0f, 1.0f);

					if (!(UISettings->bUseSlopeFlatten && bTargetIsHeightmap))
					{
						int32 Delta = DataScanline[X] - FlattenHeight;
						switch (UISettings->FlattenMode)
						{
						case ECyLandToolFlattenMode::Terrace:
							{
								const FTransform& LocalToWorld = this->Target.CyLandInfo->GetCyLandProxy()->ActorToWorld();
								float ScaleZ = LocalToWorld.GetScale3D().Z;
								float TranslateZ = LocalToWorld.GetTranslation().Z;
								float TerraceInterval = UISettings->TerraceInterval;
								float Smoothness = UISettings->TerraceSmooth;								
								float WorldHeight = CyLandDataAccess::GetLocalHeight(DataScanline[X]);
								
								//move into world space
								WorldHeight = (WorldHeight * ScaleZ) + TranslateZ;
								float CurrentHeight = WorldHeight;

								//smoothing part
								float CurrentLevel = WorldHeight / TerraceInterval;								
								Smoothness = 1.0f / FMath::Max(Smoothness, 0.0001f);
								float CurrentPhase = FMath::Frac(CurrentLevel);
								float Halfmask = FMath::Clamp(FMath::CeilToFloat(CurrentPhase - 0.5f), 0.0f, 1.0f);
								CurrentLevel = FMath::FloorToFloat(WorldHeight / TerraceInterval);
								float SCurve = FMath::Lerp(CurrentPhase, (1.0f - CurrentPhase), Halfmask) * 2.0f;
								SCurve = FMath::Pow(SCurve, Smoothness) * 0.5f;
								SCurve = FMath::Lerp(SCurve, 1.0f - SCurve, Halfmask) * TerraceInterval;
								WorldHeight = (CurrentLevel * TerraceInterval)  + SCurve;
								//end of smoothing part

								float FinalHeight = FMath::Lerp(CurrentHeight, WorldHeight , Strength);
								FinalHeight = (FinalHeight - TranslateZ) / ScaleZ;
								DataScanline[X] = CyLandDataAccess::GetTexHeight(FinalHeight);	
							}
							break;
						case ECyLandToolFlattenMode::Raise:
							if (Delta < 0)
							{
								DataScanline[X] = FMath::CeilToInt(FMath::Lerp((float)DataScanline[X], (float)FlattenHeight, Strength));
							}
							break;
						case ECyLandToolFlattenMode::Lower:
							if (Delta > 0)
							{
								DataScanline[X] = FMath::FloorToInt(FMath::Lerp((float)DataScanline[X], (float)FlattenHeight, Strength));
							}
							break;
						default:
						case ECyLandToolFlattenMode::Both:
							if (Delta > 0)
							{
								DataScanline[X] = FMath::FloorToInt(FMath::Lerp((float)DataScanline[X], (float)FlattenHeight, Strength));
							}
							else
							{
								DataScanline[X] = FMath::CeilToInt(FMath::Lerp((float)DataScanline[X], (float)FlattenHeight, Strength));
							}
							break;
						}
					}
					else
					{
						typename ToolTarget::CacheClass::DataType DestValue = -(FlattenNormal.X * X + FlattenNormal.Y * Y + FlattenPlaneDist) / FlattenNormal.Z;
						//float PlaneDist = FlattenNormal | FVector(X, Y, HeightData(HeightDataIndex)) + FlattenPlaneDist;
						float PlaneDist = DataScanline[X] - DestValue;
						DestValue = DataScanline[X] - PlaneDist * Strength;
						switch (UISettings->FlattenMode)
						{
						case ECyLandToolFlattenMode::Raise:
							if (PlaneDist < 0)
							{
								DataScanline[X] = FMath::CeilToInt(FMath::Lerp((float)DataScanline[X], (float)DestValue, Strength));
							}
							break;
						case ECyLandToolFlattenMode::Lower:
							if (PlaneDist > 0)
							{
								DataScanline[X] = FMath::FloorToInt(FMath::Lerp((float)DataScanline[X], (float)DestValue, Strength));
							}
							break;
						default:
						case ECyLandToolFlattenMode::Both:
							if (PlaneDist > 0)
							{
								DataScanline[X] = FMath::FloorToInt(FMath::Lerp((float)DataScanline[X], (float)DestValue, Strength));
							}
							else
							{
								DataScanline[X] = FMath::CeilToInt(FMath::Lerp((float)DataScanline[X], (float)DestValue, Strength));
							}
							break;
						}
					}
				}
			}
		}

		ACyLand* CyLand = this->CyLandInfo->CyLandActor.Get();

		if (CyLand != nullptr && CyLand->HasProceduralContent && !GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
		{
			FMessageLog("MapCheck").Warning()->AddToken(FTextToken::Create(LOCTEXT("CyLandProcedural_ChangingDataWithoutSettings", "This map contains CyLand procedural content, modifying the CyLand data will result in data loss when the map is reopened with CyLand Procedural settings on. Please enable CyLand Procedural settings before modifying the data.")));
			FMessageLog("MapCheck").Open(EMessageSeverity::Warning);
		}

		this->Cache.SetCachedData(X1, Y1, X2, Y2, Data, UISettings->PaintingRestriction);
		this->Cache.Flush();
	}
};

template<class ToolTarget>
class FCyLandToolFlatten : public FCyLandToolPaintBase < ToolTarget, FCyLandToolStrokeFlatten<ToolTarget> >
{
protected:
	UStaticMesh* PlaneMesh;
	UStaticMeshComponent* MeshComponent;
	bool CanToolBeActivatedNextTick;
	bool CanToolBeActivatedValue;
	float EyeDropperFlattenTargetValue;

public:
	FCyLandToolFlatten(FEdModeCyLand* InEdMode)
		: FCyLandToolPaintBase<ToolTarget, FCyLandToolStrokeFlatten<ToolTarget>>(InEdMode)
		, PlaneMesh(LoadObject<UStaticMesh>(NULL, TEXT("/Engine/EditorLandscapeResources/FlattenPlaneMesh.FlattenPlaneMesh")))
		, MeshComponent(NULL)
		, CanToolBeActivatedNextTick(false)
		, CanToolBeActivatedValue(false)
		, EyeDropperFlattenTargetValue(0.0f)
	{
		check(PlaneMesh);
	}

	virtual bool GetCursor(EMouseCursor::Type& OutCursor) const override
	{ 
		if (this->EdMode->UISettings->bFlattenEyeDropperModeActivated)
		{
			OutCursor = EMouseCursor::EyeDropper;
			return true;
		}
		
		return false; 
	}

	virtual void SetCanToolBeActivated(bool Value) override
	{ 
		CanToolBeActivatedNextTick = true;
		CanToolBeActivatedValue = Value;
	}

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		Collector.AddReferencedObject(PlaneMesh);
		Collector.AddReferencedObject(MeshComponent);
	}

	virtual const TCHAR* GetToolName() override { return TEXT("Flatten"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "CyLandMode_Flatten", "Flatten"); };

	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override
	{
		if (CanToolBeActivatedNextTick)
		{
			this->bCanToolBeActivated = CanToolBeActivatedValue;
			CanToolBeActivatedNextTick = false;
		}

		FCyLandToolPaintBase<ToolTarget, FCyLandToolStrokeFlatten<ToolTarget>>::Tick(ViewportClient, DeltaTime);

		bool bShowGrid = this->EdMode->UISettings->bUseFlattenTarget && this->EdMode->CurrentToolTarget.TargetType == ECyLandToolTargetType::Heightmap && this->EdMode->UISettings->bShowFlattenTargetPreview;
		MeshComponent->SetVisibility(bShowGrid);
	}

	virtual bool MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y) override
	{
		bool bResult = FCyLandToolPaintBase<ToolTarget, FCyLandToolStrokeFlatten<ToolTarget>>::MouseMove(ViewportClient, Viewport, x, y);

		if (ViewportClient->IsLevelEditorClient() && MeshComponent != NULL)
		{
			FVector MousePosition;
			this->EdMode->CyLandMouseTrace((FEditorViewportClient*)ViewportClient, x, y, MousePosition);

			const FTransform LocalToWorld = this->EdMode->CurrentToolTarget.CyLandInfo->GetCyLandProxy()->ActorToWorld();
			FVector Origin;
			Origin.X = FMath::RoundToFloat(MousePosition.X);
			Origin.Y = FMath::RoundToFloat(MousePosition.Y);
			Origin.Z = (FMath::RoundToFloat((this->EdMode->UISettings->FlattenTarget - LocalToWorld.GetTranslation().Z) / LocalToWorld.GetScale3D().Z * LANDSCAPE_INV_ZSCALE) - 0.1f) * LANDSCAPE_ZSCALE;
			MeshComponent->SetRelativeLocation(Origin, false);

			// Clamp the value to the height map
			uint16 TexHeight = CyLandDataAccess::GetTexHeight(MousePosition.Z);
			float Height = CyLandDataAccess::GetLocalHeight(TexHeight);

			// Convert the height back to world space
			this->EdMode->UISettings->FlattenEyeDropperModeDesiredTarget = (Height * LocalToWorld.GetScale3D().Z) + LocalToWorld.GetTranslation().Z;
		}

		return bResult;
	}

	virtual void EnterTool() override
	{
		FCyLandToolPaintBase<ToolTarget, FCyLandToolStrokeFlatten<ToolTarget>>::EnterTool();

		ACyLandProxy* CyLandProxy = this->EdMode->CurrentToolTarget.CyLandInfo->GetCyLandProxy();
		MeshComponent = NewObject<UStaticMeshComponent>(CyLandProxy, NAME_None, RF_Transient);
		MeshComponent->SetStaticMesh(PlaneMesh);
		MeshComponent->AttachToComponent(CyLandProxy->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		MeshComponent->RegisterComponent();

		bool bShowGrid = this->EdMode->UISettings->bUseFlattenTarget && this->EdMode->CurrentToolTarget.TargetType == ECyLandToolTargetType::Heightmap && this->EdMode->UISettings->bShowFlattenTargetPreview;
		MeshComponent->SetVisibility(bShowGrid);

		// Try to set a sane initial location for the preview grid
		const FTransform LocalToWorld = this->EdMode->CurrentToolTarget.CyLandInfo->GetCyLandProxy()->GetRootComponent()->GetComponentToWorld();
		FVector Origin = FVector::ZeroVector;
		Origin.Z = (FMath::RoundToFloat((this->EdMode->UISettings->FlattenTarget - LocalToWorld.GetTranslation().Z) / LocalToWorld.GetScale3D().Z * LANDSCAPE_INV_ZSCALE) - 0.1f) * LANDSCAPE_ZSCALE;
		MeshComponent->SetRelativeLocation(Origin, false);
	}

	virtual void ExitTool() override
	{
		FCyLandToolPaintBase<ToolTarget, FCyLandToolStrokeFlatten<ToolTarget>>::ExitTool();

		MeshComponent->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
		MeshComponent->DestroyComponent();
	}
};

// 
// FCyLandToolNoise
//
template<class ToolTarget>
class FCyLandToolStrokeNoise : public FCyLandToolStrokePaintBase<ToolTarget>
{
public:
	FCyLandToolStrokeNoise(FEdModeCyLand* InEdMode, FEditorViewportClient* InViewportClient, const FCyLandToolTarget& InTarget)
		: FCyLandToolStrokePaintBase<ToolTarget>(InEdMode, InViewportClient, InTarget)
	{
	}

	void Apply(FEditorViewportClient* ViewportClient, FCyLandBrush* Brush, const UCyLandEditorObject* UISettings, const TArray<FCyLandToolInteractorPosition>& InteractorPositions)
	{
		if (!this->CyLandInfo) return;

		// Get list of verts to update
		FCyLandBrushData BrushInfo = Brush->ApplyBrush(InteractorPositions);
		if (!BrushInfo)
		{
			return;
		}

		int32 X1, Y1, X2, Y2;
		BrushInfo.GetInclusiveBounds(X1, Y1, X2, Y2);

		// Tablet pressure
		float Pressure = ViewportClient->Viewport->IsPenActive() ? ViewportClient->Viewport->GetTabletPressure() : 1.0f;

		// expand the area by one vertex in each direction to ensure normals are calculated correctly
		CA_SUPPRESS(6326);
		if (ToolTarget::TargetType == ECyLandToolTargetType::Heightmap)
		{
			X1 -= 1;
			Y1 -= 1;
			X2 += 1;
			Y2 += 1;
		}

		this->Cache.CacheData(X1, Y1, X2, Y2);
		TArray<typename ToolTarget::CacheClass::DataType> Data;
		this->Cache.GetCachedData(X1, Y1, X2, Y2, Data);

		float BrushSizeAdjust = 1.0f;
		CA_SUPPRESS(6326);
		if (ToolTarget::TargetType != ECyLandToolTargetType::Weightmap && UISettings->BrushRadius < UISettings->MaximumValueRadius)
		{
			BrushSizeAdjust = UISettings->BrushRadius / UISettings->MaximumValueRadius;
		}

		CA_SUPPRESS(6326);
		bool bUseWeightTargetValue = UISettings->bUseWeightTargetValue && ToolTarget::TargetType == ECyLandToolTargetType::Weightmap;

		// Apply the brush
		for (int32 Y = BrushInfo.GetBounds().Min.Y; Y < BrushInfo.GetBounds().Max.Y; Y++)
		{
			const float* BrushScanline = BrushInfo.GetDataPtr(FIntPoint(0, Y));
			auto* DataScanline = Data.GetData() + (Y - Y1) * (X2 - X1 + 1) + (0 - X1);

			for (int32 X = BrushInfo.GetBounds().Min.X; X < BrushInfo.GetBounds().Max.X; X++)
			{
				const float BrushValue = BrushScanline[X];

				if (BrushValue > 0.0f)
				{
					float OriginalValue = DataScanline[X];
					if (bUseWeightTargetValue)
					{
						FNoiseParameter NoiseParam(0, UISettings->NoiseScale, 255.0f / 2.0f);
						float DestValue = NoiseModeConversion(ECyLandToolNoiseMode::Add, NoiseParam.NoiseAmount, NoiseParam.Sample(X, Y)) * UISettings->WeightTargetValue;
						switch (UISettings->NoiseMode)
						{
						case ECyLandToolNoiseMode::Add:
							if (OriginalValue >= DestValue)
							{
								continue;
							}
							break;
						case ECyLandToolNoiseMode::Sub:
							DestValue += (1.0f - UISettings->WeightTargetValue) * NoiseParam.NoiseAmount;
							if (OriginalValue <= DestValue)
							{
								continue;
							}
							break;
						}
						DataScanline[X] = ToolTarget::CacheClass::ClampValue(FMath::RoundToInt(FMath::Lerp(OriginalValue, DestValue, BrushValue * UISettings->ToolStrength * Pressure)));
					}
					else
					{
						float TotalStrength = BrushValue * UISettings->ToolStrength * Pressure * ToolTarget::StrengthMultiplier(this->CyLandInfo, UISettings->BrushRadius);
						FNoiseParameter NoiseParam(0, UISettings->NoiseScale, TotalStrength * BrushSizeAdjust);
						float PaintAmount = NoiseModeConversion(UISettings->NoiseMode, NoiseParam.NoiseAmount, NoiseParam.Sample(X, Y));
						DataScanline[X] = ToolTarget::CacheClass::ClampValue(OriginalValue + PaintAmount);
					}
				}
			}
		}

		ACyLand* CyLand = this->CyLandInfo->CyLandActor.Get();

		if (CyLand != nullptr && CyLand->HasProceduralContent && !GetMutableDefault<UEditorExperimentalSettings>()->bProceduralLandscape)
		{
			FMessageLog("MapCheck").Warning()->AddToken(FTextToken::Create(LOCTEXT("CyLandProcedural_ChangingDataWithoutSettings", "This map contains CyLand procedural content, modifying the CyLand data will result in data loss when the map is reopened with CyLand Procedural settings on. Please enable CyLand Procedural settings before modifying the data.")));
			FMessageLog("MapCheck").Open(EMessageSeverity::Warning);
		}

		this->Cache.SetCachedData(X1, Y1, X2, Y2, Data, UISettings->PaintingRestriction);
		this->Cache.Flush();
	}
};

template<class ToolTarget>
class FCyLandToolNoise : public FCyLandToolPaintBase < ToolTarget, FCyLandToolStrokeNoise<ToolTarget> >
{
public:
	FCyLandToolNoise(FEdModeCyLand* InEdMode)
		: FCyLandToolPaintBase<ToolTarget, FCyLandToolStrokeNoise<ToolTarget> >(InEdMode)
	{
	}

	virtual const TCHAR* GetToolName() override { return TEXT("Noise"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "CyLandMode_Noise", "Noise"); };
};


//
// Toolset initialization
//
void FEdModeCyLand::InitializeTool_Paint()
{
	auto Tool_Sculpt = MakeUnique<FCyLandToolSculpt>(this);
	Tool_Sculpt->ValidBrushes.Add("BrushSet_Circle");
	Tool_Sculpt->ValidBrushes.Add("BrushSet_Alpha");
	Tool_Sculpt->ValidBrushes.Add("BrushSet_Pattern");
	Tool_Sculpt->ValidBrushes.Add("BrushSet_Component");
	CyLandTools.Add(MoveTemp(Tool_Sculpt));

	auto Tool_Paint = MakeUnique<FCyLandToolPaint>(this);
	Tool_Paint->ValidBrushes.Add("BrushSet_Circle");
	Tool_Paint->ValidBrushes.Add("BrushSet_Alpha");
	Tool_Paint->ValidBrushes.Add("BrushSet_Pattern");
	Tool_Paint->ValidBrushes.Add("BrushSet_Component");
	CyLandTools.Add(MoveTemp(Tool_Paint));
}

void FEdModeCyLand::InitializeTool_Smooth()
{
	auto Tool_Smooth_Heightmap = MakeUnique<FCyLandToolSmooth<FHeightmapToolTarget>>(this);
	Tool_Smooth_Heightmap->ValidBrushes.Add("BrushSet_Circle");
	Tool_Smooth_Heightmap->ValidBrushes.Add("BrushSet_Alpha");
	Tool_Smooth_Heightmap->ValidBrushes.Add("BrushSet_Pattern");
	CyLandTools.Add(MoveTemp(Tool_Smooth_Heightmap));

	auto Tool_Smooth_Weightmap = MakeUnique<FCyLandToolSmooth<FWeightmapToolTarget>>(this);
	Tool_Smooth_Weightmap->ValidBrushes.Add("BrushSet_Circle");
	Tool_Smooth_Weightmap->ValidBrushes.Add("BrushSet_Alpha");
	Tool_Smooth_Weightmap->ValidBrushes.Add("BrushSet_Pattern");
	CyLandTools.Add(MoveTemp(Tool_Smooth_Weightmap));
}

void FEdModeCyLand::InitializeTool_Flatten()
{
	auto Tool_Flatten_Heightmap = MakeUnique<FCyLandToolFlatten<FHeightmapToolTarget>>(this);
	Tool_Flatten_Heightmap->ValidBrushes.Add("BrushSet_Circle");
	Tool_Flatten_Heightmap->ValidBrushes.Add("BrushSet_Alpha");
	Tool_Flatten_Heightmap->ValidBrushes.Add("BrushSet_Pattern");
	CyLandTools.Add(MoveTemp(Tool_Flatten_Heightmap));

	auto Tool_Flatten_Weightmap = MakeUnique<FCyLandToolFlatten<FWeightmapToolTarget>>(this);
	Tool_Flatten_Weightmap->ValidBrushes.Add("BrushSet_Circle");
	Tool_Flatten_Weightmap->ValidBrushes.Add("BrushSet_Alpha");
	Tool_Flatten_Weightmap->ValidBrushes.Add("BrushSet_Pattern");
	CyLandTools.Add(MoveTemp(Tool_Flatten_Weightmap));
}

void FEdModeCyLand::InitializeTool_Noise()
{
	auto Tool_Noise_Heightmap = MakeUnique<FCyLandToolNoise<FHeightmapToolTarget>>(this);
	Tool_Noise_Heightmap->ValidBrushes.Add("BrushSet_Circle");
	Tool_Noise_Heightmap->ValidBrushes.Add("BrushSet_Alpha");
	Tool_Noise_Heightmap->ValidBrushes.Add("BrushSet_Pattern");
	CyLandTools.Add(MoveTemp(Tool_Noise_Heightmap));

	auto Tool_Noise_Weightmap = MakeUnique<FCyLandToolNoise<FWeightmapToolTarget>>(this);
	Tool_Noise_Weightmap->ValidBrushes.Add("BrushSet_Circle");
	Tool_Noise_Weightmap->ValidBrushes.Add("BrushSet_Alpha");
	Tool_Noise_Weightmap->ValidBrushes.Add("BrushSet_Pattern");
	CyLandTools.Add(MoveTemp(Tool_Noise_Weightmap));
}

#undef LOCTEXT_NAMESPACE