// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NewCyLandUtils.h"

#include "CyLand.h"
#include "CyLandDataAccess.h"
#include "CyLandEditorObject.h"
#include "CyLandEditorModule.h"
#include "CyLandEditorUtils.h"
#include "CyLandEdMode.h"

#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "Templates/UnrealTemplate.h"

#define LOCTEXT_NAMESPACE "CyLandEditor.NewCyLand"

const int32 FNewCyLandUtils::SectionSizes[6] = { 7, 15, 31, 63, 127, 255 };
const int32 FNewCyLandUtils::NumSections[2] = { 1, 2 };

void FNewCyLandUtils::ChooseBestComponentSizeForImport(UCyLandEditorObject* UISettings)
{
	int32 Width = UISettings->ImportCyLand_Width;
	int32 Height = UISettings->ImportCyLand_Height;

	bool bFoundMatch = false;
	if(Width > 0 && Height > 0)
	{
		// Try to find a section size and number of sections that exactly matches the dimensions of the heightfield
		for(int32 SectionSizesIdx = ARRAY_COUNT(SectionSizes) - 1; SectionSizesIdx >= 0; SectionSizesIdx--)
		{
			for(int32 NumSectionsIdx = ARRAY_COUNT(NumSections) - 1; NumSectionsIdx >= 0; NumSectionsIdx--)
			{
				int32 ss = SectionSizes[SectionSizesIdx];
				int32 ns = NumSections[NumSectionsIdx];

				if(((Width - 1) % (ss * ns)) == 0 && ((Width - 1) / (ss * ns)) <= 32 &&
					((Height - 1) % (ss * ns)) == 0 && ((Height - 1) / (ss * ns)) <= 32)
				{
					bFoundMatch = true;
					UISettings->NewCyLand_QuadsPerSection = ss;
					UISettings->NewCyLand_SectionsPerComponent = ns;
					UISettings->NewCyLand_ComponentCount.X = (Width - 1) / (ss * ns);
					UISettings->NewCyLand_ComponentCount.Y = (Height - 1) / (ss * ns);
					UISettings->NewCyLand_ClampSize();
					break;
				}
			}
			if(bFoundMatch)
			{
				break;
			}
		}

		if(!bFoundMatch)
		{
			// if there was no exact match, try increasing the section size until we encompass the whole heightmap
			const int32 CurrentSectionSize = UISettings->NewCyLand_QuadsPerSection;
			const int32 CurrentNumSections = UISettings->NewCyLand_SectionsPerComponent;
			for(int32 SectionSizesIdx = 0; SectionSizesIdx < ARRAY_COUNT(SectionSizes); SectionSizesIdx++)
			{
				if(SectionSizes[SectionSizesIdx] < CurrentSectionSize)
				{
					continue;
				}

				const int32 ComponentsX = FMath::DivideAndRoundUp((Width - 1), SectionSizes[SectionSizesIdx] * CurrentNumSections);
				const int32 ComponentsY = FMath::DivideAndRoundUp((Height - 1), SectionSizes[SectionSizesIdx] * CurrentNumSections);
				if(ComponentsX <= 32 && ComponentsY <= 32)
				{
					bFoundMatch = true;
					UISettings->NewCyLand_QuadsPerSection = SectionSizes[SectionSizesIdx];
					//UISettings->NewCyLand_SectionsPerComponent = ;
					UISettings->NewCyLand_ComponentCount.X = ComponentsX;
					UISettings->NewCyLand_ComponentCount.Y = ComponentsY;
					UISettings->NewCyLand_ClampSize();
					break;
				}
			}
		}

		if(!bFoundMatch)
		{
			// if the heightmap is very large, fall back to using the largest values we support
			const int32 MaxSectionSize = SectionSizes[ARRAY_COUNT(SectionSizes) - 1];
			const int32 MaxNumSubSections = NumSections[ARRAY_COUNT(NumSections) - 1];
			const int32 ComponentsX = FMath::DivideAndRoundUp((Width - 1), MaxSectionSize * MaxNumSubSections);
			const int32 ComponentsY = FMath::DivideAndRoundUp((Height - 1), MaxSectionSize * MaxNumSubSections);

			bFoundMatch = true;
			UISettings->NewCyLand_QuadsPerSection = MaxSectionSize;
			UISettings->NewCyLand_SectionsPerComponent = MaxNumSubSections;
			UISettings->NewCyLand_ComponentCount.X = ComponentsX;
			UISettings->NewCyLand_ComponentCount.Y = ComponentsY;
			UISettings->NewCyLand_ClampSize();
		}

		check(bFoundMatch);
	}
}

