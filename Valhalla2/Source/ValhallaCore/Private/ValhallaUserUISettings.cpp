// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaUserUISettings.h"

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Policies/PrettyJsonPrintPolicy.h"

namespace
{
	// The field names, once: the writer, the reader and the known-field list share them.
	namespace Key
	{
		const TCHAR* const Version = TEXT("Version");
		const TCHAR* const UpdatedAt = TEXT("UpdatedAt");
		const TCHAR* const bLocked = TEXT("bLocked");
		const TCHAR* const UiScale = TEXT("UiScale");
		const TCHAR* const PanelOpacity = TEXT("PanelOpacity");
		const TCHAR* const Panels = TEXT("Panels");
		const TCHAR* const Colours = TEXT("Colours");
		const TCHAR* const ChatFontSize = TEXT("ChatFontSize");
		const TCHAR* const ChatVisibleLines = TEXT("ChatVisibleLines");
		const TCHAR* const bChatTimestamps = TEXT("bChatTimestamps");
		const TCHAR* const LogFilters = TEXT("LogFilters");
		const TCHAR* const bShowNpcNameplates = TEXT("bShowNpcNameplates");
		const TCHAR* const bShowPlayerNameplates = TEXT("bShowPlayerNameplates");
		const TCHAR* const bFloatingCombatText = TEXT("bFloatingCombatText");
		const TCHAR* const NameplateFontSize = TEXT("NameplateFontSize");

		const TCHAR* const AnchorMin = TEXT("AnchorMin");
		const TCHAR* const AnchorMax = TEXT("AnchorMax");
		const TCHAR* const Alignment = TEXT("Alignment");
		const TCHAR* const Position = TEXT("Position");
		const TCHAR* const Size = TEXT("Size");
		const TCHAR* const Scale = TEXT("Scale");
		const TCHAR* const bVisible = TEXT("bVisible");
		const TCHAR* const bSet = TEXT("bSet");

		bool IsKnownTopLevel(const FString& Name)
		{
			static const TSet<FString> Known = {
				Version, UpdatedAt, bLocked, UiScale, PanelOpacity, Panels, Colours, ChatFontSize, ChatVisibleLines,
				bChatTimestamps, LogFilters, bShowNpcNameplates, bShowPlayerNameplates, bFloatingCombatText, NameplateFontSize,
			};
			return Known.Contains(Name);
		}
	}

	/** Font sizes past this are a typo, not a preference. */
	constexpr int32 MaxFontSize = 64;
	constexpr int32 MaxChatLines = 100;
	/** A panel's content size, px, either axis. */
	constexpr double MaxPanelSize = 8192.0;
	/** A panel's offset from its anchor, px, either axis. */
	constexpr double MaxPanelOffset = 16384.0;

	TSharedRef<FJsonValue> VectorToJson(const FVector2D& V)
	{
		TArray<TSharedPtr<FJsonValue>> Pair;
		Pair.Add(MakeShared<FJsonValueNumber>(V.X));
		Pair.Add(MakeShared<FJsonValueNumber>(V.Y));
		return MakeShared<FJsonValueArray>(Pair);
	}

	/** [x, y] with two finite numbers, else false (Out untouched). */
	bool VectorFromJson(const TSharedPtr<FJsonValue>& Value, FVector2D& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
		if (!Value.IsValid() || !Value->TryGetArray(Pair) || !Pair || Pair->Num() != 2)
		{
			return false;
		}
		double X = 0.0, Y = 0.0;
		if (!(*Pair)[0].IsValid() || !(*Pair)[1].IsValid() || !(*Pair)[0]->TryGetNumber(X) || !(*Pair)[1]->TryGetNumber(Y)
			|| !FMath::IsFinite(X) || !FMath::IsFinite(Y))
		{
			return false;
		}
		Out = FVector2D(X, Y);
		return true;
	}

