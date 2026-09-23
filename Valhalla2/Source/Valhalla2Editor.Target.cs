// Copyright Valhalla 2.0. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class Valhalla2EditorTarget : TargetRules
{
	public Valhalla2EditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		ExtraModuleNames.AddRange(new string[] { "ValhallaCore", "ValhallaGame" });
	}
}