void FNewCyLandUtils::ImportCyLandData( UCyLandEditorObject* UISettings, TArray< FCyLandFileResolution >& ImportResolutions )
{
	if ( !UISettings )
	{
		return;
	}

	ImportResolutions.Reset(1);
	UISettings->ImportCyLand_Width = 0;
	UISettings->ImportCyLand_Height = 0;
	UISettings->ClearImportCyLandData();
	UISettings->ImportCyLand_HeightmapImportResult = ECyLandImportResult::Success;
	UISettings->ImportCyLand_HeightmapErrorMessage = FText();

	if(!UISettings->ImportCyLand_HeightmapFilename.IsEmpty())
	{
		ICyLandEditorModule& CyLandEditorModule = FModuleManager::GetModuleChecked<ICyLandEditorModule>("CyLandEditor");
		const ICyLandHeightmapFileFormat* HeightmapFormat = CyLandEditorModule.GetHeightmapFormatByExtension(*FPaths::GetExtension(UISettings->ImportCyLand_HeightmapFilename, true));

		if(HeightmapFormat)
		{
			FCyLandHeightmapInfo HeightmapImportInfo = HeightmapFormat->Validate(*UISettings->ImportCyLand_HeightmapFilename);
			UISettings->ImportCyLand_HeightmapImportResult = HeightmapImportInfo.ResultCode;
			UISettings->ImportCyLand_HeightmapErrorMessage = HeightmapImportInfo.ErrorMessage;
			ImportResolutions = MoveTemp(HeightmapImportInfo.PossibleResolutions);
			if(HeightmapImportInfo.DataScale.IsSet())
			{
				UISettings->NewCyLand_Scale = HeightmapImportInfo.DataScale.GetValue();
				UISettings->NewCyLand_Scale.Z *= LANDSCAPE_INV_ZSCALE;
			}
		}
		else
		{
			UISettings->ImportCyLand_HeightmapImportResult = ECyLandImportResult::Error;
			UISettings->ImportCyLand_HeightmapErrorMessage = LOCTEXT("Import_UnknownFileType", "File type not recognised");
		}
	}

	if(ImportResolutions.Num() > 0)
	{
		int32 i = ImportResolutions.Num() / 2;
		UISettings->ImportCyLand_Width = ImportResolutions[i].Width;
		UISettings->ImportCyLand_Height = ImportResolutions[i].Height;
		UISettings->ImportCyLandData();
		ChooseBestComponentSizeForImport(UISettings);
	}
}

TOptional< TArray< FCyLandImportLayerInfo > > FNewCyLandUtils::CreateImportLayersInfo( UCyLandEditorObject* UISettings, int32 NewCyLandPreviewMode )
{
	const int32 ComponentCountX = UISettings->NewCyLand_ComponentCount.X;
	const int32 ComponentCountY = UISettings->NewCyLand_ComponentCount.Y;
	const int32 QuadsPerComponent = UISettings->NewCyLand_SectionsPerComponent * UISettings->NewCyLand_QuadsPerSection;
	const int32 SizeX = ComponentCountX * QuadsPerComponent + 1;
	const int32 SizeY = ComponentCountY * QuadsPerComponent + 1;

	TArray<FCyLandImportLayerInfo> ImportLayers;

	if(NewCyLandPreviewMode == ENewCyLandPreviewMode::NewCyLand)
	{
		const auto& ImportCyLandLayersList = UISettings->ImportCyLand_Layers;
		ImportLayers.Reserve(ImportCyLandLayersList.Num());

		// Fill in LayerInfos array and allocate data
		for(const FCyLandImportLayer& UIImportLayer : ImportCyLandLayersList)
		{
			FCyLandImportLayerInfo ImportLayer = FCyLandImportLayerInfo(UIImportLayer.LayerName);
			ImportLayer.LayerInfo = UIImportLayer.LayerInfo;
			ImportLayer.SourceFilePath = "";
			ImportLayer.LayerData = TArray<uint8>();
			ImportLayers.Add(MoveTemp(ImportLayer));
		}

		// Fill the first weight-blended layer to 100%
		if(FCyLandImportLayerInfo* FirstBlendedLayer = ImportLayers.FindByPredicate([](const FCyLandImportLayerInfo& ImportLayer) { return ImportLayer.LayerInfo && !ImportLayer.LayerInfo->bNoWeightBlend; }))
		{
			FirstBlendedLayer->LayerData.AddUninitialized(SizeX * SizeY);

			uint8* ByteData = FirstBlendedLayer->LayerData.GetData();
			for(int32 i = 0; i < SizeX * SizeY; i++)
			{
				ByteData[i] = 255;
			}
		}
	}
	else if(NewCyLandPreviewMode == ENewCyLandPreviewMode::ImportCyLand)
	{
		const uint32 ImportSizeX = UISettings->ImportCyLand_Width;
		const uint32 ImportSizeY = UISettings->ImportCyLand_Height;

		if(UISettings->ImportCyLand_HeightmapImportResult == ECyLandImportResult::Error)
		{
			// Cancel import
			return TOptional< TArray< FCyLandImportLayerInfo > >();
		}

		TArray<FCyLandImportLayer>& ImportCyLandLayersList = UISettings->ImportCyLand_Layers;
		ImportLayers.Reserve(ImportCyLandLayersList.Num());

		// Fill in LayerInfos array and allocate data
		for(FCyLandImportLayer& UIImportLayer : ImportCyLandLayersList)
		{
			ImportLayers.Add((const FCyLandImportLayer&)UIImportLayer); //slicing is fine here
			FCyLandImportLayerInfo& ImportLayer = ImportLayers.Last();

			if(ImportLayer.LayerInfo != nullptr && ImportLayer.SourceFilePath != "")
			{
				ICyLandEditorModule& CyLandEditorModule = FModuleManager::GetModuleChecked<ICyLandEditorModule>("CyLandEditor");
				const ICyLandWeightmapFileFormat* WeightmapFormat = CyLandEditorModule.GetWeightmapFormatByExtension(*FPaths::GetExtension(ImportLayer.SourceFilePath, true));

				if(WeightmapFormat)
				{
					FCyLandWeightmapImportData WeightmapImportData = WeightmapFormat->Import(*ImportLayer.SourceFilePath, ImportLayer.LayerName, { ImportSizeX, ImportSizeY });
					UIImportLayer.ImportResult = WeightmapImportData.ResultCode;
					UIImportLayer.ErrorMessage = WeightmapImportData.ErrorMessage;
					ImportLayer.LayerData = MoveTemp(WeightmapImportData.Data);
				}
				else
				{
					UIImportLayer.ImportResult = ECyLandImportResult::Error;
					UIImportLayer.ErrorMessage = LOCTEXT("Import_UnknownFileType", "File type not recognised");
				}

				if(UIImportLayer.ImportResult == ECyLandImportResult::Error)
				{
					ImportLayer.LayerData.Empty();
					FMessageDialog::Open(EAppMsgType::Ok, UIImportLayer.ErrorMessage);

					// Cancel import
					return TOptional< TArray< FCyLandImportLayerInfo > >();
				}
			}
		}
	}

	return ImportLayers;
}

