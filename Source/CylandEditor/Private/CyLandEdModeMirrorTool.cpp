// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInterface.h"
#include "AI/NavigationSystemBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UnrealWidget.h"
#include "EditorModeManager.h"
#include "EditorViewportClient.h"
#include "CyLandToolInterface.h"
#include "CyLandProxy.h"
#include "CyLandEdMode.h"
#include "Containers/ArrayView.h"
#include "CyLandEditorObject.h"
#include "ScopedTransaction.h"
#include "CyLandEdit.h"
#include "CyLandDataAccess.h"
#include "CyLandRender.h"
#include "CyLandHeightfieldCollisionComponent.h"
//#include "CyLandDataAccess.h"

#define LOCTEXT_NAMESPACE "CyLand"

class FCyLandToolMirror : public FCyLandTool
{
protected:
	FEdModeCyLand* EdMode;
	UMaterialInstanceDynamic* MirrorPlaneMaterial;

	ECoordSystem SavedCoordSystem;

public:
	FCyLandToolMirror(FEdModeCyLand* InEdMode)
		: EdMode(InEdMode)
	{
		UMaterialInterface* BaseMirrorPlaneMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EditorLandscapeResources/MirrorPlaneMaterial.MirrorPlaneMaterial"));
		MirrorPlaneMaterial = UMaterialInstanceDynamic::Create(BaseMirrorPlaneMaterial, GetTransientPackage());
		MirrorPlaneMaterial->SetScalarParameterValue(FName("LineThickness"), 2.0f);
	}

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		Collector.AddReferencedObject(MirrorPlaneMaterial);
	}

	virtual const TCHAR* GetToolName() override { return TEXT("Mirror"); }
	virtual FText GetDisplayName() override { return FText(); /*NSLOCTEXT("UnrealEd", "CyLandTool_Mirror", "Mirror CyLand");*/ };

	virtual void SetEditRenderType() override { GCyLandEditRenderMode = ECyLandEditRenderMode::None | (GCyLandEditRenderMode & ECyLandEditRenderMode::BitMaskForMask); }
	virtual bool SupportsMask() override { return false; }

	virtual ECyLandToolTargetTypeMask::Type GetSupportedTargetTypes() override
	{
		return ECyLandToolTargetTypeMask::Heightmap;
	}

	virtual void EnterTool() override
	{
		if (EdMode->UISettings->MirrorPoint == FVector2D::ZeroVector)
		{
			CenterMirrorPoint();
		}
		GLevelEditorModeTools().SetWidgetMode(FWidget::WM_Translate);
		SavedCoordSystem = GLevelEditorModeTools().GetCoordSystem();
		GLevelEditorModeTools().SetCoordSystem(COORD_Local);
	}

	virtual void ExitTool() override
	{
		GLevelEditorModeTools().SetCoordSystem(SavedCoordSystem);
	}

	virtual bool BeginTool(FEditorViewportClient* ViewportClient, const FCyLandToolTarget& Target, const FVector& InHitLocation) override
	{
		return true;
	}

	virtual void EndTool(FEditorViewportClient* ViewportClient) override
	{
	}

	virtual bool MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y) override
	{
		return false;
	}

	virtual bool InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport, FKey InKey, EInputEvent InEvent) override
	{
		if (InKey == EKeys::Enter && InEvent == IE_Pressed)
		{
			ApplyMirror();
		}

		return false;
	}

	virtual bool InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale) override
	{
		if (InViewportClient->GetCurrentWidgetAxis() != EAxisList::None)
		{
			const ACyLandProxy* CyLandProxy = EdMode->CurrentToolTarget.CyLandInfo->GetCyLandProxy();
			const FTransform CyLandToWorld = CyLandProxy->CyLandActorToWorld();

			EdMode->UISettings->MirrorPoint += FVector2D(CyLandToWorld.InverseTransformVector(InDrag));

			return true;
		}

		return false;
	}

	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override
	{
		// The editor can try to render the tool before the UpdateCyLandEditorData command runs and the CyLand editor realizes that the CyLand has been hidden/deleted
		const UCyLandInfo* const CyLandInfo = EdMode->CurrentToolTarget.CyLandInfo.Get();
		const ACyLandProxy* const CyLandProxy = CyLandInfo->GetCyLandProxy();
		if (CyLandProxy)
		{
			const FTransform CyLandToWorld = CyLandProxy->CyLandActorToWorld();

			int32 MinX, MinY, MaxX, MaxY;
			if (CyLandInfo->GetCyLandExtent(MinX, MinY, MaxX, MaxY))
			{
				FVector MirrorPoint3D = FVector((MaxX + MinX) / 2.0f, (MaxY + MinY) / 2.0f, 0);
				FVector MirrorPlaneScale = FVector(0, 1, 100);

				if (EdMode->UISettings->MirrorOp == ECyLandMirrorOperation::MinusXToPlusX ||
					EdMode->UISettings->MirrorOp == ECyLandMirrorOperation::PlusXToMinusX ||
					EdMode->UISettings->MirrorOp == ECyLandMirrorOperation::RotateMinusXToPlusX ||
					EdMode->UISettings->MirrorOp == ECyLandMirrorOperation::RotatePlusXToMinusX)
				{
					MirrorPoint3D.X = EdMode->UISettings->MirrorPoint.X;
					MirrorPlaneScale.Y = (MaxY - MinY) / 2.0f;
				}
				else
				{
					MirrorPoint3D.Y = EdMode->UISettings->MirrorPoint.Y;
					MirrorPlaneScale.Y = (MaxX - MinX) / 2.0f;
				}

				MirrorPoint3D.Z = GetLocalZAtPoint(CyLandInfo, FMath::RoundToInt(MirrorPoint3D.X), FMath::RoundToInt(MirrorPoint3D.Y));
				MirrorPoint3D = CyLandToWorld.TransformPosition(MirrorPoint3D);

				FMatrix Matrix;
				if (EdMode->UISettings->MirrorOp == ECyLandMirrorOperation::MinusYToPlusY ||
					EdMode->UISettings->MirrorOp == ECyLandMirrorOperation::PlusYToMinusY ||
					EdMode->UISettings->MirrorOp == ECyLandMirrorOperation::RotateMinusYToPlusY ||
					EdMode->UISettings->MirrorOp == ECyLandMirrorOperation::RotatePlusYToMinusY)
				{
					Matrix = FScaleRotationTranslationMatrix(MirrorPlaneScale, FRotator(0, 90, 0), FVector::ZeroVector);
				}
				else
				{
					Matrix = FScaleMatrix(MirrorPlaneScale);
				}

				Matrix *= CyLandToWorld.ToMatrixWithScale();
				Matrix.SetOrigin(MirrorPoint3D);

				// Convert plane from horizontal to vertical
				Matrix = FMatrix(FVector(0, 1, 0), FVector(0, 0, 1), FVector(1, 0, 0), FVector(0, 0, 0)) * Matrix;

				const FBox Box = FBox(FVector(-1, -1, 0), FVector(+1, +1, 0));
				DrawWireBox(PDI, Matrix, Box, FLinearColor::Green, SDPG_World);

				const float CyLandScaleRatio = CyLandToWorld.GetScale3D().Z / CyLandToWorld.GetScale3D().X;
				FVector2D UVScale = FVector2D(FMath::RoundToFloat(MirrorPlaneScale.Y / 10), FMath::RoundToFloat(MirrorPlaneScale.Z * CyLandScaleRatio / 10 / 2) * 2);
				MirrorPlaneMaterial->SetVectorParameterValue(FName("GridSize"), FVector(UVScale, 0));
				DrawPlane10x10(PDI, Matrix, 1, FVector2D(0, 0), FVector2D(1, 1), MirrorPlaneMaterial->GetRenderProxy(), SDPG_World);
			}
		}
	}

	virtual bool OverrideSelection() const override
	{
		return true;
	}

	virtual bool IsSelectionAllowed(AActor* InActor, bool bInSelection) const override
	{
		// Only filter selection not deselection
		if (bInSelection)
		{
			return false;
		}

		return true;
	}

	virtual bool UsesTransformWidget() const override
	{
		// The editor can try to render the transform widget before the CyLand editor ticks and realises that the CyLand has been hidden/deleted
		const UCyLandInfo* const CyLandInfo = EdMode->CurrentToolTarget.CyLandInfo.Get();
		const ACyLandProxy* const CyLandProxy = CyLandInfo->GetCyLandProxy();
		if (CyLandProxy)
		{
			return true;
		}

		return false;
	}

	virtual EAxisList::Type GetWidgetAxisToDraw(FWidget::EWidgetMode CheckMode) const override
	{
		if (CheckMode == FWidget::WM_Translate)
		{
			switch (EdMode->UISettings->MirrorOp)
			{
			case ECyLandMirrorOperation::MinusXToPlusX:
			case ECyLandMirrorOperation::PlusXToMinusX:
			case ECyLandMirrorOperation::RotateMinusXToPlusX:
			case ECyLandMirrorOperation::RotatePlusXToMinusX:
				return EAxisList::X;
			case ECyLandMirrorOperation::MinusYToPlusY:
			case ECyLandMirrorOperation::PlusYToMinusY:
			case ECyLandMirrorOperation::RotateMinusYToPlusY:
			case ECyLandMirrorOperation::RotatePlusYToMinusY:
				return EAxisList::Y;
			default:
				check(0);
				return EAxisList::None;
			}
		}
		else
		{
			return EAxisList::None;
		}

		return EAxisList::None;
	}

	virtual FVector GetWidgetLocation() const override
	{
		const UCyLandInfo* const CyLandInfo = EdMode->CurrentToolTarget.CyLandInfo.Get();
		const ACyLandProxy* const CyLandProxy = CyLandInfo->GetCyLandProxy();
		if (CyLandProxy)
		{
			const FTransform CyLandToWorld = CyLandProxy->CyLandActorToWorld();

			int32 MinX, MinY, MaxX, MaxY;
			if (!CyLandInfo->GetCyLandExtent(MinX, MinY, MaxX, MaxY))
			{
				MinX = MinY = 0;
				MaxX = MaxY = 0;
			}

			FVector MirrorPoint3D = FVector((MaxX + MinX) / 2.0f, (MaxY + MinY) / 2.0f, 0);
			if (EdMode->UISettings->MirrorOp == ECyLandMirrorOperation::MinusXToPlusX ||
				EdMode->UISettings->MirrorOp == ECyLandMirrorOperation::PlusXToMinusX ||
				EdMode->UISettings->MirrorOp == ECyLandMirrorOperation::RotateMinusXToPlusX ||
				EdMode->UISettings->MirrorOp == ECyLandMirrorOperation::RotatePlusXToMinusX)
			{
				MirrorPoint3D.X = EdMode->UISettings->MirrorPoint.X;
			}
			else
			{
				MirrorPoint3D.Y = EdMode->UISettings->MirrorPoint.Y;
			}
			MirrorPoint3D.Z = GetLocalZAtPoint(CyLandInfo, FMath::RoundToInt(MirrorPoint3D.X), FMath::RoundToInt(MirrorPoint3D.Y));
			MirrorPoint3D = CyLandToWorld.TransformPosition(MirrorPoint3D);
			MirrorPoint3D.Z += 1000.f; // place the widget a little off the ground for better visibility
			return MirrorPoint3D;
		}

		return FVector::ZeroVector;
	}

	virtual FMatrix GetWidgetRotation() const override
	{
		const ACyLandProxy* const CyLandProxy = EdMode->CurrentToolTarget.CyLandInfo->GetCyLandProxy();
		if (CyLandProxy)
		{
			const FTransform CyLandToWorld = CyLandProxy->CyLandActorToWorld();

			FMatrix Result = FQuatRotationTranslationMatrix(CyLandToWorld.GetRotation(), FVector::ZeroVector);
			if (EdMode->UISettings->MirrorOp == ECyLandMirrorOperation::PlusXToMinusX ||
				EdMode->UISettings->MirrorOp == ECyLandMirrorOperation::PlusYToMinusY ||
				EdMode->UISettings->MirrorOp == ECyLandMirrorOperation::RotatePlusXToMinusX ||
				EdMode->UISettings->MirrorOp == ECyLandMirrorOperation::RotatePlusYToMinusY)
			{
				Result = FRotationMatrix(FRotator(0, 180, 0)) * Result;
			}
			return Result;
		}

		return FMatrix::Identity;
	}

