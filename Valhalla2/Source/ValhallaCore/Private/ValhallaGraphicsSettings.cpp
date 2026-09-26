// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaGraphicsSettings.h"

#include "Policies/CondensedJsonPrintPolicy.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	const TCHAR* const KnownFields[] = {
		TEXT("Version"), TEXT("UpdatedAt"), TEXT("Quality"), TEXT("GlobalIllumination"),
		TEXT("ResolutionScale"), TEXT("FrameRateCap"), TEXT("VSync"), TEXT("MotionBlur"),
	};

	bool IsKnownField(const FString& Name)
	{
		for (const TCHAR* Known : KnownFields)
		{
			if (Name == Known)
			{
				return true;
			}
		}
		return false;
	}
}

void FValhallaGraphicsSettings::SetPreset(EValhallaGraphicsQuality InQuality)
{
	Quality = InQuality;
	bGlobalIllumination = PresetGlobalIllumination(InQuality);
}

int32 FValhallaGraphicsSettings::GetMotionBlurQualityCVar() const
{
	// BaseScalability's PostProcessQuality: 3 up to High, 4 on Epic. Low's own 0
	// is not used: the check box is the player's choice whatever the preset.
	return bMotionBlur ? (Quality == EValhallaGraphicsQuality::Epic ? 4 : 3) : 0;
}

int32 FValhallaGraphicsSettings::GetGlobalIlluminationLevel() const
{
	return bGlobalIllumination ? FMath::Max(1, static_cast<int32>(Quality)) : 0;
}

void FValhallaGraphicsSettings::Sanitize()
{
	Quality = static_cast<EValhallaGraphicsQuality>(FMath::Clamp(static_cast<int32>(Quality), 0, 3));
	ResolutionScale = FMath::Clamp(ResolutionScale, MinResolutionScale, MaxResolutionScale);
	if (FrameRateCap <= 0)
	{
		FrameRateCap = 0;
	}
	else
	{
		FrameRateCap = FMath::Clamp(FrameRateCap, MinFrameRateCap, MaxFrameRateCap);
	}
	Version = FMath::Max(Version, 1);
}

const TArray<int32>& FValhallaGraphicsSettings::GetFrameRateCapSteps()
{
	static const TArray<int32> Steps = { 0, 30, 60, 90, 120, 144, 165, 240 };
	return Steps;
}

const TCHAR* FValhallaGraphicsSettings::QualityToString(EValhallaGraphicsQuality InQuality)
{
	switch (InQuality)
	{
	case EValhallaGraphicsQuality::Low: return TEXT("low");
	case EValhallaGraphicsQuality::Medium: return TEXT("medium");
	case EValhallaGraphicsQuality::Epic: return TEXT("epic");
	default: return TEXT("high");
	}
}

bool FValhallaGraphicsSettings::QualityFromString(const FString& Text, EValhallaGraphicsQuality& Out)
{
	for (int32 Level = 0; Level <= 3; ++Level)
	{
		const EValhallaGraphicsQuality Candidate = static_cast<EValhallaGraphicsQuality>(Level);
		if (Text.Equals(QualityToString(Candidate), ESearchCase::IgnoreCase))
		{
			Out = Candidate;
			return true;
		}
	}
	return false;
}

FString FValhallaGraphicsSettings::GetPresetDisplayName() const
{
	if (IsCustom())
	{
		return TEXT("Custom");
	}
	FString Name = QualityToString(Quality);
	Name[0] = FChar::ToUpper(Name[0]);
	return Name;
}

TSharedRef<FJsonObject> FValhallaGraphicsSettings::ToJson() const
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Unknown : UnknownFields)
	{
		Json->SetField(Unknown.Key, Unknown.Value);
	}
	Json->SetNumberField(TEXT("Version"), Version);
	Json->SetStringField(TEXT("UpdatedAt"), UpdatedAt);
	Json->SetStringField(TEXT("Quality"), QualityToString(Quality));
	Json->SetBoolField(TEXT("GlobalIllumination"), bGlobalIllumination);
	Json->SetNumberField(TEXT("ResolutionScale"), ResolutionScale);
	Json->SetNumberField(TEXT("FrameRateCap"), FrameRateCap);
	Json->SetBoolField(TEXT("VSync"), bVSync);
	Json->SetBoolField(TEXT("MotionBlur"), bMotionBlur);
	return Json;
}