	/** The reader's typed accessors, each reporting a wrong type once. */
	struct FReader
	{
		const FJsonObject& Json;
		TArray<FString>* Warnings;
		const FString Where;

		void Warn(const FString& Field, const TCHAR* Expected) const
		{
			if (Warnings)
			{
				Warnings->Add(FString::Printf(TEXT("%s%s: expected %s; kept the default"), *Where, *Field, Expected));
			}
		}

		const TSharedPtr<FJsonValue>* Find(const TCHAR* Field) const
		{
			const TSharedPtr<FJsonValue>* Value = Json.Values.Find(Field);
			return Value && Value->IsValid() && !(*Value)->IsNull() ? Value : nullptr;
		}

		void Bool(const TCHAR* Field, bool& Out) const
		{
			if (const TSharedPtr<FJsonValue>* Value = Find(Field))
			{
				// Type-checked: FJsonValue's TryGetBool also accepts numbers and strings.
				if ((*Value)->Type == EJson::Boolean) { Out = (*Value)->AsBool(); } else { Warn(Field, TEXT("true or false")); }
			}
		}

		void Number(const TCHAR* Field, float& Out) const
		{
			if (const TSharedPtr<FJsonValue>* Value = Find(Field))
			{
				double D = 0.0;
				if ((*Value)->Type == EJson::Number && (*Value)->TryGetNumber(D) && FMath::IsFinite(D)) { Out = static_cast<float>(D); }
				else { Warn(Field, TEXT("a number")); }
			}
		}

		void Int(const TCHAR* Field, int32& Out) const
		{
			if (const TSharedPtr<FJsonValue>* Value = Find(Field))
			{
				double D = 0.0;
				if ((*Value)->Type == EJson::Number && (*Value)->TryGetNumber(D) && FMath::IsFinite(D))
				{
					Out = static_cast<int32>(FMath::Clamp(FMath::RoundToDouble(D), -1.0e9, 1.0e9));
				}
				else { Warn(Field, TEXT("a number")); }
			}
		}

		void String(const TCHAR* Field, FString& Out) const
		{
			if (const TSharedPtr<FJsonValue>* Value = Find(Field))
			{
				if ((*Value)->Type == EJson::String) { Out = (*Value)->AsString(); } else { Warn(Field, TEXT("a string")); }
			}
		}

		void Vector(const TCHAR* Field, FVector2D& Out) const
		{
			if (const TSharedPtr<FJsonValue>* Value = Find(Field))
			{
				if (!VectorFromJson(*Value, Out)) { Warn(Field, TEXT("[x, y]")); }
			}
		}

		const FJsonObject* Object(const TCHAR* Field) const
		{
			const TSharedPtr<FJsonValue>* Value = Find(Field);
			if (!Value)
			{
				return nullptr;
			}
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if ((*Value)->TryGetObject(Obj) && Obj && Obj->IsValid())
			{
				return Obj->Get();
			}
			Warn(Field, TEXT("an object"));
			return nullptr;
		}
	};

