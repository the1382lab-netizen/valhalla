// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaUIConfig.h"

#include "Dom/JsonObject.h"
#include "Misc/Parse.h"

namespace
{
	FLinearColor Hex(const TCHAR* In)
	{
		return FValhallaUIConfig::ParseHexColor(In, FLinearColor::White);
	}

	TSharedPtr<FJsonObject> Section(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject>* Found = nullptr;
		if (Obj.IsValid() && Obj->TryGetObjectField(Key, Found) && Found && Found->IsValid())
		{
			return *Found;
		}
		return nullptr;
	}

	void Num(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, float& Out)
	{
		double Value = 0.0;
		if (Obj.IsValid() && Obj->TryGetNumberField(Key, Value))
		{
			Out = static_cast<float>(Value);
		}
	}

	void Int(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, int32& Out)
	{
		double Value = 0.0;
		if (Obj.IsValid() && Obj->TryGetNumberField(Key, Value))
		{
			Out = FMath::RoundToInt(Value);
		}
	}

	void Str(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, FString& Out)
	{
		FString Value;
		if (Obj.IsValid() && Obj->TryGetStringField(Key, Value))
		{
			Out = Value;
		}
	}

	void Colour(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, FLinearColor& Out)
	{
		FString Value;
		if (Obj.IsValid() && Obj->TryGetStringField(Key, Value))
		{
			Out = FValhallaUIConfig::ParseHexColor(Value, Out);
		}
	}

	void Px(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, int32& Out)
	{
		FString Value;
		if (Obj.IsValid() && Obj->TryGetStringField(Key, Value))
		{
			Out = FValhallaUIConfig::ParsePixelSize(Value, Out);
			return;
		}
		// The editor has been known to write a bare number; take that too.
		Int(Obj, Key, Out);
	}

	void Bar(const TSharedPtr<FJsonObject>& Obj, FValhallaUIBarConfig& Out)
	{
		Num(Obj, TEXT("width"), Out.Width);
		Num(Obj, TEXT("height"), Out.Height);
		Colour(Obj, TEXT("color"), Out.Color);
		Colour(Obj, TEXT("bgColor"), Out.BgColor);
		Num(Obj, TEXT("bgAlpha"), Out.BgAlpha);
	}
}

FValhallaUIConfig::FValhallaUIConfig()
{
	// DEFAULT_UI_CONFIG (ui-config.ts:131), colour for colour.
	Hud.HpHigh = Hex(TEXT("#44ff44"));
	Hud.HpMid = Hex(TEXT("#ffaa00"));
	Hud.HpLow = Hex(TEXT("#ff4444"));
	Hud.HpBg = Hex(TEXT("#000000"));
	Hud.Mana.Color = Hex(TEXT("#4488ff"));
	Hud.Mana.BgColor = Hex(TEXT("#000000"));
	Hud.Energy.Color = Hex(TEXT("#ddaa00"));
	Hud.Energy.BgColor = Hex(TEXT("#000000"));
	Hud.ClassColor = Hex(TEXT("#ffffff"));
	Hud.ClassStrokeColor = Hex(TEXT("#000000"));

	ActionBar.Bg = Hex(TEXT("#1a1a1a"));
	ActionBar.Border = Hex(TEXT("#666666"));
	ActionBar.CooldownOverlay = Hex(TEXT("#880000"));
	ActionBar.KeyLabelColor = Hex(TEXT("#888888"));

	Chat.BgColor = Hex(TEXT("#0a0a1a"));
	Chat.BorderColor = Hex(TEXT("#333355"));
	Chat.General = Hex(TEXT("#ffffff"));
	Chat.World = Hex(TEXT("#ffdd00"));
	Chat.Whisper = Hex(TEXT("#ff88cc"));
	Chat.System = Hex(TEXT("#ffaa44"));

	Inventory.Bg = Hex(TEXT("#1a1a2e"));
	Inventory.Border = Hex(TEXT("#333355"));
	Inventory.SlotBg = Hex(TEXT("#2a2a3e"));
	Inventory.Highlight = Hex(TEXT("#ffaa00"));
	Inventory.TitleColor = Hex(TEXT("#ffcc00"));
	Inventory.LabelColor = Hex(TEXT("#aaaacc"));
	Inventory.ValueColor = Hex(TEXT("#ffffff"));

	CastBar.Color = Hex(TEXT("#ffaa00"));
	CastBar.BgColor = Hex(TEXT("#000000"));
	CastBar.TextColor = Hex(TEXT("#ffffff"));

	Nameplates.Color = Hex(TEXT("#ffffff"));
	Nameplates.StrokeColor = Hex(TEXT("#000000"));
	Nameplates.BgColor = Hex(TEXT("#000000"));

	DeathOverlay.TextColor = Hex(TEXT("#ff4444"));
}

