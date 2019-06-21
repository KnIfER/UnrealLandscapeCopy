// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "UnrealWidget.h"
#include "CyLandProxy.h"
#include "EdMode.h"
#include "CyLandToolInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "CyLandInfo.h"
#include "CyLandLayerInfoObject.h"
#include "CyLandGizmoActiveActor.h"

class ACyLand;
class FCanvas;
class FEditorViewportClient;
class FCyLandToolSplines;
class FPrimitiveDrawInterface;
class FSceneView;
class FUICommandList;
class FViewport;
class UCyLandComponent;
class UCyLandEditorObject;
class UViewportInteractor;
struct FHeightmapToolTarget;
struct FViewportActionKeyInput;
struct FViewportClick;
template<class ToolTarget> class FCyLandToolCopyPaste;

DECLARE_LOG_CATEGORY_EXTERN(LogCyLandEdMode, Log, All);

// Forward declarations
class UCyLandEditorObject;
class UCyLandLayerInfoObject;
class FCyLandToolSplines;
class UViewportInteractor;
struct FViewportActionKeyInput;

struct FHeightmapToolTarget;
template<typename TargetType> class FCyLandToolCopyPaste;

struct FCyLandToolMode
{
	const FName				ToolModeName;
	int32					SupportedTargetTypes; // ECyLandToolTargetTypeMask::Type

	TArray<FName>			ValidTools;
	FName					CurrentToolName;

	FCyLandToolMode(FName InToolModeName, int32 InSupportedTargetTypes)
		: ToolModeName(InToolModeName)
		, SupportedTargetTypes(InSupportedTargetTypes)
	{
	}
};

struct FCyLandTargetListInfo
{
	FText TargetName;
	ECyLandToolTargetType::Type TargetType;
	TWeakObjectPtr<UCyLandInfo> CyLandInfo;

	//Values cloned from FCyLandLayerStruct LayerStruct;			// ignored for heightmap
	TWeakObjectPtr<UCyLandLayerInfoObject> LayerInfoObj;			// ignored for heightmap
	FName LayerName;												// ignored for heightmap
	TWeakObjectPtr<class ACyLandProxy> Owner;					// ignored for heightmap
	TWeakObjectPtr<class UMaterialInstanceConstant> ThumbnailMIC;	// ignored for heightmap
	int32 DebugColorChannel;										// ignored for heightmap
	uint32 bValid : 1;												// ignored for heightmap
	int32 ProceduralLayerIndex;

	FCyLandTargetListInfo(FText InTargetName, ECyLandToolTargetType::Type InTargetType, const FCyLandInfoLayerSettings& InLayerSettings, int32 InProceduralLayerIndex)
		: TargetName(InTargetName)
		, TargetType(InTargetType)
		, CyLandInfo(InLayerSettings.Owner->GetCyLandInfo())
		, LayerInfoObj(InLayerSettings.LayerInfoObj)
		, LayerName(InLayerSettings.LayerName)
		, Owner(InLayerSettings.Owner)
		, ThumbnailMIC(InLayerSettings.ThumbnailMIC)
		, DebugColorChannel(InLayerSettings.DebugColorChannel)
		, bValid(InLayerSettings.bValid)
		, ProceduralLayerIndex (InProceduralLayerIndex)
	{
	}

	FCyLandTargetListInfo(FText InTargetName, ECyLandToolTargetType::Type InTargetType, UCyLandInfo* InCyLandInfo, int32 InProceduralLayerIndex)
		: TargetName(InTargetName)
		, TargetType(InTargetType)
		, CyLandInfo(InCyLandInfo)
		, LayerInfoObj(NULL)
		, LayerName(NAME_None)
		, Owner(NULL)
		, ThumbnailMIC(NULL)
		, bValid(true)
		, ProceduralLayerIndex(InProceduralLayerIndex)
	{
	}

	FCyLandInfoLayerSettings* GetCyLandInfoLayerSettings() const
	{
		if (TargetType == ECyLandToolTargetType::Weightmap)
		{
			int32 Index = INDEX_NONE;
			if (LayerInfoObj.IsValid())
			{
				Index = CyLandInfo->GetLayerInfoIndex(LayerInfoObj.Get(), Owner.Get());
			}
			else
			{
				Index = CyLandInfo->GetLayerInfoIndex(LayerName, Owner.Get());
			}
			if (ensure(Index != INDEX_NONE))
			{
				return &CyLandInfo->Layers[Index];
			}
		}
		return NULL;
	}

