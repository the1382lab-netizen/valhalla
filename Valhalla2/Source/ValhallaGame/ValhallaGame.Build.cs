// Copyright Valhalla 2.0. All Rights Reserved.

using UnrealBuildTool;

public class ValhallaGame : ModuleRules
{
	public ValhallaGame(ReadOnlyTargetRules Target) : base(Target)
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
				"InputCore",

				// Enhanced Input is built entirely in code in Phase 2a: the mapping
				// context and every action are NewObject'd by AValhallaPlayerController,
				// so the project ships no input .uassets at all.
				"EnhancedInput",

				// Push-model / FRepMovement helpers and the replication macros.
				"NetCore",

				// Phase 8a's VFX. Public rather than private because
				// AValhallaSpellProjectile carries a UNiagaraComponent as a
				// UPROPERTY, so the type is part of this module's headers.
				"Niagara",

				// Phase 8a's animation blending. UValhallaAnimInstance's proxy
				// owns an FAnimNode_TwoWayBlend, which lives here rather than
				// in Engine; FAnimNode_SequencePlayer_Standalone is in Engine.
				// Public for the same reason as Niagara: it is in a header.
				"AnimGraphRuntime",

				// Phase 1's data tables: classes, skills, items, zones.
				"ValhallaCore",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				// The 1.0 JSON is loaded by ValhallaCore, but Phase 2b's skill
				// payloads and the debug HUD read raw JSON too.
				"Json",

				// Phase 7b builds the login and character-select screens from
				// UUserWidget subclasses with no Blueprint assets; Phase 8
				// replaces AValhallaHUD::DrawHUD with UMG too.
				"UMG",
				"Slate",
				"SlateCore",

				// B-07 step 2: UValhallaUISettings (Project Settings > Valhalla >
				// UI, the game HUD class) is a UDeveloperSettings.
				"DeveloperSettings",

				// Phase 7b: UValhallaBackendSubsystem's client for the 1.0
				// Express server. Private, so nothing that depends on
				// ValhallaGame inherits an HTTP client it did not ask for —
				// the same reasoning as HTTPServer below.
				"HTTP",
			}
		);

		// ── Phase 6b: the editor's live dashboard ────────────────────────
		//
		// The admin HTTP API is compiled into the targets a developer runs and
		// out of the one a player runs. `#if !UE_SERVER` would have been the
		// obvious-looking way to write "server only" and would have been
		// exactly backwards — see WITH_VALHALLA_ADMIN_API's comment in
		// ValhallaAdminServer.h. A target-type test says what is actually meant.
		bool bAdminApi = Target.Type == TargetType.Editor || Target.Type == TargetType.Server;

		PrivateDefinitions.Add("WITH_VALHALLA_ADMIN_API=" + (bAdminApi ? "1" : "0"));

		if (bAdminApi)
		{
			// The engine's own listener + router. Private, so nothing that
			// depends on ValhallaGame inherits an HTTP server.
			PrivateDependencyModuleNames.Add("HTTPServer");
		}
	}
}