FLinearColor FValhallaUIConfig::ParseHexColor(const FString& In, const FLinearColor& Fallback)
{
	FString Digits = In.TrimStartAndEnd();
	Digits.RemoveFromStart(TEXT("#"));
	Digits.RemoveFromStart(TEXT("0x"));

	if (Digits.Len() == 3)
	{
		// #rgb → #rrggbb, as CSS reads it.
		FString Expanded;
		for (const TCHAR Char : Digits)
		{
			Expanded.AppendChar(Char);
			Expanded.AppendChar(Char);
		}
		Digits = Expanded;
	}

	if (Digits.Len() != 6)
	{
		return Fallback;
	}

	for (const TCHAR Char : Digits)
	{
		if (!FChar::IsHexDigit(Char))
		{
			return Fallback;
		}
	}

	const uint32 Packed = FParse::HexNumber(*Digits);
	const FColor Srgb(
		static_cast<uint8>((Packed >> 16) & 0xFF),
		static_cast<uint8>((Packed >> 8) & 0xFF),
		static_cast<uint8>(Packed & 0xFF));
	return FLinearColor::FromSRGBColor(Srgb);
}

int32 FValhallaUIConfig::ParsePixelSize(const FString& Css, int32 Fallback)
{
	const FString Trimmed = Css.TrimStartAndEnd();
	int32 Value = 0;
	int32 Digits = 0;
	for (const TCHAR Char : Trimmed)
	{
		if (!FChar::IsDigit(Char))
		{
			break;
		}
		Value = Value * 10 + (Char - TEXT('0'));
		++Digits;
	}
	return Digits > 0 ? Value : Fallback;
}