	FCyLandEditorLayerSettings* GetEditorLayerSettings() const
	{
		if (TargetType == ECyLandToolTargetType::Weightmap)
		{
			check(LayerInfoObj.IsValid());
			ACyLandProxy* Proxy = CyLandInfo->GetCyLandProxy();
			FCyLandEditorLayerSettings* EditorLayerSettings = Proxy->EditorLayerSettings.FindByKey(LayerInfoObj.Get());
			if (EditorLayerSettings)
			{
				return EditorLayerSettings;
			}
			else
			{
				int32 Index = Proxy->EditorLayerSettings.Add(FCyLandEditorLayerSettings(LayerInfoObj.Get()));
				return &Proxy->EditorLayerSettings[Index];
			}
		}
		return NULL;
	}

	FName GetLayerName() const;

	FString& ReimportFilePath() const
	{
		if (TargetType == ECyLandToolTargetType::Weightmap)
		{
			FCyLandEditorLayerSettings* EditorLayerSettings = GetEditorLayerSettings();
			check(EditorLayerSettings);
			return EditorLayerSettings->ReimportLayerFilePath;
		}
		else //if (TargetType == ECyLandToolTargetType::Heightmap)
		{
			return CyLandInfo->GetCyLandProxy()->ReimportHeightmapFilePath;
		}
	}
};

struct FCyLandListInfo
{
	FString CyLandName;
	UCyLandInfo* Info;
	int32 ComponentQuads;
	int32 NumSubsections;
	int32 Width;
	int32 Height;

	FCyLandListInfo(const TCHAR* InName, UCyLandInfo* InInfo, int32 InComponentQuads, int32 InNumSubsections, int32 InWidth, int32 InHeight)
		: CyLandName(InName)
		, Info(InInfo)
		, ComponentQuads(InComponentQuads)
		, NumSubsections(InNumSubsections)
		, Width(InWidth)
		, Height(InHeight)
	{
	}
};

struct FGizmoHistory
{
	ACyLandGizmoActor* Gizmo;
	FString GizmoName;

	FGizmoHistory(ACyLandGizmoActor* InGizmo)
		: Gizmo(InGizmo)
	{
		GizmoName = Gizmo->GetPathName();
	}

	FGizmoHistory(ACyLandGizmoActiveActor* InGizmo)
	{
		// handle for ACyLandGizmoActiveActor -> ACyLandGizmoActor
		// ACyLandGizmoActor is only for history, so it has limited data
		Gizmo = InGizmo->SpawnGizmoActor();
		GizmoName = Gizmo->GetPathName();
	}
};


namespace ECyLandEdge
{
	enum Type
	{
		None,

		// Edges
		X_Negative,
		X_Positive,
		Y_Negative,
		Y_Positive,

		// Corners
		X_Negative_Y_Negative,
		X_Positive_Y_Negative,
		X_Negative_Y_Positive,
		X_Positive_Y_Positive,
	};
}

namespace ENewCyLandPreviewMode
{
	enum Type
	{
		None,
		NewCyLand,
		ImportCyLand,
	};
}

enum class ECyLandEditingState : uint8
{
	Unknown,
	Enabled,
	BadFeatureLevel,
	PIEWorld,
	SIEWorld,
	NoCyLand,
};

/**
 * CyLand editor mode
 */
class FEdModeCyLand : public FEdMode
{
public:

	UCyLandEditorObject* UISettings;

	FCyLandToolMode* CurrentToolMode;
	FCyLandTool* CurrentTool;
	FCyLandBrush* CurrentBrush;
	FCyLandToolTarget CurrentToolTarget;

	// GizmoBrush for Tick
	FCyLandBrush* GizmoBrush;
	// UI setting for additional UI Tools
	int32 CurrentToolIndex;
	// UI setting for additional UI Tools
	int32 CurrentBrushSetIndex;

	ENewCyLandPreviewMode::Type NewCyLandPreviewMode;
	ECyLandEdge::Type DraggingEdge;
	float DraggingEdge_Remainder;

	TWeakObjectPtr<ACyLandGizmoActiveActor> CurrentGizmoActor;
	// UI callbacks for copy/paste tool
	FCyLandToolCopyPaste<FHeightmapToolTarget>* CopyPasteTool;
	void CopyDataToGizmo();
	void PasteDataFromGizmo();

