// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/Paths.h"
#include "Misc/AutomationTest.h"
#include "Misc/EngineVersion.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "LevelEditorViewport.h"
#include "EditorModeManager.h"
#include "CyLandEditorModule.h"
#include "CyLandEditorObject.h"

#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"

#include "Private/CyLandEditorDetailCustomization_NewCyLand.h"

#if WITH_DEV_AUTOMATION_TESTS

#define LOCTEXT_NAMESPACE "CyLandEditor.NewCyLand"

DEFINE_LOG_CATEGORY_STATIC(LogCyLandAutomationTests, Log, All);

/**
* CyLand test helper functions
*/
namespace CyLandTestUtils
{
	/**
	* Finds the viewport to use for the CyLand tool
	*/
	static FLevelEditorViewportClient* FindSelectedViewport()
	{
		FLevelEditorViewportClient* SelectedViewport = NULL;

		for(FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
		{
			if (!ViewportClient->IsOrtho())
			{
				SelectedViewport = ViewportClient;
			}
		}

		return SelectedViewport;
	}
}

/**
* Latent command to create a new CyLand
*/
DEFINE_LATENT_AUTOMATION_COMMAND(FCreateCyLandCommand);
bool FCreateCyLandCommand::Update()
{
	//Switch to the CyLand tool
	GLevelEditorModeTools().ActivateMode(ICyLandEditorModule::EM_Landscape_Mimic);
	FEdModeCyLand* CyLandEdMode = (FEdModeCyLand*)GLevelEditorModeTools().GetActiveMode(ICyLandEditorModule::EM_Landscape_Mimic);

	//Modify the "Section size"
	CyLandEdMode->UISettings->NewCyLand_QuadsPerSection = 7;
	CyLandEdMode->UISettings->NewCyLand_ClampSize();

	//Create the CyLand
	TSharedPtr<FCyLandEditorDetailCustomization_NewCyLand> Customization_NewCyLand = MakeShareable(new FCyLandEditorDetailCustomization_NewCyLand);
	Customization_NewCyLand->OnCreateButtonClicked();

	if (CyLandEdMode->CurrentToolTarget.CyLandInfo.IsValid())
	{
		UE_LOG(LogCyLandAutomationTests, Display, TEXT("Created a new CyLand"));
	}
	else
	{
		UE_LOG(LogCyLandAutomationTests, Error, TEXT("Failed to create a new CyLand"));
	}

	return true;
}

/**
* Latent command to start using the sculpting tool
*/
DEFINE_LATENT_AUTOMATION_COMMAND(FBeginModifyCyLandCommand);
bool FBeginModifyCyLandCommand::Update()
{
	//Find the CyLand
	FEdModeCyLand* CyLandEdMode = (FEdModeCyLand*)GLevelEditorModeTools().GetActiveMode(ICyLandEditorModule::EM_Landscape_Mimic);

	//Find a location on the edge of the CyLand along the x axis so the default camera can see it in the distance.
	FVector CyLandSizePerComponent = CyLandEdMode->UISettings->NewCyLand_QuadsPerSection * CyLandEdMode->UISettings->NewCyLand_SectionsPerComponent * CyLandEdMode->UISettings->NewCyLand_Scale;
	FVector TargetLoctaion(0);
	TargetLoctaion.X = -CyLandSizePerComponent.X * (CyLandEdMode->UISettings->NewCyLand_ComponentCount.X / 2.f);

	ACyLandProxy* Proxy = CyLandEdMode->CurrentToolTarget.CyLandInfo.Get()->GetCurrentLevelCyLandProxy(true);
	if (Proxy)
	{
		TargetLoctaion = Proxy->CyLandActorToWorld().InverseTransformPosition(TargetLoctaion);
	}

	//Begin using the sculpting tool
	FLevelEditorViewportClient* SelectedViewport = CyLandTestUtils::FindSelectedViewport();
	CyLandEdMode->CurrentTool->BeginTool(SelectedViewport, CyLandEdMode->CurrentToolTarget, TargetLoctaion);
	SelectedViewport->Invalidate();

	UE_LOG(LogCyLandAutomationTests, Display, TEXT("Modified the CyLand using the sculpt tool"));

	return true;
}

/**
*  Latent command stop using the sculpting tool
*/
DEFINE_LATENT_AUTOMATION_COMMAND(FEndModifyCyLandCommand);
bool FEndModifyCyLandCommand::Update()
{
	//Find the CyLand
	FEdModeCyLand* CyLandEdMode = (FEdModeCyLand*)GLevelEditorModeTools().GetActiveMode(ICyLandEditorModule::EM_Landscape_Mimic);

	//End using the sculpting tool
	FLevelEditorViewportClient* SelectedViewport = CyLandTestUtils::FindSelectedViewport();
	CyLandEdMode->CurrentTool->EndTool(SelectedViewport);
	return true;
}

/**
* CyLand creation / edit test
*/
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCyLandEditorTest, "System.Promotion.Editor.CyLand Editor", EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter);
bool FCyLandEditorTest::RunTest(const FString& Parameters)
{
	//New level
	UWorld* NewMap = FAutomationEditorCommonUtils::CreateNewMap();
	if (NewMap)
	{
		UE_LOG(LogCyLandAutomationTests, Display, TEXT("Created an empty level"));
	}
	else
	{
		UE_LOG(LogCyLandAutomationTests, Error, TEXT("Failed to create an empty level"));
		return false;
	}

	ADD_LATENT_AUTOMATION_COMMAND(FCreateCyLandCommand());

	//For some reason the heightmap component takes a few ticks to register with the nav system.  We crash if we try to modify the heightmap before then.
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.0f));
	ADD_LATENT_AUTOMATION_COMMAND(FBeginModifyCyLandCommand());
	ADD_LATENT_AUTOMATION_COMMAND(FEndModifyCyLandCommand());

	return true;
}

#undef LOCTEXT_NAMESPACE

#endif //WITH_DEV_AUTOMATION_TESTS
