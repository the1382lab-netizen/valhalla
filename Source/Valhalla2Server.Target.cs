// Copyright Valhalla 2.0. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class Valhalla2ServerTarget : TargetRules
{
	public Valhalla2ServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		ExtraModuleNames.AddRange(new string[] { "ValhallaCore", "ValhallaGame" });
	}
}