	// UI callbacks for splines tool
	FCyLandToolSplines* SplinesTool;
	void ShowSplineProperties();
	virtual void SelectAllConnectedSplineControlPoints();
	virtual void SelectAllConnectedSplineSegments();
	virtual void SplineMoveToCurrentLevel();
	void SetbUseAutoRotateOnJoin(bool InbAutoRotateOnJoin);
	bool GetbUseAutoRotateOnJoin();

	// UI callbacks for ramp tool
	void ApplyRampTool();
	bool CanApplyRampTool();
	void ResetRampTool();

	// UI callbacks for mirror tool
	void ApplyMirrorTool();
	void CenterMirrorTool();

	/** Constructor */
	FEdModeCyLand();

	/** Initialization */
	void InitializeBrushes();
	void InitializeTool_Paint();
	void InitializeTool_Smooth();
	void InitializeTool_Flatten();
	void InitializeTool_Erosion();
	void InitializeTool_HydraErosion();
	void InitializeTool_Noise();
	void InitializeTool_Retopologize();
	void InitializeTool_NewCyLand();
	void InitializeTool_ResizeCyLand();
	void InitializeTool_Select();
	void InitializeTool_AddComponent();
	void InitializeTool_DeleteComponent();
	void InitializeTool_MoveToLevel();
	void InitializeTool_Mask();
	void InitializeTool_CopyPaste();
	void InitializeTool_Visibility();
	void InitializeTool_Splines();
	void InitializeTool_Ramp();
	void InitializeTool_Mirror();
	void InitializeTool_BPCustom();
	void InitializeToolModes();

	/** Destructor */
	virtual ~FEdModeCyLand();

	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	virtual bool UsesToolkits() const override;

	TSharedRef<FUICommandList> GetUICommandList() const;

	/** FEdMode: Called when the mode is entered */
	virtual void Enter() override;

	/** FEdMode: Called when the mode is exited */
	virtual void Exit() override;