protected:
	float GetLocalZAtPoint(const UCyLandInfo* CyLandInfo, int32 x, int32 y) const
	{
		// try to find Z location
		TSet<UCyLandComponent*> Components;
		CyLandInfo->GetComponentsInRegion(x, y, x, y, Components);
		for (UCyLandComponent* Component : Components)
		{
			FCyLandComponentDataInterface DataInterface(Component);
			return CyLandDataAccess::GetLocalHeight(DataInterface.GetHeight(x - Component->SectionBaseX, y - Component->SectionBaseY));
		}
		return 0.0f;
	}

	/**
	 * @param SourceData  Data from the "source" side of the mirror op, including blend region
	 * @param DestData    Result of the mirror op, including blend region
	 * @param SourceSizeX Width of SourceData
	 * @param SourceSizeY Height of SourceData
	 * @param DestSizeX   Width of DestData
	 * @param DestSizeY   Height of DestData
	 * @param MirrorPos   Position of the mirror point in the source data (X or Y pos depending on MirrorOp)
	 * @param BlendWidth  Width of the blend region (in X or Y axis depending on MirrorOp)
	 */
	template<typename T>
	void ApplyMirrorInternal(TArrayView<const T> SourceData, TArrayView<T> DestData, int32 SourceSizeX, int32 SourceSizeY, int32 DestSizeX, int32 DestSizeY, int32 MirrorPos, int32 BlendWidth)
	{
		checkSlow(SourceData.Num() == SourceSizeX * SourceSizeY);
		checkSlow(DestData.Num()   == DestSizeX   * DestSizeY);

		switch (EdMode->UISettings->MirrorOp)
		{
		case ECyLandMirrorOperation::MinusXToPlusX:
		case ECyLandMirrorOperation::RotateMinusXToPlusX:
			{
				checkSlow(SourceSizeY == DestSizeY);
				checkSlow(MirrorPos + BlendWidth + 1 == SourceSizeX);
				const int32 BlendStart = (DestSizeX - MirrorPos - 1) - BlendWidth;
				const int32 BlendEnd   = BlendStart + 2 * BlendWidth + 1;
				const int32 Offset = 2 * MirrorPos - DestSizeX + 1;
				const bool bFlipY = (EdMode->UISettings->MirrorOp == ECyLandMirrorOperation::RotateMinusXToPlusX);
				for (int32 Y = 0; Y < DestSizeY; ++Y)
				{
					TArrayView<const T> SourceLine1 = SourceData.Slice(Y * SourceSizeX, SourceSizeX);
					TArrayView<const T> SourceLine2 = bFlipY ? SourceData.Slice((SourceSizeY - Y - 1) * SourceSizeX, SourceSizeX) : SourceLine1;
					TArrayView<T> DestLine = DestData.Slice(Y * DestSizeX, DestSizeX);

					// Pre-blend
					int32 DestX = 0;
					for (int32 SourceX = Offset; DestX < BlendStart; ++DestX, ++SourceX)
					{
						DestLine.GetData()[DestX] = SourceLine1.GetData()[SourceX];
					}

					// Blend
					for (int32 SourceX1 = BlendStart + Offset, SourceX2 = BlendEnd + Offset - 1;
						 DestX < BlendEnd; ++DestX, ++SourceX1, --SourceX2)
					{
						const float Frac = (float)(DestX - BlendStart + 1) / (BlendEnd - BlendStart + 1);
						const float Alpha = FMath::Cos(Frac * PI) * -0.5f + 0.5f;
						DestLine.GetData()[DestX] = FMath::Lerp(SourceLine1.GetData()[SourceX1], SourceLine2.GetData()[SourceX2], Alpha);
					}

					// Post-Blend
					for (int32 SourceX = BlendStart + Offset - 1; DestX < DestSizeX; ++DestX, --SourceX)
					{
						DestLine.GetData()[DestX] = SourceLine2.GetData()[SourceX];
					}
				}
			}
			break;
		case ECyLandMirrorOperation::PlusXToMinusX:
		case ECyLandMirrorOperation::RotatePlusXToMinusX:
			{
				checkSlow(SourceSizeY == DestSizeY);
				const int32 BlendStart = (SourceSizeX - MirrorPos - 1) - BlendWidth;
				const int32 BlendEnd   = BlendStart + 2 * BlendWidth + 1;
				const int32 Offset = 2 * MirrorPos - SourceSizeX + 1;
				const bool bFlipY = (EdMode->UISettings->MirrorOp == ECyLandMirrorOperation::RotatePlusXToMinusX);
				for (int32 Y = 0; Y < DestSizeY; ++Y)
				{
					TArrayView<const T> SourceLine1 = SourceData.Slice(Y * SourceSizeX, SourceSizeX);
					TArrayView<const T> SourceLine2 = bFlipY ? SourceData.Slice((SourceSizeY - Y - 1) * SourceSizeX, SourceSizeX) : SourceLine1;
					TArrayView<T> DestLine = DestData.Slice(Y * DestSizeX, DestSizeX);

					// Pre-blend
					int32 DestX = 0;
					for (int32 SourceX = SourceSizeX - 1; DestX < BlendStart; ++DestX, --SourceX)
					{
						DestLine.GetData()[DestX] = SourceLine2.GetData()[SourceX];
					}

					// Blend
					for (int32 SourceX1 = BlendStart + Offset, SourceX2 = BlendEnd + Offset - 1;
						 DestX < BlendEnd; ++DestX, ++SourceX1, --SourceX2)
					{
						const float Frac = (float)(DestX - BlendStart + 1) / (BlendEnd - BlendStart + 1);
						const float Alpha = FMath::Cos(Frac * PI) * -0.5f + 0.5f;
						DestLine.GetData()[DestX] = FMath::Lerp(SourceLine2.GetData()[SourceX2], SourceLine1.GetData()[SourceX1], Alpha);
					}

					// Post-Blend
					for (int32 SourceX = BlendEnd + Offset; DestX < DestSizeX; ++DestX, ++SourceX)
					{
						DestLine.GetData()[DestX] = SourceLine1.GetData()[SourceX];
					}
				}
			}
			break;
		case ECyLandMirrorOperation::MinusYToPlusY:
		case ECyLandMirrorOperation::RotateMinusYToPlusY:
			{
				checkSlow(SourceSizeX == DestSizeX);
				checkSlow(MirrorPos + BlendWidth + 1 == SourceSizeY);
				const int32 BlendStart = (DestSizeY - MirrorPos - 1) - BlendWidth;
				const int32 BlendEnd   = BlendStart + 2 * BlendWidth + 1;
				const int32 Offset = 2 * MirrorPos - DestSizeY + 1;
				const bool bFlipX = (EdMode->UISettings->MirrorOp == ECyLandMirrorOperation::RotateMinusYToPlusY);

				// Pre-blend
				int32 DestY = 0;
				for (int32 SourceY = Offset; DestY < BlendStart; ++DestY, ++SourceY)
				{
					TArrayView<const T> SourceLine = SourceData.Slice(SourceY * SourceSizeX, SourceSizeX);
					TArrayView<T> DestLine = DestData.Slice(DestY * DestSizeX, DestSizeX);
					FMemory::Memcpy(DestLine.GetData(), SourceLine.GetData(), DestSizeX * sizeof(T));
				}

				// Blend
				for (int32 SourceY1 = BlendStart + Offset, SourceY2 = BlendEnd + Offset - 1;
					 DestY < BlendEnd; ++DestY, ++SourceY1, --SourceY2)
				{
					const float Frac = (float)(DestY - BlendStart + 1) / (BlendEnd - BlendStart + 1);
					const float Alpha = FMath::Cos(Frac * PI) * -0.5f + 0.5f;
					TArrayView<const T> SourceLine1 = SourceData.Slice(SourceY1 * SourceSizeX, SourceSizeX);
					TArrayView<const T> SourceLine2 = SourceData.Slice(SourceY2 * SourceSizeX, SourceSizeX);
					TArrayView<T> DestLine = DestData.Slice(DestY * DestSizeX, DestSizeX);
					for (int32 DestX = 0; DestX < DestSizeX; ++DestX)
					{
						const int32 SourceX2 = bFlipX ? (SourceSizeX - DestX - 1) : DestX;
						DestLine.GetData()[DestX] = FMath::Lerp(SourceLine1.GetData()[DestX], SourceLine2.GetData()[SourceX2], Alpha);
					}
				}

				// Post-Blend
				for (int32 SourceY = BlendStart + Offset - 1; DestY < DestSizeY; ++DestY, --SourceY)
				{
					TArrayView<const T> SourceLine = SourceData.Slice(SourceY * SourceSizeX, SourceSizeX);
					TArrayView<T> DestLine = DestData.Slice(DestY * DestSizeX, DestSizeX);
					FMemory::Memcpy(DestLine.GetData(), SourceLine.GetData(), DestSizeX * sizeof(T));
					if (bFlipX)
					{
						Algo::Reverse(DestLine);
					}
				}
			}
			break;
		case ECyLandMirrorOperation::PlusYToMinusY:
		case ECyLandMirrorOperation::RotatePlusYToMinusY:
			{
				checkSlow(SourceSizeX == DestSizeX);
				const int32 BlendStart = (SourceSizeY - MirrorPos - 1) - BlendWidth;
				const int32 BlendEnd   = BlendStart + 2 * BlendWidth + 1;
				const int32 Offset = 2 * MirrorPos - SourceSizeY + 1;
				const bool bFlipX = (EdMode->UISettings->MirrorOp == ECyLandMirrorOperation::RotatePlusYToMinusY);

				// Pre-blend
				int32 DestY = 0;
				for (int32 SourceY = SourceSizeY - 1; DestY < BlendStart; ++DestY, --SourceY)
				{
					TArrayView<const T> SourceLine = SourceData.Slice(SourceY * SourceSizeX, SourceSizeX);
					TArrayView<T> DestLine = DestData.Slice(DestY * DestSizeX, DestSizeX);
					FMemory::Memcpy(DestLine.GetData(), SourceLine.GetData(), DestSizeX * sizeof(T));
					if (bFlipX)
					{
						Algo::Reverse(DestLine);
					}
				}

				// Blend
				for (int32 SourceY1 = BlendStart + Offset, SourceY2 = BlendEnd + Offset - 1;
					 DestY < BlendEnd; ++DestY, ++SourceY1, --SourceY2)
				{
					const float Frac = (float)(DestY - BlendStart + 1) / (BlendEnd - BlendStart + 1);
					const float Alpha = FMath::Cos(Frac * PI) * -0.5f + 0.5f;
					TArrayView<const T> SourceLine1 = SourceData.Slice(SourceY1 * SourceSizeX, SourceSizeX);
					TArrayView<const T> SourceLine2 = SourceData.Slice(SourceY2 * SourceSizeX, SourceSizeX);
					TArrayView<T> DestLine = DestData.Slice(DestY * DestSizeX, DestSizeX);
					for (int32 DestX = 0; DestX < DestSizeX; ++DestX)
					{
						const int32 SourceX2 = bFlipX ? (SourceSizeX - DestX - 1) : DestX;
						DestLine.GetData()[DestX] = FMath::Lerp(SourceLine2.GetData()[SourceX2], SourceLine1.GetData()[DestX], Alpha);
					}
				}

				// Post-Blend
				for (int32 SourceY = BlendEnd + Offset; DestY < DestSizeY; ++DestY, ++SourceY)
				{
					TArrayView<const T> SourceLine = SourceData.Slice(SourceY * SourceSizeX, SourceSizeX);
					TArrayView<T> DestLine = DestData.Slice(DestY * DestSizeX, DestSizeX);
					FMemory::Memcpy(DestLine.GetData(), SourceLine.GetData(), DestSizeX * sizeof(T));
				}
			}
			break;
		default:
			check(0);
			return;
		}
	}

