// Copyright Valhalla 2.0. All Rights Reserved.
//
// Phase 8b: the two pure halves of the game HUD.
//
//   Valhalla.Game.UI.ConfigParse  — ui-config.json through FValhallaUIConfig:
//     the real file (its three sections since B-07 step 4: chat, inventory,
//     nameplates), a partial file (defaults fill the gaps rather than zeros),
//     a pre-B-07 file (the old layout sections are ignored), and the colour /
//     CSS-size helpers.
//   Valhalla.Game.UI.ChatCommands — the chat box's grammar (GameScene.ts:5222
//     `submitChatInput`): /g /world /p /w /invite /accept /decline /leave, the
//     sticky channel, the usage lines, and Tab's channel cycle.
//   Valhalla.Game.UI.HudBlueprintGroundwork — B-07: the game HUD class
//     setting names WBP_GameHUD (and resolves to it), the C++ class never lays
//     out from a Blueprint, the designer panel names (which WBP_GameHUD must
//     match) are BindWidget / BindWidgetOptional members of the right types,
//     and the bar widget clamps its fraction.
//   Valhalla.Game.UI.MovablePanels — B-21: every movable panel names a
//     BindWidget(Optional) widget member, the flowing ones a Size Box;
//     ResolvePanelLayout keeps the designer's layout for empty settings and
//     applies a set entry and UiScale; ApplyUserLayout on a HUD with no layout
//     is a no-op; in WBP_GameHUD each movable panel is its own root-canvas
//     child (the B-21 split of Character + Inventory).
//   Valhalla.Game.UI.OptionsMenu — B-21 step 4: EValhallaHUDButton::Options,
//     the HUD's OptionsButton / OptionsMenuClass, every options-menu widget
//     name a BindWidgetOptional member of the right kind, the show switches
//     name hideable panels, the controls text from a mapping context, and
//     WBP_OptionsMenu (when present) a child of the C++ menu that WBP_GameHUD
//     names.
//   Valhalla.Game.UI.StyleColours — B-21 step 5: every colour key is a
//     "Valhalla|HUD Style" FLinearColor; the effective colour is the override
//     after ApplyUserStyle and the class default without one (and after the
//     override is removed); log filters restored from the settings.
//   Valhalla.Game.UI.EditModeMaths — B-21 step 3: ChooseAnchor's nine cases,
//     the snap (grid, edges, kept on the canvas), AnchorLayoutForRect put back
//     through ResolvePanelLayout at any UiScale, and IsInteractiveChild.
//   Valhalla.Game.UI.HudPlayFixes — Kevin's fixes from play (2026-09-25): the
//     party row button (PartySelect, not focusable), ServerSetTargetByName,
//     the click-eating target / party frames, and WBP_GameHUD's vitals order
//     (class line, HP, mana) and Visible target / party borders.
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
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/SizeBox.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "ValhallaChatCommands.h"
#include "ValhallaDataSettings.h"
#include "ValhallaDataSubsystem.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "ValhallaGameHUDWidget.h"
#include "ValhallaOptionsMenuWidget.h"
#include "ValhallaPlayerController.h"
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
	TestEqual(TEXT("default inventory 8 cols"), Defaults.Inventory.Cols, 8);
	TestEqual(TEXT("default inventory 4 rows"), Defaults.Inventory.Rows, 4);
	TestEqual(TEXT("default chat 50 lines typing"), Defaults.Chat.MaxMessages, 50);
	TestEqual(TEXT("default chat 9 lines idle"), Defaults.Chat.VisibleLines, 9);
	TestTrue(TEXT("default nameplate text #ffffff"),
		NearlyEqual(Defaults.Nameplates.Color, FValhallaUIConfig::ParseHexColor(TEXT("#ffffff"), FLinearColor::Black)));
	TestFalse(TEXT("a default-constructed config carried no sections"), Defaults.HasAllSections());
	TestEqual(TEXT("three sections"), FValhallaUIConfig::SectionCount, 3);

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
	TestTrue(TEXT("ui-config.json carries all three sections"), Real.HasAllSections());
	TestEqual(TEXT("chat.maxMessages"), Real.Chat.MaxMessages, 50);
	TestEqual(TEXT("chat.visibleLines"), Real.Chat.VisibleLines, 9);
	TestEqual(TEXT("inventory cols x rows = 32"), Real.Inventory.Cols * Real.Inventory.Rows, 32);
	TestEqual(TEXT("nameplates.yOffset"), Real.Nameplates.YOffset, -44.f);
	TestEqual(TEXT("nameplates.fontSize \"12px\""), Real.Nameplates.FontSize, 12);
	// B-07 step 4: the layout sections live in WBP_GameHUD now, not the file.
	const TSharedPtr<FJsonObject>& RawFile = Tables.UiConfig;
	if (TestTrue(TEXT("raw ui-config kept"), RawFile.IsValid()))
	{
		for (const TCHAR* Gone : { TEXT("hud"), TEXT("actionBar"), TEXT("castBar"), TEXT("deathOverlay") })
		{
			TestFalse(*FString::Printf(TEXT("ui-config.json has no '%s' section (WBP_GameHUD owns it)"), Gone), RawFile->HasField(Gone));
		}
	}

	// ── A partial file keeps the defaults, not zeros ────────────────────
	const TSharedPtr<FJsonObject> Partial = ParseJson(TEXT(
		"{ \"version\": \"9.9\", \"inventory\": { \"cols\": 6 },"
		"  \"nameplates\": { \"fontSize\": \"16px\" } }"));
	TestTrue(TEXT("partial JSON parsed"), Partial.IsValid());

	FValhallaUIConfig Parsed;
	const bool bAll = FValhallaUIConfig::Parse(Partial, Parsed);
	TestFalse(TEXT("partial file reports missing sections"), bAll);
	TestTrue(TEXT("inventory present"), Parsed.bHasInventory);
	TestFalse(TEXT("chat absent"), Parsed.bHasChat);
	TestEqual(TEXT("version read"), Parsed.Version, FString(TEXT("9.9")));
	TestEqual(TEXT("cols overridden"), Parsed.Inventory.Cols, 6);
	TestEqual(TEXT("rows kept at its default"), Parsed.Inventory.Rows, 4);
	TestEqual(TEXT("nameplate font overridden"), Parsed.Nameplates.FontSize, 16);
	TestEqual(TEXT("nameplate bgAlpha kept"), Parsed.Nameplates.BgAlpha, 0.5f);
	TestEqual(TEXT("chat idle lines defaulted"), Parsed.Chat.VisibleLines, 9);

	// ── A pre-B-07 file: the old layout sections are read without complaint and ignored ──
	const TSharedPtr<FJsonObject> Old = ParseJson(TEXT(
		"{ \"version\": \"1.0.0\", \"hud\": { \"hpBar\": { \"width\": 999 } }, \"actionBar\": { \"slotSize\": 60 },"
		"  \"chat\": { \"maxWidth\": 500, \"maxMessages\": 30, \"visibleLines\": 5 },"
		"  \"inventory\": { \"cols\": 10, \"rows\": 3, \"slotSize\": 60 }, \"nameplates\": { \"yOffset\": -50 } }"));
	FValhallaUIConfig FromOld;
	TestTrue(TEXT("old file still has the three sections"), FValhallaUIConfig::Parse(Old, FromOld));
	TestEqual(TEXT("old file: chat.maxMessages"), FromOld.Chat.MaxMessages, 30);
	TestEqual(TEXT("old file: chat.visibleLines"), FromOld.Chat.VisibleLines, 5);
	TestEqual(TEXT("old file: 10 x 3"), FromOld.Inventory.Cols * FromOld.Inventory.Rows, 30);
	TestEqual(TEXT("old file: nameplates.yOffset"), FromOld.Nameplates.YOffset, -50.f);

	// ── No file at all ──────────────────────────────────────────────────
	FValhallaUIConfig FromNothing;
	TestFalse(TEXT("null root -> false"), FValhallaUIConfig::Parse(nullptr, FromNothing));
	TestEqual(TEXT("null root -> defaults"), FromNothing.Inventory.Rows, 4);

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

	// Emotes (B-15 A-030): the verb, no argument.
	Check(TEXT("/sit"),              ECh::General, EAct::Emote, ECh::General, TEXT("sit"),   TEXT(""), false);
	Check(TEXT("/stand"),            ECh::Party,   EAct::Emote, ECh::General, TEXT("stand"), TEXT(""), false);
	Check(TEXT("/WAVE"),             ECh::General, EAct::Emote, ECh::General, TEXT("wave"),  TEXT(""), false);
	Check(TEXT("/cheer"),            ECh::General, EAct::Emote, ECh::General, TEXT("cheer"), TEXT(""), false);
	Check(TEXT("/bow"),              ECh::General, EAct::Emote, ECh::General, TEXT("bow"),   TEXT(""), false);
	Check(TEXT("/sitting"),          ECh::General, EAct::Unknown, ECh::System, TEXT("Unknown command: /sitting"), TEXT(""), false);

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
	// B-07 step 4: DefaultGame.ini names WBP_GameHUD (the code-built layout is
	// gone, so an empty setting means a blank HUD).
	const TSoftClassPtr<UValhallaGameHUDWidget>& Setting = GetDefault<UValhallaUISettings>()->GameHUDClass;
	TestFalse(TEXT("Game HUD Class is set"), Setting.IsNull());
	TestEqual(TEXT("Game HUD Class is WBP_GameHUD"), Setting.ToSoftObjectPath().ToString(),
		FString(TEXT("/Game/Valhalla/UI/HUD/WBP_GameHUD.WBP_GameHUD_C")));
	if (FPackageName::DoesPackageExist(TEXT("/Game/Valhalla/UI/HUD/WBP_GameHUD")))
	{
		const UClass* Resolved = UValhallaUISettings::ResolveGameHUDClass(Setting, FString()).Get();
		TestTrue(TEXT("the setting resolves to WBP_GameHUD_C"), Resolved && Resolved->GetName() == TEXT("WBP_GameHUD_C"));
		TestTrue(TEXT("WBP_GameHUD_C is a UValhallaGameHUDWidget"), Resolved && Resolved->IsChildOf(UValhallaGameHUDWidget::StaticClass()));
	}
	else
	{
		AddWarning(TEXT("WBP_GameHUD is not in this build; only the setting's path was checked."));
	}
	// That fallback has no layout any more, so it logs an error; expected here.
	AddExpectedError(TEXT("Game HUD Class is empty"), EAutomationExpectedErrorFlags::Contains, 1);
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