TArray< uint16 > FNewCyLandUtils::ComputeHeightData( UCyLandEditorObject* UISettings, TArray< FCyLandImportLayerInfo >& ImportLayers, int32 NewCyLandPreviewMode )
{
	const int32 ComponentCountX = UISettings->NewCyLand_ComponentCount.X;
	const int32 ComponentCountY = UISettings->NewCyLand_ComponentCount.Y;
	const int32 QuadsPerComponent = UISettings->NewCyLand_SectionsPerComponent * UISettings->NewCyLand_QuadsPerSection;
	const int32 SizeX = ComponentCountX * QuadsPerComponent + 1;
	const int32 SizeY = ComponentCountY * QuadsPerComponent + 1;

	const uint32 ImportSizeX = UISettings->ImportCyLand_Width;
	const uint32 ImportSizeY = UISettings->ImportCyLand_Height;

	// Initialize heightmap data
	TArray<uint16> Data;
	Data.AddUninitialized(SizeX * SizeY);
	uint16* WordData = Data.GetData();

	// Initialize blank heightmap data
	for(int32 i = 0; i < SizeX * SizeY; i++)
	{
		WordData[i] = 32768;
	}

	if(NewCyLandPreviewMode == ENewCyLandPreviewMode::ImportCyLand)
	{
		const TArray<uint16>& ImportData = UISettings->GetImportCyLandData();
		if(ImportData.Num() != 0)
		{
			const int32 OffsetX = (int32)(SizeX - ImportSizeX) / 2;
			const int32 OffsetY = (int32)(SizeY - ImportSizeY) / 2;

			// Heightmap
			Data = CyLandEditorUtils::ExpandData(ImportData,
				0, 0, ImportSizeX - 1, ImportSizeY - 1,
				-OffsetX, -OffsetY, SizeX - OffsetX - 1, SizeY - OffsetY - 1);

			// Layers
			for(int32 LayerIdx = 0; LayerIdx < ImportLayers.Num(); LayerIdx++)
			{
				TArray<uint8>& ImportLayerData = ImportLayers[LayerIdx].LayerData;
				if(ImportLayerData.Num())
				{
					ImportLayerData = CyLandEditorUtils::ExpandData(ImportLayerData,
						0, 0, ImportSizeX - 1, ImportSizeY - 1,
						-OffsetX, -OffsetY, SizeX - OffsetX - 1, SizeY - OffsetY - 1);
				}
			}
		}
	}

	return Data;
}

#undef LOCTEXT_NAMESPACE
