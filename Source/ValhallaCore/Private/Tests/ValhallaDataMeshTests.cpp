// Copyright Valhalla 2.0. All Rights Reserved.
//
// Phase 6b: `meshId`, and the rule that decides which id the art comes from.
//
// This one fails quietly in the worst way a content bug can. An item whose
// `meshId` is silently not read falls back to its `spriteId` and resolves to a
// *real, existing* asset — the wrong one, the one some other item was grouped
// with under the 1.0 sprite sheets. Nothing logs, nothing is missing, and the
// symptom is "the iron dagger looks like a sword", which is the sort of thing
// that gets filed as an art bug and looked at by the wrong person for a week.
//
// So the test does two separate things, and both matter:
//
//   1. It proves the JSON *key* is read, by loading a data root in which one
//      item has a `meshId` that nothing else in the project has. A loader that
//      dropped the field would leave it empty and this fails.
//   2. It proves the *fallback* is a fallback and not a replacement: an item
//      with no `meshId` still resolves through `spriteId`, which is every item
//      in the shipping 1.0 data and therefore the case that must not break.
//
// Run from the editor's Session Frontend, or headless:
//   UnrealEditor-Cmd.exe <project>.uproject -ExecCmds="Automation RunTests Valhalla.; Quit" -unattended -nullrhi

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ValhallaDataSettings.h"
#include "ValhallaDataSubsystem.h"
#include "ValhallaTypes.h"

#ifndef VALHALLA_TEST_FLAGS
#define VALHALLA_TEST_FLAGS ( \
	EAutomationTestFlags::EditorContext \
	| EAutomationTestFlags::ClientContext \
	| EAutomationTestFlags::ServerContext \
	| EAutomationTestFlags::CommandletContext \
	| EAutomationTestFlags::ProductFilter)
#endif

namespace ValhallaMeshIdTests
{
	/**
	 * The mesh id injected into the scratch copy of items.json.
	 *
	 * Deliberately not a real asset name: the test is about the data layer, and
	 * a value that happens to name something on disk would let a bug that
	 * resolved the wrong field still pass by coincidence.
	 */
	static const TCHAR* InjectedMeshId = TEXT("test_only_meshid_marker");

	/** Where the scratch data root is built. Removed at the end of the test. */
	static FString ScratchRoot()
	{
		return FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("ValhallaTests"), TEXT("MeshIdFallback"));
	}

	/**
	 * Copy the real data root into a scratch directory, adding `meshId` to one
	 * item on the way through.
	 *
	 * The other six files are copied byte for byte rather than regenerated:
	 * `LoadTablesFromRoot` wants all seven present, and a scratch classes.json
	 * written by this test would be one more thing that could be wrong.
	 *
	 * @param OutItemId  The id of the item that was given a meshId.
	 */
	static bool BuildScratchRoot(FAutomationTestBase& Test, const FString& RealRoot, const FString& Scratch, FName& OutItemId)
	{
		IFileManager& FileManager = IFileManager::Get();
		FileManager.DeleteDirectory(*Scratch, /*RequireExists=*/false, /*Tree=*/true);
		if (!FileManager.MakeDirectory(*Scratch, /*Tree=*/true))
		{
			Test.AddError(FString::Printf(TEXT("could not create the scratch data root '%s'."), *Scratch));
			return false;
		}

		for (const FString& Filename : UValhallaDataSubsystem::GetDataFilenames())
		{
			const FString Source = FPaths::Combine(RealRoot, Filename);
			const FString Target = FPaths::Combine(Scratch, Filename);

			if (Filename != TEXT("items.json"))
			{
				if (FileManager.Copy(*Target, *Source) != COPY_OK)
				{
					Test.AddError(FString::Printf(TEXT("could not copy '%s' into the scratch root."), *Filename));
					return false;
				}
				continue;
			}

			// items.json, rewritten.
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *Source))
			{
				Test.AddError(FString::Printf(TEXT("could not read '%s'."), *Source));
				return false;
			}

			TSharedPtr<FJsonObject> Root;
			const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Text);
			if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
			{
				Test.AddError(TEXT("items.json did not parse as JSON."));
				return false;
			}

			const TSharedPtr<FJsonObject>* Items = nullptr;
			if (!Root->TryGetObjectField(TEXT("items"), Items) || !Items || !Items->IsValid())
			{
				Test.AddError(TEXT("items.json has no `items` object."));
				return false;
			}

			// The first item that has a spriteId, so the test can also check
			// that meshId *wins* over a spriteId that is really there.
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Items)->Values)
			{
				const TSharedPtr<FJsonObject>* ItemObj = nullptr;
				if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(ItemObj) || !ItemObj)
				{
					continue;
				}
				FString SpriteId;
				if ((*ItemObj)->TryGetStringField(TEXT("spriteId"), SpriteId) && !SpriteId.IsEmpty())
				{
					(*ItemObj)->SetStringField(TEXT("meshId"), InjectedMeshId);
					OutItemId = FName(*Pair.Key);
					break;
				}
			}

			if (OutItemId.IsNone())
			{
				Test.AddError(TEXT("items.json has no item with a spriteId to hang the test on."));
				return false;
			}

			FString Rewritten;
			const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Rewritten);
			FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

			if (!FFileHelper::SaveStringToFile(Rewritten, *Target))
			{
				Test.AddError(FString::Printf(TEXT("could not write '%s'."), *Target));
				return false;
			}
		}

		return true;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Valhalla.Core.Data.MeshIdFallback
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaDataMeshIdFallbackTest,
	"Valhalla.Core.Data.MeshIdFallback",
	VALHALLA_TEST_FLAGS)

bool FValhallaDataMeshIdFallbackTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ValhallaMeshIdTests;

	// ── 1. The rule, on data that needs no files at all ───────────────────
	{
		FValhallaItemTemplate Item;
		Item.SpriteId = TEXT("sword_iron");
		TestEqual(TEXT("no meshId: the art id is the spriteId"), Item.GetArtId(), FString(TEXT("sword_iron")));

		Item.MeshId = TEXT("dagger_iron");
		TestEqual(TEXT("meshId set: it wins over spriteId"), Item.GetArtId(), FString(TEXT("dagger_iron")));

		Item.MeshId.Reset();
		TestEqual(TEXT("meshId cleared: back to the spriteId"), Item.GetArtId(), FString(TEXT("sword_iron")));

		FValhallaItemTemplate Ring;
		TestTrue(TEXT("neither set (a ring): no art id at all"), Ring.GetArtId().IsEmpty());
	}

	// ── 2. The shipping data: every item still falls back ─────────────────
	const FString RealRoot = UValhallaDataSettings::Get()
		? UValhallaDataSettings::Get()->GetResolvedDataRoot()
		: UValhallaDataSettings::ResolveDataRoot(FString());

	FValhallaDataTables RealTables;
	if (!UValhallaDataSubsystem::LoadTablesFromRoot(RealRoot, RealTables))
	{
		AddError(FString::Printf(TEXT("could not load the Valhalla data from '%s'."), *RealRoot));
		return false;
	}

	int32 Checked = 0;
	for (const TPair<FName, FValhallaItemTemplate>& Pair : RealTables.Items)
	{
		// No 1.0 item authors a meshId — the field is 2.0's, and the day one
		// does, this line is the deliberate place to update.
		TestTrue(
			FString::Printf(TEXT("items.json item '%s' authors no meshId"), *Pair.Key.ToString()),
			Pair.Value.MeshId.IsEmpty());

		TestEqual(
			FString::Printf(TEXT("items.json item '%s' resolves art through spriteId"), *Pair.Key.ToString()),
			Pair.Value.GetArtId(), Pair.Value.SpriteId);
		++Checked;
	}
	TestTrue(TEXT("items.json actually had items in it"), Checked > 0);

	// ── 3. The key is read ────────────────────────────────────────────────
	const FString Scratch = ScratchRoot();
	FName MarkedItemId;
	if (!BuildScratchRoot(*this, RealRoot, Scratch, MarkedItemId))
	{
		IFileManager::Get().DeleteDirectory(*Scratch, false, true);
		return false;
	}

	FValhallaDataTables ScratchTables;
	const bool bLoaded = UValhallaDataSubsystem::LoadTablesFromRoot(Scratch, ScratchTables);
	TestTrue(TEXT("the scratch data root loads"), bLoaded);

	if (const FValhallaItemTemplate* Marked = ScratchTables.Items.Find(MarkedItemId))
	{
		TestEqual(
			FString::Printf(TEXT("'%s' read its meshId out of the JSON"), *MarkedItemId.ToString()),
			Marked->MeshId, FString(InjectedMeshId));

		TestEqual(
			FString::Printf(TEXT("'%s' resolves art through meshId, not spriteId"), *MarkedItemId.ToString()),
			Marked->GetArtId(), FString(InjectedMeshId));

		TestFalse(
			TEXT("the spriteId is still there — meshId overrides it, it does not erase it"),
			Marked->SpriteId.IsEmpty());
	}
	else
	{
		AddError(FString::Printf(TEXT("the scratch items.json lost item '%s'."), *MarkedItemId.ToString()));
	}

	// Every *other* item in the same file is unaffected: a loader that wrote
	// the field onto the wrong struct, or onto all of them, fails here.
	int32 OthersWithMeshId = 0;
	for (const TPair<FName, FValhallaItemTemplate>& Pair : ScratchTables.Items)
	{
		if (Pair.Key != MarkedItemId && !Pair.Value.MeshId.IsEmpty())
		{
			++OthersWithMeshId;
		}
	}
	TestEqual(TEXT("exactly one item picked up a meshId"), OthersWithMeshId, 0);

	IFileManager::Get().DeleteDirectory(*Scratch, false, true);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