	float SaneFloat(float Value, float Min, float Max, float Default)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, Min, Max) : Default;
	}

	FVector2D SaneVector(const FVector2D& V, double Min, double Max)
	{
		return FVector2D(
			FMath::IsFinite(V.X) ? FMath::Clamp(V.X, Min, Max) : 0.0,
			FMath::IsFinite(V.Y) ? FMath::Clamp(V.Y, Min, Max) : 0.0);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  FValhallaPanelLayout
// ─────────────────────────────────────────────────────────────────────────────

bool FValhallaPanelLayout::Equals(const FValhallaPanelLayout& Other, float Tolerance) const
{
	return AnchorMin.Equals(Other.AnchorMin, Tolerance) && AnchorMax.Equals(Other.AnchorMax, Tolerance)
		&& Alignment.Equals(Other.Alignment, Tolerance) && Position.Equals(Other.Position, Tolerance)
		&& Size.Equals(Other.Size, Tolerance) && FMath::IsNearlyEqual(Scale, Other.Scale, Tolerance)
		&& bVisible == Other.bVisible && bSet == Other.bSet;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Colours
// ─────────────────────────────────────────────────────────────────────────────

FString FValhallaUserUISettings::ColourToHex(const FLinearColor& Colour)
{
	return TEXT("#") + Colour.ToFColor(/*bSRGB=*/true).ToHex().ToLower();
}

bool FValhallaUserUISettings::ColourFromHex(const FString& Hex, FLinearColor& Out)
{
	FString Digits = Hex.TrimStartAndEnd();
	Digits.RemoveFromStart(TEXT("#"));
	if (Digits.Len() != 6 && Digits.Len() != 8)
	{
		return false;
	}
	for (const TCHAR Ch : Digits)
	{
		if (!FChar::IsHexDigit(Ch))
		{
			return false;
		}
	}
	// FColor::FromHex reads RRGGBB and RRGGBBAA; sRGB bytes -> linear.
	Out = FLinearColor::FromSRGBColor(FColor::FromHex(Digits));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  JSON out
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<FJsonObject> FValhallaUserUISettings::ToJson() const
{
	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();

	// Unknown fields first, so a known field of the same name can never be shadowed by one.
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Unknown : UnknownFields)
	{
		if (Unknown.Value.IsValid() && !Key::IsKnownTopLevel(Unknown.Key))
		{
			Json->SetField(Unknown.Key, Unknown.Value);
		}
	}

	// A newer document keeps its version number: its unknown fields came back with it.
	Json->SetNumberField(Key::Version, FMath::Max(Version, CurrentVersion));
	Json->SetStringField(Key::UpdatedAt, UpdatedAt);
	Json->SetBoolField(Key::bLocked, bLocked);
	Json->SetNumberField(Key::UiScale, UiScale);
	Json->SetNumberField(Key::PanelOpacity, PanelOpacity);

	// Sorted keys: the same settings always write the same bytes (diffable cache files).
	TArray<FName> PanelKeys;
	Panels.GetKeys(PanelKeys);
	PanelKeys.Sort(FNameLexicalLess());
	const TSharedRef<FJsonObject> PanelsJson = MakeShared<FJsonObject>();
	for (const FName PanelKey : PanelKeys)
	{
		const FValhallaPanelLayout& Layout = Panels.FindChecked(PanelKey);
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetField(Key::AnchorMin, VectorToJson(Layout.AnchorMin));
		Entry->SetField(Key::AnchorMax, VectorToJson(Layout.AnchorMax));
		Entry->SetField(Key::Alignment, VectorToJson(Layout.Alignment));
		Entry->SetField(Key::Position, VectorToJson(Layout.Position));
		Entry->SetField(Key::Size, VectorToJson(Layout.Size));
		Entry->SetNumberField(Key::Scale, Layout.Scale);
		Entry->SetBoolField(Key::bVisible, Layout.bVisible);
		Entry->SetBoolField(Key::bSet, Layout.bSet);
		PanelsJson->SetObjectField(PanelKey.ToString(), Entry);
	}
	Json->SetObjectField(Key::Panels, PanelsJson);

	TArray<FName> ColourKeys;
	Colours.GetKeys(ColourKeys);
	ColourKeys.Sort(FNameLexicalLess());
	const TSharedRef<FJsonObject> ColoursJson = MakeShared<FJsonObject>();
	for (const FName ColourKey : ColourKeys)
	{
		ColoursJson->SetStringField(ColourKey.ToString(), ColourToHex(Colours.FindChecked(ColourKey)));
	}
	Json->SetObjectField(Key::Colours, ColoursJson);

	Json->SetNumberField(Key::ChatFontSize, ChatFontSize);
	Json->SetNumberField(Key::ChatVisibleLines, ChatVisibleLines);
	Json->SetBoolField(Key::bChatTimestamps, bChatTimestamps);

	TArray<FName> FilterKeys;
	LogFilters.GetKeys(FilterKeys);
	FilterKeys.Sort(FNameLexicalLess());
	const TSharedRef<FJsonObject> FiltersJson = MakeShared<FJsonObject>();
	for (const FName FilterKey : FilterKeys)
	{
		FiltersJson->SetBoolField(FilterKey.ToString(), LogFilters.FindChecked(FilterKey));
	}
	Json->SetObjectField(Key::LogFilters, FiltersJson);

	Json->SetBoolField(Key::bShowNpcNameplates, bShowNpcNameplates);
	Json->SetBoolField(Key::bShowPlayerNameplates, bShowPlayerNameplates);
	Json->SetBoolField(Key::bFloatingCombatText, bFloatingCombatText);
	Json->SetNumberField(Key::NameplateFontSize, NameplateFontSize);
	return Json;
}

FString FValhallaUserUISettings::ToJsonString(bool bPretty) const
{
	FString Out;
	const TSharedRef<FJsonObject> Json = ToJson();
	if (bPretty)
	{
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Json, Writer);
	}
	else
	{
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Json, Writer);
	}
	return Out;
}

