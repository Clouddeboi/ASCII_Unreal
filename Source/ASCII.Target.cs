// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;
using System.Collections.Generic;

public class ASCIITarget : TargetRules
{
	public ASCIITarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V8;

		ExtraModuleNames.AddRange( new string[] { "ASCII" } );
	}
}
