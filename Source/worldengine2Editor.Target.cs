// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;
using System.Collections.Generic;

public class worldengine2EditorTarget : TargetRules
{
	public worldengine2EditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;

		ExtraModuleNames.AddRange( new string[] { "worldengine2" } );
        ExtraModuleNames.AddRange(new string[] { "CyLand" });
        ExtraModuleNames.AddRange(new string[] { "CyLandEditor" });
    }
}