// ─────────────────────────────────────────────────────────────────────────────
//  JSON in
// ─────────────────────────────────────────────────────────────────────────────

bool FValhallaUserUISettings::FromJson(const TSharedPtr<FJsonObject>& Json, FValhallaUserUISettings& Out, TArray<FString>* OutWarnings)
{
	Out = FValhallaUserUISettings();
	if (!Json.IsValid())
	{
		return false;
	}

	const FReader Root{ *Json, OutWarnings, FString() };
	Root.Int(Key::Version, Out.Version);
	if (Out.Version > CurrentVersion && OutWarnings)
	{
		OutWarnings->Add(FString::Printf(TEXT("Version %d is newer than this build's %d; loaded the fields it knows"), Out.Version, CurrentVersion));
	}
	Out.Version = FMath::Max(Out.Version, 1);
	Root.String(Key::UpdatedAt, Out.UpdatedAt);
	Root.Bool(Key::bLocked, Out.bLocked);
	Root.Number(Key::UiScale, Out.UiScale);
	Root.Number(Key::PanelOpacity, Out.PanelOpacity);

	if (const FJsonObject* PanelsJson = Root.Object(Key::Panels))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : PanelsJson->Values)
		{
			const TSharedPtr<FJsonObject>* EntryJson = nullptr;
			if (Entry.Key.IsEmpty() || !Entry.Value.IsValid() || !Entry.Value->TryGetObject(EntryJson) || !EntryJson || !EntryJson->IsValid())
			{
				if (OutWarnings) { OutWarnings->Add(FString::Printf(TEXT("Panels.%s: expected an object; skipped"), *Entry.Key)); }
				continue;
			}
			FValhallaPanelLayout Layout;
			const FReader Panel{ **EntryJson, OutWarnings, FString::Printf(TEXT("Panels.%s."), *Entry.Key) };
			Panel.Vector(Key::AnchorMin, Layout.AnchorMin);
			Panel.Vector(Key::AnchorMax, Layout.AnchorMax);
			Panel.Vector(Key::Alignment, Layout.Alignment);
			Panel.Vector(Key::Position, Layout.Position);
			Panel.Vector(Key::Size, Layout.Size);
			Panel.Number(Key::Scale, Layout.Scale);
			Panel.Bool(Key::bVisible, Layout.bVisible);
			Panel.Bool(Key::bSet, Layout.bSet);
			Out.Panels.Add(FName(*Entry.Key), Layout);
		}
	}

	if (const FJsonObject* ColoursJson = Root.Object(Key::Colours))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : ColoursJson->Values)
		{
			FLinearColor Colour;
			if (!Entry.Key.IsEmpty() && Entry.Value.IsValid() && Entry.Value->Type == EJson::String && ColourFromHex(Entry.Value->AsString(), Colour))
			{
				Out.Colours.Add(FName(*Entry.Key), Colour);
			}
			else if (OutWarnings)
			{
				OutWarnings->Add(FString::Printf(TEXT("Colours.%s: expected \"#rrggbbaa\"; skipped"), *Entry.Key));
			}
		}
	}

	Root.Int(Key::ChatFontSize, Out.ChatFontSize);
	Root.Int(Key::ChatVisibleLines, Out.ChatVisibleLines);
	Root.Bool(Key::bChatTimestamps, Out.bChatTimestamps);

	if (const FJsonObject* FiltersJson = Root.Object(Key::LogFilters))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : FiltersJson->Values)
		{
			if (!Entry.Key.IsEmpty() && Entry.Value.IsValid() && Entry.Value->Type == EJson::Boolean)
			{
				Out.LogFilters.Add(FName(*Entry.Key), Entry.Value->AsBool());
			}
			else if (OutWarnings)
			{
				OutWarnings->Add(FString::Printf(TEXT("LogFilters.%s: expected true or false; skipped"), *Entry.Key));
			}
		}
	}

	Root.Bool(Key::bShowNpcNameplates, Out.bShowNpcNameplates);
	Root.Bool(Key::bShowPlayerNameplates, Out.bShowPlayerNameplates);
	Root.Bool(Key::bFloatingCombatText, Out.bFloatingCombatText);
	Root.Int(Key::NameplateFontSize, Out.NameplateFontSize);

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Json->Values)
	{
		if (!Key::IsKnownTopLevel(Field.Key))
		{
			Out.UnknownFields.Add(Field.Key, Field.Value);
		}
	}

	Out.Sanitize();
	return true;
}

