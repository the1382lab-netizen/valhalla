// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaUIArt.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Fonts/CompositeFont.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "ValhallaDataSettings.h"
#include "ValhallaDataSubsystem.h"

namespace ValhallaUIArt
{
	// sRGB hex from the design canvas (Option A, "Gilded Hall").
	const FLinearColor Background     = FLinearColor::FromSRGBColor(FColor(0x0C, 0x0A, 0x07));
	const FLinearColor BackgroundGlow = FLinearColor::FromSRGBColor(FColor(0x2A, 0x21, 0x12));
	const FLinearColor Panel          = FLinearColor::FromSRGBColor(FColor(0x15, 0x11, 0x0A));
	const FLinearColor Row            = FLinearColor::FromSRGBColor(FColor(0x17, 0x12, 0x0A));
	const FLinearColor RowHover       = FLinearColor::FromSRGBColor(FColor(0x1D, 0x17, 0x0C));
	const FLinearColor RowSelected    = FLinearColor::FromSRGBColor(FColor(0x2A, 0x20, 0x10));
	const FLinearColor Gold           = FLinearColor::FromSRGBColor(FColor(0xB0, 0x8D, 0x3C));
	const FLinearColor GoldLight      = FLinearColor::FromSRGBColor(FColor(0xD9, 0xB6, 0x5E));
	const FLinearColor Line           = FLinearColor::FromSRGBColor(FColor(0x5C, 0x4A, 0x24));
	const FLinearColor Ink            = FLinearColor::FromSRGBColor(FColor(0xF1, 0xE6, 0xCC));
	const FLinearColor InkSoft        = FLinearColor::FromSRGBColor(FColor(0xC9, 0xB8, 0x8F));
	const FLinearColor InkDim         = FLinearColor::FromSRGBColor(FColor(0x9A, 0x8B, 0x6C));
	const FLinearColor InkOnGold      = FLinearColor::FromSRGBColor(FColor(0x1A, 0x13, 0x05));
	const FLinearColor Danger         = FLinearColor::FromSRGBColor(FColor(0xE2, 0xA4, 0x97));
	const FLinearColor Online         = FLinearColor::FromSRGBColor(FColor(0x7F, 0xB0, 0x69));

	namespace
	{
		/** <repo>/Import/UI, next to the data root's <repo>/shared/data. */
		FString ImportUiDir()
		{
			return FPaths::ConvertRelativePathToFull(FPaths::Combine(
				UValhallaDataSettings::Get()->GetResolvedDataRoot(), TEXT("../../Import/UI")));
		}

		/**
		 * A font family: the UFont asset when one is made; else the imported font
		 * faces (/Game/Valhalla/UI/Fonts/FF_<File>, what a packaged build has) as a
		 * runtime composite font; else the TTFs in Import/UI/Fonts the same way
		 * (editor and dev before the import); else nothing (the engine font).
		 */
		struct FFamily
		{
			TWeakObjectPtr<const UFont> Asset;
			TSharedPtr<const FCompositeFont> Disk;
			bool bResolved = false;
		};