public:
	virtual void ApplyMirror()
	{
		FScopedTransaction Transaction(LOCTEXT("Mirror_Apply", "CyLand Editing: Mirror CyLand"));

		const UCyLandInfo* const CyLandInfo = EdMode->CurrentToolTarget.CyLandInfo.Get();
		const ACyLandProxy* const CyLandProxy = CyLandInfo->GetCyLandProxy();
		const FTransform CyLandToWorld = CyLandProxy->CyLandActorToWorld();
		const FVector2D MirrorPoint = EdMode->UISettings->MirrorPoint;
		int32 BlendWidth = FMath::Clamp(EdMode->UISettings->MirrorSmoothingWidth, 0, 32768);

		FCyLandEditDataInterface CyLandEdit(EdMode->CurrentToolTarget.CyLandInfo.Get());

		int32 MinX, MinY, MaxX, MaxY;
		if (!CyLandInfo->GetCyLandExtent(MinX, MinY, MaxX, MaxY))
		{
			return;
		}

		const int32 SizeX = (1 + MaxX - MinX);
		const int32 SizeY = (1 + MaxY - MinY);

		int32 SourceMinX, SourceMinY;
		int32 SourceMaxX, SourceMaxY;
		int32 DestMinX, DestMinY;
		int32 DestMaxX, DestMaxY;
		int32 MirrorPos;

		switch (EdMode->UISettings->MirrorOp)
		{
		case ECyLandMirrorOperation::MinusXToPlusX:
		case ECyLandMirrorOperation::RotateMinusXToPlusX:
		case ECyLandMirrorOperation::PlusXToMinusX:
		case ECyLandMirrorOperation::RotatePlusXToMinusX:
			{
				MirrorPos = FMath::RoundToInt(MirrorPoint.X);
				if (MirrorPos <= MinX || MirrorPos >= MaxX)
				{
					return;
				}
				const int32 MirrorSize = FMath::Max(MaxX - MirrorPos, MirrorPos - MinX); // not including the mirror column itself
				BlendWidth = FMath::Min(BlendWidth, MirrorSize);
				if (EdMode->UISettings->MirrorOp == ECyLandMirrorOperation::MinusXToPlusX ||
					EdMode->UISettings->MirrorOp == ECyLandMirrorOperation::RotateMinusXToPlusX)
				{
					SourceMinX = MirrorPos - MirrorSize;
					SourceMaxX = MirrorPos + BlendWidth;
					DestMinX = MirrorPos - BlendWidth - 1; // extra column to calc normals for mirror column
					DestMaxX = MirrorPos + MirrorSize;
				}
				else
				{
					SourceMinX = MirrorPos - BlendWidth;
					SourceMaxX = MirrorPos + MirrorSize;
					DestMinX = MirrorPos - MirrorSize;
					DestMaxX = MirrorPos + BlendWidth + 1; // extra column to calc normals for mirror column
				}
				SourceMinY = MinY;
				SourceMaxY = MaxY;
				DestMinY = MinY;
				DestMaxY = MaxY;
				MirrorPos -= SourceMinX;
			}
			break;
		case ECyLandMirrorOperation::MinusYToPlusY:
		case ECyLandMirrorOperation::RotateMinusYToPlusY:
		case ECyLandMirrorOperation::PlusYToMinusY:
		case ECyLandMirrorOperation::RotatePlusYToMinusY:
			{
				MirrorPos = FMath::RoundToInt(MirrorPoint.Y);
				if (MirrorPos <= MinY || MirrorPos >= MaxY)
				{
					return;
				}
				const int32 MirrorSize = FMath::Max(MaxY - MirrorPos, MirrorPos - MinY); // not including the mirror row itself
				SourceMinX = MinX;
				SourceMaxX = MaxX;
				DestMinX = MinX;
				DestMaxX = MaxX;
				if (EdMode->UISettings->MirrorOp == ECyLandMirrorOperation::MinusYToPlusY ||
					EdMode->UISettings->MirrorOp == ECyLandMirrorOperation::RotateMinusYToPlusY)
				{
					SourceMinY = MirrorPos - MirrorSize;
					SourceMaxY = MirrorPos + BlendWidth;
					DestMinY = MirrorPos - BlendWidth - 1; // extra column to calc normals for mirror column
					DestMaxY = MirrorPos + MirrorSize;
				}
				else
				{
					SourceMinY = MirrorPos - BlendWidth;
					SourceMaxY = MirrorPos + MirrorSize;
					DestMinY = MirrorPos - MirrorSize;
					DestMaxY = MirrorPos + BlendWidth + 1; // extra column to calc normals for mirror column
				}
				MirrorPos -= SourceMinY;
			}
			break;
		default:
			check(0);
			return;
		}

		const int32 SourceSizeX = SourceMaxX - SourceMinX + 1;
		const int32 SourceSizeY = SourceMaxY - SourceMinY + 1;
		const int32 DestSizeX = DestMaxX - DestMinX + 1;
		const int32 DestSizeY = DestMaxY - DestMinY + 1;

		TArray<uint16> SourceHeightData, DestHeightData;
		SourceHeightData.AddUninitialized(SourceSizeX * SourceSizeY);
		DestHeightData.AddUninitialized(DestSizeX * DestSizeY);
		int32 TempMinX = SourceMinX; // GetHeightData overwrites its input min/max x/y
		int32 TempMaxX = SourceMaxX;
		int32 TempMinY = SourceMinY;
		int32 TempMaxY = SourceMaxY;
		CyLandEdit.GetHeightData(TempMinX, TempMinY, TempMaxX, TempMaxY, &SourceHeightData[0], SourceSizeX);
		ApplyMirrorInternal<uint16>(SourceHeightData, DestHeightData, SourceSizeX, SourceSizeY, DestSizeX, DestSizeY, MirrorPos, BlendWidth);
		CyLandEdit.SetHeightData(DestMinX, DestMinY, DestMaxX, DestMaxY, &DestHeightData[0], DestSizeX, true);

		TArray<uint8> SourceWeightData, DestWeightData;
		SourceWeightData.AddUninitialized(SourceSizeX * SourceSizeY);
		DestWeightData.AddUninitialized(DestSizeX * DestSizeY);
		for (const auto& LayerSettings : CyLandInfo->Layers)
		{
			UCyLandLayerInfoObject* LayerInfo = LayerSettings.LayerInfoObj;
			if (LayerInfo)
			{
				TempMinX = SourceMinX; // GetWeightData overwrites its input min/max x/y
				TempMaxX = SourceMaxX;
				TempMinY = SourceMinY;
				TempMaxY = SourceMaxY;
				CyLandEdit.GetWeightData(LayerInfo, TempMinX, TempMinY, TempMaxX, TempMaxY, &SourceWeightData[0], SourceSizeX);
				ApplyMirrorInternal<uint8>(SourceWeightData, DestWeightData, SourceSizeX, SourceSizeY, DestSizeX, DestSizeY, MirrorPos, BlendWidth);
				CyLandEdit.SetAlphaData(LayerInfo, DestMinX, DestMinY, DestMaxX, DestMaxY, &DestWeightData[0], DestSizeX, ECyLandLayerPaintingRestriction::None, false, false);
				//LayerInfo->IsReferencedFromLoadedData = true;
			}
		}

		CyLandEdit.Flush();

		TSet<UCyLandComponent*> Components;
		if (CyLandEdit.GetComponentsInRegion(DestMinX, DestMinY, DestMaxX, DestMaxY, &Components) && Components.Num() > 0)
		{
			for (UCyLandComponent* Component : Components)
			{
				// Recreate collision for modified components and update the navmesh
				UCyLandHeightfieldCollisionComponent* CollisionComponent = Component->CollisionComponent.Get();
				if (CollisionComponent)
				{
					CollisionComponent->RecreateCollision();
					FNavigationSystem::UpdateComponentData(*CollisionComponent);
				}
			}

			// Flush dynamic foliage (grass)
			ACyLandProxy::InvalidateGeneratedComponentData(Components);

			EdMode->UpdateLayerUsageInformation();
		}
	}

	void CenterMirrorPoint()
	{
		UCyLandInfo* const CyLandInfo = EdMode->CurrentToolTarget.CyLandInfo.Get();
		ACyLandProxy* const CyLandProxy = CyLandInfo->GetCyLandProxy();
		int32 MinX, MinY, MaxX, MaxY;
		if (CyLandInfo->GetCyLandExtent(MinX, MinY, MaxX, MaxY))
		{
			EdMode->UISettings->MirrorPoint = FVector2D((float)(MinX + MaxX) / 2.0f, (float)(MinY + MaxY) / 2.0f);
		}
		else
		{
			EdMode->UISettings->MirrorPoint = FVector2D(0, 0);
		}
	}
};

void FEdModeCyLand::ApplyMirrorTool()
{
	if (CurrentTool->GetToolName() == FName("Mirror"))
	{
		FCyLandToolMirror* MirrorTool = (FCyLandToolMirror*)CurrentTool;
		MirrorTool->ApplyMirror();
		GEditor->RedrawLevelEditingViewports();
	}
}

void FEdModeCyLand::CenterMirrorTool()
{
	if (CurrentTool->GetToolName() == FName("Mirror"))
	{
		FCyLandToolMirror* MirrorTool = (FCyLandToolMirror*)CurrentTool;
		MirrorTool->CenterMirrorPoint();
		GEditor->RedrawLevelEditingViewports();
	}
}

//
// Toolset initialization
//
void FEdModeCyLand::InitializeTool_Mirror()
{
	auto Tool_Mirror = MakeUnique<FCyLandToolMirror>(this);
	Tool_Mirror->ValidBrushes.Add("BrushSet_Dummy");
	CyLandTools.Add(MoveTemp(Tool_Mirror));
}

#undef LOCTEXT_NAMESPACE