	/** FEdMode: Called when the mouse is moved over the viewport */
	virtual bool MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y) override;

	/**
	 * FEdMode: Called when the mouse is moved while a window input capture is in effect
	 *
	 * @param	InViewportClient	Level editor viewport client that captured the mouse input
	 * @param	InViewport			Viewport that captured the mouse input
	 * @param	InMouseX			New mouse cursor X coordinate
	 * @param	InMouseY			New mouse cursor Y coordinate
	 *
	 * @return	true if input was handled
	 */
	virtual bool CapturedMouseMove(FEditorViewportClient* InViewportClient, FViewport* InViewport, int32 InMouseX, int32 InMouseY) override;

	/** FEdMode: Called when a mouse button is pressed */
	virtual bool StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;

	/** FEdMode: Called when a mouse button is released */
	virtual bool EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;

	/** FEdMode: Allow us to disable mouse delta tracking during painting */
	virtual bool DisallowMouseDeltaTracking() const override;

	/** FEdMode: Called once per frame */
	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override;

	/** FEdMode: Called when clicking on a hit proxy */
	virtual bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click) override;

	/** True if we are interactively changing the brush size, falloff, or strength */
	bool IsAdjustingBrush(FViewport* InViewport) const;
	void ChangeBrushSize(bool bIncrease);
	void ChangeBrushFalloff(bool bIncrease);
	void ChangeBrushStrength(bool bIncrease);

	/** FEdMode: Called when a key is pressed */
	virtual bool InputKey(FEditorViewportClient* InViewportClient, FViewport* InViewport, FKey InKey, EInputEvent InEvent) override;

	/** FEdMode: Called when mouse drag input is applied */
	virtual bool InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale) override;

	/** FEdMode: Render elements for the CyLand tool */
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;

	/** FEdMode: Render HUD elements for this tool */
	virtual void DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas) override;

	/** FEdMode: Handling SelectActor */
	virtual bool Select(AActor* InActor, bool bInSelected) override;

	/** FEdMode: Check to see if an actor can be selected in this mode - no side effects */
	virtual bool IsSelectionAllowed(AActor* InActor, bool bInSelection) const override;

	/** FEdMode: Called when the currently selected actor has changed */
	virtual void ActorSelectionChangeNotify() override;

	virtual void ActorMoveNotify() override;

	virtual void PostUndo() override;

	virtual EEditAction::Type GetActionEditDuplicate() override;
	virtual EEditAction::Type GetActionEditDelete() override;
	virtual EEditAction::Type GetActionEditCut() override;
	virtual EEditAction::Type GetActionEditCopy() override;
	virtual EEditAction::Type GetActionEditPaste() override;
	virtual bool ProcessEditDuplicate() override;
	virtual bool ProcessEditDelete() override;
	virtual bool ProcessEditCut() override;
	virtual bool ProcessEditCopy() override;
	virtual bool ProcessEditPaste() override;

	/** FEdMode: If the EdMode is handling InputDelta (ie returning true from it), this allows a mode to indicated whether or not the Widget should also move. */
	virtual bool AllowWidgetMove() override { return true; }

	/** FEdMode: Draw the transform widget while in this mode? */
	virtual bool ShouldDrawWidget() const override;

	/** FEdMode: Returns true if this mode uses the transform widget */
	virtual bool UsesTransformWidget() const override;

	virtual EAxisList::Type GetWidgetAxisToDraw(FWidget::EWidgetMode InWidgetMode) const override;

	virtual FVector GetWidgetLocation() const override;
	virtual bool GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData) override;
	virtual bool GetCustomInputCoordinateSystem(FMatrix& InMatrix, void* InData) override;

	virtual bool GetCursor(EMouseCursor::Type& OutCursor) const override;

	/** Forces real-time perspective viewports */
	void ForceRealTimeViewports(const bool bEnable, const bool bStoreCurrentState);

	/** Trace under the mouse cursor and return the CyLand hit and the hit location (in CyLand quad space) */
	bool CyLandMouseTrace(FEditorViewportClient* ViewportClient, float& OutHitX, float& OutHitY);
	bool CyLandMouseTrace(FEditorViewportClient* ViewportClient, FVector& OutHitLocation);

	/** Trace under the specified coordinates and return the CyLand hit and the hit location (in CyLand quad space) */
	bool CyLandMouseTrace(FEditorViewportClient* ViewportClient, int32 MouseX, int32 MouseY, float& OutHitX, float& OutHitY);
	bool CyLandMouseTrace(FEditorViewportClient* ViewportClient, int32 MouseX, int32 MouseY, FVector& OutHitLocation);

	/** Trace under the mouse cursor / specified screen coordinates against a world-space plane and return the hit location (in world space) */
	bool CyLandPlaneTrace(FEditorViewportClient* ViewportClient, const FPlane& Plane, FVector& OutHitLocation);
	bool CyLandPlaneTrace(FEditorViewportClient* ViewportClient, int32 MouseX, int32 MouseY, const FPlane& Plane, FVector& OutHitLocation);

	/** Trace under the specified laser start and direction and return the CyLand hit and the hit location (in CyLand quad space) */
	bool CyLandTrace(const FVector& InRayOrigin, const FVector& InRayEnd, FVector& OutHitLocation);

	void SetCurrentToolMode(FName ToolModeName, bool bRestoreCurrentTool = true);

	/** Change current tool */
	void SetCurrentTool(FName ToolName);
	void SetCurrentTool(int32 ToolIdx);

	void SetCurrentBrushSet(FName BrushSetName);
	void SetCurrentBrushSet(int32 BrushSetIndex);

	void SetCurrentBrush(FName BrushName);
	void SetCurrentBrush(int32 BrushIndex);

	const TArray<TSharedRef<FCyLandTargetListInfo>>& GetTargetList() const;
	const TArray<FName>* GetTargetDisplayOrderList() const;
	const TArray<FName>& GetTargetShownList() const;
	int32 GetTargetLayerStartingIndex() const;
	const TArray<FCyLandListInfo>& GetCyLandList();

	void AddLayerInfo(UCyLandLayerInfoObject* LayerInfo);

	int32 UpdateCyLandList();
	void UpdateTargetList();
	
	/** Update Display order list */
	void UpdateTargetLayerDisplayOrder(ECyLandLayerDisplayMode InTargetDisplayOrder);
	void MoveTargetLayerDisplayOrder(int32 IndexToMove, int32 IndexToDestination);

	/** Update shown layer list */	
	void UpdateShownLayerList();
	bool ShouldShowLayer(TSharedRef<FCyLandTargetListInfo> Target) const;
	void UpdateLayerUsageInformation(TWeakObjectPtr<UCyLandLayerInfoObject>* LayerInfoObjectThatChanged = nullptr);
	void OnCyLandMaterialChangedDelegate();
	void RefreshDetailPanel();

	// Procedural Layers
	int32 GetProceduralLayerCount() const;
	void SetCurrentProceduralLayer(int32 InLayerIndex);
	int32 GetCurrentProceduralLayerIndex() const;
	FName GetCurrentProceduralLayerName() const;
	FName GetProceduralLayerName(int32 InLayerIndex) const;
	void SetProceduralLayerName(int32 InLayerIndex, const FName& InName);
	float GetProceduralLayerWeight(int32 InLayerIndex) const;
	void SetProceduralLayerWeight(float InWeight, int32 InLayerIndex);
	void SetProceduralLayerVisibility(bool InVisible, int32 InLayerIndex);
	bool IsProceduralLayerVisible(int32 InLayerIndex) const;
	void AddBrushToCurrentProceduralLayer(int32 InTargetType, class ACyLandBlueprintCustomBrush* InBrush);
	void RemoveBrushFromCurrentProceduralLayer(int32 InTargetType, class ACyLandBlueprintCustomBrush* InBrush);
	bool AreAllBrushesCommitedToCurrentProceduralLayer(int32 InTargetType);
	void SetCurrentProceduralLayerBrushesCommitState(int32 InTargetType, bool InCommited);
	TArray<int8>& GetBrushesOrderForCurrentProceduralLayer(int32 InTargetType) const;
	class ACyLandBlueprintCustomBrush* GetBrushForCurrentProceduralLayer(int32 InTargetType, int8 BrushIndex) const;
	TArray<class ACyLandBlueprintCustomBrush*> GetBrushesForCurrentProceduralLayer(int32 InTargetType);
	struct FCyProceduralLayer* GetCurrentProceduralLayer() const;
	void ChangeHeightmapsToCurrentProceduralLayerHeightmaps(bool InResetCurrentEditingHeightmap = false);
	void RequestProceduralContentUpdate();

	void OnLevelActorAdded(AActor* InActor);
	void OnLevelActorRemoved(AActor* InActor);

	DECLARE_EVENT(FEdModeCyLand, FTargetsListUpdated);
	static FTargetsListUpdated TargetsListUpdated;

	/** Called when the user presses a button on their motion controller device */
	void OnVRAction(FEditorViewportClient& ViewportClient, UViewportInteractor* Interactor, const FViewportActionKeyInput& Action, bool& bOutIsInputCaptured, bool& bWasHandled);

	void OnVRHoverUpdate(UViewportInteractor* Interactor, FVector& HoverImpactPoint, bool& bWasHandled);

	/** Handle notification that visible levels may have changed and we should update the editable landscapes list */
	void HandleLevelsChanged(bool ShouldExitMode);

	void OnMaterialCompilationFinished(UMaterialInterface* MaterialInterface);

	void ReimportData(const FCyLandTargetListInfo& TargetInfo);
	void ImportData(const FCyLandTargetListInfo& TargetInfo, const FString& Filename);

	/** Resample CyLand to a different resolution or change the component size */
	ACyLand* ChangeComponentSetting(int32 NumComponentsX, int32 NumComponentsY, int32 InNumSubsections, int32 InSubsectionSizeQuads, bool bResample);

	/** Delete the specified CyLand components */
	void DeleteCyLandComponents(UCyLandInfo* CyLandInfo, TSet<UCyLandComponent*> ComponentsToDelete);

	TArray<FCyLandToolMode> CyLandToolModes;
	TArray<TUniquePtr<FCyLandTool>> CyLandTools;
	TArray<FCyLandBrushSet> CyLandBrushSets;

	// For collision add visualization
	FCyLandAddCollision* CyLandRenderAddCollision;

	ECyLandEditingState GetEditingState() const;

	bool IsEditingEnabled() const
	{
		return GetEditingState() == ECyLandEditingState::Enabled;
	}
	
private:
	TArray<TSharedRef<FCyLandTargetListInfo>> CyLandTargetList;
	TArray<FCyLandListInfo> CyLandList;
	TArray<FName> ShownTargetLayerList;
	
	/** Represent the index offset of the target layer in CyLandTargetList */
	int32 TargetLayerStartingIndex;

	UMaterialInterface* CachedCyLandMaterial;

	const FViewport* ToolActiveViewport;

	FDelegateHandle OnWorldChangeDelegateHandle;
	FDelegateHandle OnLevelsChangedDelegateHandle;
	FDelegateHandle OnMaterialCompilationFinishedDelegateHandle;

	FDelegateHandle OnLevelActorDeletedDelegateHandle;
	FDelegateHandle OnLevelActorAddedDelegateHandle;

	/** Check if we are painting using the VREditor */
	bool bIsPaintingInVR;

	/** The interactor that is currently painting, prevents multiple interactors from sculpting when one actually is */
	class UViewportInteractor* InteractorPainting;
};
