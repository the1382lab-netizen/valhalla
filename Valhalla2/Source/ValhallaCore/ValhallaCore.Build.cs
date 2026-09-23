// Copyright Valhalla 2.0. All Rights Reserved.

using UnrealBuildTool;

public class ValhallaCore : ModuleRules
{
	public ValhallaCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		// Each .cpp keeps its own anonymous namespace. With unity builds on, UBT
		// merges unchanged files into one translation unit and two files that
		// both define e.g. `FindData` in an anonymous namespace stop compiling.
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Json",
				"JsonUtilities",
				"DeveloperSettings",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
			}
		);
	}
}