// ─────────────────────────────────────────────────────────────────────────────
//  Valhalla.Game.UI.MovablePanels (B-21)
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaUIMovablePanelsTest,
	"Valhalla.Game.UI.MovablePanels",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaUIMovablePanelsTest::RunTest(const FString& /*Parameters*/)
{
	// ── The list ────────────────────────────────────────────────────────
	const TArray<FValhallaMovablePanel>& Panels = UValhallaGameHUDWidget::GetMovablePanels();
	TestEqual(TEXT("eleven movable panels"), Panels.Num(), 11);
	TSet<FName> Keys, Members, Flowing, Hideable;
	for (const FValhallaMovablePanel& Info : Panels)
	{
		const FString Name = Info.Key.ToString();
		TestFalse(*FString::Printf(TEXT("%s: key is unique"), *Name), Keys.Contains(Info.Key));
		TestFalse(*FString::Printf(TEXT("%s: member is unique"), *Name), Members.Contains(Info.Member));
		Keys.Add(Info.Key);
		Members.Add(Info.Member);
		const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(UValhallaGameHUDWidget::StaticClass(), Info.Member);
		if (TestNotNull(*FString::Printf(TEXT("%s: %s is a member"), *Name, *Info.Member.ToString()), Property))
		{
			TestTrue(*FString::Printf(TEXT("%s: %s is a widget"), *Name, *Info.Member.ToString()), Property->PropertyClass->IsChildOf(UWidget::StaticClass()));
#if WITH_EDITORONLY_DATA
			TestTrue(*FString::Printf(TEXT("%s: %s binds by name"), *Name, *Info.Member.ToString()),
				Property->HasMetaData(TEXT("BindWidget")) || Property->HasMetaData(TEXT("BindWidgetOptional")));
#endif
		}
		TestTrue(*FString::Printf(TEXT("%s: listed in the panel names"), *Name),
			UValhallaGameHUDWidget::GetRequiredPanelNames().Contains(Info.Member) || UValhallaGameHUDWidget::GetOptionalPanelNames().Contains(Info.Member));
		if (Info.Sizing != EValhallaPanelSizing::Scaled)
		{
			Flowing.Add(Info.Key);
			TestFalse(*FString::Printf(TEXT("%s: a flowing panel names its Size Box"), *Name), Info.SizeBox.IsNone());
		}
		if (Info.bHideable)
		{
			Hideable.Add(Info.Key);
		}
		TestTrue(*FString::Printf(TEXT("%s: FindMovablePanel"), *Name), UValhallaGameHUDWidget::FindMovablePanel(Info.Key) == &Info);
	}
	for (const TCHAR* Key : { TEXT("Vitals"), TEXT("ActionBar"), TEXT("CastBar"), TEXT("TargetFrame"), TEXT("Party"), TEXT("CombatLog"),
		TEXT("Chat"), TEXT("Loot"), TEXT("Skills"), TEXT("Character"), TEXT("Inventory") })
	{
		TestTrue(*FString::Printf(TEXT("%s is movable"), Key), Keys.Contains(FName(Key)));
	}
	TestTrue(TEXT("flowing: chat, combat log, skills"), Flowing.Num() == 3 && Flowing.Contains(TEXT("Chat")) && Flowing.Contains(TEXT("CombatLog")) && Flowing.Contains(TEXT("Skills")));
	TestFalse(TEXT("windows the player opens cannot be hidden"), Hideable.Contains(TEXT("Loot")) || Hideable.Contains(TEXT("Skills"))
		|| Hideable.Contains(TEXT("Character")) || Hideable.Contains(TEXT("Inventory")));
	TestNull(TEXT("an unknown key is not movable"), UValhallaGameHUDWidget::FindMovablePanel(TEXT("Tooltip")));

	// ── ResolvePanelLayout ──────────────────────────────────────────────
	FValhallaPanelLayout Designer;
	Designer.AnchorMin = Designer.AnchorMax = FVector2D(0.0, 1.0);
	Designer.Alignment = FVector2D(0.0, 1.0);
	Designer.Position = FVector2D(232.0, -8.0);
	Designer.Size = FVector2D(360.0, 170.0);
	const FValhallaPanelLayout Same = UValhallaGameHUDWidget::ResolvePanelLayout(Designer, nullptr, 1.f);
	TestTrue(TEXT("no entry, scale 1: exactly the designer's"), Same.Equals(Designer, 1.e-4f));
	FValhallaPanelLayout Unset = Designer;
	Unset.Position = FVector2D(999.0, 999.0);
	Unset.bSet = false;
	TestTrue(TEXT("an entry that is not bSet is ignored"), UValhallaGameHUDWidget::ResolvePanelLayout(Designer, &Unset, 1.f).Equals(Designer, 1.e-4f));

	FValhallaPanelLayout User;
	User.AnchorMin = User.AnchorMax = FVector2D(1.0, 1.0);
	User.Alignment = FVector2D(1.0, 1.0);
	User.Position = FVector2D(-20.0, -30.0);
	User.Size = FVector2D(500.0, 0.0);
	User.Scale = 1.2f;
	User.bVisible = false;
	User.bSet = true;
	const FValhallaPanelLayout Placed = UValhallaGameHUDWidget::ResolvePanelLayout(Designer, &User, 1.f);
	TestTrue(TEXT("a set entry takes its anchors and alignment"), Placed.AnchorMin.Equals(FVector2D(1.0, 1.0)) && Placed.Alignment.Equals(FVector2D(1.0, 1.0)));
	TestTrue(TEXT("and its position"), Placed.Position.Equals(FVector2D(-20.0, -30.0)));
	TestTrue(TEXT("a zero size axis keeps the designer's"), Placed.Size.Equals(FVector2D(500.0, 170.0)));
	TestEqual(TEXT("its scale"), Placed.Scale, 1.2f);
	TestTrue(TEXT("its visibility, and bSet"), !Placed.bVisible && Placed.bSet);
	const FValhallaPanelLayout Big = UValhallaGameHUDWidget::ResolvePanelLayout(Designer, &User, 1.5f);
	TestTrue(TEXT("UiScale multiplies the position"), Big.Position.Equals(FVector2D(-30.0, -45.0), 1.e-3));
	TestTrue(TEXT("and the render scale"), FMath::IsNearlyEqual(Big.Scale, 1.8f, 1.e-4f));
	TestTrue(TEXT("but not the content size"), Big.Size.Equals(FVector2D(500.0, 170.0)));
	const FValhallaPanelLayout DesignerBig = UValhallaGameHUDWidget::ResolvePanelLayout(Designer, nullptr, 2.f);
	TestTrue(TEXT("UiScale scales an unset panel too"), DesignerBig.Position.Equals(FVector2D(464.0, -16.0), 1.e-3) && FMath::IsNearlyEqual(DesignerBig.Scale, 2.f));
	TestTrue(TEXT("UiScale is clamped"), FMath::IsNearlyEqual(UValhallaGameHUDWidget::ResolvePanelLayout(Designer, nullptr, 9.f).Scale, FValhallaUserUISettings::MaxUiScale));

	// ── ApplyUserLayout with no layout is a no-op ───────────────────────
	UValhallaGameHUDWidget* Bare = NewObject<UValhallaGameHUDWidget>(GetTransientPackage(), NAME_None, RF_Transient);
	FValhallaUserUISettings Everything;
	Everything.UiScale = 1.5f;
	Everything.Panels.Add(TEXT("Chat"), User);
	TestEqual(TEXT("empty settings on a HUD with no layout: nothing applied"), Bare->ApplyUserLayout(FValhallaUserUISettings()), 0);
	TestEqual(TEXT("full settings on a HUD with no layout: nothing applied"), Bare->ApplyUserLayout(Everything), 0);
	TestNull(TEXT("and no designer layout recorded"), Bare->GetDesignerLayout(TEXT("Chat")));
	const UValhallaGameHUDWidget* Cdo = GetDefault<UValhallaGameHUDWidget>();
	TestNull(TEXT("the CDO records no designer layout"), Cdo->GetDesignerLayout(TEXT("Vitals")));

	// ── WBP_GameHUD: every movable panel on the root canvas, on its own ──
	if (!FPackageName::DoesPackageExist(TEXT("/Game/Valhalla/UI/HUD/WBP_GameHUD")))
	{
		AddWarning(TEXT("WBP_GameHUD is not in this build; its tree was not checked."));
		return true;
	}
	const UWidgetBlueprintGeneratedClass* HudClass = Cast<UWidgetBlueprintGeneratedClass>(
		StaticLoadClass(UValhallaGameHUDWidget::StaticClass(), nullptr, TEXT("/Game/Valhalla/UI/HUD/WBP_GameHUD.WBP_GameHUD_C")));
	const UWidgetTree* Tree = HudClass ? HudClass->GetWidgetTreeArchetype() : nullptr;
	const UCanvasPanel* Root = Tree ? Cast<UCanvasPanel>(Tree->RootWidget) : nullptr;
	if (!TestNotNull(TEXT("WBP_GameHUD has a Canvas Panel root"), Root))
	{
		return true;
	}
	TSet<const UWidget*> CanvasChildren;
	for (const FValhallaMovablePanel& Info : Panels)
	{
		const UWidget* Widget = Tree->FindWidget(Info.Member);
		if (!TestNotNull(*FString::Printf(TEXT("WBP_GameHUD has %s"), *Info.Member.ToString()), Widget))
		{
			continue;
		}
		while (Widget && Widget->GetParent() && Widget->GetParent() != Root)
		{
			Widget = Widget->GetParent();
		}
		const bool bOnCanvas = Widget && Widget->GetParent() == Root;
		TestTrue(*FString::Printf(TEXT("%s sits in a root-canvas child"), *Info.Key.ToString()), bOnCanvas);
		if (bOnCanvas)
		{
			TestFalse(*FString::Printf(TEXT("%s's canvas child (%s) is its own"), *Info.Key.ToString(), *Widget->GetName()), CanvasChildren.Contains(Widget));
			CanvasChildren.Add(Widget);
		}
		if (Info.Sizing != EValhallaPanelSizing::Scaled && Info.Key != TEXT("Chat"))
		{
			TestNotNull(*FString::Printf(TEXT("WBP_GameHUD has %s's Size Box %s"), *Info.Key.ToString(), *Info.SizeBox.ToString()),
				Cast<USizeBox>(Tree->FindWidget(Info.SizeBox)));
		}
	}
	const UBorder* Chat = Cast<UBorder>(Tree->FindWidget(TEXT("ChatPanel")));
	TestTrue(TEXT("ChatPanel holds the chat Size Box"), Chat && Cast<USizeBox>(Chat->GetContent()) != nullptr);
	TestNull(TEXT("the Character + Inventory pair is split (no InventoryPair)"), Tree->FindWidget(TEXT("InventoryPair")));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Valhalla.Game.UI.OptionsMenu (B-21 step 4)
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaUIOptionsMenuTest,
	"Valhalla.Game.UI.OptionsMenu",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaUIOptionsMenuTest::RunTest(const FString& /*Parameters*/)
{
	// ── The cog's action ────────────────────────────────────────────────
	const UEnum* Buttons = StaticEnum<EValhallaHUDButton>();
	TestTrue(TEXT("EValhallaHUDButton::Options exists"),
		Buttons && Buttons->GetValueByName(TEXT("EValhallaHUDButton::Options")) == static_cast<int64>(EValhallaHUDButton::Options));
	TestTrue(TEXT("Options comes after the existing actions (their saved values are unchanged)"),
		static_cast<int32>(EValhallaHUDButton::Options) > static_cast<int32>(EValhallaHUDButton::PartyLeave));

	// ── The HUD's side ──────────────────────────────────────────────────
	TestTrue(TEXT("OptionsButton is an optional HUD panel"), UValhallaGameHUDWidget::GetOptionalPanelNames().Contains(TEXT("OptionsButton")));
	const FObjectPropertyBase* Cog = FindFProperty<FObjectPropertyBase>(UValhallaGameHUDWidget::StaticClass(), TEXT("OptionsButton"));
	TestTrue(TEXT("OptionsButton is a UValhallaHUDButton"), Cog && Cog->PropertyClass == UValhallaHUDButton::StaticClass());
	const UValhallaGameHUDWidget* HudCdo = GetDefault<UValhallaGameHUDWidget>();
	TestTrue(TEXT("OptionsMenuClass defaults to the C++ menu"), HudCdo->GetOptionsMenuClass() == UValhallaOptionsMenuWidget::StaticClass());
	TestNull(TEXT("no menu before OpenOptions"), HudCdo->GetOptionsMenu());
	TestFalse(TEXT("the CDO's menu is closed"), HudCdo->IsOptionsOpen());

	// ── The menu's designer names ───────────────────────────────────────
	TSet<FName> Seen;
	for (const FName Name : UValhallaOptionsMenuWidget::GetOptionalWidgetNames())
	{
		TestFalse(*FString::Printf(TEXT("%s listed once"), *Name.ToString()), Seen.Contains(Name));
		Seen.Add(Name);
		const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(UValhallaOptionsMenuWidget::StaticClass(), Name);
		if (!TestNotNull(*FString::Printf(TEXT("%s is a member of the options menu"), *Name.ToString()), Property))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s is a widget"), *Name.ToString()), Property->PropertyClass->IsChildOf(UWidget::StaticClass()));
#if WITH_EDITORONLY_DATA
		TestTrue(*FString::Printf(TEXT("%s is BindWidgetOptional"), *Name.ToString()), Property->HasMetaData(TEXT("BindWidgetOptional")));
#endif
		// The kind the name promises.
		const FString Text = Name.ToString();
		const UClass* Kind = Text.EndsWith(TEXT("Check")) ? UCheckBox::StaticClass()
			: Text.EndsWith(TEXT("Slider")) ? USlider::StaticClass()
			: Text.EndsWith(TEXT("Button")) ? UButton::StaticClass()
			: (Text.EndsWith(TEXT("Text")) || Text == TEXT("EditTitle")) ? UTextBlock::StaticClass()
			: Text == TEXT("Tabs") ? UWidgetSwitcher::StaticClass() : nullptr;
		if (Kind)
		{
			TestTrue(*FString::Printf(TEXT("%s is a %s"), *Text, *Kind->GetName()), Property->PropertyClass->IsChildOf(Kind));
		}
	}
	for (const TCHAR* Needed : { TEXT("Tabs"), TEXT("CloseButton"), TEXT("LockCheck"), TEXT("UiScaleSlider"), TEXT("OpacitySlider"),
		TEXT("ResetLayoutButton"), TEXT("ColourList"), TEXT("ColourEditor"), TEXT("HueSlider"), TEXT("SaturationSlider"), TEXT("ValueSlider"),
		TEXT("ChatFontSizeSlider"), TEXT("ChatLinesSlider"), TEXT("TimestampsCheck"), TEXT("LogFilterList"),
		TEXT("NpcNameplatesCheck"), TEXT("PlayerNameplatesCheck"), TEXT("FloatingTextCheck"), TEXT("NameplateFontSlider"), TEXT("ControlsText") })
	{
		TestTrue(*FString::Printf(TEXT("the menu binds %s"), Needed), Seen.Contains(FName(Needed)));
	}
	for (const FName Key : UValhallaOptionsMenuWidget::GetShowPanelKeys())
	{
		const FValhallaMovablePanel* Info = UValhallaGameHUDWidget::FindMovablePanel(Key);
		TestTrue(*FString::Printf(TEXT("Show%sCheck: a hideable panel"), *Key.ToString()), Info && Info->bHideable);
		TestTrue(*FString::Printf(TEXT("Show%sCheck is a menu widget"), *Key.ToString()),
			Seen.Contains(FName(*FString::Printf(TEXT("Show%sCheck"), *Key.ToString()))));
	}
	TestEqual(TEXT("twelve preset colours"), UValhallaOptionsMenuWidget::GetPresetColours().Num(), 12);

	// ── The controls list comes from the mapping context ────────────────
	UInputMappingContext* Context = NewObject<UInputMappingContext>(GetTransientPackage(), NAME_None, RF_Transient);
	UInputAction* Skills = NewObject<UInputAction>(Context, TEXT("IA_ToggleSkills"), RF_Transient);
	UInputAction* Bar1 = NewObject<UInputAction>(Context, TEXT("IA_ActionBar1"), RF_Transient);
	UInputAction* Bar2 = NewObject<UInputAction>(Context, TEXT("IA_ActionBar2"), RF_Transient);
	UInputAction* Bar3 = NewObject<UInputAction>(Context, TEXT("IA_ActionBar3"), RF_Transient);
	Context->MapKey(Skills, EKeys::K);
	Context->MapKey(Bar1, EKeys::One);
	Context->MapKey(Bar2, EKeys::Two);
	Context->MapKey(Bar3, EKeys::Three);
	const FString Controls = UValhallaOptionsMenuWidget::BuildControlsText(Context);
	TestTrue(TEXT("controls: Skills on K"), Controls.Contains(TEXT("Skills:   K")));
	TestTrue(TEXT("controls: the action bar as one range"), Controls.Contains(TEXT("Action bar slots:   1 - 3")));
	TestFalse(TEXT("controls: no context -> a note, not a crash"), UValhallaOptionsMenuWidget::BuildControlsText(nullptr).IsEmpty());

	// ── WBP_OptionsMenu, and WBP_GameHUD's class default and cog ────────
	if (!FPackageName::DoesPackageExist(TEXT("/Game/Valhalla/UI/HUD/WBP_OptionsMenu")))
	{
		AddWarning(TEXT("WBP_OptionsMenu is not in this build; its wiring was not checked."));
		return true;
	}
	const UClass* MenuClass = StaticLoadClass(UValhallaOptionsMenuWidget::StaticClass(), nullptr, TEXT("/Game/Valhalla/UI/HUD/WBP_OptionsMenu.WBP_OptionsMenu_C"));
	TestTrue(TEXT("WBP_OptionsMenu is a UValhallaOptionsMenuWidget"), MenuClass && MenuClass->IsChildOf(UValhallaOptionsMenuWidget::StaticClass()));
	const UWidgetBlueprintGeneratedClass* MenuGenerated = Cast<UWidgetBlueprintGeneratedClass>(MenuClass);
	const UWidgetTree* MenuTree = MenuGenerated ? MenuGenerated->GetWidgetTreeArchetype() : nullptr;
	if (TestNotNull(TEXT("WBP_OptionsMenu has a tree"), MenuTree))
	{
		for (const FName Name : UValhallaOptionsMenuWidget::GetOptionalWidgetNames())
		{
			TestNotNull(*FString::Printf(TEXT("WBP_OptionsMenu has %s"), *Name.ToString()), MenuTree->FindWidget(Name));
		}
		const UWidgetSwitcher* Switcher = Cast<UWidgetSwitcher>(MenuTree->FindWidget(TEXT("Tabs")));
		TestTrue(TEXT("five tabs"), Switcher && Switcher->GetNumWidgets() == 5);
	}
	const UClass* HudClass = StaticLoadClass(UValhallaGameHUDWidget::StaticClass(), nullptr, TEXT("/Game/Valhalla/UI/HUD/WBP_GameHUD.WBP_GameHUD_C"));
	const UValhallaGameHUDWidget* HudDefaults = HudClass ? Cast<UValhallaGameHUDWidget>(HudClass->GetDefaultObject()) : nullptr;
	TestTrue(TEXT("WBP_GameHUD's Options Menu Class is WBP_OptionsMenu"), HudDefaults && HudDefaults->GetOptionsMenuClass() == MenuClass);
	const UWidgetBlueprintGeneratedClass* HudGenerated = Cast<UWidgetBlueprintGeneratedClass>(HudClass);
	const UWidgetTree* HudTree = HudGenerated ? HudGenerated->GetWidgetTreeArchetype() : nullptr;
	const UValhallaHUDButton* CogButton = HudTree ? Cast<UValhallaHUDButton>(HudTree->FindWidget(TEXT("OptionsButton"))) : nullptr;
	TestTrue(TEXT("WBP_GameHUD has the OptionsButton cog, Action = Options"), CogButton && CogButton->Action == EValhallaHUDButton::Options);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Valhalla.Game.UI.StyleColours (B-21 step 5)
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaUIStyleColoursTest,
	"Valhalla.Game.UI.StyleColours",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaUIStyleColoursTest::RunTest(const FString& /*Parameters*/)
{
	using namespace ValhallaUITests;

	// ── Every key is a HUD Style colour ─────────────────────────────────
	const TArray<FValhallaStyleColourKey>& Keys = UValhallaGameHUDWidget::GetStyleColourKeys();
	TestEqual(TEXT("sixteen colours"), Keys.Num(), 16);
	for (const TCHAR* Key : { TEXT("HpHighColour"), TEXT("HpMidColour"), TEXT("HpLowColour"), TEXT("ManaColour"), TEXT("EnergyColour"),
		TEXT("CastBarColour"), TEXT("HighlightColour"), TEXT("LabelColour"), TEXT("ValueColour"), TEXT("ChatGeneralColour"),
		TEXT("ChatWorldColour"), TEXT("ChatWhisperColour"), TEXT("ChatPartyColour"), TEXT("ChatSystemColour"), TEXT("PanelTintColour") })
	{
		TestNotNull(*FString::Printf(TEXT("%s is offered"), Key), UValhallaGameHUDWidget::FindStyleColourKey(Key));
	}
	for (const FValhallaStyleColourKey& Info : Keys)
	{
		const FStructProperty* Property = FindFProperty<FStructProperty>(UValhallaGameHUDWidget::StaticClass(), Info.Key);
		if (TestNotNull(*FString::Printf(TEXT("%s is a property"), *Info.Key.ToString()), Property))
		{
			TestTrue(*FString::Printf(TEXT("%s is an FLinearColor"), *Info.Key.ToString()), Property->Struct == TBaseStructure<FLinearColor>::Get());
#if WITH_EDITORONLY_DATA
			TestEqual(*FString::Printf(TEXT("%s is a HUD Style property"), *Info.Key.ToString()), Property->GetMetaData(TEXT("Category")), FString(TEXT("Valhalla|HUD Style")));
#endif
		}
		TestFalse(*FString::Printf(TEXT("%s has a label"), *Info.Key.ToString()), FString(Info.Label).IsEmpty());
	}

	// ── Defaults (CDO-level): no override -> the class default ──────────
	const UValhallaGameHUDWidget* Cdo = GetDefault<UValhallaGameHUDWidget>();
	const FLinearColor DefaultHigh = FLinearColor::FromSRGBColor(FColor(0x44, 0xff, 0x44));
	TestTrue(TEXT("CDO: HpHighColour default #44ff44"), NearlyEqual(Cdo->GetDefaultColour(TEXT("HpHighColour")), DefaultHigh));
	TestTrue(TEXT("CDO: effective = default"), NearlyEqual(Cdo->GetEffectiveColour(TEXT("HpHighColour")), DefaultHigh));
	TestTrue(TEXT("CDO: PanelTintColour default white"), NearlyEqual(Cdo->GetDefaultColour(TEXT("PanelTintColour")), FLinearColor::White));

	// ── An override wins, only for its key, and goes again with it ──────
	UValhallaGameHUDWidget* Hud = NewObject<UValhallaGameHUDWidget>(GetTransientPackage(), NAME_None, RF_Transient);
	FValhallaUserUISettings Settings;
	const FLinearColor Red = FLinearColor::FromSRGBColor(FColor(0xff, 0x00, 0x00));
	Settings.Colours.Add(TEXT("HpHighColour"), Red);
	Settings.Colours.Add(TEXT("NotAHudColour"), FLinearColor::Blue);
	Settings.ChatFontSize = 12;
	Settings.bChatTimestamps = true;
	Settings.LogFilters.Add(TEXT("misses"), false);
	Settings.bFloatingCombatText = false;
	Hud->ApplyUserStyle(Settings); // no layout: records, touches no widget
	TestTrue(TEXT("override: HpHighColour is the player's"), NearlyEqual(Hud->GetEffectiveColour(TEXT("HpHighColour")), Red));
	TestTrue(TEXT("override: the default is unchanged"), NearlyEqual(Hud->GetDefaultColour(TEXT("HpHighColour")), DefaultHigh));
	TestTrue(TEXT("no override: HpMidColour is the class default"),
		NearlyEqual(Hud->GetEffectiveColour(TEXT("HpMidColour")), Cdo->GetDefaultColour(TEXT("HpMidColour"))));
	TestTrue(TEXT("an unknown key is not recorded"), NearlyEqual(Hud->GetEffectiveColour(TEXT("NotAHudColour")), FLinearColor(1.f, 0.f, 1.f, 1.f)));
	TestFalse(TEXT("the saved log filter is restored"), Hud->IsLogFilterOn(TEXT("misses")));
	TestTrue(TEXT("a filter not saved stays on"), Hud->IsLogFilterOn(TEXT("outDmg")));
	TestTrue(TEXT("the CDO was not touched"), NearlyEqual(Cdo->GetEffectiveColour(TEXT("HpHighColour")), DefaultHigh));

	Settings.Colours.Reset();
	Settings.LogFilters.Reset();
	Hud->ApplyUserStyle(Settings);
	TestTrue(TEXT("override removed: back to the default"), NearlyEqual(Hud->GetEffectiveColour(TEXT("HpHighColour")), DefaultHigh));
	TestTrue(TEXT("filters reset: misses shown again"), Hud->IsLogFilterOn(TEXT("misses")));

	// ── Log filter labels ───────────────────────────────────────────────
	TestEqual(TEXT("filter label"), UValhallaGameHUDWidget::GetLogFilterLabel(TEXT("outDmg")), FString(TEXT("Your damage")));
	TestEqual(TEXT("unknown filter label is the key"), UValhallaGameHUDWidget::GetLogFilterLabel(TEXT("zzz")), FString(TEXT("zzz")));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Valhalla.Game.UI.EditModeMaths (B-21 step 3)
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaUIEditModeMathsTest,
	"Valhalla.Game.UI.EditModeMaths",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaUIEditModeMathsTest::RunTest(const FString& /*Parameters*/)
{
	const FVector2D Viewport(1000.0, 800.0);
	auto Rect = [](double X, double Y, double W, double H)
	{
		return FSlateRect(static_cast<float>(X), static_cast<float>(Y), static_cast<float>(X + W), static_cast<float>(Y + H));
	};

	// ── ChooseAnchor: the nine points ───────────────────────────────────
	struct FCase { const TCHAR* Name; FSlateRect R; FVector2D Want; };
	const FCase Cases[] = {
		{ TEXT("top left"),      Rect(10, 12, 200, 100),   FVector2D(0.0, 0.0) },
		{ TEXT("top centre"),    Rect(400, 12, 200, 60),   FVector2D(0.5, 0.0) },
		{ TEXT("top right"),     Rect(780, 12, 208, 200),  FVector2D(1.0, 0.0) },
		{ TEXT("middle left"),   Rect(16, 300, 300, 200),  FVector2D(0.0, 0.5) },
		{ TEXT("centre"),        Rect(380, 330, 240, 140), FVector2D(0.5, 0.5) },
		{ TEXT("middle right"),  Rect(700, 350, 290, 100), FVector2D(1.0, 0.5) },
		{ TEXT("bottom left"),   Rect(14, 720, 216, 72),   FVector2D(0.0, 1.0) },
		{ TEXT("bottom centre"), Rect(300, 730, 400, 62),  FVector2D(0.5, 1.0) },
		{ TEXT("bottom right"),  Rect(900, 700, 88, 88),   FVector2D(1.0, 1.0) },
	};
	for (const FCase& Case : Cases)
	{
		const FVector2D Got = UValhallaGameHUDWidget::ChooseAnchor(Case.R, Viewport);
		TestTrue(*FString::Printf(TEXT("ChooseAnchor %s -> (%.1f, %.1f), got (%.1f, %.1f)"), Case.Name, Case.Want.X, Case.Want.Y, Got.X, Got.Y), Got.Equals(Case.Want));
	}
	TestTrue(TEXT("a tie goes to the centre"), UValhallaGameHUDWidget::ChooseAnchor(Rect(0, 0, 1000, 800), Viewport).Equals(FVector2D(0.5, 0.5)));

	// ── Snap ────────────────────────────────────────────────────────────
	const FVector2D Size(100.0, 50.0);
	TestTrue(TEXT("snap: to the 4 px grid"), UValhallaGameHUDWidget::SnapPanelPosition(FVector2D(13.0, 22.0), Size, Viewport).Equals(FVector2D(12.0, 24.0)));
	TestTrue(TEXT("snap: 5 px from the left and bottom edges -> on them"),
		UValhallaGameHUDWidget::SnapPanelPosition(FVector2D(5.0, 745.0), Size, Viewport).Equals(FVector2D(0.0, 750.0)));
	TestTrue(TEXT("snap: 7 px from the right, 3 px from the top -> on them"),
		UValhallaGameHUDWidget::SnapPanelPosition(FVector2D(893.0, 3.0), Size, Viewport).Equals(FVector2D(900.0, 0.0)));
	TestTrue(TEXT("snap: 9 px away is only the grid"), UValhallaGameHUDWidget::SnapPanelPosition(FVector2D(9.0, 9.0), Size, Viewport).Equals(FVector2D(8.0, 8.0)));
	TestTrue(TEXT("snap: kept on the canvas"), UValhallaGameHUDWidget::SnapPanelPosition(FVector2D(-40.0, 900.0), Size, Viewport).Equals(FVector2D(0.0, 750.0)));
	TestTrue(TEXT("snap: no grid, no edges leaves the position"),
		UValhallaGameHUDWidget::SnapPanelPosition(FVector2D(13.0, 22.0), Size, Viewport, 0.f, 0.f).Equals(FVector2D(13.0, 22.0)));

	// ── AnchorLayoutForRect round trip ──────────────────────────────────
	// The drawn rectangle's alignment point = anchor x canvas + Position x
	// UiScale (ResolvePanelLayout multiplies Position by UiScale), the same
	// point whatever the render scale about it.
	for (const float UiScale : { 1.f, 1.5f, 0.75f })
	{
		for (const FCase& Case : Cases)
		{
			const FValhallaPanelLayout Layout = UValhallaGameHUDWidget::AnchorLayoutForRect(Case.R, Viewport, UiScale);
			TestTrue(*FString::Printf(TEXT("layout %s: set, anchored where ChooseAnchor says, alignment = anchor"), Case.Name),
				Layout.bSet && Layout.AnchorMin.Equals(Case.Want) && Layout.AnchorMax.Equals(Case.Want) && Layout.Alignment.Equals(Case.Want));
			const FValhallaPanelLayout Resolved = UValhallaGameHUDWidget::ResolvePanelLayout(FValhallaPanelLayout(), &Layout, UiScale);
			const FVector2D RectSize = FVector2D(Case.R.GetSize());
			const FVector2D DrawnPoint = FVector2D(Case.R.Left, Case.R.Top) + Resolved.Alignment * RectSize;
			const FVector2D SlotPoint = Resolved.AnchorMin * Viewport + Resolved.Position;
			TestTrue(*FString::Printf(TEXT("layout %s at UI scale %.2f: puts the panel back where it was drawn"), Case.Name, UiScale),
				DrawnPoint.Equals(SlotPoint, 0.01));
		}
	}
	// On a bigger window a re-anchored panel keeps its distance from its corner.
	const FValhallaPanelLayout BottomRight = UValhallaGameHUDWidget::AnchorLayoutForRect(Rect(900, 700, 88, 88), Viewport, 1.f);
	const FVector2D Bigger(1600.0, 900.0);
	const FVector2D CornerThen = BottomRight.AnchorMin * Bigger + BottomRight.Position; // its bottom-right corner
	TestTrue(TEXT("re-anchored: 12 px from the bottom-right corner on any window"), CornerThen.Equals(FVector2D(1588.0, 888.0), 0.01));

	// ── What the overlay lets through ───────────────────────────────────
	const UButton* Button = NewObject<UButton>(GetTransientPackage(), NAME_None, RF_Transient);
	const UCheckBox* Check = NewObject<UCheckBox>(GetTransientPackage(), NAME_None, RF_Transient);
	const UScrollBox* Scroll = NewObject<UScrollBox>(GetTransientPackage(), NAME_None, RF_Transient);
	const UBorder* Border = NewObject<UBorder>(GetTransientPackage(), NAME_None, RF_Transient);
	TestTrue(TEXT("a button is interactive"), UValhallaGameHUDWidget::IsInteractiveChild(Button));
	TestTrue(TEXT("a check box is interactive"), UValhallaGameHUDWidget::IsInteractiveChild(Check));
	TestFalse(TEXT("a scroll box is dragged (the panel wins)"), UValhallaGameHUDWidget::IsInteractiveChild(Scroll));
	TestFalse(TEXT("a panel background is dragged"), UValhallaGameHUDWidget::IsInteractiveChild(Border));
	TestFalse(TEXT("null is not"), UValhallaGameHUDWidget::IsInteractiveChild(nullptr));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Valhalla.Game.UI.HudPlayFixes (Kevin's HUD fixes from play, 2026-09-25)
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FValhallaUIHudPlayFixesTest,
	"Valhalla.Game.UI.HudPlayFixes",
	VALHALLA_GAME_TEST_FLAGS)

bool FValhallaUIHudPlayFixesTest::RunTest(const FString& /*Parameters*/)
{
	// ── Party rows: a click targets the member ──────────────────────────
	TestTrue(TEXT("PartySelect comes after the existing actions (their saved values are unchanged)"),
		static_cast<int32>(EValhallaHUDButton::PartySelect) > static_cast<int32>(EValhallaHUDButton::Options));
	const UValhallaPartyRowButton* Row = GetDefault<UValhallaPartyRowButton>();
	TestTrue(TEXT("a party row is a HUD button with Action = PartySelect"), Row->IsA<UValhallaHUDButton>() && Row->Action == EValhallaHUDButton::PartySelect);
	TestFalse(TEXT("a party row is not focusable (WASD keeps walking after a click)"), Row->GetIsFocusable());
	const UFunction* ByName = AValhallaPlayerController::StaticClass()->FindFunctionByName(TEXT("ServerSetTargetByName"));
	TestTrue(TEXT("ServerSetTargetByName is a server RPC"), ByName && ByName->HasAnyFunctionFlags(FUNC_NetServer));

	// ── Target and party frames eat clicks ──────────────────────────────
	const TArray<FName>& Eaters = UValhallaGameHUDWidget::GetClickEatingPanelNames();
	TestTrue(TEXT("the click-eating panels are the target and party frames"),
		Eaters.Num() == 2 && Eaters.Contains(TEXT("TargetFramePanel")) && Eaters.Contains(TEXT("PartyPanel")));
	for (const FName Name : Eaters)
	{
		TestTrue(*FString::Printf(TEXT("%s is an optional panel"), *Name.ToString()), UValhallaGameHUDWidget::GetOptionalPanelNames().Contains(Name));
	}
	TestFalse(TEXT("the vitals do not eat clicks"), Eaters.Contains(TEXT("VitalsPanel")));

	// ── WBP_GameHUD ─────────────────────────────────────────────────────
	if (!FPackageName::DoesPackageExist(TEXT("/Game/Valhalla/UI/HUD/WBP_GameHUD")))
	{
		AddWarning(TEXT("WBP_GameHUD is not in this build; its tree was not checked."));
		return true;
	}
	const UWidgetBlueprintGeneratedClass* HudClass = Cast<UWidgetBlueprintGeneratedClass>(
		StaticLoadClass(UValhallaGameHUDWidget::StaticClass(), nullptr, TEXT("/Game/Valhalla/UI/HUD/WBP_GameHUD.WBP_GameHUD_C")));
	const UWidgetTree* Tree = HudClass ? HudClass->GetWidgetTreeArchetype() : nullptr;
	if (!TestNotNull(TEXT("WBP_GameHUD has a tree"), Tree))
	{
		return true;
	}
	// Vitals top to bottom: the class line, HP, then mana / energy.
	const UPanelWidget* Vitals = Cast<UPanelWidget>(Tree->FindWidget(TEXT("VitalsPanel")));
	auto RowOf = [Vitals](const UWidget* Widget)
	{
		while (Widget && Widget->GetParent() && Widget->GetParent() != Vitals)
		{
			Widget = Widget->GetParent();
		}
		return Vitals && Widget ? Vitals->GetChildIndex(Widget) : INDEX_NONE;
	};
	const int32 ClassRow = RowOf(Tree->FindWidget(TEXT("ClassText")));
	const int32 HpRow = RowOf(Tree->FindWidget(TEXT("HpBar")));
	const int32 ManaRow = RowOf(Tree->FindWidget(TEXT("ManaBar")));
	TestTrue(TEXT("vitals: all three in VitalsPanel"), ClassRow != INDEX_NONE && HpRow != INDEX_NONE && ManaRow != INDEX_NONE);
	TestTrue(TEXT("vitals: the class line above the HP bar"), ClassRow < HpRow);
	TestTrue(TEXT("vitals: the HP bar above the mana / energy bar"), HpRow < ManaRow);
	for (const FName Name : Eaters)
	{
		const UWidget* Frame = Tree->FindWidget(Name);
		TestTrue(*FString::Printf(TEXT("WBP_GameHUD's %s is a Visible border"), *Name.ToString()),
			Frame && Frame->IsA<UBorder>() && Frame->GetVisibility() == ESlateVisibility::Visible);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