		FFamily& ResolveFamily(FFamily& Family, const TCHAR* AssetPath, const TArray<TPair<FName, FString>>& DiskFaces)
		{
			if (Family.bResolved && (Family.Asset.IsValid() || Family.Disk.IsValid()))
			{
				return Family;
			}
			Family.bResolved = true;

			if (const UFont* Font = LoadObject<UFont>(nullptr, AssetPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				Family.Asset = Font;
				return Family;
			}

			TSharedRef<FStandaloneCompositeFont> Composite = MakeShared<FStandaloneCompositeFont>();
			bool bAny = false;
			for (const TPair<FName, FString>& Face : DiskFaces)
			{
				// Cinzel-Regular.ttf -> FF_Cinzel_Regular (import_ui_fonts.py).
				const FString AssetName = TEXT("FF_") + FPaths::GetBaseFilename(Face.Value).Replace(TEXT("-"), TEXT("_"));
				const FString FacePath = FString::Printf(TEXT("/Game/Valhalla/UI/Fonts/%s.%s"), *AssetName, *AssetName);
				if (const UFontFace* FontFace = LoadObject<UFontFace>(nullptr, *FacePath, nullptr, LOAD_NoWarn | LOAD_Quiet))
				{
					FTypefaceEntry& Entry = Composite->DefaultTypeface.Fonts.Add_GetRef(FTypefaceEntry(Face.Key));
					Entry.Font = FFontData(FontFace);
					bAny = true;
					continue;
				}

				const FString Path = FPaths::Combine(ImportUiDir(), TEXT("Fonts"), Face.Value);
				if (FPaths::FileExists(Path))
				{
					Composite->DefaultTypeface.AppendFont(Face.Key, Path, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
					bAny = true;
				}
			}
			if (bAny)
			{
				Family.Disk = Composite;
			}
			return Family;
		}

		FSlateFontInfo FromFamily(const FFamily& Family, int32 Size, FName Typeface, FName FallbackStyle)
		{
			if (const UFont* Font = Family.Asset.Get())
			{
				return FSlateFontInfo(Font, Size, Typeface);
			}
			if (Family.Disk.IsValid())
			{
				return FSlateFontInfo(Family.Disk, Size, Typeface);
			}
			return FCoreStyle::GetDefaultFontStyle(FallbackStyle, Size);
		}
	}

	FSlateFontInfo DisplayFont(int32 Size, FName Typeface)
	{
		static FFamily Family;
		ResolveFamily(Family, TEXT("/Game/Valhalla/UI/Fonts/F_Cinzel.F_Cinzel"),
			{ { TEXT("Regular"), TEXT("Cinzel-Regular.ttf") }, { TEXT("Bold"), TEXT("Cinzel-Bold.ttf") } });
		return FromFamily(Family, Size, Typeface, Typeface == TEXT("Bold") ? TEXT("Bold") : TEXT("Regular"));
	}

	FSlateFontInfo BodyFont(int32 Size, FName Typeface)
	{
		static FFamily Family;
		ResolveFamily(Family, TEXT("/Game/Valhalla/UI/Fonts/F_EBGaramond.F_EBGaramond"),
			{ { TEXT("Regular"), TEXT("EBGaramond-Regular.ttf") }, { TEXT("SemiBold"), TEXT("EBGaramond-SemiBold.ttf") },
			  { TEXT("Italic"), TEXT("EBGaramond-Italic.ttf") } });
		return FromFamily(Family, Size, Typeface, Typeface == TEXT("SemiBold") ? TEXT("Bold") : TEXT("Regular"));
	}

	UTexture2D* LoadUiTexture(const FString& Folder, const FString& Name)
	{
		static TMap<FString, TWeakObjectPtr<UTexture2D>> Cache;
		const FString Key = Folder + TEXT("/") + Name;
		if (const TWeakObjectPtr<UTexture2D>* Found = Cache.Find(Key))
		{
			if (UTexture2D* Texture = Found->Get())
			{
				return Texture;
			}
		}

		const FString AssetPath = FString::Printf(TEXT("/Game/Valhalla/UI/%s/%s.%s"), *Folder, *Name, *Name);
		UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *AssetPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Texture)
		{
			const FString DiskPath = FPaths::Combine(ImportUiDir(), Folder, Name + TEXT(".png"));
			if (FPaths::FileExists(DiskPath))
			{
				Texture = FImageUtils::ImportFileAsTexture2D(DiskPath);
				if (Texture)
				{
					Texture->Filter = TF_Bilinear;
					Texture->UpdateResource();
					// A transient texture with no owner; rooted so the cache entry
					// (and every brush that uses it) stays valid.
					Texture->AddToRoot();
				}
			}
		}
		if (Texture)
		{
			Cache.Add(Key, Texture);
		}
		return Texture;
	}

	FSlateBrush PanelFrameBrush()
	{
		FSlateBrush Brush;
		if (UTexture2D* Frame = LoadUiTexture(TEXT("Frames"), TEXT("T_UI_Panel")))
		{
			Brush.SetResourceObject(Frame);
			Brush.ImageSize = FVector2D(Frame->GetSizeX(), Frame->GetSizeY());
			Brush.DrawAs = ESlateBrushDrawType::Box;
			// T_UI_Panel's trim is 24 px of 256 (Tools/ui/make_panel_frames.py).
			Brush.Margin = FMargin(24.f / 256.f);
			return Brush;
		}
		// No frame art yet: a gold outline over the panel colour.
		return FSlateRoundedBoxBrush(Panel, 0.f, Gold, 2.f);
	}

	FSlateBrush SolidBrush(const FLinearColor& Colour)
	{
		return FSlateColorBrush(Colour);
	}

	FSlateBrush OutlineBrush(const FLinearColor& Colour, float Thickness)
	{
		return FSlateRoundedBoxBrush(FLinearColor::Transparent, 0.f, Colour, Thickness);
	}

	FString ClassIconName(FName ClassId, const FString& IconField)
	{
		const FString Own = IconField.TrimStartAndEnd();
		if (!Own.IsEmpty())
		{
			return FPaths::GetBaseFilename(Own);
		}
		FString Id = ClassId.IsNone() ? FString() : ClassId.ToString();
		if (Id.IsEmpty())
		{
			return TEXT("T_ClassIcon_Default");
		}
		Id[0] = FChar::ToUpper(Id[0]);
		return TEXT("T_ClassIcon_") + Id;
	}

	UTexture2D* ClassIconFor(const UObject* WorldContext, FName ClassId)
	{
		FString IconField;
		if (const UGameInstance* GameInstance = WorldContext ? UGameplayStatics::GetGameInstance(WorldContext) : nullptr)
		{
			if (const UValhallaDataSubsystem* Data = GameInstance->GetSubsystem<UValhallaDataSubsystem>())
			{
				if (const FValhallaClassTemplate* Class = Data->FindClass(ClassId))
				{
					IconField = Class->Icon;
				}
			}
		}

		if (UTexture2D* Icon = LoadUiTexture(TEXT("ClassIcons"), ClassIconName(ClassId, IconField)))
		{
			return Icon;
		}
		return LoadUiTexture(TEXT("ClassIcons"), TEXT("T_ClassIcon_Default"));
	}
}
