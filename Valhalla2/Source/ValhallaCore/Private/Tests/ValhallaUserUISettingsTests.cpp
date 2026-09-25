// Copyright Valhalla 2.0. All Rights Reserved.
//
// B-21: FValhallaUserUISettings, the per-character UI settings document.
//
//   Valhalla.Core.UISettings.RoundTrip   — every field out to JSON and back:
//     panels, colours (as "#rrggbbaa"), vectors (as [x, y]), log filters,
//     unknown fields kept; the writer's output is stable.
//   Valhalla.Core.UISettings.Defaults    — empty, corrupt and non-object text
//     load as the defaults; wrong-typed fields keep their defaults with a
//     warning; out-of-range values are clamped; ResetSection.
//   Valhalla.Core.UISettings.ForwardVersion — a Version 7 document with fields
//     this build does not know loads every field it does know, and writes the
//     unknown ones (and its version) back.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "ValhallaUserUISettings.h"

#include <limits>

#define VALHALLA_UI_SETTINGS_TEST_FLAGS ( \
	EAutomationTestFlags::EditorContext \
	| EAutomationTestFlags::ClientContext \
	| EAutomationTestFlags::ProductFilter)

namespace ValhallaUISettingsTests
{
	static TSharedPtr<FJsonObject> Parse(const FString& Text)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Text);
		FJsonSerializer::Deserialize(Reader, Root);
		return Root;
	}

	static FValhallaUserUISettings MakeEverything()
	{
		FValhallaUserUISettings S;
		S.UpdatedAt = TEXT("2026-09-24T17:10:17.252Z");
		S.bLocked = false;
		S.UiScale = 1.25f;
		S.PanelOpacity = 0.6f;
		S.PanelBorder = 6.f;

		FValhallaPanelLayout Chat;
		Chat.AnchorMin = FVector2D(0.0, 1.0);
		Chat.AnchorMax = FVector2D(0.0, 1.0);
		Chat.Alignment = FVector2D(0.0, 1.0);
		Chat.Position = FVector2D(300.0, -8.0);
		Chat.Size = FVector2D(420.0, 200.0);
		Chat.Scale = 1.1f;
		Chat.bVisible = true;
		Chat.bSet = true;
		S.Panels.Add(TEXT("Chat"), Chat);

		FValhallaPanelLayout Party;
		Party.AnchorMin = FVector2D(1.0, 0.5);
		Party.AnchorMax = FVector2D(1.0, 0.5);
		Party.Alignment = FVector2D(1.0, 0.5);
		Party.Position = FVector2D(-12.5, 40.25);
		Party.Scale = 0.75f;
		Party.bVisible = false;
		Party.bSet = true;
		S.Panels.Add(TEXT("Party"), Party);

		FValhallaPanelLayout Unset;
		Unset.Position = FVector2D(1.0, 2.0);
		S.Panels.Add(TEXT("Loot"), Unset);

		S.Colours.Add(TEXT("HpHighColour"), FLinearColor::FromSRGBColor(FColor(0x44, 0xff, 0x44, 0xff)));
		S.Colours.Add(TEXT("CooldownColour"), FLinearColor::FromSRGBColor(FColor(0x88, 0x00, 0x00, 0x99)));
		S.ChatFontSize = 10;
		S.ChatVisibleLines = 12;
		S.bChatTimestamps = true;
		S.LogFilters.Add(TEXT("misses"), false);
		S.LogFilters.Add(TEXT("outDmg"), true);
		S.bShowNpcNameplates = false;
		S.bShowPlayerNameplates = false;
		S.bFloatingCombatText = false;
		S.NameplateFontSize = 14;
		return S;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Valhalla.Core.UISettings.RoundTrip
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValhallaUISettingsRoundTripTest, "Valhalla.Core.UISettings.RoundTrip",
	VALHALLA_UI_SETTINGS_TEST_FLAGS)

bool FValhallaUISettingsRoundTripTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ValhallaUISettingsTests;

	const FValhallaUserUISettings Original = MakeEverything();
	const FString Text = Original.ToJsonString();

	FValhallaUserUISettings Loaded;
	TArray<FString> Warnings;
	TestTrue(TEXT("parses"), FValhallaUserUISettings::FromJsonString(Text, Loaded, &Warnings));
	TestEqual(TEXT("no warnings on its own output"), Warnings.Num(), 0);
	TestTrue(TEXT("every field survives the round trip"), Loaded.EquivalentTo(Original));
	TestEqual(TEXT("UpdatedAt survives"), Loaded.UpdatedAt, Original.UpdatedAt);
	TestEqual(TEXT("PanelBorder survives"), Loaded.PanelBorder, 6.f);
	TestEqual(TEXT("Version"), Loaded.Version, FValhallaUserUISettings::CurrentVersion);
	TestEqual(TEXT("three panel entries"), Loaded.Panels.Num(), 3);
	TestNotNull(TEXT("Chat is set"), Loaded.FindSetPanel(TEXT("Chat")));
	TestNull(TEXT("Loot is present but not set"), Loaded.FindSetPanel(TEXT("Loot")));
	if (const FValhallaPanelLayout* Party = Loaded.Panels.Find(TEXT("Party")))
	{
		TestTrue(TEXT("Party position (fractions kept)"), Party->Position.Equals(FVector2D(-12.5, 40.25), 1.e-4));
		TestFalse(TEXT("Party hidden"), Party->bVisible);
		TestTrue(TEXT("Party anchors"), Party->AnchorMin.Equals(FVector2D(1.0, 0.5)) && Party->Alignment.Equals(FVector2D(1.0, 0.5)));
	}
	TestTrue(TEXT("a pretty and a condensed write parse the same"), [&]()
	{
		FValhallaUserUISettings Condensed;
		return FValhallaUserUISettings::FromJsonString(Original.ToJsonString(false), Condensed) && Condensed.EquivalentTo(Original);
	}());
	TestEqual(TEXT("the writer is stable (same settings, same bytes)"), Loaded.ToJsonString(), Text);

	// The wire shapes.
	const TSharedPtr<FJsonObject> Json = Parse(Text);
	TestTrue(TEXT("output is a JSON object"), Json.IsValid());
	if (Json.IsValid())
	{
		const TSharedPtr<FJsonObject>* Colours = nullptr;
		FString Hp, Cooldown;
		TestTrue(TEXT("Colours is an object"), Json->TryGetObjectField(TEXT("Colours"), Colours) && Colours);
		if (Colours)
		{
			(*Colours)->TryGetStringField(TEXT("HpHighColour"), Hp);
			(*Colours)->TryGetStringField(TEXT("CooldownColour"), Cooldown);
		}
		TestEqual(TEXT("a colour is sRGB #rrggbbaa"), Hp, FString(TEXT("#44ff44ff")));
		TestEqual(TEXT("alpha is kept"), Cooldown, FString(TEXT("#88000099")));

		const TSharedPtr<FJsonObject>* Panels = nullptr;
		const TSharedPtr<FJsonObject>* Chat = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Position = nullptr;
		TestTrue(TEXT("Panels.Chat is an object"), Json->TryGetObjectField(TEXT("Panels"), Panels) && Panels
			&& (*Panels)->TryGetObjectField(TEXT("Chat"), Chat) && Chat);
		TestTrue(TEXT("a vector is [x, y]"), Chat && (*Chat)->TryGetArrayField(TEXT("Position"), Position) && Position && Position->Num() == 2
			&& FMath::IsNearlyEqual((*Position)[0]->AsNumber(), 300.0) && FMath::IsNearlyEqual((*Position)[1]->AsNumber(), -8.0));

		const TSharedPtr<FJsonObject>* Filters = nullptr;
		bool bMisses = true;
		TestTrue(TEXT("LogFilters is key -> bool"), Json->TryGetObjectField(TEXT("LogFilters"), Filters) && Filters
			&& (*Filters)->TryGetBoolField(TEXT("misses"), bMisses) && !bMisses);
	}

	// Colour helpers.
	FLinearColor Colour;
	TestTrue(TEXT("#rrggbb parses (alpha 1)"), FValhallaUserUISettings::ColourFromHex(TEXT("#ff8800"), Colour) && FMath::IsNearlyEqual(Colour.A, 1.f));
	TestEqual(TEXT("and writes back with its alpha"), FValhallaUserUISettings::ColourToHex(Colour), FString(TEXT("#ff8800ff")));
	TestTrue(TEXT("without '#'"), FValhallaUserUISettings::ColourFromHex(TEXT("112233cc"), Colour));
	TestFalse(TEXT("#rgb is refused"), FValhallaUserUISettings::ColourFromHex(TEXT("#f80"), Colour));
	TestFalse(TEXT("non-hex is refused"), FValhallaUserUISettings::ColourFromHex(TEXT("#gg0000ff"), Colour));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Valhalla.Core.UISettings.Defaults
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValhallaUISettingsDefaultsTest, "Valhalla.Core.UISettings.Defaults",
	VALHALLA_UI_SETTINGS_TEST_FLAGS)

bool FValhallaUISettingsDefaultsTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ValhallaUISettingsTests;

	const FValhallaUserUISettings Defaults;
	TestTrue(TEXT("locked by default"), Defaults.bLocked);
	TestEqual(TEXT("scale 1"), Defaults.UiScale, 1.f);
	TestEqual(TEXT("opacity 1"), Defaults.PanelOpacity, 1.f);
	TestEqual(TEXT("border 0 (the HUD's default)"), Defaults.PanelBorder, 0.f);
	TestTrue(TEXT("nameplates and floaters on"), Defaults.bShowNpcNameplates && Defaults.bShowPlayerNameplates && Defaults.bFloatingCombatText);
	TestTrue(TEXT("no overrides"), Defaults.Panels.Num() == 0 && Defaults.Colours.Num() == 0 && Defaults.LogFilters.Num() == 0);
	TestTrue(TEXT("never changed"), Defaults.UpdatedAt.IsEmpty() && Defaults.GetUpdatedAtTime() == FDateTime::MinValue());

	// Text that is not a settings object at all.
	for (const TCHAR* Bad : { TEXT(""), TEXT("   "), TEXT("{ not json"), TEXT("[1, 2, 3]"), TEXT("42"), TEXT("\"text\""), TEXT("{\"UiScale\": 1.5,") })
	{
		FValhallaUserUISettings Out = MakeEverything();
		TArray<FString> Warnings;
		const bool bParsed = FValhallaUserUISettings::FromJsonString(Bad, Out, &Warnings);
		TestFalse(FString::Printf(TEXT("'%s' is refused"), Bad), bParsed);
		TestTrue(FString::Printf(TEXT("'%s' leaves the defaults"), Bad), Out.EquivalentTo(Defaults) && Out.UpdatedAt.IsEmpty());
		TestTrue(FString::Printf(TEXT("'%s' says why"), Bad), Warnings.Num() == 1);
	}

	FValhallaUserUISettings Empty;
	TestTrue(TEXT("{} parses"), FValhallaUserUISettings::FromJsonString(TEXT("{}"), Empty));
	TestTrue(TEXT("{} is the defaults"), Empty.EquivalentTo(Defaults));
	TestFalse(TEXT("a null object is refused"), FValhallaUserUISettings::FromJson(nullptr, Empty));

	// Wrong types keep their defaults; out-of-range values clamp; one good field still lands.
	const FString Mixed = TEXT(R"({
		"bLocked": "no",
		"UiScale": 9,
		"PanelOpacity": 0.05,
		"PanelBorder": 40,
		"ChatFontSize": "big",
		"ChatVisibleLines": 5000,
		"NameplateFontSize": -3,
		"bChatTimestamps": true,
		"Panels": {
			"Chat": { "Position": [10], "Scale": 99, "bSet": true, "AnchorMin": [2, -1] },
			"Vitals": "left",
			"Party": { "Position": ["a", 1], "bSet": 1 }
		},
		"Colours": { "HpHighColour": "green", "ManaColour": "#4488ffff", "LabelColour": 5 },
		"LogFilters": { "misses": "off", "heals": false },
		"UpdatedAt": 12
	})");
	FValhallaUserUISettings Out;
	TArray<FString> Warnings;
	TestTrue(TEXT("a mixed document parses"), FValhallaUserUISettings::FromJsonString(Mixed, Out, &Warnings));
	TestTrue(TEXT("bLocked kept its default"), Out.bLocked);
	TestEqual(TEXT("UiScale clamped to 2"), Out.UiScale, FValhallaUserUISettings::MaxUiScale);
	TestEqual(TEXT("PanelOpacity clamped to 0.2"), Out.PanelOpacity, FValhallaUserUISettings::MinPanelOpacity);
	TestEqual(TEXT("PanelBorder clamped to 12"), Out.PanelBorder, FValhallaUserUISettings::MaxPanelBorder);
	TestEqual(TEXT("ChatFontSize kept its default"), Out.ChatFontSize, 0);
	TestEqual(TEXT("ChatVisibleLines clamped"), Out.ChatVisibleLines, 100);
	TestEqual(TEXT("NameplateFontSize clamped to 0"), Out.NameplateFontSize, 0);
	TestTrue(TEXT("the good field landed"), Out.bChatTimestamps);
	TestTrue(TEXT("UpdatedAt of the wrong type is empty"), Out.UpdatedAt.IsEmpty());
	TestEqual(TEXT("two panel objects kept (Vitals was not an object)"), Out.Panels.Num(), 2);
	if (const FValhallaPanelLayout* Chat = Out.Panels.Find(TEXT("Chat")))
	{
		TestTrue(TEXT("a one-number position keeps the default"), Chat->Position.Equals(FVector2D::ZeroVector));
		TestEqual(TEXT("panel scale clamped to 4"), Chat->Scale, FValhallaPanelLayout::MaxScale);
		TestTrue(TEXT("anchors clamped to 0..1"), Chat->AnchorMin.Equals(FVector2D(1.0, 0.0)));
		TestTrue(TEXT("bSet read"), Chat->bSet);
	}
	if (const FValhallaPanelLayout* Party = Out.Panels.Find(TEXT("Party")))
	{
		TestFalse(TEXT("bSet of the wrong type keeps false"), Party->bSet);
	}
	TestEqual(TEXT("only the good colour"), Out.Colours.Num(), 1);
	TestTrue(TEXT("ManaColour read"), Out.Colours.Contains(TEXT("ManaColour")));
	TestEqual(TEXT("only the boolean filter"), Out.LogFilters.Num(), 1);
	TestTrue(TEXT("warnings name the bad fields"), Warnings.Num() >= 9);
	const FString AllWarnings = FString::Join(Warnings, TEXT("\n"));
	TestTrue(TEXT("a warning names bLocked"), AllWarnings.Contains(TEXT("bLocked")));
	TestTrue(TEXT("a warning names Panels.Vitals"), AllWarnings.Contains(TEXT("Panels.Vitals")));
	TestTrue(TEXT("a warning names Colours.HpHighColour"), AllWarnings.Contains(TEXT("Colours.HpHighColour")));

	// NaN never survives Sanitize.
	FValhallaUserUISettings Nan;
	Nan.UiScale = std::numeric_limits<float>::quiet_NaN();
	Nan.PanelBorder = std::numeric_limits<float>::quiet_NaN();
	FValhallaPanelLayout NanPanel;
	NanPanel.Scale = std::numeric_limits<float>::quiet_NaN();
	NanPanel.Position = FVector2D(std::numeric_limits<double>::infinity(), 3.0);
	Nan.Panels.Add(TEXT("Chat"), NanPanel);
	Nan.Sanitize();
	TestEqual(TEXT("NaN UiScale -> 1"), Nan.UiScale, 1.f);
	TestEqual(TEXT("NaN PanelBorder -> 0 (the default)"), Nan.PanelBorder, 0.f);
	FValhallaUserUISettings Thin;
	Thin.PanelBorder = 0.3f;
	Thin.Sanitize();
	TestEqual(TEXT("a sliver of a border clamps up to 1 px"), Thin.PanelBorder, FValhallaUserUISettings::MinPanelBorder);
	Thin.PanelBorder = -5.f;
	Thin.Sanitize();
	TestEqual(TEXT("a negative border is the default"), Thin.PanelBorder, 0.f);
	TestEqual(TEXT("NaN panel scale -> 1"), Nan.Panels[TEXT("Chat")].Scale, 1.f);
	TestTrue(TEXT("infinite position -> 0"), Nan.Panels[TEXT("Chat")].Position.Equals(FVector2D(0.0, 3.0)));

	// ResetSection.
	FValhallaUserUISettings Reset = MakeEverything();
	Reset.ResetSection(EValhallaUISettingsSection::Layout);
	TestEqual(TEXT("Layout clears the panels"), Reset.Panels.Num(), 0);
	TestEqual(TEXT("and leaves the colours"), Reset.Colours.Num(), 2);
	Reset.ResetSection(EValhallaUISettingsSection::Style);
	TestTrue(TEXT("Style clears scale, opacity, border, colours"),
		Reset.UiScale == 1.f && Reset.PanelOpacity == 1.f && Reset.PanelBorder == 0.f && Reset.Colours.Num() == 0);
	TestFalse(TEXT("and leaves the lock"), Reset.bLocked);
	Reset.ResetSection(EValhallaUISettingsSection::Chat);
	TestTrue(TEXT("Chat clears the chat fields and filters"), Reset.ChatFontSize == 0 && !Reset.bChatTimestamps && Reset.LogFilters.Num() == 0);
	Reset.ResetSection(EValhallaUISettingsSection::Nameplates);
	TestTrue(TEXT("Nameplates restores the switches"), Reset.bShowNpcNameplates && Reset.bFloatingCombatText && Reset.NameplateFontSize == 0);
	FValhallaUserUISettings All = MakeEverything();
	All.ResetSection(EValhallaUISettingsSection::All);
	TestTrue(TEXT("All is the defaults"), All.EquivalentTo(Defaults));
	TestTrue(TEXT("All keeps the stamp for the caller"), All.UpdatedAt == MakeEverything().UpdatedAt);

	// Touch stamps a parsable UTC time.
	FValhallaUserUISettings Touched;
	Touched.Touch();
	TestTrue(TEXT("Touch writes ISO 8601"), Touched.GetUpdatedAtTime() > FDateTime(2026, 1, 1));
	TestTrue(TEXT("and it round-trips"), [&]()
	{
		FValhallaUserUISettings Back;
		return FValhallaUserUISettings::FromJsonString(Touched.ToJsonString(), Back) && Back.GetUpdatedAtTime() == Touched.GetUpdatedAtTime();
	}());
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Valhalla.Core.UISettings.ForwardVersion
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValhallaUISettingsForwardVersionTest, "Valhalla.Core.UISettings.ForwardVersion",
	VALHALLA_UI_SETTINGS_TEST_FLAGS)

bool FValhallaUISettingsForwardVersionTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ValhallaUISettingsTests;

	const FString Future = TEXT(R"({
		"Version": 7,
		"UpdatedAt": "2031-01-02T03:04:05.000Z",
		"UiScale": 1.5,
		"Panels": { "Chat": { "Position": [300, -8], "bSet": true, "Snap": "grid", "Docked": { "to": "Vitals" } } },
		"Colours": { "HpHighColour": "#00ff00ff" },
		"Keybinds": { "Jump": "Space" },
		"Theme": "brass",
		"ChatTabs": [ "General", "Combat" ]
	})");
	FValhallaUserUISettings Out;
	TArray<FString> Warnings;
	TestTrue(TEXT("a newer version parses"), FValhallaUserUISettings::FromJsonString(Future, Out, &Warnings));
	TestEqual(TEXT("its version is remembered"), Out.Version, 7);
	TestEqual(TEXT("UiScale read"), Out.UiScale, 1.5f);
	TestTrue(TEXT("the known panel fields read"), Out.FindSetPanel(TEXT("Chat")) && Out.FindSetPanel(TEXT("Chat"))->Position.Equals(FVector2D(300.0, -8.0)));
	TestTrue(TEXT("colour read"), Out.Colours.Contains(TEXT("HpHighColour")));
	TestEqual(TEXT("three unknown top-level fields kept"), Out.UnknownFields.Num(), 3);
	TestTrue(TEXT("one warning: the version"), Warnings.Num() == 1 && Warnings[0].Contains(TEXT("newer")));

	const TSharedPtr<FJsonObject> Written = Parse(Out.ToJsonString());
	TestTrue(TEXT("writes back"), Written.IsValid());
	if (Written.IsValid())
	{
		double Version = 0.0;
		FString Theme;
		const TSharedPtr<FJsonObject>* Keybinds = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Tabs = nullptr;
		TestTrue(TEXT("the newer version number is written back"), Written->TryGetNumberField(TEXT("Version"), Version) && Version == 7.0);
		TestTrue(TEXT("an unknown string is written back"), Written->TryGetStringField(TEXT("Theme"), Theme) && Theme == TEXT("brass"));
		TestTrue(TEXT("an unknown object is written back"), Written->TryGetObjectField(TEXT("Keybinds"), Keybinds) && Keybinds);
		TestTrue(TEXT("an unknown array is written back"), Written->TryGetArrayField(TEXT("ChatTabs"), Tabs) && Tabs && Tabs->Num() == 2);
	}

	// A reset keeps a newer build's fields: they are not this build's to drop.
	Out.ResetSection(EValhallaUISettingsSection::All);
	TestEqual(TEXT("ResetSection(All) keeps the unknown fields"), Out.UnknownFields.Num(), 3);

	// An older document (no Version at all) is version 1.
	FValhallaUserUISettings Old;
	TestTrue(TEXT("no Version parses"), FValhallaUserUISettings::FromJsonString(TEXT("{\"bLocked\": false}"), Old));
	TestEqual(TEXT("and is version 1"), Old.Version, 1);
	TestFalse(TEXT("its field read"), Old.bLocked);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
