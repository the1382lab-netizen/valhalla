// Copyright Valhalla 2.0. All Rights Reserved.
//
// B-04 production-secret rules, as pure functions:
//
//   ServerSecret   GetServerSecret's resolution order (command line, env,
//                  secrets.local.env, legacy ini, dev default in editor builds
//                  only), which server kinds must refuse the dev default, and
//                  that the `Join request` log line never carries a whole token.
//
// Run from the editor's Session Frontend, or headless:
//   UnrealEditor-Cmd.exe <project>.uproject -ExecCmds="Automation RunTests Valhalla.; Quit" -unattended -nullrhi

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ValhallaDataSettings.h"
#include "ValhallaGameMode.h"

#ifndef VALHALLA_GAME_TEST_FLAGS
#define VALHALLA_GAME_TEST_FLAGS ( \
	EAutomationTestFlags::EditorContext \
	| EAutomationTestFlags::ClientContext \
	| EAutomationTestFlags::ServerContext \
	| EAutomationTestFlags::CommandletContext \
	| EAutomationTestFlags::ProductFilter)
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaSecurityServerSecretTest,
	"Valhalla.Game.Security.ServerSecret",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaSecurityServerSecretTest::RunTest(const FString& /*Parameters*/)
{
	using ESrc = EValhallaSecretSource;
	const FString None;

	// ── Resolution order: first non-empty source wins ────────────────────
	ESrc Source = ESrc::None;
	TestEqual(TEXT("command line wins over everything"),
		UValhallaDataSettings::ResolveServerSecret(TEXT("cli"), TEXT("env"), TEXT("file"), TEXT("ini"), true, Source), FString(TEXT("cli")));
	TestTrue(TEXT("... source CommandLine"), Source == ESrc::CommandLine);

	TestEqual(TEXT("then the environment"),
		UValhallaDataSettings::ResolveServerSecret(None, TEXT("env"), TEXT("file"), TEXT("ini"), true, Source), FString(TEXT("env")));
	TestTrue(TEXT("... source Environment"), Source == ESrc::Environment);

	TestEqual(TEXT("then secrets.local.env"),
		UValhallaDataSettings::ResolveServerSecret(None, None, TEXT("file"), TEXT("ini"), true, Source), FString(TEXT("file")));
	TestTrue(TEXT("... source SecretsFile"), Source == ESrc::SecretsFile);

	TestEqual(TEXT("then the legacy ini value"),
		UValhallaDataSettings::ResolveServerSecret(None, None, None, TEXT("ini"), true, Source), FString(TEXT("ini")));
	TestTrue(TEXT("... source LegacyIni (GetServerSecret warns for it)"), Source == ESrc::LegacyIni);

	TestEqual(TEXT("whitespace-only counts as empty"),
		UValhallaDataSettings::ResolveServerSecret(TEXT("  "), TEXT(" "), TEXT("file"), None, true, Source), FString(TEXT("file")));

	TestEqual(TEXT("nothing, editor build: the dev default"),
		UValhallaDataSettings::ResolveServerSecret(None, None, None, None, true, Source), FString(UValhallaDataSettings::GetDevServerSecret()));
	TestTrue(TEXT("... source DevDefault"), Source == ESrc::DevDefault);

	TestEqual(TEXT("nothing, non-editor build: empty"),
		UValhallaDataSettings::ResolveServerSecret(None, None, None, None, false, Source), FString());
	TestTrue(TEXT("... source None"), Source == ESrc::None);

	TestTrue(TEXT("dev-server-secret is recognised"), UValhallaDataSettings::IsDevServerSecret(TEXT("dev-server-secret")));
	TestFalse(TEXT("a real secret is not"), UValhallaDataSettings::IsDevServerSecret(TEXT("3f9c0d1e2b")));

	// ── Which servers must refuse the dev default ───────────────────────
	//                                                  NetMode             dedicated GIsEditor editorBuild
	TestTrue(TEXT("UnrealEditor.exe -server (editor build, GIsEditor false) refuses"),
		AValhallaGameMode::RequiresRealServerSecret(NM_DedicatedServer, true, false, true));
	TestTrue(TEXT("cooked dedicated server refuses"),
		AValhallaGameMode::RequiresRealServerSecret(NM_DedicatedServer, true, false, false));
	TestTrue(TEXT("packaged listen server refuses"),
		AValhallaGameMode::RequiresRealServerSecret(NM_ListenServer, false, false, false));
	TestFalse(TEXT("listen server in an editor build (-game) keeps the default"),
		AValhallaGameMode::RequiresRealServerSecret(NM_ListenServer, false, false, true));
	TestFalse(TEXT("PIE separate server (in-process, GIsEditor true) keeps the default"),
		AValhallaGameMode::RequiresRealServerSecret(NM_DedicatedServer, false, true, true));
	TestFalse(TEXT("PIE listen server keeps the default"),
		AValhallaGameMode::RequiresRealServerSecret(NM_ListenServer, false, true, true));
	TestFalse(TEXT("standalone never needs one"),
		AValhallaGameMode::RequiresRealServerSecret(NM_Standalone, false, false, false));

	// ── The Join request line: tokens cut to 8 characters ───────────────
	const FString Jwt(TEXT("eyJhbGciOiJIUzI1NiJ9.eyJ1c2VySWQiOjF9.c2lnbmF0dXJlc2lnbmF0dXJl"));
	const FString Redacted = AValhallaGameMode::RedactJoinOptions(FString::Printf(TEXT("?Name=P?token=%s?characterId=3"), *Jwt));
	TestFalse(TEXT("the full token is gone"), Redacted.Contains(Jwt));
	TestFalse(TEXT("so is its signature"), Redacted.Contains(TEXT("c2lnbmF0dXJl")));
	TestTrue(TEXT("its first 8 characters stay"), Redacted.Contains(TEXT("token=eyJhbGci")));
	TestTrue(TEXT("the other options survive"), Redacted.StartsWith(TEXT("?Name=P?token=")) && Redacted.EndsWith(TEXT("?characterId=3")));
	TestFalse(TEXT("'&'-separated token redacted too"),
		AValhallaGameMode::RedactJoinOptions(FString::Printf(TEXT("?token=%s&characterId=3"), *Jwt)).Contains(Jwt));
	TestFalse(TEXT("token as the last option redacted"),
		AValhallaGameMode::RedactJoinOptions(FString::Printf(TEXT("?class=wizard?token=%s"), *Jwt)).Contains(Jwt));
	TestEqual(TEXT("options without a token are unchanged"),
		AValhallaGameMode::RedactJoinOptions(TEXT("?class=wizard?charname=Gandalf")), FString(TEXT("?class=wizard?charname=Gandalf")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