bool FValhallaUserUISettings::FromJsonString(const FString& Text, FValhallaUserUISettings& Out, TArray<FString>* OutWarnings)
{
	TSharedPtr<FJsonObject> Json;
	const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Text);
	if (Text.TrimStartAndEnd().IsEmpty() || !FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		Out = FValhallaUserUISettings();
		if (OutWarnings)
		{
			OutWarnings->Add(Text.TrimStartAndEnd().IsEmpty() ? TEXT("empty; using the defaults") : TEXT("not a JSON object; using the defaults"));
		}
		return false;
	}
	return FromJson(Json, Out, OutWarnings);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Edits
// ─────────────────────────────────────────────────────────────────────────────

void FValhallaUserUISettings::Sanitize()
{
	UiScale = SaneFloat(UiScale, MinUiScale, MaxUiScale, 1.f);
	PanelOpacity = SaneFloat(PanelOpacity, MinPanelOpacity, MaxPanelOpacity, 1.f);
	for (TPair<FName, FValhallaPanelLayout>& Entry : Panels)
	{
		FValhallaPanelLayout& Layout = Entry.Value;
		Layout.AnchorMin = SaneVector(Layout.AnchorMin, 0.0, 1.0);
		Layout.AnchorMax = SaneVector(Layout.AnchorMax, 0.0, 1.0);
		Layout.Alignment = SaneVector(Layout.Alignment, 0.0, 1.0);
		Layout.Position = SaneVector(Layout.Position, -MaxPanelOffset, MaxPanelOffset);
		Layout.Size = SaneVector(Layout.Size, 0.0, MaxPanelSize);
		Layout.Scale = SaneFloat(Layout.Scale, FValhallaPanelLayout::MinScale, FValhallaPanelLayout::MaxScale, 1.f);
	}
	ChatFontSize = FMath::Clamp(ChatFontSize, 0, MaxFontSize);
	ChatVisibleLines = FMath::Clamp(ChatVisibleLines, 0, MaxChatLines);
	NameplateFontSize = FMath::Clamp(NameplateFontSize, 0, MaxFontSize);
}

