// Copyright Valhalla 2.0. All Rights Reserved.
//
// Phase 8b: the two pure halves of the game HUD.
//
//   Valhalla.Game.UI.ConfigParse  — ui-config.json through FValhallaUIConfig:
//     the real file (all seven sections, the numbers the UI Layout editor
//     wrote), a partial file (defaults fill the gaps rather than zeros), and
//     the colour / CSS-size helpers.
//   Valhalla.Game.UI.ChatCommands — the chat box's grammar (GameScene.ts:5222
//     `submitChatInput`): /g /world /p /w /invite /accept /decline /leave, the
//     sticky channel, the usage lines, and Tab's channel cycle.
//   Valhalla.Game.UI.HudBlueprintGroundwork — B-07 step 2: the game HUD
//     class setting defaults to the code-built HUD, the C++ class never lays
//     out from a Blueprint, the designer panel names (which WBP_GameHUD must
//     match) are BindWidget / BindWidgetOptional members of the right types,
//     and the bar widget clamps its fraction.
//
// None needs a world or a viewport; all run headless (the bar test makes one
// bare widget object, never constructed into Slate).

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Components/EditableTextBox.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/Border.h"
#include "Components/UniformGridPanel.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "ValhallaChatCommands.h"
#include "ValhallaDataSettings.h"
#include "ValhallaDataSubsystem.h"
#include "ValhallaGameHUDWidget.h"
#include "ValhallaUIConfig.h"
#include "ValhallaUISettings.h"

#include <limits>

#ifndef VALHALLA_GAME_TEST_FLAGS
#define VALHALLA_GAME_TEST_FLAGS ( \
	EAutomationTestFlags::EditorContext \
	| EAutomationTestFlags::ClientContext \
	| EAutomationTestFlags::ServerContext \
	| EAutomationTestFlags::CommandletContext \
	| EAutomationTestFlags::ProductFilter)
#endif