bool FValhallaUIConfig::Parse(const TSharedPtr<FJsonObject>& Root, FValhallaUIConfig& Out)
{
	Out = FValhallaUIConfig();
	if (!Root.IsValid())
	{
		return false;
	}

	Str(Root, TEXT("version"), Out.Version);

	// ── hud ─────────────────────────────────────────────────────────────
	if (const TSharedPtr<FJsonObject> Hud = Section(Root, TEXT("hud")))
	{
		Out.bHasHud = true;

		if (const TSharedPtr<FJsonObject> Hp = Section(Hud, TEXT("hpBar")))
		{
			Num(Hp, TEXT("x"), Out.Hud.HpX);
			Num(Hp, TEXT("width"), Out.Hud.HpWidth);
			Num(Hp, TEXT("height"), Out.Hud.HpHeight);
			Num(Hp, TEXT("yOffsetFromBottom"), Out.Hud.HpYOffsetFromBottom);
			const TSharedPtr<FJsonObject> Colors = Section(Hp, TEXT("colors"));
			Colour(Colors, TEXT("high"), Out.Hud.HpHigh);
			Colour(Colors, TEXT("mid"), Out.Hud.HpMid);
			Colour(Colors, TEXT("low"), Out.Hud.HpLow);
			Colour(Colors, TEXT("bg"), Out.Hud.HpBg);
			Num(Colors, TEXT("bgAlpha"), Out.Hud.HpBgAlpha);
		}

		if (const TSharedPtr<FJsonObject> Mana = Section(Hud, TEXT("manaBar")))
		{
			Bar(Mana, Out.Hud.Mana);
			Num(Mana, TEXT("gapAboveHp"), Out.Hud.ManaGapAboveHp);
		}

		if (const TSharedPtr<FJsonObject> Energy = Section(Hud, TEXT("energyBar")))
		{
			Bar(Energy, Out.Hud.Energy);
		}

		if (const TSharedPtr<FJsonObject> ClassText = Section(Hud, TEXT("classText")))
		{
			Px(ClassText, TEXT("fontSize"), Out.Hud.ClassFontSize);
			Str(ClassText, TEXT("fontFamily"), Out.Hud.ClassFontFamily);
			Colour(ClassText, TEXT("color"), Out.Hud.ClassColor);
			Colour(ClassText, TEXT("strokeColor"), Out.Hud.ClassStrokeColor);
			Num(ClassText, TEXT("strokeThickness"), Out.Hud.ClassStrokeThickness);
		}
	}

	// ── actionBar ───────────────────────────────────────────────────────
	if (const TSharedPtr<FJsonObject> Bar_ = Section(Root, TEXT("actionBar")))
	{
		Out.bHasActionBar = true;
		Num(Bar_, TEXT("slotSize"), Out.ActionBar.SlotSize);
		Num(Bar_, TEXT("slotGap"), Out.ActionBar.SlotGap);
		Num(Bar_, TEXT("padding"), Out.ActionBar.Padding);
		Num(Bar_, TEXT("bottomMargin"), Out.ActionBar.BottomMargin);
		const TSharedPtr<FJsonObject> Colors = Section(Bar_, TEXT("colors"));
		Colour(Colors, TEXT("bg"), Out.ActionBar.Bg);
		Num(Colors, TEXT("bgAlpha"), Out.ActionBar.BgAlpha);
		Colour(Colors, TEXT("border"), Out.ActionBar.Border);
		Colour(Colors, TEXT("cooldownOverlay"), Out.ActionBar.CooldownOverlay);
		Num(Colors, TEXT("cooldownOverlayAlpha"), Out.ActionBar.CooldownOverlayAlpha);
		Colour(Colors, TEXT("keyLabelColor"), Out.ActionBar.KeyLabelColor);
	}

	// ── chat ────────────────────────────────────────────────────────────
	if (const TSharedPtr<FJsonObject> Chat = Section(Root, TEXT("chat")))
	{
		Out.bHasChat = true;
		Num(Chat, TEXT("maxWidth"), Out.Chat.MaxWidth);
		Num(Chat, TEXT("height"), Out.Chat.Height);
		Num(Chat, TEXT("bottomMargin"), Out.Chat.BottomMargin);
		Num(Chat, TEXT("lineHeight"), Out.Chat.LineHeight);
		Num(Chat, TEXT("padding"), Out.Chat.Padding);
		Num(Chat, TEXT("inputHeight"), Out.Chat.InputHeight);
		Int(Chat, TEXT("maxMessages"), Out.Chat.MaxMessages);
		Int(Chat, TEXT("visibleLines"), Out.Chat.VisibleLines);
		Px(Chat, TEXT("fontSize"), Out.Chat.FontSize);
		Colour(Chat, TEXT("bgColor"), Out.Chat.BgColor);
		Num(Chat, TEXT("bgAlpha"), Out.Chat.BgAlpha);
		Colour(Chat, TEXT("borderColor"), Out.Chat.BorderColor);
		Num(Chat, TEXT("borderAlpha"), Out.Chat.BorderAlpha);
		const TSharedPtr<FJsonObject> Colors = Section(Chat, TEXT("colors"));
		Colour(Colors, TEXT("general"), Out.Chat.General);
		Colour(Colors, TEXT("world"), Out.Chat.World);
		Colour(Colors, TEXT("whisper"), Out.Chat.Whisper);
		Colour(Colors, TEXT("system"), Out.Chat.System);
	}

	// ── inventory ───────────────────────────────────────────────────────
	if (const TSharedPtr<FJsonObject> Inv = Section(Root, TEXT("inventory")))
	{
		Out.bHasInventory = true;
		Int(Inv, TEXT("cols"), Out.Inventory.Cols);
		Int(Inv, TEXT("rows"), Out.Inventory.Rows);
		Num(Inv, TEXT("slotSize"), Out.Inventory.SlotSize);
		Num(Inv, TEXT("slotGap"), Out.Inventory.SlotGap);
		Num(Inv, TEXT("charPanelWidth"), Out.Inventory.CharPanelWidth);
		Num(Inv, TEXT("panelGap"), Out.Inventory.PanelGap);
		Num(Inv, TEXT("panelHeight"), Out.Inventory.PanelHeight);
		const TSharedPtr<FJsonObject> Colors = Section(Inv, TEXT("colors"));
		Colour(Colors, TEXT("bg"), Out.Inventory.Bg);
		Num(Colors, TEXT("bgAlpha"), Out.Inventory.BgAlpha);
		Colour(Colors, TEXT("border"), Out.Inventory.Border);
		Colour(Colors, TEXT("slotBg"), Out.Inventory.SlotBg);
		Colour(Colors, TEXT("highlight"), Out.Inventory.Highlight);
		Colour(Colors, TEXT("titleColor"), Out.Inventory.TitleColor);
		Colour(Colors, TEXT("labelColor"), Out.Inventory.LabelColor);
		Colour(Colors, TEXT("valueColor"), Out.Inventory.ValueColor);
	}

	// ── castBar ─────────────────────────────────────────────────────────
	if (const TSharedPtr<FJsonObject> CastObj = Section(Root, TEXT("castBar")))
	{
		Out.bHasCastBar = true;
		Num(CastObj, TEXT("width"), Out.CastBar.Width);
		Num(CastObj, TEXT("height"), Out.CastBar.Height);
		Num(CastObj, TEXT("yAboveActionBar"), Out.CastBar.YAboveActionBar);
		Colour(CastObj, TEXT("color"), Out.CastBar.Color);
		Colour(CastObj, TEXT("bgColor"), Out.CastBar.BgColor);
		Num(CastObj, TEXT("bgAlpha"), Out.CastBar.BgAlpha);
		Colour(CastObj, TEXT("textColor"), Out.CastBar.TextColor);
		Px(CastObj, TEXT("fontSize"), Out.CastBar.FontSize);
	}

	// ── nameplates ──────────────────────────────────────────────────────
	if (const TSharedPtr<FJsonObject> Plate = Section(Root, TEXT("nameplates")))
	{
		Out.bHasNameplates = true;
		Px(Plate, TEXT("fontSize"), Out.Nameplates.FontSize);
		Str(Plate, TEXT("fontWeight"), Out.Nameplates.FontWeight);
		Colour(Plate, TEXT("color"), Out.Nameplates.Color);
		Colour(Plate, TEXT("strokeColor"), Out.Nameplates.StrokeColor);
		Num(Plate, TEXT("strokeThickness"), Out.Nameplates.StrokeThickness);
		Colour(Plate, TEXT("bgColor"), Out.Nameplates.BgColor);
		Num(Plate, TEXT("bgAlpha"), Out.Nameplates.BgAlpha);
		Num(Plate, TEXT("yOffset"), Out.Nameplates.YOffset);
		Num(Plate, TEXT("bgPaddingX"), Out.Nameplates.BgPaddingX);
		Num(Plate, TEXT("bgPaddingY"), Out.Nameplates.BgPaddingY);
		Num(Plate, TEXT("bgRadius"), Out.Nameplates.BgRadius);
	}

	// ── deathOverlay ────────────────────────────────────────────────────
	if (const TSharedPtr<FJsonObject> Death = Section(Root, TEXT("deathOverlay")))
	{
		Out.bHasDeathOverlay = true;
		Num(Death, TEXT("bgAlpha"), Out.DeathOverlay.BgAlpha);
		Colour(Death, TEXT("textColor"), Out.DeathOverlay.TextColor);
		Px(Death, TEXT("fontSize"), Out.DeathOverlay.FontSize);
	}

	return Out.HasAllSections();
}