void FValhallaUserUISettings::ResetSection(EValhallaUISettingsSection Section)
{
	const FValhallaUserUISettings Defaults;
	switch (Section)
	{
	case EValhallaUISettingsSection::All:
	{
		// A reset keeps what a newer build stored: it is not this build's to drop.
		TMap<FString, TSharedPtr<FJsonValue>> Keep = MoveTemp(UnknownFields);
		const FString Stamp = UpdatedAt;
		*this = Defaults;
		UnknownFields = MoveTemp(Keep);
		UpdatedAt = Stamp;
		break;
	}
	case EValhallaUISettingsSection::Layout:
		Panels.Reset();
		break;
	case EValhallaUISettingsSection::Style:
		UiScale = Defaults.UiScale;
		PanelOpacity = Defaults.PanelOpacity;
		Colours.Reset();
		break;
	case EValhallaUISettingsSection::Chat:
		ChatFontSize = Defaults.ChatFontSize;
		ChatVisibleLines = Defaults.ChatVisibleLines;
		bChatTimestamps = Defaults.bChatTimestamps;
		LogFilters.Reset();
		break;
	case EValhallaUISettingsSection::Nameplates:
		bShowNpcNameplates = Defaults.bShowNpcNameplates;
		bShowPlayerNameplates = Defaults.bShowPlayerNameplates;
		bFloatingCombatText = Defaults.bFloatingCombatText;
		NameplateFontSize = Defaults.NameplateFontSize;
		break;
	}
}

void FValhallaUserUISettings::Touch()
{
	UpdatedAt = FDateTime::UtcNow().ToIso8601();
}

const FValhallaPanelLayout* FValhallaUserUISettings::FindSetPanel(FName Key) const
{
	const FValhallaPanelLayout* Layout = Panels.Find(Key);
	return Layout && Layout->bSet ? Layout : nullptr;
}

FDateTime FValhallaUserUISettings::GetUpdatedAtTime() const
{
	FDateTime Time;
	return !UpdatedAt.IsEmpty() && FDateTime::ParseIso8601(*UpdatedAt, Time) ? Time : FDateTime::MinValue();
}

bool FValhallaUserUISettings::EquivalentTo(const FValhallaUserUISettings& Other) const
{
	constexpr float Tolerance = 1.e-3f;
	if (bLocked != Other.bLocked || !FMath::IsNearlyEqual(UiScale, Other.UiScale, Tolerance)
		|| !FMath::IsNearlyEqual(PanelOpacity, Other.PanelOpacity, Tolerance)
		|| ChatFontSize != Other.ChatFontSize || ChatVisibleLines != Other.ChatVisibleLines || bChatTimestamps != Other.bChatTimestamps
		|| bShowNpcNameplates != Other.bShowNpcNameplates || bShowPlayerNameplates != Other.bShowPlayerNameplates
		|| bFloatingCombatText != Other.bFloatingCombatText || NameplateFontSize != Other.NameplateFontSize
		|| Panels.Num() != Other.Panels.Num() || Colours.Num() != Other.Colours.Num() || LogFilters.Num() != Other.LogFilters.Num())
	{
		return false;
	}
	for (const TPair<FName, FValhallaPanelLayout>& Entry : Panels)
	{
		const FValhallaPanelLayout* Theirs = Other.Panels.Find(Entry.Key);
		if (!Theirs || !Entry.Value.Equals(*Theirs, Tolerance))
		{
			return false;
		}
	}
	for (const TPair<FName, FLinearColor>& Entry : Colours)
	{
		const FLinearColor* Theirs = Other.Colours.Find(Entry.Key);
		if (!Theirs || Entry.Value.ToFColor(true) != Theirs->ToFColor(true))
		{
			return false;
		}
	}
	for (const TPair<FName, bool>& Entry : LogFilters)
	{
		const bool* Theirs = Other.LogFilters.Find(Entry.Key);
		if (!Theirs || *Theirs != Entry.Value)
		{
			return false;
		}
	}
	return true;
}
