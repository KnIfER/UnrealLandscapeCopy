// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Editor.h"
#include "Modules/ModuleInterface.h"
#include "Templates/SharedPointer.h"

class ICyLandHeightmapFileFormat;
class ICyLandWeightmapFileFormat;
class FUICommandList;

/**
 * CyLandEditor module interface
 */
class ICyLandEditorModule : public IModuleInterface
{
public:
	const static FEditorModeID EM_Landscape_Mimic;
	// Register / unregister a CyLand file format plugin
	virtual void RegisterHeightmapFileFormat(TSharedRef<ICyLandHeightmapFileFormat> FileFormat) = 0;
	virtual void RegisterWeightmapFileFormat(TSharedRef<ICyLandWeightmapFileFormat> FileFormat) = 0;
	virtual void UnregisterHeightmapFileFormat(TSharedRef<ICyLandHeightmapFileFormat> FileFormat) = 0;
	virtual void UnregisterWeightmapFileFormat(TSharedRef<ICyLandWeightmapFileFormat> FileFormat) = 0;

	// Gets the type string used by the import/export file dialog
	virtual const TCHAR* GetHeightmapImportDialogTypeString() const = 0;
	virtual const TCHAR* GetWeightmapImportDialogTypeString() const = 0;
	virtual const TCHAR* GetHeightmapExportDialogTypeString() const = 0;
	virtual const TCHAR* GetWeightmapExportDialogTypeString() const = 0;

	// Gets the heightmap/weightmap format associated with a given extension (null if no plugin is registered for this extension)
	virtual const ICyLandHeightmapFileFormat* GetHeightmapFormatByExtension(const TCHAR* Extension) const = 0;
	virtual const ICyLandWeightmapFileFormat* GetWeightmapFormatByExtension(const TCHAR* Extension) const = 0;

	virtual TSharedPtr<FUICommandList> GetCyLandLevelViewportCommandList() const = 0;
};