namespace ValhallaUITests
{
	static TSharedPtr<FJsonObject> ParseJson(const FString& Text)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Text);
		FJsonSerializer::Deserialize(Reader, Root);
		return Root;
	}

	static bool NearlyEqual(const FLinearColor& A, const FLinearColor& B)
	{
		return A.Equals(B, 0.002f);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Valhalla.Game.UI.ConfigParse
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaUIConfigParseTest,
	"Valhalla.Game.UI.ConfigParse",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaUIConfigParseTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ValhallaUITests;

	// ── Helpers ─────────────────────────────────────────────────────────
	TestEqual(TEXT("\"12px\" -> 12"), FValhallaUIConfig::ParsePixelSize(TEXT("12px"), 0), 12);
	TestEqual(TEXT("\" 32px \" -> 32"), FValhallaUIConfig::ParsePixelSize(TEXT(" 32px "), 0), 32);
	TestEqual(TEXT("\"px\" -> fallback"), FValhallaUIConfig::ParsePixelSize(TEXT("px"), 7), 7);

	const FLinearColor Red = FValhallaUIConfig::ParseHexColor(TEXT("#ff0000"), FLinearColor::Black);
	TestTrue(TEXT("#ff0000 is linear red"), NearlyEqual(Red, FLinearColor(1.f, 0.f, 0.f, 1.f)));
	TestTrue(TEXT("#f00 expands like CSS"),
		NearlyEqual(FValhallaUIConfig::ParseHexColor(TEXT("#f00"), FLinearColor::Black), Red));
	TestTrue(TEXT("#888888 is sRGB, stored linear (~0.246)"),
		FMath::IsNearlyEqual(FValhallaUIConfig::ParseHexColor(TEXT("#888888"), FLinearColor::Black).R, 0.2462f, 0.002f));
	TestTrue(TEXT("garbage -> fallback"),
		NearlyEqual(FValhallaUIConfig::ParseHexColor(TEXT("#zzzzzz"), FLinearColor::Green), FLinearColor::Green));

	// ── Defaults (DEFAULT_UI_CONFIG) ────────────────────────────────────
	const FValhallaUIConfig Defaults;
	TestEqual(TEXT("default action bar slot 44"), Defaults.ActionBar.SlotSize, 44.f);
	TestEqual(TEXT("default inventory 8 cols"), Defaults.Inventory.Cols, 8);
	TestTrue(TEXT("default death text #ff4444"),
		NearlyEqual(Defaults.DeathOverlay.TextColor, FValhallaUIConfig::ParseHexColor(TEXT("#ff4444"), FLinearColor::Black)));
	TestFalse(TEXT("a default-constructed config carried no sections"), Defaults.HasAllSections());

	// ── The real file, through the real loader ──────────────────────────
	FString DataRoot;
	if (const UValhallaDataSettings* Settings = UValhallaDataSettings::Get())
	{
		DataRoot = Settings->GetResolvedDataRoot();
	}
	else
	{
		DataRoot = UValhallaDataSettings::ResolveDataRoot(FString());
	}

	FValhallaDataTables Tables;
	if (!UValhallaDataSubsystem::LoadTablesFromRoot(DataRoot, Tables))
	{
		AddError(FString::Printf(TEXT("could not load the 1.0 data from %s"), *DataRoot));
		return false;
	}

	const FValhallaUIConfig& Real = Tables.UIConfigTyped;
	TestTrue(TEXT("ui-config.json carries all seven sections"), Real.HasAllSections());
	TestEqual(TEXT("hud.hpBar.width"), Real.Hud.HpWidth, 200.f);
	TestEqual(TEXT("hud.classText.fontSize \"12px\""), Real.Hud.ClassFontSize, 12);
	TestEqual(TEXT("actionBar.slotSize"), Real.ActionBar.SlotSize, 44.f);
	TestEqual(TEXT("chat.maxMessages"), Real.Chat.MaxMessages, 50);
	TestEqual(TEXT("chat.fontSize \"11px\""), Real.Chat.FontSize, 11);
	TestEqual(TEXT("inventory cols x rows = 32"), Real.Inventory.Cols * Real.Inventory.Rows, 32);
	TestEqual(TEXT("castBar.yAboveActionBar"), Real.CastBar.YAboveActionBar, 12.f);
	TestEqual(TEXT("nameplates.yOffset"), Real.Nameplates.YOffset, -44.f);
	TestEqual(TEXT("deathOverlay.fontSize \"32px\""), Real.DeathOverlay.FontSize, 32);
	TestTrue(TEXT("chat.colors.whisper #ff88cc"),
		NearlyEqual(Real.Chat.Whisper, FValhallaUIConfig::ParseHexColor(TEXT("#ff88cc"), FLinearColor::Black)));

	// ── A partial file keeps the defaults, not zeros ────────────────────
	const TSharedPtr<FJsonObject> Partial = ParseJson(TEXT(
		"{ \"version\": \"9.9\", \"actionBar\": { \"slotSize\": 60, \"colors\": { \"bg\": \"#102030\" } },"
		"  \"deathOverlay\": { \"fontSize\": \"40px\" } }"));
	TestTrue(TEXT("partial JSON parsed"), Partial.IsValid());

	FValhallaUIConfig Parsed;
	const bool bAll = FValhallaUIConfig::Parse(Partial, Parsed);
	TestFalse(TEXT("partial file reports missing sections"), bAll);
	TestTrue(TEXT("actionBar present"), Parsed.bHasActionBar);
	TestFalse(TEXT("chat absent"), Parsed.bHasChat);
	TestEqual(TEXT("version read"), Parsed.Version, FString(TEXT("9.9")));
	TestEqual(TEXT("slotSize overridden"), Parsed.ActionBar.SlotSize, 60.f);
	TestEqual(TEXT("slotGap kept at its default"), Parsed.ActionBar.SlotGap, 4.f);
	TestTrue(TEXT("border kept at its default"),
		NearlyEqual(Parsed.ActionBar.Border, Defaults.ActionBar.Border));
	TestEqual(TEXT("death font overridden"), Parsed.DeathOverlay.FontSize, 40);
	TestEqual(TEXT("death bgAlpha kept"), Parsed.DeathOverlay.BgAlpha, 0.7f);
	TestEqual(TEXT("chat height defaulted"), Parsed.Chat.Height, 170.f);

	// ── No file at all ──────────────────────────────────────────────────
	FValhallaUIConfig FromNothing;
	TestFalse(TEXT("null root -> false"), FValhallaUIConfig::Parse(nullptr, FromNothing));
	TestEqual(TEXT("null root -> defaults"), FromNothing.Inventory.SlotSize, 48.f);

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Valhalla.Game.UI.ChatCommands
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaUIChatCommandsTest,
	"Valhalla.Game.UI.ChatCommands",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaUIChatCommandsTest::RunTest(const FString& /*Parameters*/)
{
	using EAct = EValhallaChatAction;
	using ECh = EValhallaChatChannel;

	auto Check = [this](const TCHAR* Input, ECh Current, EAct Action, ECh Channel, const TCHAR* Text, const TCHAR* Target, bool bSticky)
	{
		const FValhallaChatParse P = ValhallaChat::Parse(Input, Current);
		const FString What = FString::Printf(TEXT("'%s'"), Input);
		TestEqual(*(What + TEXT(" action")), static_cast<int32>(P.Action), static_cast<int32>(Action));
		if (Action == EAct::Send || Action == EAct::Usage || Action == EAct::Unknown || P.bSetsChannel)
		{
			TestEqual(*(What + TEXT(" channel")), static_cast<int32>(P.Channel), static_cast<int32>(Channel));
		}
		TestEqual(*(What + TEXT(" text")), P.Text, FString(Text));
		TestEqual(*(What + TEXT(" target")), P.Target, FString(Target));
		TestTrue(*(What + TEXT(" sticky")), P.bSetsChannel == bSticky);
	};

	// Plain text goes out on the sticky channel; a whisper box falls back to general.
	Check(TEXT("hello there"),       ECh::General, EAct::Send, ECh::General, TEXT("hello there"), TEXT(""), false);
	Check(TEXT("  padded  "),        ECh::World,   EAct::Send, ECh::World,   TEXT("padded"),      TEXT(""), false);
	Check(TEXT("stuck"),             ECh::Whisper, EAct::Send, ECh::General, TEXT("stuck"),       TEXT(""), false);
	Check(TEXT("   "),               ECh::General, EAct::None, ECh::General, TEXT(""),            TEXT(""), false);

	// Channel prefixes: send and stick.
	Check(TEXT("/g hi all"),         ECh::World,   EAct::Send, ECh::General, TEXT("hi all"),      TEXT(""), true);
	Check(TEXT("/world anyone?"),    ECh::General, EAct::Send, ECh::World,   TEXT("anyone?"),     TEXT(""), true);
	Check(TEXT("/y shout"),          ECh::General, EAct::Send, ECh::World,   TEXT("shout"),       TEXT(""), true);
	Check(TEXT("/p pull now"),       ECh::General, EAct::Send, ECh::Party,   TEXT("pull now"),    TEXT(""), true);
	Check(TEXT("/PARTY caps"),       ECh::General, EAct::Send, ECh::Party,   TEXT("caps"),        TEXT(""), true);

	// A bare prefix is a channel switch with nothing sent.
	Check(TEXT("/world"),            ECh::General, EAct::None, ECh::World,   TEXT(""),            TEXT(""), true);
	Check(TEXT("/p"),                ECh::General, EAct::None, ECh::Party,   TEXT(""),            TEXT(""), true);

	// Whispers: name + text, never sticky; incomplete is a usage line.
	Check(TEXT("/w Bjorn got a sec?"), ECh::General, EAct::Send, ECh::Whisper, TEXT("got a sec?"), TEXT("Bjorn"), false);
	Check(TEXT("/whisper Ann hi"),     ECh::Party,   EAct::Send, ECh::Whisper, TEXT("hi"),         TEXT("Ann"),   false);
	Check(TEXT("/w Bjorn"),            ECh::General, EAct::Usage, ECh::System, TEXT("Usage: /w PlayerName message"), TEXT(""), false);
	Check(TEXT("/w"),                  ECh::General, EAct::Usage, ECh::System, TEXT("Usage: /w PlayerName message"), TEXT(""), false);

	// Party verbs.
	Check(TEXT("/invite Freya"),     ECh::General, EAct::PartyInvite,  ECh::General, TEXT(""), TEXT("Freya"), false);
	Check(TEXT("/invite"),           ECh::General, EAct::Usage,        ECh::System,  TEXT("Usage: /invite PlayerName"), TEXT(""), false);
	Check(TEXT("/accept"),           ECh::General, EAct::PartyAccept,  ECh::General, TEXT(""), TEXT(""), false);
	Check(TEXT("/DECLINE"),          ECh::General, EAct::PartyDecline, ECh::General, TEXT(""), TEXT(""), false);
	Check(TEXT("/leave"),            ECh::General, EAct::PartyLeave,   ECh::General, TEXT(""), TEXT(""), false);

	// Anything else with a slash is a typo, shown locally.
	Check(TEXT("/dance wildly"),     ECh::General, EAct::Unknown, ECh::System, TEXT("Unknown command: /dance"), TEXT(""), false);

	// Wire names and Tab's cycle.
	TestEqual(TEXT("general wire"), FString(ValhallaChat::ChannelToWire(ECh::General)), FString(TEXT("general")));
	TestEqual(TEXT("whisper wire"), FString(ValhallaChat::ChannelToWire(ECh::Whisper)), FString(TEXT("whisper")));
	TestEqual(TEXT("party wire"),   FString(ValhallaChat::ChannelToWire(ECh::Party)),   FString(TEXT("party")));
	TestEqual(TEXT("Tab: General -> World"), static_cast<int32>(ValhallaChat::NextChannel(ECh::General)), static_cast<int32>(ECh::World));
	TestEqual(TEXT("Tab: World -> Party"),   static_cast<int32>(ValhallaChat::NextChannel(ECh::World)),   static_cast<int32>(ECh::Party));
	TestEqual(TEXT("Tab: Party -> General"), static_cast<int32>(ValhallaChat::NextChannel(ECh::Party)),   static_cast<int32>(ECh::General));
	TestEqual(TEXT("label"), ValhallaChat::ChannelLabel(ECh::World), FString(TEXT("[World]")));

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Valhalla.Game.UI.HudBlueprintGroundwork (B-07 step 2)
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaUIHudBlueprintGroundworkTest,
	"Valhalla.Game.UI.HudBlueprintGroundwork",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaUIHudBlueprintGroundworkTest::RunTest(const FString& /*Parameters*/)
{
	// ── The game HUD class setting ──────────────────────────────────────
	// Empty until step 3/4 assigns WBP_GameHUD in DefaultGame.ini; flip this
	// check then.
	TestTrue(TEXT("Game HUD Class is empty (the code-built HUD)"), GetDefault<UValhallaUISettings>()->GameHUDClass.IsNull());
	TestTrue(TEXT("no setting, no override -> UValhallaGameHUDWidget"),
		UValhallaUISettings::ResolveGameHUDClass(TSoftClassPtr<UValhallaGameHUDWidget>(), FString()).Get() == UValhallaGameHUDWidget::StaticClass());
	TestTrue(TEXT("a /Script override naming the C++ class resolves to it"),
		UValhallaUISettings::ResolveGameHUDClass(TSoftClassPtr<UValhallaGameHUDWidget>(), TEXT("/Script/ValhallaGame.ValhallaGameHUDWidget")).Get()
			== UValhallaGameHUDWidget::StaticClass());

	// ── The C++ class builds itself ─────────────────────────────────────
	const UValhallaGameHUDWidget* Cdo = GetDefault<UValhallaGameHUDWidget>();
	TestFalse(TEXT("the C++ HUD class never lays out from a Blueprint"), Cdo->WantsLayoutFromBlueprint());
	TestFalse(TEXT("CDO: bLayoutFromBlueprint is false"), Cdo->IsLayoutFromBlueprint());
	TestTrue(TEXT("SlotWidgetClass defaults to the C++ cell"), Cdo->GetSlotWidgetClass().Get() == UValhallaHUDSlotWidget::StaticClass());
	TestTrue(TEXT("BarWidgetClass defaults to the C++ bar"), Cdo->GetBarWidgetClass().Get() == UValhallaHUDBarWidget::StaticClass());

	// ── The designer names step 3's WBP_GameHUD must match ──────────────
	auto CheckPanel = [this](FName Name, bool bRequired)
	{
		const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(UValhallaGameHUDWidget::StaticClass(), Name);
		if (!TestNotNull(*FString::Printf(TEXT("%s is an object property"), *Name.ToString()), Property))
		{
			return;
		}
		TestTrue(*FString::Printf(TEXT("%s is a widget"), *Name.ToString()), Property->PropertyClass->IsChildOf(UWidget::StaticClass()));
#if WITH_EDITORONLY_DATA
		TestTrue(*FString::Printf(TEXT("%s is %s"), *Name.ToString(), bRequired ? TEXT("BindWidget") : TEXT("BindWidgetOptional")),
			Property->HasMetaData(bRequired ? TEXT("BindWidget") : TEXT("BindWidgetOptional")));
#endif
	};
	TestEqual(TEXT("seven required panels"), UValhallaGameHUDWidget::GetRequiredPanelNames().Num(), 7);
	for (const FName Name : UValhallaGameHUDWidget::GetRequiredPanelNames())
	{
		CheckPanel(Name, true);
	}
	for (const FName Name : UValhallaGameHUDWidget::GetOptionalPanelNames())
	{
		CheckPanel(Name, false);
	}
	auto CheckType = [this](const TCHAR* Name, const UClass* Expected)
	{
		const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(UValhallaGameHUDWidget::StaticClass(), Name);
		const UClass* Actual = Property ? static_cast<const UClass*>(Property->PropertyClass) : nullptr;
		TestTrue(*FString::Printf(TEXT("%s is a %s"), Name, *Expected->GetName()), Actual == Expected);
	};
	CheckType(TEXT("VitalsPanel"), UPanelWidget::StaticClass());
	CheckType(TEXT("HpBar"), UValhallaHUDBarWidget::StaticClass());
	CheckType(TEXT("ManaBar"), UValhallaHUDBarWidget::StaticClass());
	CheckType(TEXT("XpBar"), UValhallaHUDBarWidget::StaticClass());
	CheckType(TEXT("ActionBarRow"), UPanelWidget::StaticClass());
	CheckType(TEXT("ChatPanel"), UBorder::StaticClass());
	CheckType(TEXT("ChatScroll"), UScrollBox::StaticClass());
	CheckType(TEXT("ChatInput"), UEditableTextBox::StaticClass());
	CheckType(TEXT("InventoryGrid"), UUniformGridPanel::StaticClass());
	CheckType(TEXT("EquipmentPanel"), UPanelWidget::StaticClass());

	// ── The bar clamps ──────────────────────────────────────────────────
	TestEqual(TEXT("clamp 1.7 -> 1"), UValhallaHUDBarWidget::ClampFraction(1.7f), 1.f);
	TestEqual(TEXT("clamp -0.3 -> 0"), UValhallaHUDBarWidget::ClampFraction(-0.3f), 0.f);
	TestEqual(TEXT("clamp 0.25 -> 0.25"), UValhallaHUDBarWidget::ClampFraction(0.25f), 0.25f);
	TestEqual(TEXT("clamp NaN -> 0"), UValhallaHUDBarWidget::ClampFraction(std::numeric_limits<float>::quiet_NaN()), 0.f);

	// A bare bar (no tree, no Slate) keeps the clamped value and touches nothing.
	UValhallaHUDBarWidget* Bar = NewObject<UValhallaHUDBarWidget>(GetTransientPackage(), NAME_None, RF_Transient);
	Bar->SetFraction(1.5f);
	TestEqual(TEXT("SetFraction(1.5) -> 1"), Bar->GetFraction(), 1.f);
	Bar->SetFraction(-2.f);
	TestEqual(TEXT("SetFraction(-2) -> 0"), Bar->GetFraction(), 0.f);
	Bar->SetOverlayFraction(0.4f);
	TestEqual(TEXT("SetOverlayFraction(0.4) -> 0.4"), Bar->GetOverlayFraction(), 0.4f);
	TestFalse(TEXT("a bare bar has no designer tree"), Bar->HasDesignerTree());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