FString FValhallaGraphicsSettings::ToJsonString(bool bPretty) const
{
	FString Out;
	if (bPretty)
	{
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(ToJson(), Writer);
	}
	else
	{
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(ToJson(), Writer);
	}
	return Out;
}

bool FValhallaGraphicsSettings::FromJson(const TSharedPtr<FJsonObject>& Json, FValhallaGraphicsSettings& Out, TArray<FString>* OutWarnings)
{
	Out = FValhallaGraphicsSettings();
	if (!Json.IsValid())
	{
		return false;
	}

	auto Warn = [OutWarnings](const FString& Message)
	{
		if (OutWarnings)
		{
			OutWarnings->Add(Message);
		}
	};
	auto ReadBool = [&Json, &Warn](const TCHAR* Name, bool& Field)
	{
		const TSharedPtr<FJsonValue> Value = Json->TryGetField(Name);
		if (!Value.IsValid())
		{
			return;
		}
		if (Value->Type != EJson::Boolean)
		{
			Warn(FString::Printf(TEXT("%s is not true / false; kept %s."), Name, Field ? TEXT("true") : TEXT("false")));
			return;
		}
		Field = Value->AsBool();
	};
	auto ReadInt = [&Json, &Warn](const TCHAR* Name, int32& Field)
	{
		const TSharedPtr<FJsonValue> Value = Json->TryGetField(Name);
		if (!Value.IsValid())
		{
			return;
		}
		if (Value->Type != EJson::Number)
		{
			Warn(FString::Printf(TEXT("%s is not a number; kept %d."), Name, Field));
			return;
		}
		Field = FMath::RoundToInt(Value->AsNumber());
	};

	ReadInt(TEXT("Version"), Out.Version);
	Json->TryGetStringField(TEXT("UpdatedAt"), Out.UpdatedAt);

	FString QualityText;
	if (Json->TryGetStringField(TEXT("Quality"), QualityText))
	{
		if (!QualityFromString(QualityText, Out.Quality))
		{
			Warn(FString::Printf(TEXT("Quality '%s' is not low / medium / high / epic; kept high."), *QualityText));
		}
	}
	// The preset's GI choice unless the document says otherwise.
	Out.bGlobalIllumination = PresetGlobalIllumination(Out.Quality);
	ReadBool(TEXT("GlobalIllumination"), Out.bGlobalIllumination);
	ReadInt(TEXT("ResolutionScale"), Out.ResolutionScale);
	ReadInt(TEXT("FrameRateCap"), Out.FrameRateCap);
	ReadBool(TEXT("VSync"), Out.bVSync);
	ReadBool(TEXT("MotionBlur"), Out.bMotionBlur);

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Json->Values)
	{
		if (!IsKnownField(Field.Key))
		{
			Out.UnknownFields.Add(Field.Key, Field.Value);
		}
	}

	const FValhallaGraphicsSettings Before = Out;
	Out.Sanitize();
	if (Out.ResolutionScale != Before.ResolutionScale || Out.FrameRateCap != Before.FrameRateCap)
	{
		Warn(FString::Printf(TEXT("clamped: ResolutionScale %d -> %d, FrameRateCap %d -> %d."),
			Before.ResolutionScale, Out.ResolutionScale, Before.FrameRateCap, Out.FrameRateCap));
	}
	return true;
}

bool FValhallaGraphicsSettings::FromJsonString(const FString& Text, FValhallaGraphicsSettings& Out, TArray<FString>* OutWarnings)
{
	TSharedPtr<FJsonObject> Json;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
	{
		Out = FValhallaGraphicsSettings();
		if (OutWarnings)
		{
			OutWarnings->Add(TEXT("not a JSON object; using the defaults."));
		}
		return false;
	}
	return FromJson(Json, Out, OutWarnings);
}

void FValhallaGraphicsSettings::Touch()
{
	UpdatedAt = FDateTime::UtcNow().ToIso8601();
}

FDateTime FValhallaGraphicsSettings::GetUpdatedAtTime() const
{
	FDateTime Time;
	return !UpdatedAt.IsEmpty() && FDateTime::ParseIso8601(*UpdatedAt, Time) ? Time : FDateTime::MinValue();
}

bool FValhallaGraphicsSettings::SameValues(const FValhallaGraphicsSettings& Other) const
{
	return Quality == Other.Quality
		&& bGlobalIllumination == Other.bGlobalIllumination
		&& ResolutionScale == Other.ResolutionScale
		&& FrameRateCap == Other.FrameRateCap
		&& bVSync == Other.bVSync
		&& bMotionBlur == Other.bMotionBlur;
}
