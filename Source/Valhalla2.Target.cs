// Copyright Valhalla 2.0. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class Valhalla2Target : TargetRules
{
	public Valhalla2Target(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		ExtraModuleNames.AddRange(new string[] { "ValhallaCore", "ValhallaGame" });
	}
}
