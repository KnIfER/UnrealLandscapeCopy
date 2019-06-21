// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "EngineDefines.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "InputCoreTypes.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "CollisionQueryParams.h"
#include "Components/MeshComponent.h"
#include "Exporters/Exporter.h"
#include "Editor/UnrealEdEngine.h"
#include "UObject/PropertyPortFlags.h"
#include "EngineUtils.h"
#include "EditorUndoClient.h"
#include "UnrealWidget.h"
#include "EditorModeManager.h"
#include "UnrealEdGlobals.h"
#include "EditorViewportClient.h"
#include "CyLandToolInterface.h"
#include "CyLandProxy.h"
#include "CyLandEdMode.h"
#include "ScopedTransaction.h"
#include "CyLandRender.h"
#include "CyLandSplineProxies.h"
#include "PropertyEditorModule.h"
#include "CyLandSplineImportExport.h"
#include "CyLandSplinesComponent.h"
#include "CyLandSplineSegment.h"
#include "CyLandSplineControlPoint.h"
#include "CyControlPointMeshComponent.h"
#include "Algo/Copy.h"
#include "HAL/PlatformApplicationMisc.h"
#include "UnrealExporter.h"


#define LOCTEXT_NAMESPACE "CyLand"

//
// FCyLandToolSplines
//
class FCyLandToolSplines : public FCyLandTool, public FEditorUndoClient
{
public:
	FCyLandToolSplines(FEdModeCyLand* InEdMode)
		: EdMode(InEdMode)
		, CyLandInfo(NULL)
		, SelectedSplineControlPoints()
		, SelectedSplineSegments()
		, DraggingTangent_Segment(NULL)
		, DraggingTangent_End(false)
		, bMovingControlPoint(false)
		, bAutoRotateOnJoin(true)
		, bAutoChangeConnectionsOnMove(true)
		, bDeleteLooseEnds(false)
		, bCopyMeshToNewControlPoint(false)
	{
		// Register to update when an undo/redo operation has been called to update our list of actors
		GEditor->RegisterForUndo(this);
	}

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		Collector.AddReferencedObject(CyLandInfo);
		Collector.AddReferencedObjects(SelectedSplineControlPoints);
		Collector.AddReferencedObjects(SelectedSplineSegments);
		Collector.AddReferencedObject(DraggingTangent_Segment);
	}

	~FCyLandToolSplines()
	{
		// GEditor is invalid at shutdown as the object system is unloaded before the CyLand module.
		if (UObjectInitialized())
		{
			// Remove undo delegate
			GEditor->UnregisterForUndo(this);
		}
	}

	virtual const TCHAR* GetToolName() override { return TEXT("Splines"); }
	virtual FText GetDisplayName() override { return NSLOCTEXT("UnrealEd", "CyLandMode_Splines", "Splines"); };

	virtual void SetEditRenderType() override { GCyLandEditRenderMode = ECyLandEditRenderMode::None | (GCyLandEditRenderMode & ECyLandEditRenderMode::BitMaskForMask); }
	virtual bool SupportsMask() override { return false; }

	void CreateSplineComponent(ACyLandProxy* CyLand, FVector Scale3D)
	{
		CyLand->Modify();
		CyLand->SplineComponent = NewObject<UCyLandSplinesComponent>(CyLand, NAME_None, RF_Transactional);
		CyLand->SplineComponent->RelativeScale3D = Scale3D;
		CyLand->SplineComponent->AttachToComponent(CyLand->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		CyLand->SplineComponent->ShowSplineEditorMesh(true);
	}

	void UpdatePropertiesWindows()
	{
		if (GLevelEditorModeTools().IsModeActive(EdMode->GetID()))
		{
			TArray<UObject*> Objects;
			Objects.Reset(SelectedSplineControlPoints.Num() + SelectedSplineSegments.Num());
			Algo::Copy(SelectedSplineControlPoints, Objects);
			Algo::Copy(SelectedSplineSegments, Objects);

			FPropertyEditorModule& PropertyModule = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
			PropertyModule.UpdatePropertyViews(Objects);
		}
	}

	void ClearSelectedControlPoints()
	{
		for (UCyLandSplineControlPoint* ControlPoint : SelectedSplineControlPoints)
		{
			checkSlow(ControlPoint->IsSplineSelected());
			ControlPoint->Modify(false);
			ControlPoint->SetSplineSelected(false);
		}
		SelectedSplineControlPoints.Empty();
	}

	void ClearSelectedSegments()
	{
		for (UCyLandSplineSegment* Segment : SelectedSplineSegments)
		{
			checkSlow(Segment->IsSplineSelected());
			Segment->Modify(false);
			Segment->SetSplineSelected(false);
		}
		SelectedSplineSegments.Empty();
	}

	void ClearSelection()
	{
		ClearSelectedControlPoints();
		ClearSelectedSegments();
	}

	void DeselectControlPoint(UCyLandSplineControlPoint* ControlPoint)
	{
		checkSlow(ControlPoint->IsSplineSelected());
		SelectedSplineControlPoints.Remove(ControlPoint);
		ControlPoint->Modify(false);
		ControlPoint->SetSplineSelected(false);
	}

	void DeSelectSegment(UCyLandSplineSegment* Segment)
	{
		checkSlow(Segment->IsSplineSelected());
		SelectedSplineSegments.Remove(Segment);
		Segment->Modify(false);
		Segment->SetSplineSelected(false);
	}

	void SelectControlPoint(UCyLandSplineControlPoint* ControlPoint)
	{
		checkSlow(!ControlPoint->IsSplineSelected());
		SelectedSplineControlPoints.Add(ControlPoint);
		ControlPoint->Modify(false);
		ControlPoint->SetSplineSelected(true);
	}

	void SelectSegment(UCyLandSplineSegment* Segment)
	{
		checkSlow(!Segment->IsSplineSelected());
		SelectedSplineSegments.Add(Segment);
		Segment->Modify(false);
		Segment->SetSplineSelected(true);

		GLevelEditorModeTools().SetWidgetMode(FWidget::WM_Scale);
	}

	void SelectConnected()
	{
		TArray<UCyLandSplineControlPoint*> ControlPointsToProcess = SelectedSplineControlPoints.Array();

		while (ControlPointsToProcess.Num() > 0)
		{
			const UCyLandSplineControlPoint* ControlPoint = ControlPointsToProcess.Pop();

			for (const FCyLandSplineConnection& Connection : ControlPoint->ConnectedSegments)
			{
				UCyLandSplineControlPoint* OtherEnd = Connection.GetFarConnection().ControlPoint;

				if (!OtherEnd->IsSplineSelected())
				{
					SelectControlPoint(OtherEnd);
					ControlPointsToProcess.Add(OtherEnd);
				}
			}
		}

		TArray<UCyLandSplineSegment*> SegmentsToProcess = SelectedSplineSegments.Array();

		while (SegmentsToProcess.Num() > 0)
		{
			const UCyLandSplineSegment* Segment = SegmentsToProcess.Pop();

			for (const FCyLandSplineSegmentConnection& SegmentConnection : Segment->Connections)
			{
				for (const FCyLandSplineConnection& Connection : SegmentConnection.ControlPoint->ConnectedSegments)
				{
					if (Connection.Segment != Segment && !Connection.Segment->IsSplineSelected())
					{
						SelectSegment(Connection.Segment);
						SegmentsToProcess.Add(Connection.Segment);
					}
				}
			}
		}
	}

	void SelectAdjacentControlPoints()
	{
		for (UCyLandSplineSegment* Segment : SelectedSplineSegments)
		{
			if (!Segment->Connections[0].ControlPoint->IsSplineSelected())
			{
				SelectControlPoint(Segment->Connections[0].ControlPoint);
			}
			if (!Segment->Connections[1].ControlPoint->IsSplineSelected())
			{
				SelectControlPoint(Segment->Connections[1].ControlPoint);
			}
		}
	}

	void SelectAdjacentSegments()
	{
		for (UCyLandSplineControlPoint* ControlPoint : SelectedSplineControlPoints)
		{
			for (const FCyLandSplineConnection& Connection : ControlPoint->ConnectedSegments)
			{
				if (!Connection.Segment->IsSplineSelected())
				{
					SelectSegment(Connection.Segment);
				}
			}
		}
	}

	void AddSegment(UCyLandSplineControlPoint* Start, UCyLandSplineControlPoint* End, bool bAutoRotateStart, bool bAutoRotateEnd)
	{
		FScopedTransaction Transaction(LOCTEXT("CyLandSpline_AddSegment", "Add CyLand Spline Segment"));

		if (Start == End)
		{
			//UE_LOG( TEXT("Can't join spline control point to itself.") );
			return;
		}

		if (Start->GetOuterUCyLandSplinesComponent() != End->GetOuterUCyLandSplinesComponent())
		{
			//UE_LOG( TEXT("Can't join spline control points across different terrains.") );
			return;
		}

		for (const FCyLandSplineConnection& Connection : Start->ConnectedSegments)
		{
			// if the *other* end on the connected segment connects to the "end" control point...
			if (Connection.GetFarConnection().ControlPoint == End)
			{
				//UE_LOG( TEXT("Spline control points already joined connected!") );
				return;
			}
		}

		UCyLandSplinesComponent* SplinesComponent = Start->GetOuterUCyLandSplinesComponent();
		SplinesComponent->Modify();
		Start->Modify();
		End->Modify();

		UCyLandSplineSegment* NewSegment = NewObject<UCyLandSplineSegment>(SplinesComponent, NAME_None, RF_Transactional);
		SplinesComponent->Segments.Add(NewSegment);

		NewSegment->Connections[0].ControlPoint = Start;
		NewSegment->Connections[1].ControlPoint = End;

		NewSegment->Connections[0].SocketName = Start->GetBestConnectionTo(End->Location);
		NewSegment->Connections[1].SocketName = End->GetBestConnectionTo(Start->Location);

		FVector StartLocation; FRotator StartRotation;
		Start->GetConnectionLocationAndRotation(NewSegment->Connections[0].SocketName, StartLocation, StartRotation);
		FVector EndLocation; FRotator EndRotation;
		End->GetConnectionLocationAndRotation(NewSegment->Connections[1].SocketName, EndLocation, EndRotation);

		// Set up tangent lengths
		NewSegment->Connections[0].TangentLen = (EndLocation - StartLocation).Size();
		NewSegment->Connections[1].TangentLen = NewSegment->Connections[0].TangentLen;

		NewSegment->AutoFlipTangents();

		// set up other segment options
		UCyLandSplineSegment* CopyFromSegment = nullptr;
		if (Start->ConnectedSegments.Num() > 0)
		{
			CopyFromSegment = Start->ConnectedSegments[0].Segment;
		}
		else if (End->ConnectedSegments.Num() > 0)
		{
			CopyFromSegment = End->ConnectedSegments[0].Segment;
		}
		else
		{
			// Use defaults
		}

		if (CopyFromSegment != nullptr)
		{
			NewSegment->LayerName         = CopyFromSegment->LayerName;
			NewSegment->SplineMeshes      = CopyFromSegment->SplineMeshes;
			NewSegment->LDMaxDrawDistance = CopyFromSegment->LDMaxDrawDistance;
			NewSegment->bRaiseTerrain     = CopyFromSegment->bRaiseTerrain;
			NewSegment->bLowerTerrain     = CopyFromSegment->bLowerTerrain;
			NewSegment->bPlaceSplineMeshesInStreamingLevels = CopyFromSegment->bPlaceSplineMeshesInStreamingLevels;
			NewSegment->BodyInstance  = CopyFromSegment->BodyInstance;
			NewSegment->bCastShadow       = CopyFromSegment->bCastShadow;
		}

		Start->ConnectedSegments.Add(FCyLandSplineConnection(NewSegment, 0));
		End->ConnectedSegments.Add(FCyLandSplineConnection(NewSegment, 1));

		bool bUpdatedStart = false;
		bool bUpdatedEnd = false;
		if (bAutoRotateStart)
		{
			Start->AutoCalcRotation();
			Start->UpdateSplinePoints();
			bUpdatedStart = true;
		}
		if (bAutoRotateEnd)
		{
			End->AutoCalcRotation();
			End->UpdateSplinePoints();
			bUpdatedEnd = true;
		}

		// Control points' points are currently based on connected segments, so need to be updated.
		if (!bUpdatedStart && Start->Mesh)
		{
			Start->UpdateSplinePoints();
		}
		if (!bUpdatedEnd && End->Mesh)
		{
			End->UpdateSplinePoints();
		}

		// If we've called UpdateSplinePoints on either control point it will already have called UpdateSplinePoints on the new segment
		if (!(bUpdatedStart || bUpdatedEnd))
		{
			NewSegment->UpdateSplinePoints();
		}
	}

	void AddControlPoint(UCyLandSplinesComponent* SplinesComponent, const FVector& LocalLocation)
	{
		FScopedTransaction Transaction(LOCTEXT("CyLandSpline_AddControlPoint", "Add CyLand Spline Control Point"));

		SplinesComponent->Modify();

		UCyLandSplineControlPoint* NewControlPoint = NewObject<UCyLandSplineControlPoint>(SplinesComponent, NAME_None, RF_Transactional);
		SplinesComponent->ControlPoints.Add(NewControlPoint);

		NewControlPoint->Location = LocalLocation;

		if (SelectedSplineControlPoints.Num() > 0)
		{
			UCyLandSplineControlPoint* FirstPoint = *SelectedSplineControlPoints.CreateConstIterator();
			NewControlPoint->Rotation    = (NewControlPoint->Location - FirstPoint->Location).Rotation();
			NewControlPoint->Width       = FirstPoint->Width;
			NewControlPoint->SideFalloff = FirstPoint->SideFalloff;
			NewControlPoint->EndFalloff  = FirstPoint->EndFalloff;

			if (bCopyMeshToNewControlPoint)
			{
				NewControlPoint->Mesh             = FirstPoint->Mesh;
				NewControlPoint->MeshScale        = FirstPoint->MeshScale;
				NewControlPoint->bPlaceSplineMeshesInStreamingLevels = FirstPoint->bPlaceSplineMeshesInStreamingLevels;
				NewControlPoint->BodyInstance = FirstPoint->BodyInstance;
				NewControlPoint->bCastShadow      = FirstPoint->bCastShadow;
			}

			for (UCyLandSplineControlPoint* ControlPoint : SelectedSplineControlPoints)
			{
				AddSegment(ControlPoint, NewControlPoint, bAutoRotateOnJoin, true);
			}
		}
		else
		{
			// required to make control point visible
			NewControlPoint->UpdateSplinePoints();
		}

		ClearSelection();
		SelectControlPoint(NewControlPoint);
		UpdatePropertiesWindows();

		if (!SplinesComponent->IsRegistered())
		{
			SplinesComponent->RegisterComponent();
		}
		else
		{
			SplinesComponent->MarkRenderStateDirty();
		}
	}

	void DeleteSegment(UCyLandSplineSegment* ToDelete, bool bInDeleteLooseEnds)
	{
		FScopedTransaction Transaction(LOCTEXT("CyLandSpline_DeleteSegment", "Delete CyLand Spline Segment"));

		UCyLandSplinesComponent* SplinesComponent = ToDelete->GetOuterUCyLandSplinesComponent();
		SplinesComponent->Modify();

		ToDelete->Modify();
		ToDelete->DeleteSplinePoints();

		ToDelete->Connections[0].ControlPoint->Modify();
		ToDelete->Connections[1].ControlPoint->Modify();
		ToDelete->Connections[0].ControlPoint->ConnectedSegments.Remove(FCyLandSplineConnection(ToDelete, 0));
		ToDelete->Connections[1].ControlPoint->ConnectedSegments.Remove(FCyLandSplineConnection(ToDelete, 1));

		if (bInDeleteLooseEnds)
		{
			if (ToDelete->Connections[0].ControlPoint->ConnectedSegments.Num() == 0)
			{
				SplinesComponent->ControlPoints.Remove(ToDelete->Connections[0].ControlPoint);
			}
			if (ToDelete->Connections[1].ControlPoint != ToDelete->Connections[0].ControlPoint
				&& ToDelete->Connections[1].ControlPoint->ConnectedSegments.Num() == 0)
			{
				SplinesComponent->ControlPoints.Remove(ToDelete->Connections[1].ControlPoint);
			}
		}

		SplinesComponent->Segments.Remove(ToDelete);

		// Control points' points are currently based on connected segments, so need to be updated.
		if (ToDelete->Connections[0].ControlPoint->Mesh != NULL)
		{
			ToDelete->Connections[0].ControlPoint->UpdateSplinePoints();
		}
		if (ToDelete->Connections[1].ControlPoint->Mesh != NULL)
		{
			ToDelete->Connections[1].ControlPoint->UpdateSplinePoints();
		}

		SplinesComponent->MarkRenderStateDirty();
	}

	void DeleteControlPoint(UCyLandSplineControlPoint* ToDelete, bool bInDeleteLooseEnds)
	{
		FScopedTransaction Transaction(LOCTEXT("CyLandSpline_DeleteControlPoint", "Delete CyLand Spline Control Point"));

		UCyLandSplinesComponent* SplinesComponent = ToDelete->GetOuterUCyLandSplinesComponent();
		SplinesComponent->Modify();

		ToDelete->Modify();
		ToDelete->DeleteSplinePoints();

		if (ToDelete->ConnectedSegments.Num() == 2
			&& ToDelete->ConnectedSegments[0].Segment != ToDelete->ConnectedSegments[1].Segment)
		{
			int32 Result = FMessageDialog::Open(EAppMsgType::YesNoCancel, LOCTEXT("WantToJoinControlPoint", "Control point has two segments attached, do you want to join them?"));
			switch (Result)
			{
			case EAppReturnType::Yes:
			{
				// Copy the other end of connection 1 into the near end of connection 0, then delete connection 1
				TArray<FCyLandSplineConnection>& Connections = ToDelete->ConnectedSegments;
				Connections[0].Segment->Modify();
				Connections[1].Segment->Modify();

				Connections[0].GetNearConnection() = Connections[1].GetFarConnection();
				Connections[0].Segment->UpdateSplinePoints();

				Connections[1].Segment->DeleteSplinePoints();

				// Get the control point at the *other* end of the segment and remove it from it
				UCyLandSplineControlPoint* OtherEnd = Connections[1].GetFarConnection().ControlPoint;
				OtherEnd->Modify();

				FCyLandSplineConnection* OtherConnection = OtherEnd->ConnectedSegments.FindByKey(FCyLandSplineConnection(Connections[1].Segment, 1 - Connections[1].End));
				*OtherConnection = FCyLandSplineConnection(Connections[0].Segment, Connections[0].End);

				SplinesComponent->Segments.Remove(Connections[1].Segment);

				ToDelete->ConnectedSegments.Empty();

				SplinesComponent->ControlPoints.Remove(ToDelete);
				SplinesComponent->MarkRenderStateDirty();

				return;
			}
				break;
			case EAppReturnType::No:
				// Use the "delete all segments" code below
				break;
			case EAppReturnType::Cancel:
				// Do nothing
				return;
			}
		}

		for (FCyLandSplineConnection& Connection : ToDelete->ConnectedSegments)
		{
			Connection.Segment->Modify();
			Connection.Segment->DeleteSplinePoints();

			// Get the control point at the *other* end of the segment and remove it from it
			UCyLandSplineControlPoint* OtherEnd = Connection.GetFarConnection().ControlPoint;
			OtherEnd->Modify();
			OtherEnd->ConnectedSegments.Remove(FCyLandSplineConnection(Connection.Segment, 1 - Connection.End));
			SplinesComponent->Segments.Remove(Connection.Segment);

			if (bInDeleteLooseEnds)
			{
				if (OtherEnd != ToDelete
					&& OtherEnd->ConnectedSegments.Num() == 0)
				{
					SplinesComponent->ControlPoints.Remove(OtherEnd);
				}
			}
		}

		ToDelete->ConnectedSegments.Empty();

		SplinesComponent->ControlPoints.Remove(ToDelete);
		SplinesComponent->MarkRenderStateDirty();
	}

	void SplitSegment(UCyLandSplineSegment* Segment, const FVector& LocalLocation)
	{
		FScopedTransaction Transaction(LOCTEXT("CyLandSpline_SplitSegment", "Split CyLand Spline Segment"));

		UCyLandSplinesComponent* SplinesComponent = Segment->GetOuterUCyLandSplinesComponent();
		SplinesComponent->Modify();
		Segment->Modify();
		Segment->Connections[1].ControlPoint->Modify();

		float t;
		FVector Location;
		FVector Tangent;
		Segment->FindNearest(LocalLocation, t, Location, Tangent);

		UCyLandSplineControlPoint* NewControlPoint = NewObject<UCyLandSplineControlPoint>(SplinesComponent, NAME_None, RF_Transactional);
		SplinesComponent->ControlPoints.Add(NewControlPoint);

		NewControlPoint->Location = Location;
		NewControlPoint->Rotation = Tangent.Rotation();
		NewControlPoint->Rotation.Roll = FMath::Lerp(Segment->Connections[0].ControlPoint->Rotation.Roll, Segment->Connections[1].ControlPoint->Rotation.Roll, t);
		NewControlPoint->Width = FMath::Lerp(Segment->Connections[0].ControlPoint->Width, Segment->Connections[1].ControlPoint->Width, t);
		NewControlPoint->SideFalloff = FMath::Lerp(Segment->Connections[0].ControlPoint->SideFalloff, Segment->Connections[1].ControlPoint->SideFalloff, t);
		NewControlPoint->EndFalloff = FMath::Lerp(Segment->Connections[0].ControlPoint->EndFalloff, Segment->Connections[1].ControlPoint->EndFalloff, t);

		UCyLandSplineSegment* NewSegment = NewObject<UCyLandSplineSegment>(SplinesComponent, NAME_None, RF_Transactional);
		SplinesComponent->Segments.Add(NewSegment);

		NewSegment->Connections[0].ControlPoint = NewControlPoint;
		NewSegment->Connections[0].TangentLen = Tangent.Size() * (1 - t);
		NewSegment->Connections[0].ControlPoint->ConnectedSegments.Add(FCyLandSplineConnection(NewSegment, 0));
		NewSegment->Connections[1].ControlPoint = Segment->Connections[1].ControlPoint;
		NewSegment->Connections[1].TangentLen = Segment->Connections[1].TangentLen * (1 - t);
		NewSegment->Connections[1].ControlPoint->ConnectedSegments.Add(FCyLandSplineConnection(NewSegment, 1));
		NewSegment->LayerName = Segment->LayerName;
		NewSegment->SplineMeshes = Segment->SplineMeshes;
		NewSegment->LDMaxDrawDistance = Segment->LDMaxDrawDistance;
		NewSegment->bRaiseTerrain = Segment->bRaiseTerrain;
		NewSegment->bLowerTerrain = Segment->bLowerTerrain;
		NewSegment->BodyInstance = Segment->BodyInstance;
		NewSegment->bCastShadow = Segment->bCastShadow;

		Segment->Connections[0].TangentLen *= t;
		Segment->Connections[1].ControlPoint->ConnectedSegments.Remove(FCyLandSplineConnection(Segment, 1));
		Segment->Connections[1].ControlPoint = NewControlPoint;
		Segment->Connections[1].TangentLen = -Tangent.Size() * t;
		Segment->Connections[1].ControlPoint->ConnectedSegments.Add(FCyLandSplineConnection(Segment, 1));

		Segment->UpdateSplinePoints();
		NewSegment->UpdateSplinePoints();

		ClearSelection();
		UpdatePropertiesWindows();

		SplinesComponent->MarkRenderStateDirty();
	}

	void FlipSegment(UCyLandSplineSegment* Segment)
	{
		FScopedTransaction Transaction(LOCTEXT("CyLandSpline_FlipSegment", "Flip CyLand Spline Segment"));

		UCyLandSplinesComponent* SplinesComponent = Segment->GetOuterUCyLandSplinesComponent();
		SplinesComponent->Modify();
		Segment->Modify();

		Segment->Connections[0].ControlPoint->Modify();
		Segment->Connections[1].ControlPoint->Modify();
		Segment->Connections[0].ControlPoint->ConnectedSegments.FindByKey(FCyLandSplineConnection(Segment, 0))->End = 1;
		Segment->Connections[1].ControlPoint->ConnectedSegments.FindByKey(FCyLandSplineConnection(Segment, 1))->End = 0;
		Swap(Segment->Connections[0], Segment->Connections[1]);

		Segment->UpdateSplinePoints();
	}

	void SnapControlPointToGround(UCyLandSplineControlPoint* ControlPoint)
	{
		FScopedTransaction Transaction(LOCTEXT("CyLandSpline_SnapToGround", "Snap CyLand Spline to Ground"));

		UCyLandSplinesComponent* SplinesComponent = ControlPoint->GetOuterUCyLandSplinesComponent();
		SplinesComponent->Modify();
		ControlPoint->Modify();

		const FTransform LocalToWorld = SplinesComponent->GetComponentToWorld();
		const FVector Start = LocalToWorld.TransformPosition(ControlPoint->Location);
		const FVector End = Start + FVector(0, 0, -HALF_WORLD_MAX);

		static FName TraceTag = FName(TEXT("SnapCyLandSplineControlPointToGround"));
		FHitResult Hit;
		UWorld* World = SplinesComponent->GetWorld();
		check(World);
		if (World->LineTraceSingleByObjectType(Hit, Start, End, FCollisionObjectQueryParams(ECC_WorldStatic), FCollisionQueryParams(NAME_None, FCollisionQueryParams::GetUnknownStatId(),true)))
		{
			ControlPoint->Location = LocalToWorld.InverseTransformPosition(Hit.Location);
			ControlPoint->UpdateSplinePoints();
			SplinesComponent->MarkRenderStateDirty();
		}
	}

	void MoveSelectedToLevel()
	{
		TSet<ACyLandProxy*> FromProxies;
		ACyLandProxy* ToCyLand = nullptr;

		for (UCyLandSplineControlPoint* ControlPoint : SelectedSplineControlPoints)
		{
			UCyLandSplinesComponent* CyLandSplinesComp = ControlPoint->GetOuterUCyLandSplinesComponent();
			ACyLandProxy* FromProxy = CyLandSplinesComp ? Cast<ACyLandProxy>(CyLandSplinesComp->GetOuter()) : nullptr;
			if (FromProxy)
			{
				if (!ToCyLand)
				{
					UCyLandInfo* ProxyCyLandInfo = FromProxy->GetCyLandInfo();
					check(ProxyCyLandInfo);
					ToCyLand = ProxyCyLandInfo->GetCurrentLevelCyLandProxy(true);
					if (!ToCyLand)
					{
						// No CyLand Proxy, don't support for creating only for Spline now
						return;
					}
				}

				if (ToCyLand != FromProxy)
				{
					ToCyLand->Modify();
					if (ToCyLand->SplineComponent == nullptr)
					{
						CreateSplineComponent(ToCyLand, FromProxy->SplineComponent->RelativeScale3D);
						check(ToCyLand->SplineComponent);
					}
					ToCyLand->SplineComponent->Modify();

					const FTransform OldToNewTransform =
						FromProxy->SplineComponent->GetComponentTransform().GetRelativeTransform(ToCyLand->SplineComponent->GetComponentTransform());

					if (FromProxies.Find(FromProxy) == nullptr)
					{
						FromProxies.Add(FromProxy);
						FromProxy->Modify();
						FromProxy->SplineComponent->Modify();
						FromProxy->SplineComponent->MarkRenderStateDirty();
					}

					// Handle control point mesh
					if (ControlPoint->bPlaceSplineMeshesInStreamingLevels)
					{
						// Mark previously local component as Foreign
						if (ControlPoint->LocalMeshComponent)
						{
							auto* MeshComponent = ControlPoint->LocalMeshComponent;
							verifySlow(FromProxy->SplineComponent->MeshComponentLocalOwnersMap.Remove((UMeshComponent*)MeshComponent) == 1);
							FromProxy->SplineComponent->AddForeignMeshComponent(ControlPoint, MeshComponent);
						}
						ControlPoint->LocalMeshComponent = nullptr;

						// Mark previously foreign component as local
						auto* MeshComponent = ToCyLand->SplineComponent->GetForeignMeshComponent(ControlPoint);
						if (MeshComponent)
						{
							ToCyLand->SplineComponent->RemoveForeignMeshComponent(ControlPoint, MeshComponent);
							ToCyLand->SplineComponent->MeshComponentLocalOwnersMap.Add((UMeshComponent*)MeshComponent, ControlPoint);
						}
						ControlPoint->LocalMeshComponent = MeshComponent;
					}
					else
					{
						// non-streaming case
						if (ControlPoint->LocalMeshComponent)
						{
							UCyControlPointMeshComponent* MeshComponent = ControlPoint->LocalMeshComponent;
							MeshComponent->Modify();
							MeshComponent->UnregisterComponent();
							MeshComponent->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
							MeshComponent->InvalidateLightingCache();
							MeshComponent->Rename(nullptr, ToCyLand);
							MeshComponent->AttachToComponent(ToCyLand->SplineComponent, FAttachmentTransformRules::KeepWorldTransform);

							verifySlow(FromProxy->SplineComponent->MeshComponentLocalOwnersMap.Remove(MeshComponent) == 1);
							ToCyLand->SplineComponent->MeshComponentLocalOwnersMap.Add(MeshComponent, ControlPoint);
						}
					}

					// Move control point to new level
					FromProxy->SplineComponent->ControlPoints.Remove(ControlPoint);
					ControlPoint->Rename(nullptr, ToCyLand->SplineComponent);
					ToCyLand->SplineComponent->ControlPoints.Add(ControlPoint);

					ControlPoint->Location = OldToNewTransform.TransformPosition(ControlPoint->Location);

					ControlPoint->UpdateSplinePoints(true, false);
				}
			}
		}

		for (UCyLandSplineSegment* Segment : SelectedSplineSegments)
		{
			UCyLandSplinesComponent* CyLandSplinesComp = Segment->GetOuterUCyLandSplinesComponent();
			ACyLandProxy* FromProxy = CyLandSplinesComp ? Cast<ACyLandProxy>(CyLandSplinesComp->GetOuter()) : nullptr;
			if (FromProxy)
			{
				if (!ToCyLand)
				{
					UCyLandInfo* ProxyCyLandInfo = FromProxy->GetCyLandInfo();
					check(ProxyCyLandInfo);
					ToCyLand = ProxyCyLandInfo->GetCurrentLevelCyLandProxy(true);
					if (!ToCyLand)
					{
						// No CyLand Proxy, don't support for creating only for Spline now
						return;
					}
				}

				if (ToCyLand != FromProxy)
				{
					ToCyLand->Modify();
					if (ToCyLand->SplineComponent == nullptr)
					{
						CreateSplineComponent(ToCyLand, FromProxy->SplineComponent->RelativeScale3D);
						check(ToCyLand->SplineComponent);
					}
					ToCyLand->SplineComponent->Modify();

					if (FromProxies.Find(FromProxy) == nullptr)
					{
						FromProxies.Add(FromProxy);
						FromProxy->Modify();
						FromProxy->SplineComponent->Modify();
						FromProxy->SplineComponent->MarkRenderStateDirty();
					}

					// Handle spline meshes
					if (Segment->bPlaceSplineMeshesInStreamingLevels)
					{
						// Mark previously local components as Foreign
						for (auto* MeshComponent : Segment->LocalMeshComponents)
						{
							verifySlow(FromProxy->SplineComponent->MeshComponentLocalOwnersMap.Remove(MeshComponent) == 1);
							FromProxy->SplineComponent->AddForeignMeshComponent(Segment, MeshComponent);
						}
						Segment->LocalMeshComponents.Empty();

						// Mark previously foreign components as local
						TArray<USplineMeshComponent*> MeshComponents = ToCyLand->SplineComponent->GetForeignMeshComponents(Segment);
						ToCyLand->SplineComponent->RemoveAllForeignMeshComponents(Segment);
						for (auto* MeshComponent : MeshComponents)
						{
							ToCyLand->SplineComponent->MeshComponentLocalOwnersMap.Add(MeshComponent, Segment);
						}
						Segment->LocalMeshComponents = MoveTemp(MeshComponents);
					}
					else
					{
						// non-streaming case
						for (auto* MeshComponent : Segment->LocalMeshComponents)
						{
							MeshComponent->Modify();
							MeshComponent->UnregisterComponent();
							MeshComponent->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
							MeshComponent->InvalidateLightingCache();
							MeshComponent->Rename(nullptr, ToCyLand);
							MeshComponent->AttachToComponent(ToCyLand->SplineComponent, FAttachmentTransformRules::KeepWorldTransform);

							verifySlow(FromProxy->SplineComponent->MeshComponentLocalOwnersMap.Remove(MeshComponent) == 1);
							ToCyLand->SplineComponent->MeshComponentLocalOwnersMap.Add(MeshComponent, Segment);
						}
					}

					// Move segment to new level
					FromProxy->SplineComponent->Segments.Remove(Segment);
					Segment->Rename(nullptr, ToCyLand->SplineComponent);
					ToCyLand->SplineComponent->Segments.Add(Segment);

					Segment->UpdateSplinePoints();
				}
			}
		}

		if (ToCyLand && ToCyLand->SplineComponent)
		{
			if (!ToCyLand->SplineComponent->IsRegistered())
			{
				ToCyLand->SplineComponent->RegisterComponent();
			}
			else
			{
				ToCyLand->SplineComponent->MarkRenderStateDirty();
			}
		}

		GUnrealEd->RedrawLevelEditingViewports();
	}

	void ShowSplineProperties()
	{
		TArray<UObject*> Objects;
		Objects.Reset(SelectedSplineControlPoints.Num() + SelectedSplineSegments.Num());
		Algo::Copy(SelectedSplineControlPoints, Objects);
		Algo::Copy(SelectedSplineSegments, Objects);

		FPropertyEditorModule& PropertyModule = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
		if (!PropertyModule.HasUnlockedDetailViews())
		{
			PropertyModule.CreateFloatingDetailsView(Objects, true);
		}
		else
		{
			PropertyModule.UpdatePropertyViews(Objects);
		}
	}

	virtual bool BeginTool(FEditorViewportClient* ViewportClient, const FCyLandToolTarget& InTarget, const FVector& InHitLocation) override
	{
		if (ViewportClient->IsCtrlPressed())
		{
			CyLandInfo = InTarget.CyLandInfo.Get();
			ACyLandProxy* CyLand = CyLandInfo->GetCurrentLevelCyLandProxy(true);
			if (!CyLand)
			{
				return false;
			}

			UCyLandSplinesComponent* SplinesComponent = nullptr;
			if (SelectedSplineControlPoints.Num() > 0)
			{
				UCyLandSplineControlPoint* FirstPoint = *SelectedSplineControlPoints.CreateConstIterator();
				SplinesComponent = FirstPoint->GetOuterUCyLandSplinesComponent();
			}

			if (!SplinesComponent)
			{
				if (!CyLand->SplineComponent)
				{
					CreateSplineComponent(CyLand, FVector(1.0f) / CyLand->GetRootComponent()->RelativeScale3D);
					check(CyLand->SplineComponent);
				}
				SplinesComponent = CyLand->SplineComponent;
			}

			const FTransform CyLandToSpline = CyLand->CyLandActorToWorld().GetRelativeTransform(SplinesComponent->GetComponentTransform());

			AddControlPoint(SplinesComponent, CyLandToSpline.TransformPosition(InHitLocation));

			GUnrealEd->RedrawLevelEditingViewports();

			return true;
		}

		return false;
	}

	virtual void EndTool(FEditorViewportClient* ViewportClient) override
	{
		CyLandInfo = NULL;
	}

	virtual bool MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y) override
	{
		FVector HitLocation;
		if (EdMode->CyLandMouseTrace(ViewportClient, x, y, HitLocation))
		{
			//if( bToolActive )
			//{
			//	// Apply tool
			//	ApplyTool(ViewportClient);
			//}
		}

		return true;
	}

	virtual void ApplyTool(FEditorViewportClient* ViewportClient)
	{
	}

	virtual bool HandleClick(HHitProxy* HitProxy, const FViewportClick& Click) override
	{
		if ((!HitProxy || !HitProxy->IsA(HWidgetAxis::StaticGetType()))
			&& !Click.IsShiftDown())
		{
			ClearSelection();
			UpdatePropertiesWindows();
			GUnrealEd->RedrawLevelEditingViewports();
		}

		if (HitProxy)
		{
			UCyLandSplineControlPoint* ClickedControlPoint = NULL;
			UCyLandSplineSegment* ClickedSplineSegment = NULL;

			if (HitProxy->IsA(HCyLandSplineProxy_ControlPoint::StaticGetType()))
			{
				HCyLandSplineProxy_ControlPoint* SplineProxy = (HCyLandSplineProxy_ControlPoint*)HitProxy;
				ClickedControlPoint = SplineProxy->ControlPoint;
			}
			else if (HitProxy->IsA(HCyLandSplineProxy_Segment::StaticGetType()))
			{
				HCyLandSplineProxy_Segment* SplineProxy = (HCyLandSplineProxy_Segment*)HitProxy;
				ClickedSplineSegment = SplineProxy->SplineSegment;
			}
			else if (HitProxy->IsA(HActor::StaticGetType()))
			{
				HActor* ActorProxy = (HActor*)HitProxy;
				AActor* Actor = ActorProxy->Actor;
				const UMeshComponent* MeshComponent = Cast<UMeshComponent>(ActorProxy->PrimComponent);
				if (MeshComponent)
				{
					UCyLandSplinesComponent* SplineComponent = Actor->FindComponentByClass<UCyLandSplinesComponent>();
					if (SplineComponent)
					{
						UObject* ComponentOwner = SplineComponent->GetOwnerForMeshComponent(MeshComponent);
						if (ComponentOwner)
						{
							if (UCyLandSplineControlPoint* ControlPoint = Cast<UCyLandSplineControlPoint>(ComponentOwner))
							{
								ClickedControlPoint = ControlPoint;
							}
							else if (UCyLandSplineSegment* SplineSegment = Cast<UCyLandSplineSegment>(ComponentOwner))
							{
								ClickedSplineSegment = SplineSegment;
							}
						}
					}
				}
			}

			if (ClickedControlPoint != NULL)
			{
				if (Click.IsShiftDown() && ClickedControlPoint->IsSplineSelected())
				{
					DeselectControlPoint(ClickedControlPoint);
				}
				else
				{
					SelectControlPoint(ClickedControlPoint);
				}
				GEditor->SelectNone(true, true);
				UpdatePropertiesWindows();

				GUnrealEd->RedrawLevelEditingViewports();
				return true;
			}
			else if (ClickedSplineSegment != NULL)
			{
				// save info about what we grabbed
				if (Click.IsShiftDown() && ClickedSplineSegment->IsSplineSelected())
				{
					DeSelectSegment(ClickedSplineSegment);
				}
				else
				{
					SelectSegment(ClickedSplineSegment);
				}
				GEditor->SelectNone(true, true);
				UpdatePropertiesWindows();

				GUnrealEd->RedrawLevelEditingViewports();
				return true;
			}
		}

		return false;
	}

	virtual bool InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport, FKey InKey, EInputEvent InEvent) override
	{
		if (InKey == EKeys::F4 && InEvent == IE_Pressed)
		{
			if (SelectedSplineControlPoints.Num() > 0 || SelectedSplineSegments.Num() > 0)
			{
				ShowSplineProperties();
				return true;
			}
		}

		if (InKey == EKeys::R && InEvent == IE_Pressed)
		{
			if (SelectedSplineControlPoints.Num() > 0 || SelectedSplineSegments.Num() > 0)
			{
				FScopedTransaction Transaction(LOCTEXT("CyLandSpline_AutoRotate", "Auto-rotate CyLand Spline Control Points"));

				for (UCyLandSplineControlPoint* ControlPoint : SelectedSplineControlPoints)
				{
					ControlPoint->AutoCalcRotation();
					ControlPoint->UpdateSplinePoints();
				}

				for (UCyLandSplineSegment* Segment : SelectedSplineSegments)
				{
					Segment->Connections[0].ControlPoint->AutoCalcRotation();
					Segment->Connections[0].ControlPoint->UpdateSplinePoints();
					Segment->Connections[1].ControlPoint->AutoCalcRotation();
					Segment->Connections[1].ControlPoint->UpdateSplinePoints();
				}

				return true;
			}
		}

		if (InKey == EKeys::F && InEvent == IE_Pressed)
		{
			if (SelectedSplineSegments.Num() > 0)
			{
				FScopedTransaction Transaction(LOCTEXT("CyLandSpline_FlipSegments", "Flip CyLand Spline Segments"));

				for (UCyLandSplineSegment* Segment : SelectedSplineSegments)
				{
					FlipSegment(Segment);
				}

				return true;
			}
		}

		if (InKey == EKeys::T && InEvent == IE_Pressed)
		{
			if (SelectedSplineControlPoints.Num() > 0 || SelectedSplineSegments.Num() > 0)
			{
				FScopedTransaction Transaction(LOCTEXT("CyLandSpline_AutoFlipTangents", "Auto-flip CyLand Spline Tangents"));

				for (UCyLandSplineControlPoint* ControlPoint : SelectedSplineControlPoints)
				{
					ControlPoint->AutoFlipTangents();
					ControlPoint->UpdateSplinePoints();
				}

				for (UCyLandSplineSegment* Segment : SelectedSplineSegments)
				{
					Segment->Connections[0].ControlPoint->AutoFlipTangents();
					Segment->Connections[0].ControlPoint->UpdateSplinePoints();
					Segment->Connections[1].ControlPoint->AutoFlipTangents();
					Segment->Connections[1].ControlPoint->UpdateSplinePoints();
				}

				return true;
			}
		}

		if (InKey == EKeys::End && InEvent == IE_Pressed)
		{
			if (SelectedSplineControlPoints.Num() > 0 || SelectedSplineSegments.Num() > 0)
			{
				FScopedTransaction Transaction(LOCTEXT("CyLandSpline_SnapToGround", "Snap CyLand Spline to Ground"));

				for (UCyLandSplineControlPoint* ControlPoint : SelectedSplineControlPoints)
				{
					SnapControlPointToGround(ControlPoint);
				}
				for (UCyLandSplineSegment* Segment : SelectedSplineSegments)
				{
					SnapControlPointToGround(Segment->Connections[0].ControlPoint);
					SnapControlPointToGround(Segment->Connections[1].ControlPoint);
				}
				UpdatePropertiesWindows();

				GUnrealEd->RedrawLevelEditingViewports();
				return true;
			}
		}

		if (InKey == EKeys::A && InEvent == IE_Pressed
			&& IsCtrlDown(InViewport))
		{
			if (SelectedSplineControlPoints.Num() > 0 || SelectedSplineSegments.Num() > 0)
			{
				SelectConnected();

				UpdatePropertiesWindows();

				GUnrealEd->RedrawLevelEditingViewports();
				return true;
			}
		}

		if (SelectedSplineControlPoints.Num() > 0)
		{
			if (InKey == EKeys::LeftMouseButton && InEvent == IE_Pressed
				&& IsCtrlDown(InViewport))
			{
				int32 HitX = InViewport->GetMouseX();
				int32 HitY = InViewport->GetMouseY();
				HHitProxy* HitProxy = InViewport->GetHitProxy(HitX, HitY);
				if (HitProxy != NULL)
				{
					UCyLandSplineControlPoint* ClickedControlPoint = NULL;

					if (HitProxy->IsA(HCyLandSplineProxy_ControlPoint::StaticGetType()))
					{
						HCyLandSplineProxy_ControlPoint* SplineProxy = (HCyLandSplineProxy_ControlPoint*)HitProxy;
						ClickedControlPoint = SplineProxy->ControlPoint;
					}
					else if (HitProxy->IsA(HActor::StaticGetType()))
					{
						HActor* ActorProxy = (HActor*)HitProxy;
						AActor* Actor = ActorProxy->Actor;
						const UMeshComponent* MeshComponent = Cast<UMeshComponent>(ActorProxy->PrimComponent);
						if (MeshComponent)
						{
							UCyLandSplinesComponent* SplineComponent = Actor->FindComponentByClass<UCyLandSplinesComponent>();
							if (SplineComponent)
							{
								UObject* ComponentOwner = SplineComponent->GetOwnerForMeshComponent(MeshComponent);
								if (ComponentOwner)
								{
									if (UCyLandSplineControlPoint* ControlPoint = Cast<UCyLandSplineControlPoint>(ComponentOwner))
									{
										ClickedControlPoint = ControlPoint;
									}
								}
							}
						}
					}

					if (ClickedControlPoint != NULL)
					{
						FScopedTransaction Transaction(LOCTEXT("CyLandSpline_AddSegment", "Add CyLand Spline Segment"));

						for (UCyLandSplineControlPoint* ControlPoint : SelectedSplineControlPoints)
						{
							AddSegment(ControlPoint, ClickedControlPoint, bAutoRotateOnJoin, bAutoRotateOnJoin);
						}

						GUnrealEd->RedrawLevelEditingViewports();

						return true;
					}
				}
			}
		}

		if (SelectedSplineControlPoints.Num() == 0)
		{
			if (InKey == EKeys::LeftMouseButton && InEvent == IE_Pressed
				&& IsCtrlDown(InViewport))
			{
				int32 HitX = InViewport->GetMouseX();
				int32 HitY = InViewport->GetMouseY();
				HHitProxy* HitProxy = InViewport->GetHitProxy(HitX, HitY);
				if (HitProxy)
				{
					UCyLandSplineSegment* ClickedSplineSegment = NULL;
					FTransform CyLandToSpline;

					if (HitProxy->IsA(HCyLandSplineProxy_Segment::StaticGetType()))
					{
						HCyLandSplineProxy_Segment* SplineProxy = (HCyLandSplineProxy_Segment*)HitProxy;
						ClickedSplineSegment = SplineProxy->SplineSegment;
						ACyLandProxy* CyLandProxy = ClickedSplineSegment->GetTypedOuter<ACyLandProxy>();
						check(CyLandProxy);
						CyLandToSpline = CyLandProxy->CyLandActorToWorld().GetRelativeTransform(ClickedSplineSegment->GetOuterUCyLandSplinesComponent()->GetComponentTransform());
					}
					else if (HitProxy->IsA(HActor::StaticGetType()))
					{
						HActor* ActorProxy = (HActor*)HitProxy;
						AActor* Actor = ActorProxy->Actor;
						const UMeshComponent* MeshComponent = Cast<UMeshComponent>(ActorProxy->PrimComponent);
						if (MeshComponent)
						{
							UCyLandSplinesComponent* SplineComponent = Actor->FindComponentByClass<UCyLandSplinesComponent>();
							if (SplineComponent)
							{
								UObject* ComponentOwner = SplineComponent->GetOwnerForMeshComponent(MeshComponent);
								if (ComponentOwner)
								{
									if (UCyLandSplineSegment* SplineSegment = Cast<UCyLandSplineSegment>(ComponentOwner))
									{
										ClickedSplineSegment = SplineSegment;
										ACyLandProxy* CyLandProxy = CastChecked<ACyLandProxy>(SplineComponent->GetOwner());
										CyLandToSpline = CyLandProxy->CyLandActorToWorld().GetRelativeTransform(SplineComponent->GetComponentTransform());
									}
								}
							}
						}
					}

					if (ClickedSplineSegment != NULL)
					{
						FVector HitLocation;
						if (EdMode->CyLandMouseTrace(InViewportClient, HitLocation))
						{
							FScopedTransaction Transaction(LOCTEXT("CyLandSpline_SplitSegment", "Split CyLand Spline Segment"));

							SplitSegment(ClickedSplineSegment, CyLandToSpline.TransformPosition(HitLocation));

							GUnrealEd->RedrawLevelEditingViewports();
						}

						return true;
					}
				}
			}
		}

		if (InKey == EKeys::LeftMouseButton)
		{
			// Press mouse button
			if (InEvent == IE_Pressed)
			{
				// See if we clicked on a spline handle..
				int32 HitX = InViewport->GetMouseX();
				int32 HitY = InViewport->GetMouseY();
				HHitProxy*	HitProxy = InViewport->GetHitProxy(HitX, HitY);
				if (HitProxy)
				{
					if (HitProxy->IsA(HWidgetAxis::StaticGetType()))
					{
						checkSlow(SelectedSplineControlPoints.Num() > 0);
						bMovingControlPoint = true;

						GEditor->BeginTransaction(LOCTEXT("CyLandSpline_ModifyControlPoint", "Modify CyLand Spline Control Point"));
						for (UCyLandSplineControlPoint* ControlPoint : SelectedSplineControlPoints)
						{
							ControlPoint->Modify();
							ControlPoint->GetOuterUCyLandSplinesComponent()->Modify();
						}

						return false; // We're not actually handling this case ourselves, just wrapping it in a transaction
					}
					else if (HitProxy->IsA(HCyLandSplineProxy_Tangent::StaticGetType()))
					{
						HCyLandSplineProxy_Tangent* SplineProxy = (HCyLandSplineProxy_Tangent*)HitProxy;
						DraggingTangent_Segment = SplineProxy->SplineSegment;
						DraggingTangent_End = SplineProxy->End;

						GEditor->BeginTransaction(LOCTEXT("CyLandSpline_ModifyTangent", "Modify CyLand Spline Tangent"));
						UCyLandSplinesComponent* SplinesComponent = DraggingTangent_Segment->GetOuterUCyLandSplinesComponent();
						SplinesComponent->Modify();
						DraggingTangent_Segment->Modify();

						return false; // false to let FEditorViewportClient.InputKey start mouse tracking and enable InputDelta() so we can use it
					}
				}
			}
			else if (InEvent == IE_Released)
			{
				if (bMovingControlPoint)
				{
					bMovingControlPoint = false;

					for (UCyLandSplineControlPoint* ControlPoint : SelectedSplineControlPoints)
					{
						ControlPoint->UpdateSplinePoints(true);
					}

					GEditor->EndTransaction();

					return false; // We're not actually handling this case ourselves, just wrapping it in a transaction
				}
				else if (DraggingTangent_Segment)
				{
					DraggingTangent_Segment->UpdateSplinePoints(true);

					DraggingTangent_Segment = NULL;

					GEditor->EndTransaction();

					return false; // false to let FEditorViewportClient.InputKey end mouse tracking
				}
			}
		}

		return false;
	}

	virtual bool InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale) override
	{
		FVector Drag = InDrag;

		if (DraggingTangent_Segment)
		{
			const UCyLandSplinesComponent* SplinesComponent = DraggingTangent_Segment->GetOuterUCyLandSplinesComponent();
			FCyLandSplineSegmentConnection& Connection = DraggingTangent_Segment->Connections[DraggingTangent_End];

			FVector StartLocation; FRotator StartRotation;
			Connection.ControlPoint->GetConnectionLocationAndRotation(Connection.SocketName, StartLocation, StartRotation);

			float OldTangentLen = Connection.TangentLen;
			Connection.TangentLen += SplinesComponent->GetComponentTransform().InverseTransformVector(-Drag) | StartRotation.Vector();

			// Disallow a tangent of exactly 0
			if (Connection.TangentLen == 0)
			{
				if (OldTangentLen > 0)
				{
					Connection.TangentLen = SMALL_NUMBER;
				}
				else
				{
					Connection.TangentLen = -SMALL_NUMBER;
				}
			}

			// Flipping the tangent is only allowed if not using a socket
			if (Connection.SocketName != NAME_None)
			{
				Connection.TangentLen = FMath::Max(SMALL_NUMBER, Connection.TangentLen);
			}

			DraggingTangent_Segment->UpdateSplinePoints(false);

			return true;
		}

		if (SelectedSplineControlPoints.Num() > 0 && InViewportClient->GetCurrentWidgetAxis() != EAxisList::None)
		{
			for (UCyLandSplineControlPoint* ControlPoint : SelectedSplineControlPoints)
			{
				const UCyLandSplinesComponent* SplinesComponent = ControlPoint->GetOuterUCyLandSplinesComponent();

				ControlPoint->Location += SplinesComponent->GetComponentTransform().InverseTransformVector(Drag);

				FVector RotAxis; float RotAngle;
				InRot.Quaternion().ToAxisAndAngle(RotAxis, RotAngle);
				RotAxis = (SplinesComponent->GetComponentTransform().GetRotation().Inverse() * ControlPoint->Rotation.Quaternion().Inverse()).RotateVector(RotAxis);

				// Hack: for some reason FQuat.Rotator() Clamps to 0-360 range, so use .GetNormalized() to recover the original negative rotation.
				ControlPoint->Rotation += FQuat(RotAxis, RotAngle).Rotator().GetNormalized();

				ControlPoint->Rotation.Yaw = FRotator::NormalizeAxis(ControlPoint->Rotation.Yaw);
				ControlPoint->Rotation.Pitch = FMath::Clamp(ControlPoint->Rotation.Pitch, -85.0f, 85.0f);
				ControlPoint->Rotation.Roll = FMath::Clamp(ControlPoint->Rotation.Roll, -85.0f, 85.0f);

				if (bAutoChangeConnectionsOnMove)
				{
					ControlPoint->AutoSetConnections(true);
				}

				ControlPoint->UpdateSplinePoints(false);
			}

			return true;
		}

		return false;
	}

	void FixSelection()
	{
		SelectedSplineControlPoints.Empty();
		SelectedSplineSegments.Empty();

		if (EdMode->CurrentTool != nullptr && EdMode->CurrentTool == this)
		{
			for (const FCyLandListInfo& Info : EdMode->GetCyLandList())
			{
				Info.Info->ForAllCyLandProxies([this](ACyLandProxy* Proxy)
				{
					if (Proxy->SplineComponent)
					{
						Algo::CopyIf(Proxy->SplineComponent->ControlPoints, SelectedSplineControlPoints, &UCyLandSplineControlPoint::IsSplineSelected);
						Algo::CopyIf(Proxy->SplineComponent->Segments,      SelectedSplineSegments,      &UCyLandSplineSegment::IsSplineSelected);
					}
				});
			}
		}
		else
		{
			for (const FCyLandListInfo& Info : EdMode->GetCyLandList())
			{
				Info.Info->ForAllCyLandProxies([](ACyLandProxy* Proxy)
				{
					if (Proxy->SplineComponent)
					{
						for (UCyLandSplineControlPoint* ControlPoint : Proxy->SplineComponent->ControlPoints)
						{
							ControlPoint->SetSplineSelected(false);
						}

						for (UCyLandSplineSegment* Segment : Proxy->SplineComponent->Segments)
						{
							Segment->SetSplineSelected(false);
						}
					}
				});
			}
		}
	}

	void OnUndo()
	{
		FixSelection();
		UpdatePropertiesWindows();
	}

	virtual void EnterTool() override
	{
		GEditor->SelectNone(true, true, false);

		for (const FCyLandListInfo& Info : EdMode->GetCyLandList())
		{
			Info.Info->ForAllCyLandProxies([](ACyLandProxy* Proxy)
			{
				if (Proxy->SplineComponent)
				{
					Proxy->SplineComponent->ShowSplineEditorMesh(true);
				}
			});
		}
	}

	virtual void ExitTool() override
	{
		ClearSelection();
		UpdatePropertiesWindows();

		for (const FCyLandListInfo& Info : EdMode->GetCyLandList())
		{
			Info.Info->ForAllCyLandProxies([](ACyLandProxy* Proxy)
			{
				if (Proxy->SplineComponent)
				{
					Proxy->SplineComponent->ShowSplineEditorMesh(false);
				}
			});
		}
	}

	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override
	{
		// The editor can try to render the tool before the UpdateCyLandEditorData command runs and the CyLand editor realizes that the CyLand has been hidden/deleted
		const ACyLandProxy* CyLandProxy = EdMode->CurrentToolTarget.CyLandInfo->GetCyLandProxy();
		if (CyLandProxy)
		{
			for (UCyLandSplineControlPoint* ControlPoint : SelectedSplineControlPoints)
			{
				const UCyLandSplinesComponent* SplinesComponent = ControlPoint->GetOuterUCyLandSplinesComponent();

				FVector HandlePos0 = SplinesComponent->GetComponentTransform().TransformPosition(ControlPoint->Location + ControlPoint->Rotation.Vector() * -20);
				FVector HandlePos1 = SplinesComponent->GetComponentTransform().TransformPosition(ControlPoint->Location + ControlPoint->Rotation.Vector() * 20);
				DrawDashedLine(PDI, HandlePos0, HandlePos1, FColor::White, 20, SDPG_Foreground);

				if (GLevelEditorModeTools().GetWidgetMode() == FWidget::WM_Scale)
				{
					for (const FCyLandSplineConnection& Connection : ControlPoint->ConnectedSegments)
					{
						FVector StartLocation; FRotator StartRotation;
						Connection.GetNearConnection().ControlPoint->GetConnectionLocationAndRotation(Connection.GetNearConnection().SocketName, StartLocation, StartRotation);

						FVector StartPos = SplinesComponent->GetComponentTransform().TransformPosition(StartLocation);
						FVector HandlePos = SplinesComponent->GetComponentTransform().TransformPosition(StartLocation + StartRotation.Vector() * Connection.GetNearConnection().TangentLen / 2);
						PDI->DrawLine(StartPos, HandlePos, FColor::White, SDPG_Foreground);

						if (PDI->IsHitTesting()) PDI->SetHitProxy(new HCyLandSplineProxy_Tangent(Connection.Segment, Connection.End));
						PDI->DrawPoint(HandlePos, FColor::White, 10.0f, SDPG_Foreground);
						if (PDI->IsHitTesting()) PDI->SetHitProxy(NULL);
					}
				}
			}

			if (GLevelEditorModeTools().GetWidgetMode() == FWidget::WM_Scale)
			{
				for (UCyLandSplineSegment* Segment : SelectedSplineSegments)
				{
					const UCyLandSplinesComponent* SplinesComponent = Segment->GetOuterUCyLandSplinesComponent();
					for (int32 End = 0; End <= 1; End++)
					{
						const FCyLandSplineSegmentConnection& Connection = Segment->Connections[End];

						FVector StartLocation; FRotator StartRotation;
						Connection.ControlPoint->GetConnectionLocationAndRotation(Connection.SocketName, StartLocation, StartRotation);

						FVector EndPos = SplinesComponent->GetComponentTransform().TransformPosition(StartLocation);
						FVector EndHandlePos = SplinesComponent->GetComponentTransform().TransformPosition(StartLocation + StartRotation.Vector() * Connection.TangentLen / 2);

						PDI->DrawLine(EndPos, EndHandlePos, FColor::White, SDPG_Foreground);
						if (PDI->IsHitTesting()) PDI->SetHitProxy(new HCyLandSplineProxy_Tangent(Segment, !!End));
						PDI->DrawPoint(EndHandlePos, FColor::White, 10.0f, SDPG_Foreground);
						if (PDI->IsHitTesting()) PDI->SetHitProxy(NULL);
					}
				}
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
		if (SelectedSplineControlPoints.Num() > 0)
		{
			// The editor can try to render the transform widget before the CyLand editor ticks and realizes that the CyLand has been hidden/deleted
			const ACyLandProxy* CyLandProxy = EdMode->CurrentToolTarget.CyLandInfo->GetCyLandProxy();
			if (CyLandProxy)
			{
				return true;
			}
		}

		return false;
	}

	virtual EAxisList::Type GetWidgetAxisToDraw(FWidget::EWidgetMode CheckMode) const override
	{
		if (SelectedSplineControlPoints.Num() > 0)
		{
			//if (CheckMode == FWidget::WM_Rotate
			//	&& SelectedSplineControlPoints.Num() >= 2)
			//{
			//	return AXIS_X;
			//}
			//else
			if (CheckMode != FWidget::WM_Scale)
			{
				return EAxisList::XYZ;
			}
			else
			{
				return EAxisList::None;
			}
		}

		return EAxisList::None;
	}

	virtual FVector GetWidgetLocation() const override
	{
		if (SelectedSplineControlPoints.Num() > 0)
		{
			const ACyLandProxy* CyLandProxy = EdMode->CurrentToolTarget.CyLandInfo->GetCyLandProxy();
			if (CyLandProxy)
			{
				UCyLandSplineControlPoint* FirstPoint = *SelectedSplineControlPoints.CreateConstIterator();
				UCyLandSplinesComponent* SplinesComponent = FirstPoint->GetOuterUCyLandSplinesComponent();
				return SplinesComponent->GetComponentTransform().TransformPosition(FirstPoint->Location);
			}
		}

		return FVector::ZeroVector;
	}

	virtual FMatrix GetWidgetRotation() const override
	{
		if (SelectedSplineControlPoints.Num() > 0)
		{
			const ACyLandProxy* CyLandProxy = EdMode->CurrentToolTarget.CyLandInfo->GetCyLandProxy();
			if (CyLandProxy)
			{
				UCyLandSplineControlPoint* FirstPoint = *SelectedSplineControlPoints.CreateConstIterator();
				UCyLandSplinesComponent* SplinesComponent = FirstPoint->GetOuterUCyLandSplinesComponent();
				return FQuatRotationTranslationMatrix(FirstPoint->Rotation.Quaternion() * SplinesComponent->GetComponentTransform().GetRotation(), FVector::ZeroVector);
			}
		}

		return FMatrix::Identity;
	}

	virtual EEditAction::Type GetActionEditDuplicate() override
	{
		if (SelectedSplineControlPoints.Num() > 0 || SelectedSplineSegments.Num() > 0)
		{
			return EEditAction::Process;
		}

		return EEditAction::Skip;
	}

	virtual EEditAction::Type GetActionEditDelete() override
	{
		if (SelectedSplineControlPoints.Num() > 0 || SelectedSplineSegments.Num() > 0)
		{
			return EEditAction::Process;
		}

		return EEditAction::Skip;
	}

	virtual EEditAction::Type GetActionEditCut() override
	{
		if (SelectedSplineControlPoints.Num() > 0 || SelectedSplineSegments.Num() > 0)
		{
			return EEditAction::Process;
		}

		return EEditAction::Skip;
	}

	virtual EEditAction::Type GetActionEditCopy() override
	{
		if (SelectedSplineControlPoints.Num() > 0 || SelectedSplineSegments.Num() > 0)
		{
			return EEditAction::Process;
		}

		return EEditAction::Skip;
	}

	virtual EEditAction::Type GetActionEditPaste() override
	{
		FString PasteString;
		FPlatformApplicationMisc::ClipboardPaste(PasteString);
		if (PasteString.StartsWith("BEGIN SPLINES"))
		{
			return EEditAction::Process;
		}

		return EEditAction::Skip;
	}

	virtual bool ProcessEditDuplicate() override
	{
		InternalProcessEditDuplicate();
		return true;
	}

	virtual bool ProcessEditDelete() override
	{
		InternalProcessEditDelete();
		return true;
	}

	virtual bool ProcessEditCut() override
	{
		InternalProcessEditCut();
		return true;
	}

	virtual bool ProcessEditCopy() override
	{
		InternalProcessEditCopy();
		return true;
	}

	virtual bool ProcessEditPaste() override
	{
		InternalProcessEditPaste();
		return true;
	}

	void InternalProcessEditDuplicate()
	{
		if (SelectedSplineControlPoints.Num() > 0 || SelectedSplineSegments.Num() > 0)
		{
			FScopedTransaction Transaction(LOCTEXT("CyLandSpline_Duplicate", "Duplicate CyLand Splines"));

			FString Data;
			InternalProcessEditCopy(&Data);
			InternalProcessEditPaste(&Data, true);
		}
	}

	void InternalProcessEditDelete()
	{
		if (SelectedSplineControlPoints.Num() > 0 || SelectedSplineSegments.Num() > 0)
		{
			FScopedTransaction Transaction(LOCTEXT("CyLandSpline_Delete", "Delete CyLand Splines"));

			for (UCyLandSplineControlPoint* ControlPoint : SelectedSplineControlPoints)
			{
				DeleteControlPoint(ControlPoint, bDeleteLooseEnds);
			}
			for (UCyLandSplineSegment* Segment : SelectedSplineSegments)
			{
				DeleteSegment(Segment, bDeleteLooseEnds);
			}
			ClearSelection();
			UpdatePropertiesWindows();

			GUnrealEd->RedrawLevelEditingViewports();
		}
	}

	void InternalProcessEditCut()
	{
		if (SelectedSplineControlPoints.Num() > 0 || SelectedSplineSegments.Num() > 0)
		{
			FScopedTransaction Transaction(LOCTEXT("CyLandSpline_Cut", "Cut CyLand Splines"));

			InternalProcessEditCopy();
			InternalProcessEditDelete();
		}
	}

	void InternalProcessEditCopy(FString* OutData = NULL)
	{
		if (SelectedSplineControlPoints.Num() > 0 || SelectedSplineSegments.Num() > 0)
		{
			TArray<UObject*> Objects;
			Objects.Reserve(SelectedSplineControlPoints.Num() + SelectedSplineSegments.Num() * 3); // worst case

			// Control Points then segments
			for (UCyLandSplineControlPoint* ControlPoint : SelectedSplineControlPoints)
			{
				Objects.Add(ControlPoint);
			}
			for (UCyLandSplineSegment* Segment : SelectedSplineSegments)
			{
				Objects.AddUnique(Segment->Connections[0].ControlPoint);
				Objects.AddUnique(Segment->Connections[1].ControlPoint);
			}
			for (UCyLandSplineSegment* Segment : SelectedSplineSegments)
			{
				Objects.Add(Segment);
			}

			// Perform export to text format
			FStringOutputDevice Ar;
			const FExportObjectInnerContext Context;

			Ar.Logf(TEXT("Begin Splines\r\n"));
			for (UObject* Object : Objects)
			{
				UExporter::ExportToOutputDevice(&Context, Object, NULL, Ar, TEXT("copy"), 3, PPF_ExportsNotFullyQualified | PPF_Copy | PPF_Delimited, false);
			}
			Ar.Logf(TEXT("End Splines\r\n"));

			if (OutData != NULL)
			{
				*OutData = MoveTemp(Ar);
			}
			else
			{
				FPlatformApplicationMisc::ClipboardCopy(*Ar);
			}
		}
	}

	void InternalProcessEditPaste(FString* InData = NULL, bool bOffset = false)
	{
		FScopedTransaction Transaction(LOCTEXT("CyLandSpline_Paste", "Paste CyLand Splines"));

		ACyLandProxy* CyLand = EdMode->CurrentToolTarget.CyLandInfo->GetCurrentLevelCyLandProxy(true);
		if (!CyLand)
		{
			return;
		}
		if (!CyLand->SplineComponent)
		{
			CreateSplineComponent(CyLand, FVector(1.0f) / CyLand->GetRootComponent()->RelativeScale3D);
			check(CyLand->SplineComponent);
		}
		CyLand->SplineComponent->Modify();

		const TCHAR* Data = NULL;
		FString PasteString;
		if (InData != NULL)
		{
			Data = **InData;
		}
		else
		{
			FPlatformApplicationMisc::ClipboardPaste(PasteString);
			Data = *PasteString;
		}

		FCyLandSplineTextObjectFactory Factory;
		TArray<UObject*> OutObjects = Factory.ImportSplines(CyLand->SplineComponent, Data);

		if (bOffset)
		{
			for (UObject* Object : OutObjects)
			{
				UCyLandSplineControlPoint* ControlPoint = Cast<UCyLandSplineControlPoint>(Object);
				if (ControlPoint != NULL)
				{
					CyLand->SplineComponent->ControlPoints.Add(ControlPoint);
					ControlPoint->Location += FVector(500, 500, 0);

					ControlPoint->UpdateSplinePoints();
				}
			}
		}
	}

protected:
	// Begin FEditorUndoClient
	virtual void PostUndo(bool bSuccess) override { OnUndo(); }
	virtual void PostRedo(bool bSuccess) override { PostUndo(bSuccess); }
	// End of FEditorUndoClient

protected:
	FEdModeCyLand* EdMode;
	UCyLandInfo* CyLandInfo;

	TSet<UCyLandSplineControlPoint*> SelectedSplineControlPoints;
	TSet<UCyLandSplineSegment*> SelectedSplineSegments;

	UCyLandSplineSegment* DraggingTangent_Segment;
	uint32 DraggingTangent_End : 1;

	uint32 bMovingControlPoint : 1;

	uint32 bAutoRotateOnJoin : 1;
	uint32 bAutoChangeConnectionsOnMove : 1;
	uint32 bDeleteLooseEnds : 1;
	uint32 bCopyMeshToNewControlPoint : 1;

	friend FEdModeCyLand;
};


void FEdModeCyLand::ShowSplineProperties()
{
	if (SplinesTool /*&& SplinesTool == CurrentTool*/)
	{
		SplinesTool->ShowSplineProperties();
	}
}

void FEdModeCyLand::SelectAllConnectedSplineControlPoints()
{
	if (SplinesTool /*&& SplinesTool == CurrentTool*/)
	{
		SplinesTool->SelectAdjacentControlPoints();
		SplinesTool->ClearSelectedSegments();
		SplinesTool->SelectConnected();

		SplinesTool->UpdatePropertiesWindows();
		GUnrealEd->RedrawLevelEditingViewports();
	}
}

void FEdModeCyLand::SelectAllConnectedSplineSegments()
{
	if (SplinesTool /*&& SplinesTool == CurrentTool*/)
	{
		SplinesTool->SelectAdjacentSegments();
		SplinesTool->ClearSelectedControlPoints();
		SplinesTool->SelectConnected();

		SplinesTool->UpdatePropertiesWindows();
		GUnrealEd->RedrawLevelEditingViewports();
	}
}

void FEdModeCyLand::SplineMoveToCurrentLevel()
{
	FScopedTransaction Transaction(LOCTEXT("CyLandSpline_MoveToCurrentLevel", "Move CyLand Spline to current level"));

	if (SplinesTool /*&& SplinesTool == CurrentTool*/)
	{
		// Select all connected control points
		SplinesTool->SelectAdjacentSegments();
		SplinesTool->SelectAdjacentControlPoints();
		SplinesTool->SelectConnected();

		SplinesTool->MoveSelectedToLevel();

		SplinesTool->ClearSelection();
		SplinesTool->UpdatePropertiesWindows();
	}
}

void FEdModeCyLand::SetbUseAutoRotateOnJoin(bool InbAutoRotateOnJoin)
{
	if (SplinesTool /*&& SplinesTool == CurrentTool*/)
	{
		SplinesTool->bAutoRotateOnJoin = InbAutoRotateOnJoin;
	}
}

bool FEdModeCyLand::GetbUseAutoRotateOnJoin()
{
	if (SplinesTool /*&& SplinesTool == CurrentTool*/)
	{
		return SplinesTool->bAutoRotateOnJoin;
	}
	return true; // default value
}

void FEdModeCyLand::InitializeTool_Splines()
{
	auto Tool_Splines = MakeUnique<FCyLandToolSplines>(this);
	Tool_Splines->ValidBrushes.Add("BrushSet_Splines");
	SplinesTool = Tool_Splines.Get();
	CyLandTools.Add(MoveTemp(Tool_Splines));
}

#undef LOCTEXT_NAMESPACE
