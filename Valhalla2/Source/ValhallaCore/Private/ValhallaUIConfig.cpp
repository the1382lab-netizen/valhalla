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
}

FValhallaUIConfig::FValhallaUIConfig()
{
	// DEFAULT_UI_CONFIG (ui-config.ts), colour for colour.
	Nameplates.Color = Hex(TEXT("#ffffff"));
	Nameplates.StrokeColor = Hex(TEXT("#000000"));
	Nameplates.BgColor = Hex(TEXT("#000000"));
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

	// B-07 step 4: hud, actionBar, castBar and deathOverlay (and the chat and
	// inventory sizes and colours) moved into WBP_GameHUD; a file that still
	// carries them is read without complaint, and they are ignored.

	// ── chat ────────────────────────────────────────────────────────────
	if (const TSharedPtr<FJsonObject> Chat = Section(Root, TEXT("chat")))
	{
		Out.bHasChat = true;
		Int(Chat, TEXT("maxMessages"), Out.Chat.MaxMessages);
		Int(Chat, TEXT("visibleLines"), Out.Chat.VisibleLines);
	}

	// ── inventory ───────────────────────────────────────────────────────
	if (const TSharedPtr<FJsonObject> Inv = Section(Root, TEXT("inventory")))
	{
		Out.bHasInventory = true;
		Int(Inv, TEXT("cols"), Out.Inventory.Cols);
		Int(Inv, TEXT("rows"), Out.Inventory.Rows);
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

	return Out.HasAllSections();
}
