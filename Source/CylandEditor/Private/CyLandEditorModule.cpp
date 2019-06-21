// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CyLandEditorModule.h"
#include "Modules/ModuleManager.h"
#include "Textures/SlateIcon.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "EditorStyleSet.h"
#include "EditorModeRegistry.h"
#include "EditorModeManager.h"
#include "EditorModes.h"
#include "CyLandFileFormatInterface.h"
#include "CyLandProxy.h"
#include "CyLandEdMode.h"
#include "CyLand.h"
#include "CyLandEditorCommands.h"
#include "Classes/ActorFactoryCyLand.h"
#include "CyLandFileFormatPng.h"
#include "CyLandFileFormatRaw.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "PropertyEditorModule.h"
#include "CyLandEditorDetails.h"
#include "CyLandEditorDetailCustomization_NewCyLand.h"
#include "CyLandEditorDetailCustomization_CopyPaste.h"
#include "CyLandSplineDetails.h"

#include "LevelEditor.h"

#include "CyLandRender.h"

#define LOCTEXT_NAMESPACE "CyLandEditor"

struct FRegisteredCyLandHeightmapFileFormat
{
	TSharedRef<ICyLandHeightmapFileFormat> FileFormat;
	FCyLandFileTypeInfo FileTypeInfo;
	FString ConcatenatedFileExtensions;

	FRegisteredCyLandHeightmapFileFormat(TSharedRef<ICyLandHeightmapFileFormat> InFileFormat);
};

struct FRegisteredCyLandWeightmapFileFormat
{
	TSharedRef<ICyLandWeightmapFileFormat> FileFormat;
	FCyLandFileTypeInfo FileTypeInfo;
	FString ConcatenatedFileExtensions;

	FRegisteredCyLandWeightmapFileFormat(TSharedRef<ICyLandWeightmapFileFormat> InFileFormat);
};

const FEditorModeID ICyLandEditorModule::EM_Landscape_Mimic(TEXT("EM_Landscape_Mimic"));

class FCyLandEditorModule : public ICyLandEditorModule
{
public:

	/**
	 * Called right after the module's DLL has been loaded and the module object has been created
	 */
	virtual void StartupModule() override
	{
		FCyLandEditorCommands::Register();

		// register the editor mode
		FEditorModeRegistry::Get().RegisterMode<FEdModeCyLand>(
			EM_Landscape_Mimic,
			NSLOCTEXT("EditorModes", "CyLandMode", "CyLand"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.LandscapeMode", "LevelEditor.LandscapeMode.Small"),
			true,
			300
			);

		// register customizations
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.RegisterCustomClassLayout("CyLandEditorObject", FOnGetDetailCustomizationInstance::CreateStatic(&FCyLandEditorDetails::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout("GizmoImportLayer", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FCyLandEditorStructCustomization_FCyGizmoImportLayer::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout("CyLandImportLayer", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FCyLandEditorStructCustomization_FCyLandImportLayer::MakeInstance));

		PropertyModule.RegisterCustomClassLayout("CyLandSplineControlPoint", FOnGetDetailCustomizationInstance::CreateStatic(&FCyLandSplineDetails::MakeInstance));
		PropertyModule.RegisterCustomClassLayout("CyLandSplineSegment", FOnGetDetailCustomizationInstance::CreateStatic(&FCyLandSplineDetails::MakeInstance));

		// add menu extension
		GlobalUICommandList = MakeShareable(new FUICommandList);
		const FCyLandEditorCommands& CyLandActions = FCyLandEditorCommands::Get();
		GlobalUICommandList->MapAction(CyLandActions.ViewModeNormal, FExecuteAction::CreateStatic(&ChangeCyLandViewMode, ECyLandViewMode::Normal), FCanExecuteAction(), FIsActionChecked::CreateStatic(&IsCyLandViewModeSelected, ECyLandViewMode::Normal));
		GlobalUICommandList->MapAction(CyLandActions.ViewModeLOD, FExecuteAction::CreateStatic(&ChangeCyLandViewMode, ECyLandViewMode::LOD), FCanExecuteAction(), FIsActionChecked::CreateStatic(&IsCyLandViewModeSelected, ECyLandViewMode::LOD));
		GlobalUICommandList->MapAction(CyLandActions.ViewModeLayerDensity, FExecuteAction::CreateStatic(&ChangeCyLandViewMode, ECyLandViewMode::LayerDensity), FCanExecuteAction(), FIsActionChecked::CreateStatic(&IsCyLandViewModeSelected, ECyLandViewMode::LayerDensity));
		GlobalUICommandList->MapAction(CyLandActions.ViewModeLayerDebug, FExecuteAction::CreateStatic(&ChangeCyLandViewMode, ECyLandViewMode::DebugLayer), FCanExecuteAction(), FIsActionChecked::CreateStatic(&IsCyLandViewModeSelected, ECyLandViewMode::DebugLayer));
		GlobalUICommandList->MapAction(CyLandActions.ViewModeWireframeOnTop, FExecuteAction::CreateStatic(&ChangeCyLandViewMode, ECyLandViewMode::WireframeOnTop), FCanExecuteAction(), FIsActionChecked::CreateStatic(&IsCyLandViewModeSelected, ECyLandViewMode::WireframeOnTop));
		GlobalUICommandList->MapAction(CyLandActions.ViewModeLayerUsage, FExecuteAction::CreateStatic(&ChangeCyLandViewMode, ECyLandViewMode::LayerUsage), FCanExecuteAction(), FIsActionChecked::CreateStatic(&IsCyLandViewModeSelected, ECyLandViewMode::LayerUsage));

		ViewportMenuExtender = MakeShareable(new FExtender);
		ViewportMenuExtender->AddMenuExtension("LevelViewportCyLand", EExtensionHook::First, GlobalUICommandList, FMenuExtensionDelegate::CreateStatic(&ConstructCyLandViewportMenu));
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
		LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(ViewportMenuExtender);

		// add actor factories
		UActorFactoryCyLand* CyLandActorFactory = NewObject<UActorFactoryCyLand>();
		CyLandActorFactory->NewActorClass = ACyLand::StaticClass();
		GEditor->ActorFactories.Add(CyLandActorFactory);

		UActorFactoryCyLand* CyLandProxyActorFactory = NewObject<UActorFactoryCyLand>();
		CyLandProxyActorFactory->NewActorClass = ACyLandProxy::StaticClass();
		GEditor->ActorFactories.Add(CyLandProxyActorFactory);

		// Built-in File Formats
		RegisterHeightmapFileFormat(MakeShareable(new FCyLandHeightmapFileFormat_Png()));
		RegisterWeightmapFileFormat(MakeShareable(new FCyLandWeightmapFileFormat_Png()));
		RegisterHeightmapFileFormat(MakeShareable(new FCyLandHeightmapFileFormat_Raw()));
		RegisterWeightmapFileFormat(MakeShareable(new FCyLandWeightmapFileFormat_Raw()));
	}

	/**
	 * Called before the module is unloaded, right before the module object is destroyed.
	 */
	virtual void ShutdownModule() override
	{
		FCyLandEditorCommands::Unregister();

		// unregister the editor mode
		FEditorModeRegistry::Get().UnregisterMode(EM_Landscape_Mimic);

		// unregister customizations
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout("CyLandEditorObject");
		PropertyModule.UnregisterCustomPropertyTypeLayout("GizmoImportLayer");
		PropertyModule.UnregisterCustomPropertyTypeLayout("CyLandImportLayer");

		PropertyModule.UnregisterCustomClassLayout("CyLandSplineControlPoint");
		PropertyModule.UnregisterCustomClassLayout("CyLandSplineSegment");

		// remove menu extension
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
		LevelEditorModule.GetMenuExtensibilityManager()->RemoveExtender(ViewportMenuExtender);
		ViewportMenuExtender = nullptr;
		GlobalUICommandList = nullptr;

		// remove actor factories
		// TODO - this crashes on shutdown
		// GEditor->ActorFactories.RemoveAll([](const UActorFactory* ActorFactory) { return ActorFactory->IsA<UActorFactoryCyLand>(); });
	}

	static void ConstructCyLandViewportMenu(FMenuBuilder& MenuBuilder)
	{
		struct Local
		{
			static void BuildCyLandVisualizersMenu(FMenuBuilder& InMenuBuilder)
			{
				const FCyLandEditorCommands& CyLandActions = FCyLandEditorCommands::Get();

				InMenuBuilder.BeginSection("CyLandVisualizers", LOCTEXT("CyLandHeader", "CyLand Visualizers"));
				{
					InMenuBuilder.AddMenuEntry(CyLandActions.ViewModeNormal, NAME_None, LOCTEXT("CyLandViewModeNormal", "Normal"));
					InMenuBuilder.AddMenuEntry(CyLandActions.ViewModeLOD, NAME_None, LOCTEXT("CyLandViewModeLOD", "LOD"));
					InMenuBuilder.AddMenuEntry(CyLandActions.ViewModeLayerDensity, NAME_None, LOCTEXT("CyLandViewModeLayerDensity", "Layer Density"));
					if (GLevelEditorModeTools().IsModeActive(EM_Landscape_Mimic))
					{
						InMenuBuilder.AddMenuEntry(CyLandActions.ViewModeLayerUsage, NAME_None, LOCTEXT("CyLandViewModeLayerUsage", "Layer Usage"));
						InMenuBuilder.AddMenuEntry(CyLandActions.ViewModeLayerDebug, NAME_None, LOCTEXT("CyLandViewModeLayerDebug", "Layer Debug"));
					}
					InMenuBuilder.AddMenuEntry(CyLandActions.ViewModeWireframeOnTop, NAME_None, LOCTEXT("CyLandViewModeWireframeOnTop", "Wireframe on Top"));
				}
				InMenuBuilder.EndSection();
			}
		};
		MenuBuilder.AddSubMenu(LOCTEXT("CyLandSubMenu", "Visualizers"), LOCTEXT("CyLandSubMenu_ToolTip", "Select a CyLand visualiser"), FNewMenuDelegate::CreateStatic(&Local::BuildCyLandVisualizersMenu));
	}

	static void ChangeCyLandViewMode(ECyLandViewMode::Type ViewMode)
	{
		GCyLandViewMode = ViewMode;
	}

	static bool IsCyLandViewModeSelected(ECyLandViewMode::Type ViewMode)
	{
		return GCyLandViewMode == ViewMode;
	}

	virtual void RegisterHeightmapFileFormat(TSharedRef<ICyLandHeightmapFileFormat> FileFormat) override
	{
		HeightmapFormats.Emplace(FileFormat);
		HeightmapImportDialogTypeString.Reset();
		HeightmapExportDialogTypeString.Reset();
	}

	virtual void RegisterWeightmapFileFormat(TSharedRef<ICyLandWeightmapFileFormat> FileFormat) override
	{
		WeightmapFormats.Emplace(FileFormat);
		WeightmapImportDialogTypeString.Reset();
		WeightmapExportDialogTypeString.Reset();
	}

	virtual void UnregisterHeightmapFileFormat(TSharedRef<ICyLandHeightmapFileFormat> FileFormat) override
	{
		int32 Index = HeightmapFormats.IndexOfByPredicate(
			[FileFormat](const FRegisteredCyLandHeightmapFileFormat& RegisteredFileFormat)
			{
				return RegisteredFileFormat.FileFormat == FileFormat;
			});
		if (Index != INDEX_NONE)
		{
			HeightmapFormats.RemoveAt(Index);
			HeightmapImportDialogTypeString.Reset();
			HeightmapExportDialogTypeString.Reset();
		}
	}

	virtual void UnregisterWeightmapFileFormat(TSharedRef<ICyLandWeightmapFileFormat> FileFormat) override
	{
		int32 Index = WeightmapFormats.IndexOfByPredicate(
			[FileFormat](const FRegisteredCyLandWeightmapFileFormat& RegisteredFileFormat)
			{
				return RegisteredFileFormat.FileFormat == FileFormat;
			});
		if (Index != INDEX_NONE)
		{
			WeightmapFormats.RemoveAt(Index);
			WeightmapImportDialogTypeString.Reset();
			WeightmapExportDialogTypeString.Reset();
		}
	}

	virtual const TCHAR* GetHeightmapImportDialogTypeString() const override;
	virtual const TCHAR* GetWeightmapImportDialogTypeString() const override;

	virtual const TCHAR* GetHeightmapExportDialogTypeString() const override;
	virtual const TCHAR* GetWeightmapExportDialogTypeString() const override;

	virtual const ICyLandHeightmapFileFormat* GetHeightmapFormatByExtension(const TCHAR* Extension) const override;
	virtual const ICyLandWeightmapFileFormat* GetWeightmapFormatByExtension(const TCHAR* Extension) const override;

	virtual TSharedPtr<FUICommandList> GetCyLandLevelViewportCommandList() const override;

protected:
	TSharedPtr<FExtender> ViewportMenuExtender;
	TSharedPtr<FUICommandList> GlobalUICommandList;
	TArray<FRegisteredCyLandHeightmapFileFormat> HeightmapFormats;
	TArray<FRegisteredCyLandWeightmapFileFormat> WeightmapFormats;
	mutable FString HeightmapImportDialogTypeString;
	mutable FString WeightmapImportDialogTypeString;
	mutable FString HeightmapExportDialogTypeString;
	mutable FString WeightmapExportDialogTypeString;
};

IMPLEMENT_MODULE(FCyLandEditorModule, CyLandEditor);

FRegisteredCyLandHeightmapFileFormat::FRegisteredCyLandHeightmapFileFormat(TSharedRef<ICyLandHeightmapFileFormat> InFileFormat)
	: FileFormat(MoveTemp(InFileFormat))
	, FileTypeInfo(FileFormat->GetInfo())
{
	bool bJoin = false;
	for (const FString& Extension : FileTypeInfo.Extensions)
	{
		if (bJoin)
		{
			ConcatenatedFileExtensions += TEXT(';');
		}
		ConcatenatedFileExtensions += TEXT('*');
		ConcatenatedFileExtensions += Extension;
		bJoin = true;
	}
}

FRegisteredCyLandWeightmapFileFormat::FRegisteredCyLandWeightmapFileFormat(TSharedRef<ICyLandWeightmapFileFormat> InFileFormat)
	: FileFormat(MoveTemp(InFileFormat))
	, FileTypeInfo(FileFormat->GetInfo())
{
	bool bJoin = false;
	for (const FString& Extension : FileTypeInfo.Extensions)
	{
		if (bJoin)
		{
			ConcatenatedFileExtensions += TEXT(';');
		}
		ConcatenatedFileExtensions += TEXT('*');
		ConcatenatedFileExtensions += Extension;
		bJoin = true;
	}
}

const TCHAR* FCyLandEditorModule::GetHeightmapImportDialogTypeString() const
{
	if (HeightmapImportDialogTypeString.IsEmpty())
	{
		HeightmapImportDialogTypeString = TEXT("All Heightmap files|");
		bool bJoin = false;
		for (const FRegisteredCyLandHeightmapFileFormat& HeightmapFormat : HeightmapFormats)
		{
			if (bJoin)
			{
				HeightmapImportDialogTypeString += TEXT(';');
			}
			HeightmapImportDialogTypeString += HeightmapFormat.ConcatenatedFileExtensions;
			bJoin = true;
		}
		HeightmapImportDialogTypeString += TEXT('|');
		for (const FRegisteredCyLandHeightmapFileFormat& HeightmapFormat : HeightmapFormats)
		{
			HeightmapImportDialogTypeString += HeightmapFormat.FileTypeInfo.Description.ToString();
			HeightmapImportDialogTypeString += TEXT('|');
			HeightmapImportDialogTypeString += HeightmapFormat.ConcatenatedFileExtensions;
			HeightmapImportDialogTypeString += TEXT('|');
		}
		HeightmapImportDialogTypeString += TEXT("All Files (*.*)|*.*");
	}

	return *HeightmapImportDialogTypeString;
}

const TCHAR* FCyLandEditorModule::GetWeightmapImportDialogTypeString() const
{
	if (WeightmapImportDialogTypeString.IsEmpty())
	{
		WeightmapImportDialogTypeString = TEXT("All Layer files|");
		bool bJoin = false;
		for (const FRegisteredCyLandWeightmapFileFormat& WeightmapFormat : WeightmapFormats)
		{
			if (bJoin)
			{
				WeightmapImportDialogTypeString += TEXT(';');
			}
			WeightmapImportDialogTypeString += WeightmapFormat.ConcatenatedFileExtensions;
			bJoin = true;
		}
		WeightmapImportDialogTypeString += TEXT('|');
		for (const FRegisteredCyLandWeightmapFileFormat& WeightmapFormat : WeightmapFormats)
		{
			WeightmapImportDialogTypeString += WeightmapFormat.FileTypeInfo.Description.ToString();
			WeightmapImportDialogTypeString += TEXT('|');
			WeightmapImportDialogTypeString += WeightmapFormat.ConcatenatedFileExtensions;
			WeightmapImportDialogTypeString += TEXT('|');
		}
		WeightmapImportDialogTypeString += TEXT("All Files (*.*)|*.*");
	}

	return *WeightmapImportDialogTypeString;
}

const TCHAR* FCyLandEditorModule::GetHeightmapExportDialogTypeString() const
{
	if (HeightmapExportDialogTypeString.IsEmpty())
	{
		HeightmapExportDialogTypeString = TEXT("");

		for (const FRegisteredCyLandHeightmapFileFormat& HeightmapFormat : HeightmapFormats)
		{
			if (!HeightmapFormat.FileTypeInfo.bSupportsExport)
			{
				continue;
			}
			HeightmapExportDialogTypeString += HeightmapFormat.FileTypeInfo.Description.ToString();
			HeightmapExportDialogTypeString += TEXT('|');
			HeightmapExportDialogTypeString += HeightmapFormat.ConcatenatedFileExtensions;
			HeightmapExportDialogTypeString += TEXT('|');
		}
		HeightmapExportDialogTypeString += TEXT("All Files (*.*)|*.*");
	}

	return *HeightmapExportDialogTypeString;
}

const TCHAR* FCyLandEditorModule::GetWeightmapExportDialogTypeString() const
{
	if (WeightmapExportDialogTypeString.IsEmpty())
	{
		WeightmapExportDialogTypeString = TEXT("");
		for (const FRegisteredCyLandWeightmapFileFormat& WeightmapFormat : WeightmapFormats)
		{
			if (!WeightmapFormat.FileTypeInfo.bSupportsExport)
			{
				continue;
			}
			WeightmapExportDialogTypeString += WeightmapFormat.FileTypeInfo.Description.ToString();
			WeightmapExportDialogTypeString += TEXT('|');
			WeightmapExportDialogTypeString += WeightmapFormat.ConcatenatedFileExtensions;
			WeightmapExportDialogTypeString += TEXT('|');
		}
		WeightmapExportDialogTypeString += TEXT("All Files (*.*)|*.*");
	}

	return *WeightmapExportDialogTypeString;
}

const ICyLandHeightmapFileFormat* FCyLandEditorModule::GetHeightmapFormatByExtension(const TCHAR* Extension) const
{
	auto* FoundFormat = HeightmapFormats.FindByPredicate(
		[Extension](const FRegisteredCyLandHeightmapFileFormat& HeightmapFormat)
		{
			return HeightmapFormat.FileTypeInfo.Extensions.Contains(Extension);
		});

	return FoundFormat ? &FoundFormat->FileFormat.Get() : nullptr;
}

const ICyLandWeightmapFileFormat* FCyLandEditorModule::GetWeightmapFormatByExtension(const TCHAR* Extension) const
{
	auto* FoundFormat = WeightmapFormats.FindByPredicate(
		[Extension](const FRegisteredCyLandWeightmapFileFormat& WeightmapFormat)
	{
		return WeightmapFormat.FileTypeInfo.Extensions.Contains(Extension);
	});

	return FoundFormat ? &FoundFormat->FileFormat.Get() : nullptr;
}

TSharedPtr<FUICommandList> FCyLandEditorModule::GetCyLandLevelViewportCommandList() const
{
	return GlobalUICommandList;
}

#undef LOCTEXT_NAMESPACE
