// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CyLandFileFormatRaw.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"

#define LOCTEXT_NAMESPACE "CyLandEditor.NewCyLand"

TArray<FCyLandFileResolution> CalculatePossibleRawResolutions(int64 FileSize)
{
	TArray<FCyLandFileResolution> PossibleResolutions;

	// Find all possible heightmap sizes, between 8 and 8192 width/height
	const int32 MinWidth = FMath::Max(8, (int32)FMath::DivideAndRoundUp(FileSize, (int64)8192));
	const int32 MaxWidth = FMath::TruncToInt(FMath::Sqrt(FileSize));
	for (int32 Width = MinWidth; Width <= MaxWidth; Width++)
	{
		if (FileSize % Width == 0)
		{
			FCyLandFileResolution ImportResolution;
			ImportResolution.Width = Width;
			ImportResolution.Height = FileSize / Width;
			PossibleResolutions.Add(ImportResolution);
		}
	}

	for (int32 i = PossibleResolutions.Num() - 1; i >= 0; --i)
	{
		FCyLandFileResolution ImportResolution = PossibleResolutions[i];
		if (ImportResolution.Width != ImportResolution.Height)
		{
			Swap(ImportResolution.Width, ImportResolution.Height);
			PossibleResolutions.Add(ImportResolution);
		}
	}

	return PossibleResolutions;
}

FCyLandHeightmapFileFormat_Raw::FCyLandHeightmapFileFormat_Raw()
{
	FileTypeInfo.Description = LOCTEXT("FileFormatRaw_HeightmapDesc", "Heightmap .r16/.raw files");
	FileTypeInfo.Extensions.Add(".r16");
	FileTypeInfo.Extensions.Add(".raw");
	FileTypeInfo.bSupportsExport = true;
}

FCyLandHeightmapInfo FCyLandHeightmapFileFormat_Raw::Validate(const TCHAR* HeightmapFilename) const
{
	FCyLandHeightmapInfo Result;

	int64 ImportFileSize = IFileManager::Get().FileSize(HeightmapFilename);

	if (ImportFileSize < 0)
	{
		Result.ResultCode = ECyLandImportResult::Error;
		Result.ErrorMessage = LOCTEXT("Import_HeightmapFileReadError", "Error reading heightmap file");
	}
	else if (ImportFileSize == 0 || ImportFileSize % 2 != 0)
	{
		Result.ResultCode = ECyLandImportResult::Error;
		Result.ErrorMessage = LOCTEXT("Import_HeightmapFileInvalidSize", "The heightmap file has an invalid size (possibly not 16-bit?)");
	}
	else
	{
		Result.PossibleResolutions = CalculatePossibleRawResolutions(ImportFileSize / 2);

		if (Result.PossibleResolutions.Num() == 0)
		{
			Result.ResultCode = ECyLandImportResult::Error;
			Result.ErrorMessage = LOCTEXT("Import_HeightmapFileInvalidSize", "The heightmap file has an invalid size (possibly not 16-bit?)");
		}
	}

	return Result;
}

FCyLandHeightmapImportData FCyLandHeightmapFileFormat_Raw::Import(const TCHAR* HeightmapFilename, FCyLandFileResolution ExpectedResolution) const
{
	FCyLandHeightmapImportData Result;

	TArray<uint8> TempData;
	if (!FFileHelper::LoadFileToArray(TempData, HeightmapFilename, FILEREAD_Silent))
	{
		Result.ResultCode = ECyLandImportResult::Error;
		Result.ErrorMessage = LOCTEXT("Import_HeightmapFileReadError", "Error reading heightmap file");
	}
	else if (TempData.Num() != (ExpectedResolution.Width * ExpectedResolution.Height * 2))
	{
		Result.ResultCode = ECyLandImportResult::Error;
		Result.ErrorMessage = LOCTEXT("Import_HeightmapResolutionMismatch", "The heightmap file's resolution does not match the requested resolution");
	}
	else
	{
		Result.Data.Empty(ExpectedResolution.Width * ExpectedResolution.Height);
		Result.Data.AddUninitialized(ExpectedResolution.Width * ExpectedResolution.Height);
		FMemory::Memcpy(Result.Data.GetData(), TempData.GetData(), ExpectedResolution.Width * ExpectedResolution.Height * 2);
	}

	return Result;
}

void FCyLandHeightmapFileFormat_Raw::Export(const TCHAR* HeightmapFilename, TArrayView<const uint16> Data, FCyLandFileResolution DataResolution, FVector Scale) const
{
	TArray<uint8> TempData;
	TempData.Empty(DataResolution.Width * DataResolution.Height * 2);
	TempData.AddUninitialized(DataResolution.Width * DataResolution.Height * 2);
	FMemory::Memcpy(TempData.GetData(), Data.GetData(), DataResolution.Width * DataResolution.Height * 2);

	FFileHelper::SaveArrayToFile(TempData, HeightmapFilename);
}

//////////////////////////////////////////////////////////////////////////

FCyLandWeightmapFileFormat_Raw::FCyLandWeightmapFileFormat_Raw()
{
	FileTypeInfo.Description = LOCTEXT("FileFormatRaw_WeightmapDesc", "Layer .r8/.raw files");
	FileTypeInfo.Extensions.Add(".r8");
	FileTypeInfo.Extensions.Add(".raw");
	FileTypeInfo.bSupportsExport = true;
}

FCyLandWeightmapInfo FCyLandWeightmapFileFormat_Raw::Validate(const TCHAR* WeightmapFilename, FName LayerName) const
{
	FCyLandWeightmapInfo Result;

	int64 ImportFileSize = IFileManager::Get().FileSize(WeightmapFilename);

	if (ImportFileSize < 0)
	{
		Result.ResultCode = ECyLandImportResult::Error;
		Result.ErrorMessage = LOCTEXT("Import_LayerFileReadError", "Error reading layer file");
	}
	else
	{
		Result.PossibleResolutions = CalculatePossibleRawResolutions(ImportFileSize);

		if (Result.PossibleResolutions.Num() == 0)
		{
			Result.ResultCode = ECyLandImportResult::Error;
			Result.ErrorMessage = LOCTEXT("Import_WeightmapFileInvalidSize", "The layer file has an invalid size");
		}
	}

	return Result;
}

FCyLandWeightmapImportData FCyLandWeightmapFileFormat_Raw::Import(const TCHAR* WeightmapFilename, FName LayerName, FCyLandFileResolution ExpectedResolution) const
{
	FCyLandWeightmapImportData Result;

	TArray<uint8> TempData;
	if (!FFileHelper::LoadFileToArray(TempData, WeightmapFilename, FILEREAD_Silent))
	{
		Result.ResultCode = ECyLandImportResult::Error;
		Result.ErrorMessage = LOCTEXT("Import_LayerFileReadError", "Error reading layer file");
	}
	else if (TempData.Num() != (ExpectedResolution.Width * ExpectedResolution.Height))
	{
		Result.ResultCode = ECyLandImportResult::Error;
		Result.ErrorMessage = LOCTEXT("Import_LayerResolutionMismatch", "The layer file's resolution does not match the requested resolution");
	}
	else
	{
		Result.Data = MoveTemp(TempData);
	}

	return Result;
}

void FCyLandWeightmapFileFormat_Raw::Export(const TCHAR* WeightmapFilename, FName LayerName, TArrayView<const uint8> Data, FCyLandFileResolution DataResolution) const
{
	FFileHelper::SaveArrayToFile(Data, WeightmapFilename);
}

#undef LOCTEXT_NAMESPACE
