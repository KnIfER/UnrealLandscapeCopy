// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class CyLand : ModuleRules
{
	public CyLand(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.AddRange(
            new string[] {
                "D:\\unreal\\UE_4.22\\Engine\\Source\\Runtime\\Engine\\Public", // for Engine/Private/Collision/PhysXCollision.h

                "D:\\unreal\\UE_4.22\\Engine\\Source\\ThirdParty\\PhysX3\\PxShared\\include\\foundation", // for Engine/Private/Collision/PhysXCollision.h
                "D:\\unreal\\UE_4.22\\Engine\\Source\\ThirdParty\\PhysX3\\PxShared\\include\\pvd", // for Engine/Private/Collision/PhysXCollision.h
                "D:\\unreal\\UE_4.22\\Engine\\Source\\ThirdParty\\PhysX3\\PxShared\\include\\", // for Engine/Private/Collision/PhysXCollision.h
                "D:\\unreal\\UE_4.22\\Engine\\Source\\ThirdParty\\PhysX3\\PhysX_3.4\\Include", // for Engine/Private/Collision/PhysXCollision.h
                "D:\\unreal\\UE_4.22\\Engine\\Source\\ThirdParty\\PhysX3\\PhysX_3.4\\Include\\common", // for Engine/Private/Collision/PhysXCollision.h
                "D:\\unreal\\UE_4.22\\Engine\\Source\\ThirdParty\\PhysX3\\PhysX_3.4\\Include\\extensions", // for Engine/Private/Collision/PhysXCollision.h
                "D:\\unreal\\UE_4.22\\Engine\\Source\\ThirdParty\\PhysX3\\PhysX_3.4\\Include\\geometry", // for Engine/Private/Collision/PhysXCollision.h
                "D:\\unreal\\UE_4.22\\Engine\\Source\\ThirdParty\\PhysX3\\APEX_1.4\\include", // for Engine/Private/Collision/PhysXCollision.h
                "D:\\unreal\\UE_4.22\\Engine\\Source\\ThirdParty\\PhysX3\\APEX_1.4\\include\\PhysX3", // for Engine/Private/Collision/PhysXCollision.h
                "D:\\unreal\\UE_4.22\\Engine\\Source\\ThirdParty\\PhysX3\\APEX_1.4\\include\\clothing", // for Engine/Private/Collision/PhysXCollision.h
                "D:\\unreal\\UE_4.22\\Engine\\Source\\ThirdParty\\PhysX3\\APEX_1.4\\include\\legacy", // for Engine/Private/Collision/PhysXCollision.h
                "D:\\unreal\\UE_4.22\\Engine\\Source\\ThirdParty\\PhysX3\\APEX_1.4\\include\\nvparameterized", // for Engine/Private/Collision/PhysXCollision.h
                "D:\\unreal\\UE_4.22\\Engine\\Source\\ThirdParty\\PhysX3\\APEX_1.4\\shared\\general\\RenderDebug\\public", // for Engine/Private/Collision/PhysXCollision.h

                
            }

        );


        PrivateIncludePaths.AddRange(
            new string[] {
                "D:\\unreal\\UE_4.22\\Engine\\Source\\Runtime\\Engine\\Private", // for Engine/Private/Collision/PhysXCollision.h
				"CyLand/Private",



                "D:\\unreal\\UE_4.22\\Engine\\Source\\Editor\\UnrealEd\\Private\\",
                //"UnrealEd/Private/StaticLightingSystem",
            }

        );

        PrivateIncludePathModuleNames.AddRange(
            new string[] {
                "TargetPlatform",
                "DerivedDataCache",
                "Foliage",
                "Renderer",
                "UnrealEd",
            }
        );
        PrivateIncludePathModuleNames.Add("CyLandEditor");

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
            "Foliage",
            "InputCore",
            "SwarmInterface",
        });


        PrivateDependencyModuleNames.AddRange(
                new string[] {
                    "MaterialUtilities",
                    "SlateCore",
                    "Slate",

                    "Core",
                    "CoreUObject",
                    "ApplicationCore",
                    "Engine",
                    "RenderCore",
                    "RHI",
                    "Renderer",
                }
            );
        //PrivateDependencyModuleNames.Add("CyLandEditor");
        PrivateDependencyModuleNames.AddRange(new string[] {
                    "MeshDescription",
                    "MeshUtilitiesCommon"
            });


        PrivateDependencyModuleNames.AddRange(
                new string[] {
                    "UnrealEd",
                    "MaterialUtilities",
                    "SlateCore",
                    "Slate",
                }
            );


        // Uncomment if you are using Slate UI
        // PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

        // Uncomment if you are using online features
        // PrivateDependencyModuleNames.Add("OnlineSubsystem");

        // To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
    }
}
