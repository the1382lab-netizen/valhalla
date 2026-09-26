// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaFrontEnd.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "Styling/CoreStyle.h"
#include "ValhallaBackendSubsystem.h"
#include "ValhallaCharacterPreviewStage.h"
#include "ValhallaConstants.h"
#include "ValhallaDataSettings.h"
#include "ValhallaDataSubsystem.h"
#include "ValhallaGraphicsSettingsSubsystem.h"
#include "ValhallaUIArt.h"
#include "ValhallaUserSettingsSubsystem.h"

DEFINE_LOG_CATEGORY(LogValhallaFrontEnd);

namespace
{
	/**
	 * Where a client travels when there is no dedicated server to travel to.
	 *
	 * PIE's own server port, `ULevelEditorPlaySettings::ServerPort`, defaults
	 * to 17777 and is not 7777 — so a front end that used GameServerAddress in
	 * PIE would fail to connect and look exactly like a backend problem.
	 */
	TAutoConsoleVariable<FString> CVarPieServerAddress(
		TEXT("valhalla.FrontEnd.PieServerAddress"),
		TEXT("127.0.0.1:17777"),
		TEXT("Dev only. Where the front end travels in PIE. ULevelEditorPlaySettings::ServerPort's default."),
		ECVF_Default);

	/**
	 * Comma-separated `user:pass:charname[:class]` entries, dealt to front-end
	 * clients in the order they start. See AValhallaFrontEndController's
	 * class comment for why this is a list and not a single value.
	 */
	TAutoConsoleVariable<FString> CVarAutoLogin(
		TEXT("valhalla.AutoLogin"),
		TEXT(""),
		TEXT("Dev only. Comma-separated 'user:pass:charname[:class]' dealt to front-end clients in start order. Registers and creates what is missing."),
		ECVF_Default);

	FText AsText(const FString& In) { return FText::FromString(In); }

	// ── B-08a: the Gilded Hall look (ValhallaUIArt) ─────────────────────────
	//
	// Sizes are in UMG units at 1080 p (the DPI curve scales them): the design
	// canvas was drawn at 1440 x 810, so its pixel sizes are x 1.33 here and its
	// font px are roughly the point sizes below.

	/** A label in the given font. */
	UTextBlock* MakeText(UWidgetTree& Tree, const FString& Content, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		UTextBlock* Text = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Text->SetText(AsText(Content));
		Text->SetFont(Font);
		Text->SetColorAndOpacity(FSlateColor(Color));
		return Text;
	}

	/** Body text (EB Garamond). */
	UTextBlock* MakeBody(UWidgetTree& Tree, const FString& Content, int32 Size, const FLinearColor& Color, FName Typeface = TEXT("Regular"))
	{
		return MakeText(Tree, Content, ValhallaUIArt::BodyFont(Size, Typeface), Color);
	}

	/** Display text (Cinzel), with optional letter spacing in 1/1000 em. */
	UTextBlock* MakeDisplay(UWidgetTree& Tree, const FString& Content, int32 Size, const FLinearColor& Color,
		FName Typeface = TEXT("Regular"), int32 LetterSpacing = 0)
	{
		FSlateFontInfo Font = ValhallaUIArt::DisplayFont(Size, Typeface);
		Font.LetterSpacing = LetterSpacing;
		return MakeText(Tree, Content, Font, Color);
	}

	UImage* MakeImage(UWidgetTree& Tree, const FSlateBrush& Brush)
	{
		UImage* Image = Tree.ConstructWidget<UImage>(UImage::StaticClass());
		Image->SetBrush(Brush);
		return Image;
	}

	/** An image of a UI texture (Frames/...), or a transparent stand-in when it is missing. */
	UImage* MakeArt(UWidgetTree& Tree, const TCHAR* Name, const FVector2D& Size)
	{
		FSlateBrush Brush;
		if (UTexture2D* Texture = ValhallaUIArt::LoadUiTexture(TEXT("Frames"), Name))
		{
			Brush.SetResourceObject(Texture);
		}
		else
		{
			Brush.TintColor = FSlateColor(FLinearColor::Transparent);
		}
		Brush.ImageSize = Size;
		return MakeImage(Tree, Brush);
	}

	/** Fill a slot of an overlay. */
	UOverlaySlot* FillOverlay(UOverlay* Overlay, UWidget* Child, const FMargin& Padding = FMargin(0.f))
	{
		UOverlaySlot* Slot = Overlay->AddChildToOverlay(Child);
		Slot->SetHorizontalAlignment(HAlign_Fill);
		Slot->SetVerticalAlignment(VAlign_Fill);
		Slot->SetPadding(Padding);
		return Slot;
	}

	/**
	 * The page behind both screens: the dark ground, the warm glow
	 * (T_UI_FrontEndBackdrop, when present) and the two thin inset frame lines.
	 */
	UOverlay* MakePage(UWidgetTree& Tree)
	{
		UOverlay* Page = Tree.ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Root"));
		FillOverlay(Page, MakeImage(Tree, ValhallaUIArt::SolidBrush(ValhallaUIArt::Background)));
		if (UTexture2D* Backdrop = ValhallaUIArt::LoadUiTexture(TEXT("Frames"), TEXT("T_UI_FrontEndBackdrop")))
		{
			FSlateBrush Brush;
			Brush.SetResourceObject(Backdrop);
			FillOverlay(Page, MakeImage(Tree, Brush));
		}
		UImage* Outer = MakeImage(Tree, ValhallaUIArt::OutlineBrush(FLinearColor::FromSRGBColor(FColor(0x3A, 0x2E, 0x16)), 1.f));
		Outer->SetVisibility(ESlateVisibility::HitTestInvisible);
		FillOverlay(Page, Outer, FMargin(24.f));
		UImage* Inner = MakeImage(Tree, ValhallaUIArt::OutlineBrush(FLinearColor::FromSRGBColor(FColor(0x24, 0x1C, 0x0E)), 1.f));
		Inner->SetVisibility(ESlateVisibility::HitTestInvisible);
		FillOverlay(Page, Inner, FMargin(32.f));
		return Page;
	}

	/** The gold-framed panel (T_UI_Panel, the HUD's own frame). */
	UBorder* MakePanel(UWidgetTree& Tree, const FMargin& Padding)
	{
		UBorder* Panel = Tree.ConstructWidget<UBorder>(UBorder::StaticClass());
		Panel->SetBrush(ValhallaUIArt::PanelFrameBrush());
		Panel->SetBrushColor(FLinearColor::White);
		Panel->SetPadding(Padding);
		return Panel;
	}

	/** Put Content in Button, centred, with padding. */
	void SetButtonContent(UButton* Button, UWidget* Content, const FMargin& Padding)
	{
		if (UButtonSlot* Slot = Cast<UButtonSlot>(Button->AddChild(Content)))
		{
			Slot->SetHorizontalAlignment(HAlign_Center);
			Slot->SetVerticalAlignment(VAlign_Center);
			Slot->SetPadding(Padding);
		}
	}

	FButtonStyle StyleFrom(const FSlateBrush& Normal, const FSlateBrush& Hovered, const FSlateBrush& Pressed)
	{
		FButtonStyle Style;
		Style.SetNormal(Normal);
		Style.SetHovered(Hovered);
		Style.SetPressed(Pressed);
		FSlateBrush Disabled = Normal;
		Disabled.TintColor = FSlateColor(Normal.TintColor.GetSpecifiedColor() * FLinearColor(1.f, 1.f, 1.f, 0.45f));
		Style.SetDisabled(Disabled);
		Style.SetNormalPadding(FMargin(0.f));
		Style.SetPressedPadding(FMargin(0.f));
		return Style;
	}

	/** The gold primary button (Log In, Enter World, Create Character). */
	UButton* MakeGoldButton(UWidgetTree& Tree, const FString& Caption, int32 FontSize, const FMargin& Padding)
	{
		UButton* Button = Tree.ConstructWidget<UButton>(UButton::StaticClass());
		FSlateBrush Normal;
		if (UTexture2D* Art = ValhallaUIArt::LoadUiTexture(TEXT("Frames"), TEXT("T_UI_Button")))
		{
			Normal.SetResourceObject(Art);
			Normal.DrawAs = ESlateBrushDrawType::Box;
			Normal.Margin = FMargin(0.25f);
		}
		else
		{
			Normal = FSlateRoundedBoxBrush(ValhallaUIArt::Gold, 0.f, ValhallaUIArt::GoldLight, 1.f);
		}
		FSlateBrush Hovered = Normal;
		Hovered.TintColor = FSlateColor(FLinearColor(1.18f, 1.14f, 1.05f, 1.f));
		FSlateBrush Pressed = Normal;
		Pressed.TintColor = FSlateColor(FLinearColor(0.82f, 0.8f, 0.76f, 1.f));
		Button->SetStyle(StyleFrom(Normal, Hovered, Pressed));
		Button->SetBackgroundColor(FLinearColor::White);
		SetButtonContent(Button, MakeDisplay(Tree, Caption, FontSize, ValhallaUIArt::InkOnGold, TEXT("Bold"), 140), Padding);
		return Button;
	}

	/** An outlined, transparent button (Account, Log Out, Create Character, Cancel). */
	UButton* MakeGhostButton(UWidgetTree& Tree, const FString& Caption, const FLinearColor& Line, const FLinearColor& Ink, const FMargin& Padding)
	{
		UButton* Button = Tree.ConstructWidget<UButton>(UButton::StaticClass());
		Button->SetStyle(StyleFrom(
			FSlateRoundedBoxBrush(FLinearColor::Transparent, 0.f, Line, 1.f),
			FSlateRoundedBoxBrush(FLinearColor(1.f, 1.f, 1.f, 0.04f), 0.f, ValhallaUIArt::Gold, 1.f),
			FSlateRoundedBoxBrush(FLinearColor(1.f, 1.f, 1.f, 0.08f), 0.f, ValhallaUIArt::GoldLight, 1.f)));
		Button->SetBackgroundColor(FLinearColor::White);
		SetButtonContent(Button, MakeDisplay(Tree, Caption, 13, Ink, TEXT("Regular"), 120), Padding);
		return Button;
	}

	/** A text-only link button ("Create an account"). */
	UButton* MakeLinkButton(UWidgetTree& Tree, const FString& Caption, int32 Size)
	{
		UButton* Button = Tree.ConstructWidget<UButton>(UButton::StaticClass());
		FSlateBrush Clear;
		Clear.DrawAs = ESlateBrushDrawType::NoDrawType;
		Button->SetStyle(StyleFrom(Clear, Clear, Clear));
		SetButtonContent(Button, MakeBody(Tree, Caption, Size, ValhallaUIArt::GoldLight), FMargin(2.f, 0.f));
		return Button;
	}

	/** A text field in the Gilded Hall style. */
	UEditableTextBox* MakeInput(UWidgetTree& Tree, const FString& Hint, bool bPassword)
	{
		UEditableTextBox* Box = Tree.ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
		FEditableTextBoxStyle Style = FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");
		Style.SetBackgroundImageNormal(FSlateRoundedBoxBrush(ValhallaUIArt::Background, 0.f, ValhallaUIArt::Line, 1.f));
		Style.SetBackgroundImageHovered(FSlateRoundedBoxBrush(ValhallaUIArt::Background, 0.f, ValhallaUIArt::Gold, 1.f));
		Style.SetBackgroundImageFocused(FSlateRoundedBoxBrush(ValhallaUIArt::Background, 0.f, ValhallaUIArt::GoldLight, 1.5f));
		Style.SetBackgroundImageReadOnly(FSlateRoundedBoxBrush(ValhallaUIArt::Background, 0.f, ValhallaUIArt::Line, 1.f));
		Style.TextStyle.SetFont(ValhallaUIArt::BodyFont(17));
		Style.TextStyle.SetColorAndOpacity(FSlateColor(ValhallaUIArt::Ink));
		Style.SetForegroundColor(FSlateColor(ValhallaUIArt::Ink));
		Style.SetFocusedForegroundColor(FSlateColor(ValhallaUIArt::Ink));
		Style.SetBackgroundColor(FSlateColor(FLinearColor::White));
		Style.SetPadding(FMargin(16.f, 12.f));
		Box->SetWidgetStyle(Style);
		Box->SetHintText(AsText(Hint));
		Box->SetIsPassword(bPassword);
		return Box;
	}

	/** A label over a field. */
	UVerticalBox* MakeField(UWidgetTree& Tree, const FString& Label, UWidget* Field)
	{
		UVerticalBox* Box = Tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		Box->AddChildToVerticalBox(MakeBody(Tree, Label, 15, ValhallaUIArt::InkSoft));
		Box->AddChildToVerticalBox(Field)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
		return Box;
	}

	/** A 1 px horizontal rule. */
	UImage* MakeRule(UWidgetTree& Tree)
	{
		UImage* Rule = MakeImage(Tree, ValhallaUIArt::SolidBrush(FLinearColor::FromSRGBColor(FColor(0x2D, 0x24, 0x11))));
		Rule->SetDesiredSizeOverride(FVector2D(1.f, 1.f));
		return Rule;
	}

	/** The class's display name ("Warrior"), from classes.json; the id when unknown. */
	FString ClassDisplayName(const UObject* Context, FName ClassId)
	{
		const UGameInstance* GameInstance = Context ? UGameplayStatics::GetGameInstance(Context) : nullptr;
		const UValhallaDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
		if (const FValhallaClassTemplate* Class = Data ? Data->FindClass(ClassId) : nullptr)
		{
			if (!Class->Name.IsEmpty())
			{
				return Class->Name;
			}
		}
		FString Id = ClassId.ToString();
		if (!Id.IsEmpty())
		{
			Id[0] = FChar::ToUpper(Id[0]);
		}
		return Id;
	}

	/** The zone's display name ("Eldmoor Grasslands"), from zones.json; empty when unknown. */
	FString ZoneDisplayName(const UObject* Context, FName ZoneId)
	{
		if (ZoneId.IsNone())
		{
			return FString();
		}
		const UGameInstance* GameInstance = Context ? UGameplayStatics::GetGameInstance(Context) : nullptr;
		const UValhallaDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
		const FValhallaZoneConfig* Zone = Data ? Data->FindZone(ZoneId) : nullptr;
		return Zone && !Zone->Name.IsEmpty() ? Zone->Name : ZoneId.ToString();
	}

	/** An image of a class icon at Size, or a transparent stand-in. */
	UImage* MakeClassIcon(UWidgetTree& Tree, const UObject* Context, FName ClassId, float Size)
	{
		FSlateBrush Brush;
		if (UTexture2D* Icon = ValhallaUIArt::ClassIconFor(Context, ClassId))
		{
			Brush.SetResourceObject(Icon);
		}
		else
		{
			Brush.TintColor = FSlateColor(FLinearColor::Transparent);
		}
		Brush.ImageSize = FVector2D(Size, Size);
		UImage* Image = MakeImage(Tree, Brush);
		Image->SetDesiredSizeOverride(FVector2D(Size, Size));
		return Image;
	}

	/** The game version for the login screen's corner: [/Script/EngineSettings.GeneralProjectSettings] ProjectVersion. */
	FString GameVersion()
	{
		FString Version;
		GConfig->GetString(TEXT("/Script/EngineSettings.GeneralProjectSettings"), TEXT("ProjectVersion"), Version, GGameIni);
		return Version.IsEmpty() ? TEXT("0") : Version;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Row button
// ─────────────────────────────────────────────────────────────────────────────

UValhallaCharacterRowButton::UValhallaCharacterRowButton(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InitIsFocusable(false);
}

void UValhallaCharacterRowButton::HandleClicked()
{
	if (UValhallaCharacterSelectWidget* Widget = Screen.Get())
	{
		Widget->HandleRowClicked(RowIndex);
	}
}

UValhallaClassCardButton::UValhallaClassCardButton(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	InitIsFocusable(false);
}

void UValhallaClassCardButton::HandleClicked()
{
	if (UValhallaCharacterSelectWidget* Widget = Screen.Get())
	{
		Widget->HandleClassCardClicked(CardIndex);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Login screen
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaLoginWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// Before RebuildWidget, which is what turns the tree into Slate. Building
	// in NativeConstruct instead would build a tree nothing ever looks at
	// again, which is the one mistake this whole file is written to avoid.
	BuildUi();
}

void UValhallaLoginWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Belt to NativeOnInitialized's braces: a widget constructed by a path
	// that skipped Initialize still gets a tree, and the guard inside makes
	// the second call free.
	BuildUi();

	SetError(FString());
	CheckServer();

	// B-08a: type straight away — the password when the username is remembered.
	if (!bFocusedOnce)
	{
		bFocusedOnce = true;
		const bool bHaveName = UsernameBox && !UsernameBox->GetText().IsEmpty();
		if (UEditableTextBox* Target = bHaveName ? PasswordBox.Get() : UsernameBox.Get())
		{
			Target->SetKeyboardFocus();
		}
	}
}

void UValhallaLoginWidget::BuildUi()
{
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("ValhallaLoginTree"));
	}

	if (WidgetTree->RootWidget)
	{
		return;
	}

	UWidgetTree& Tree = *WidgetTree;

	// B-08a: the Gilded Hall login (design canvas, Option A): the coin logo
	// over one gold-framed panel, server status bottom left, version bottom right.
	UOverlay* Page = MakePage(Tree);
	Tree.RootWidget = Page;

	UVerticalBox* Centre = Tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	UOverlaySlot* CentreSlot = Page->AddChildToOverlay(Centre);
	CentreSlot->SetHorizontalAlignment(HAlign_Center);
	CentreSlot->SetVerticalAlignment(VAlign_Center);

	UImage* Logo = MakeArt(Tree, TEXT("T_UI_Logo"), FVector2D(224.f, 224.f));
	Centre->AddChildToVerticalBox(Logo)->SetHorizontalAlignment(HAlign_Center);

	USizeBox* Sizer = Tree.ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Sizer->SetWidthOverride(590.f);
	Centre->AddChildToVerticalBox(Sizer)->SetPadding(FMargin(0.f, 30.f, 0.f, 0.f));

	UBorder* Panel = MakePanel(Tree, FMargin(52.f, 46.f, 52.f, 40.f));
	Sizer->SetContent(Panel);

	UVerticalBox* Column = Tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Panel->SetContent(Column);

	auto AddRow = [&Column](UWidget* Widget, float TopPad, EHorizontalAlignment HAlign = HAlign_Fill) -> UVerticalBoxSlot*
	{
		UVerticalBoxSlot* RowSlot = Column->AddChildToVerticalBox(Widget);
		RowSlot->SetPadding(FMargin(0.f, TopPad, 0.f, 0.f));
		RowSlot->SetHorizontalAlignment(HAlign);
		return RowSlot;
	};

	UsernameBox = MakeInput(Tree, TEXT("Your account name"), false);
	UsernameBox->OnTextCommitted.AddDynamic(this, &UValhallaLoginWidget::HandleTextCommitted);
	AddRow(MakeField(Tree, TEXT("Username"), UsernameBox), 0.f);

	PasswordBox = MakeInput(Tree, TEXT("Password"), true);
	PasswordBox->OnTextCommitted.AddDynamic(this, &UValhallaLoginWidget::HandleTextCommitted);
	AddRow(MakeField(Tree, TEXT("Password"), PasswordBox), 22.f);

	// Remember username (only the name; the password is never stored).
	{
		UHorizontalBox* RememberRow = Tree.ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		RememberCheck = Tree.ConstructWidget<UCheckBox>(UCheckBox::StaticClass());
		bool bRemember = true;
		GConfig->GetBool(RememberSection(), TEXT("bRememberUsername"), bRemember, GGameUserSettingsIni);
		RememberCheck->SetIsChecked(bRemember);
		RememberRow->AddChildToHorizontalBox(RememberCheck)->SetVerticalAlignment(VAlign_Center);
		UHorizontalBoxSlot* LabelSlot = RememberRow->AddChildToHorizontalBox(MakeBody(Tree, TEXT("Remember username"), 15, ValhallaUIArt::InkSoft));
		LabelSlot->SetVerticalAlignment(VAlign_Center);
		LabelSlot->SetPadding(FMargin(10.f, 0.f, 0.f, 0.f));
		AddRow(RememberRow, 20.f);

		FString Remembered;
		if (bRemember && GConfig->GetString(RememberSection(), TEXT("Username"), Remembered, GGameUserSettingsIni) && !Remembered.IsEmpty())
		{
			UsernameBox->SetText(AsText(Remembered));
		}
	}

	LoginButton = MakeGoldButton(Tree, TEXT("LOG IN"), 18, FMargin(0.f, 18.f));
	LoginButton->OnClicked.AddDynamic(this, &UValhallaLoginWidget::HandleLoginClicked);
	AddRow(LoginButton, 24.f);

	// "New to Valhalla? Create an account" — Register with what is typed.
	{
		UHorizontalBox* RegisterRow = Tree.ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		RegisterRow->AddChildToHorizontalBox(MakeBody(Tree, TEXT("New to Valhalla?"), 15, ValhallaUIArt::InkDim))->SetVerticalAlignment(VAlign_Center);
		RegisterButton = MakeLinkButton(Tree, TEXT("Create an account"), 15);
		UHorizontalBoxSlot* LinkSlot = RegisterRow->AddChildToHorizontalBox(RegisterButton);
		LinkSlot->SetVerticalAlignment(VAlign_Center);
		LinkSlot->SetPadding(FMargin(8.f, 0.f, 0.f, 0.f));
		RegisterButton->OnClicked.AddDynamic(this, &UValhallaLoginWidget::HandleRegisterClicked);
		AddRow(RegisterRow, 18.f, HAlign_Center);
	}

	ErrorText = MakeBody(Tree, FString(), 15, ValhallaUIArt::Danger);
	ErrorText->SetAutoWrapText(true);
	ErrorText->SetJustification(ETextJustify::Center);
	AddRow(ErrorText, 12.f);

	// Data sync and "contacting the backend…", under the panel.
	StatusText = MakeBody(Tree, FString(), 14, ValhallaUIArt::InkDim, TEXT("Italic"));
	Centre->AddChildToVerticalBox(StatusText)->SetHorizontalAlignment(HAlign_Center);
	if (UVerticalBoxSlot* StatusSlot = Cast<UVerticalBoxSlot>(StatusText->Slot))
	{
		StatusSlot->SetPadding(FMargin(0.f, 14.f, 0.f, 0.f));
	}

	// Bottom left: the server, with the backend address for the reason in the
	// class comment ("the backend is not running" is this screen's commonest failure).
	{
		UHorizontalBox* ServerRow = Tree.ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		ServerDot = MakeImage(Tree, FSlateRoundedBoxBrush(FLinearColor::White, 6.f, FVector2f(12.f, 12.f)));
		ServerDot->SetDesiredSizeOverride(FVector2D(12.f, 12.f));
		ServerRow->AddChildToHorizontalBox(ServerDot)->SetVerticalAlignment(VAlign_Center);
		ServerText = MakeBody(Tree, FString(), 15, ValhallaUIArt::InkSoft);
		UHorizontalBoxSlot* TextSlot = ServerRow->AddChildToHorizontalBox(ServerText);
		TextSlot->SetVerticalAlignment(VAlign_Center);
		TextSlot->SetPadding(FMargin(12.f, 0.f, 0.f, 0.f));
		UOverlaySlot* ServerSlot = Page->AddChildToOverlay(ServerRow);
		ServerSlot->SetHorizontalAlignment(HAlign_Left);
		ServerSlot->SetVerticalAlignment(VAlign_Bottom);
		ServerSlot->SetPadding(FMargin(64.f, 0.f, 0.f, 52.f));
		SetServerStatus(0);
	}

	// Bottom right: the version (still 0, Kevin 2026-09).
	{
		UTextBlock* Version = MakeBody(Tree, FString::Printf(TEXT("Version %s"), *GameVersion()), 15, ValhallaUIArt::InkDim);
		UOverlaySlot* VersionSlot = Page->AddChildToOverlay(Version);
		VersionSlot->SetHorizontalAlignment(HAlign_Right);
		VersionSlot->SetVerticalAlignment(VAlign_Bottom);
		VersionSlot->SetPadding(FMargin(0.f, 0.f, 64.f, 52.f));
	}
}

void UValhallaLoginWidget::SetServerStatus(int32 State)
{
	// 0 checking, 1 online, 2 offline.
	const FString Backend = UValhallaDataSettings::Get()->GetResolvedBackendUrl();
	if (ServerDot)
	{
		ServerDot->SetColorAndOpacity(State == 1 ? ValhallaUIArt::Online
			: State == 2 ? ValhallaUIArt::Danger : ValhallaUIArt::InkDim);
	}
	if (ServerText)
	{
		const TCHAR* Word = State == 1 ? TEXT("Server online") : State == 2 ? TEXT("Server offline") : TEXT("Checking the server…");
		ServerText->SetText(AsText(FString::Printf(TEXT("%s   ·   %s"), Word, *Backend)));
	}
}

void UValhallaLoginWidget::CheckServer()
{
	UValhallaBackendSubsystem* Backend = UValhallaBackendSubsystem::Get(this);
	if (!Backend)
	{
		SetServerStatus(2);
		return;
	}
	SetServerStatus(0);
	TWeakObjectPtr<UValhallaLoginWidget> WeakThis(this);
	Backend->Health([WeakThis](bool bOk, const FString& /*Error*/)
	{
		if (UValhallaLoginWidget* Self = WeakThis.Get())
		{
			Self->SetServerStatus(bOk ? 1 : 2);
		}
	});
}

void UValhallaLoginWidget::RememberUsernameIfWanted(const FString& Username)
{
	const bool bRemember = !RememberCheck || RememberCheck->IsChecked();
	GConfig->SetBool(RememberSection(), TEXT("bRememberUsername"), bRemember, GGameUserSettingsIni);
	GConfig->SetString(RememberSection(), TEXT("Username"), bRemember ? *Username : TEXT(""), GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void UValhallaLoginWidget::ClearPassword()
{
	if (PasswordBox)
	{
		PasswordBox->SetText(FText::GetEmpty());
	}
}

void UValhallaLoginWidget::SetError(const FString& Message)
{
	if (ErrorText)
	{
		ErrorText->SetText(AsText(Message));
	}
}

void UValhallaLoginWidget::SetBusy(bool bBusy)
{
	if (LoginButton)
	{
		LoginButton->SetIsEnabled(!bBusy);
	}
	if (RegisterButton)
	{
		RegisterButton->SetIsEnabled(!bBusy);
	}
	if (StatusText)
	{
		StatusText->SetText(AsText(bBusy ? TEXT("Contacting the server…") : TEXT("")));
	}
}

void UValhallaLoginWidget::SetStatusLine(const FString& Text)
{
	if (StatusText)
	{
		StatusText->SetText(AsText(Text));
	}
}

void UValhallaLoginWidget::SetCredentials(const FString& Username, const FString& Password)
{
	if (UsernameBox)
	{
		UsernameBox->SetText(AsText(Username));
	}
	if (PasswordBox)
	{
		PasswordBox->SetText(AsText(Password));
	}
}

void UValhallaLoginWidget::HandleLoginClicked()
{
	if (AValhallaFrontEndController* Controller = Owner.Get())
	{
		Controller->SubmitLogin(UsernameBox->GetText().ToString().TrimStartAndEnd(), PasswordBox->GetText().ToString());
	}
}

void UValhallaLoginWidget::HandleRegisterClicked()
{
	if (AValhallaFrontEndController* Controller = Owner.Get())
	{
		Controller->SubmitRegister(UsernameBox->GetText().ToString().TrimStartAndEnd(), PasswordBox->GetText().ToString());
	}
}

void UValhallaLoginWidget::HandleTextCommitted(const FText& /*Text*/, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		HandleLoginClicked();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Character select
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	/** Row brushes: the list's normal, hovered and selected looks. */
	FButtonStyle RowStyle(bool bSelected)
	{
		const FSlateBrush Normal = bSelected
			? FSlateBrush(FSlateRoundedBoxBrush(ValhallaUIArt::RowSelected, 0.f, ValhallaUIArt::GoldLight, 1.5f))
			: FSlateBrush(FSlateRoundedBoxBrush(ValhallaUIArt::Row, 0.f, FLinearColor::FromSRGBColor(FColor(0x3A, 0x2E, 0x16)), 1.f));
		const FSlateBrush Hovered = bSelected
			? Normal
			: FSlateBrush(FSlateRoundedBoxBrush(ValhallaUIArt::RowHover, 0.f, FLinearColor::FromSRGBColor(FColor(0x7D, 0x61, 0x24)), 1.f));
		return StyleFrom(Normal, Hovered, Hovered);
	}
}

void UValhallaCharacterSelectWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	BuildUi();
}

void UValhallaCharacterSelectWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildUi();
	SetError(FString());
	SetKeyboardFocus();
}

void UValhallaCharacterSelectWidget::BuildUi()
{
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("ValhallaSelectTree"));
	}

	if (WidgetTree->RootWidget)
	{
		return;
	}

	UWidgetTree& Tree = *WidgetTree;

	// B-08a: the Gilded Hall character select (design canvas, Option A): the
	// list on the left, the selected character standing on the right with
	// their name under them, actions along the bottom, account at the top.
	UOverlay* Page = MakePage(Tree);
	Tree.RootWidget = Page;

	UVerticalBox* Main = Tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	FillOverlay(Page, Main, FMargin(32.f));

	// ── header ──────────────────────────────────────────────────────────────
	{
		UHorizontalBox* Header = Tree.ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		Main->AddChildToVerticalBox(Header)->SetPadding(FMargin(40.f, 22.f, 40.f, 22.f));

		Header->AddChildToHorizontalBox(MakeArt(Tree, TEXT("T_UI_Logo"), FVector2D(70.f, 70.f)))->SetVerticalAlignment(VAlign_Center);
		UHorizontalBoxSlot* TitleSlot = Header->AddChildToHorizontalBox(
			MakeDisplay(Tree, TEXT("CHOOSE YOUR CHARACTER"), 22, ValhallaUIArt::GoldLight, TEXT("Regular"), 280));
		TitleSlot->SetVerticalAlignment(VAlign_Center);
		TitleSlot->SetPadding(FMargin(20.f, 0.f, 0.f, 0.f));

		// Nothing between the title and ACCOUNT (Kevin, 2026-09-26): an empty
		// spacer. HeaderText stays null; SetCharacters skips it.
		USpacer* HeaderGap = Tree.ConstructWidget<USpacer>(USpacer::StaticClass());
		UHorizontalBoxSlot* HeaderSlot = Header->AddChildToHorizontalBox(HeaderGap);
		HeaderSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HeaderSlot->SetPadding(FMargin(20.f, 0.f, 24.f, 0.f));

		UButton* AccountButton = MakeGhostButton(Tree, TEXT("ACCOUNT"), ValhallaUIArt::Line, ValhallaUIArt::InkSoft, FMargin(22.f, 12.f));
		AccountButton->OnClicked.AddDynamic(this, &UValhallaCharacterSelectWidget::HandleAccountClicked);
		Header->AddChildToHorizontalBox(AccountButton)->SetVerticalAlignment(VAlign_Center);

		UButton* LogOutButton = MakeGhostButton(Tree, TEXT("LOG OUT"), ValhallaUIArt::Line, ValhallaUIArt::InkSoft, FMargin(22.f, 12.f));
		LogOutButton->OnClicked.AddDynamic(this, &UValhallaCharacterSelectWidget::HandleLogOutClicked);
		UHorizontalBoxSlot* LogOutSlot = Header->AddChildToHorizontalBox(LogOutButton);
		LogOutSlot->SetVerticalAlignment(VAlign_Center);
		LogOutSlot->SetPadding(FMargin(14.f, 0.f, 0.f, 0.f));
	}
	Main->AddChildToVerticalBox(MakeRule(Tree));

	// ── body: list | preview ────────────────────────────────────────────────
	UHorizontalBox* Body = Tree.ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Main->AddChildToVerticalBox(Body)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	{
		USizeBox* ListSizer = Tree.ConstructWidget<USizeBox>(USizeBox::StaticClass());
		ListSizer->SetWidthOverride(660.f);
		Body->AddChildToHorizontalBox(ListSizer)->SetPadding(FMargin(40.f, 30.f, 20.f, 20.f));

		UVerticalBox* ListColumn = Tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		ListSizer->SetContent(ListColumn);
		ListColumn->AddChildToVerticalBox(MakeDisplay(Tree, TEXT("YOUR HEROES"), 14, ValhallaUIArt::InkSoft, TEXT("Regular"), 240));

		RowBox = Tree.ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
		UVerticalBoxSlot* ListSlot = ListColumn->AddChildToVerticalBox(RowBox);
		ListSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ListSlot->SetPadding(FMargin(0.f, 16.f, 0.f, 0.f));

		// The preview: the stage's render target, a floor glow under the feet,
		// and the selected character's name and line.
		UOverlay* Preview = Tree.ConstructWidget<UOverlay>(UOverlay::StaticClass());
		PreviewArea = Preview;
		UHorizontalBoxSlot* PreviewSlot = Body->AddChildToHorizontalBox(Preview);
		PreviewSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		PreviewSlot->SetPadding(FMargin(0.f, 10.f, 40.f, 20.f));

		USizeBox* PreviewSizer = Tree.ConstructWidget<USizeBox>(USizeBox::StaticClass());
		PreviewSizer->SetWidthOverride(460.f);
		PreviewSizer->SetHeightOverride(690.f);
		UOverlaySlot* SizerSlot = Preview->AddChildToOverlay(PreviewSizer);
		SizerSlot->SetHorizontalAlignment(HAlign_Center);
		SizerSlot->SetVerticalAlignment(VAlign_Bottom);
		SizerSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 120.f));

		UOverlay* PreviewStack = Tree.ConstructWidget<UOverlay>(UOverlay::StaticClass());
		PreviewSizer->SetContent(PreviewStack);
		PreviewImage = Tree.ConstructWidget<UImage>(UImage::StaticClass());
		PreviewImage->SetVisibility(ESlateVisibility::Hidden);
		FillOverlay(PreviewStack, PreviewImage);
		// Softens the capture's black edges into the page.
		UImage* Mask = MakeArt(Tree, TEXT("T_UI_PreviewMask"), FVector2D(460.f, 690.f));
		Mask->SetVisibility(ESlateVisibility::Hidden);
		FillOverlay(PreviewStack, Mask);
		PreviewDressing.Add(Mask);

		// The pool of light at the character's feet, over the capture (whose
		// empty space is opaque black).
		UImage* Glow = MakeArt(Tree, TEXT("T_UI_FloorGlow"), FVector2D(620.f, 110.f));
		Glow->SetVisibility(ESlateVisibility::Hidden);
		PreviewDressing.Add(Glow);
		UOverlaySlot* GlowSlot = Preview->AddChildToOverlay(Glow);
		GlowSlot->SetHorizontalAlignment(HAlign_Center);
		GlowSlot->SetVerticalAlignment(VAlign_Bottom);
		GlowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 100.f));

		UVerticalBox* Caption = Tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		UOverlaySlot* CaptionSlot = Preview->AddChildToOverlay(Caption);
		CaptionSlot->SetHorizontalAlignment(HAlign_Center);
		CaptionSlot->SetVerticalAlignment(VAlign_Bottom);
		CaptionSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 18.f));
		SelectedNameText = MakeDisplay(Tree, FString(), 38, ValhallaUIArt::Ink, TEXT("Bold"), 60);
		Caption->AddChildToVerticalBox(SelectedNameText)->SetHorizontalAlignment(HAlign_Center);
		SelectedLineText = MakeBody(Tree, FString(), 19, ValhallaUIArt::InkSoft);
		UVerticalBoxSlot* LineSlot = Caption->AddChildToVerticalBox(SelectedLineText);
		LineSlot->SetHorizontalAlignment(HAlign_Center);
		LineSlot->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));
	}
	Main->AddChildToVerticalBox(MakeRule(Tree));

	// ── footer ──────────────────────────────────────────────────────────────
	{
		UHorizontalBox* Footer = Tree.ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		Main->AddChildToVerticalBox(Footer)->SetPadding(FMargin(40.f, 26.f, 40.f, 26.f));

		CreateButton = MakeGhostButton(Tree, TEXT("CREATE CHARACTER"), ValhallaUIArt::Line, ValhallaUIArt::InkSoft, FMargin(24.f, 14.f));
		CreateButton->OnClicked.AddDynamic(this, &UValhallaCharacterSelectWidget::HandleCreateClicked);
		Footer->AddChildToHorizontalBox(CreateButton)->SetVerticalAlignment(VAlign_Center);

		DeleteButton = MakeGhostButton(Tree, TEXT("DELETE"), FLinearColor::FromSRGBColor(FColor(0x6B, 0x2A, 0x22)), ValhallaUIArt::Danger, FMargin(24.f, 14.f));
		DeleteButton->OnClicked.AddDynamic(this, &UValhallaCharacterSelectWidget::HandleDeleteClicked);
		UHorizontalBoxSlot* DeleteSlot = Footer->AddChildToHorizontalBox(DeleteButton);
		DeleteSlot->SetVerticalAlignment(VAlign_Center);
		DeleteSlot->SetPadding(FMargin(14.f, 0.f, 0.f, 0.f));

		ErrorText = MakeBody(Tree, FString(), 15, ValhallaUIArt::Danger);
		ErrorText->SetAutoWrapText(true);
		ErrorText->SetJustification(ETextJustify::Center);
		UHorizontalBoxSlot* ErrorSlot = Footer->AddChildToHorizontalBox(ErrorText);
		ErrorSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ErrorSlot->SetVerticalAlignment(VAlign_Center);
		ErrorSlot->SetPadding(FMargin(24.f, 0.f));

		EnterWorldButton = MakeGoldButton(Tree, TEXT("ENTER WORLD"), 21, FMargin(76.f, 20.f));
		EnterWorldButton->OnClicked.AddDynamic(this, &UValhallaCharacterSelectWidget::HandleEnterWorldClicked);
		Footer->AddChildToHorizontalBox(EnterWorldButton)->SetVerticalAlignment(VAlign_Center);
	}

	// ── the panels, over everything: Create, Delete, Account ───────────────
	ModalLayer = Tree.ConstructWidget<UOverlay>(UOverlay::StaticClass());
	ModalLayer->SetVisibility(ESlateVisibility::Collapsed);
	FillOverlay(Page, ModalLayer);
	FillOverlay(ModalLayer, MakeImage(Tree, ValhallaUIArt::SolidBrush(FLinearColor(0.f, 0.f, 0.f, 0.72f))));

	auto AddCard = [&Tree, this](float Width) -> TPair<UBorder*, UVerticalBox*>
	{
		USizeBox* Sizer = Tree.ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Sizer->SetWidthOverride(Width);
		UOverlaySlot* CardSlot = ModalLayer->AddChildToOverlay(Sizer);
		CardSlot->SetHorizontalAlignment(HAlign_Center);
		CardSlot->SetVerticalAlignment(VAlign_Center);
		UBorder* Card = MakePanel(Tree, FMargin(48.f, 40.f));
		Sizer->SetContent(Card);
		UVerticalBox* Column = Tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		Card->SetContent(Column);
		Sizer->SetVisibility(ESlateVisibility::Collapsed);
		return TPair<UBorder*, UVerticalBox*>(Card, Column);
	};

	// The footer's error line is under the dimmer while a card is open, so each
	// card repeats it at its bottom.
	ModalErrorTexts.Reset();
	auto AddModalError = [&Tree, this](UVerticalBox* Column)
	{
		UTextBlock* Error = MakeBody(Tree, FString(), 15, ValhallaUIArt::Danger);
		Error->SetAutoWrapText(true);
		Column->AddChildToVerticalBox(Error)->SetPadding(FMargin(0.f, 12.f, 0.f, 0.f));
		ModalErrorTexts.Add(Error);
	};

	auto AddButtonsRow = [&Tree](UVerticalBox* Column, UButton* Primary, UButton* Secondary)
	{
		UHorizontalBox* Row = Tree.ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		Column->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 26.f, 0.f, 0.f));
		Row->AddChildToHorizontalBox(Primary)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		UHorizontalBoxSlot* SecondSlot = Row->AddChildToHorizontalBox(Secondary);
		SecondSlot->SetPadding(FMargin(14.f, 0.f, 0.f, 0.f));
		SecondSlot->SetVerticalAlignment(VAlign_Fill);
	};

	// Create: a name and six class cards (every class classes.json has, not a hard-coded six).
	{
		TPair<UBorder*, UVerticalBox*> Card = AddCard(1080.f);
		CreateCard = Card.Key->GetParent();
		UVerticalBox* Column = Card.Value;
		Column->AddChildToVerticalBox(MakeDisplay(Tree, TEXT("NEW CHARACTER"), 20, ValhallaUIArt::GoldLight, TEXT("Regular"), 240));

		NewNameBox = MakeInput(Tree, FString::Printf(TEXT("%d to %d letters"), Valhalla::MinCharacterNameLength, Valhalla::MaxCharacterNameLength), false);
		Column->AddChildToVerticalBox(MakeField(Tree, TEXT("Name"), NewNameBox))->SetPadding(FMargin(0.f, 22.f, 0.f, 0.f));

		Column->AddChildToVerticalBox(MakeBody(Tree, TEXT("Class"), 15, ValhallaUIArt::InkSoft))->SetPadding(FMargin(0.f, 22.f, 0.f, 6.f));

		UUniformGridPanel* Grid = Tree.ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass());
		Grid->SetSlotPadding(FMargin(6.f));
		Column->AddChildToVerticalBox(Grid);

		ClassCardIds.Reset();
		ClassCards.Reset();
		if (const UGameInstance* GameInstance = GetGameInstance())
		{
			if (const UValhallaDataSubsystem* Data = GameInstance->GetSubsystem<UValhallaDataSubsystem>())
			{
				ClassCardIds = Data->GetAllClassIds();
			}
		}
		const int32 Columns = 3;
		for (int32 Index = 0; Index < ClassCardIds.Num(); ++Index)
		{
			const FName ClassId = ClassCardIds[Index];
			UValhallaClassCardButton* ClassButton = Tree.ConstructWidget<UValhallaClassCardButton>(UValhallaClassCardButton::StaticClass());
			ClassButton->CardIndex = Index;
			ClassButton->Screen = this;
			ClassButton->OnClicked.AddDynamic(ClassButton, &UValhallaClassCardButton::HandleClicked);

			UHorizontalBox* CardRow = Tree.ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			CardRow->AddChildToHorizontalBox(MakeClassIcon(Tree, this, ClassId, 64.f))->SetVerticalAlignment(VAlign_Center);
			UVerticalBox* CardText = Tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
			UHorizontalBoxSlot* TextSlot = CardRow->AddChildToHorizontalBox(CardText);
			TextSlot->SetVerticalAlignment(VAlign_Center);
			TextSlot->SetPadding(FMargin(14.f, 0.f, 0.f, 0.f));
			TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			CardText->AddChildToVerticalBox(MakeDisplay(Tree, ClassDisplayName(this, ClassId), 17, ValhallaUIArt::Ink, TEXT("Bold"), 40));

			FString Description;
			if (const UGameInstance* GameInstance = GetGameInstance())
			{
				if (const UValhallaDataSubsystem* Data = GameInstance->GetSubsystem<UValhallaDataSubsystem>())
				{
					if (const FValhallaClassTemplate* Class = Data->FindClass(ClassId))
					{
						Description = Class->Description;
					}
				}
			}
			UTextBlock* Line = MakeBody(Tree, Description, 14, ValhallaUIArt::InkSoft);
			Line->SetAutoWrapText(true);
			CardText->AddChildToVerticalBox(Line)->SetPadding(FMargin(0.f, 2.f, 0.f, 0.f));

			if (UButtonSlot* ContentSlot = Cast<UButtonSlot>(ClassButton->AddChild(CardRow)))
			{
				ContentSlot->SetPadding(FMargin(14.f, 12.f));
				ContentSlot->SetHorizontalAlignment(HAlign_Fill);
				ContentSlot->SetVerticalAlignment(VAlign_Center);
			}

			USizeBox* CardSizer = Tree.ConstructWidget<USizeBox>(USizeBox::StaticClass());
			CardSizer->SetMinDesiredHeight(116.f);
			CardSizer->SetContent(ClassButton);
			UUniformGridSlot* CardSlot = Grid->AddChildToUniformGrid(CardSizer, Index / Columns, Index % Columns);
			CardSlot->SetHorizontalAlignment(HAlign_Fill);
			CardSlot->SetVerticalAlignment(VAlign_Fill);
			ClassCards.Add(ClassButton);
		}
		SelectedClassCard = ClassCardIds.Num() > 0 ? 0 : INDEX_NONE;
		RefreshClassCards();

		UButton* Confirm = MakeGoldButton(Tree, TEXT("CREATE CHARACTER"), 17, FMargin(0.f, 16.f));
		Confirm->OnClicked.AddDynamic(this, &UValhallaCharacterSelectWidget::HandleCreateConfirmClicked);
		UButton* Cancel = MakeGhostButton(Tree, TEXT("CANCEL"), ValhallaUIArt::Line, ValhallaUIArt::InkSoft, FMargin(36.f, 14.f));
		Cancel->OnClicked.AddDynamic(this, &UValhallaCharacterSelectWidget::HandleCreateCancelClicked);
		AddButtonsRow(Column, Confirm, Cancel);
		AddModalError(Column);
	}

	// Delete: asks, because a character select screen must never destroy a
	// level 20 character because a mouse moved two pixels.
	{
		TPair<UBorder*, UVerticalBox*> Card = AddCard(640.f);
		DeleteCard = Card.Key->GetParent();
		UVerticalBox* Column = Card.Value;
		Column->AddChildToVerticalBox(MakeDisplay(Tree, TEXT("DELETE CHARACTER"), 20, ValhallaUIArt::Danger, TEXT("Regular"), 240));
		DeletePrompt = MakeBody(Tree, FString(), 18, ValhallaUIArt::Ink);
		DeletePrompt->SetAutoWrapText(true);
		Column->AddChildToVerticalBox(DeletePrompt)->SetPadding(FMargin(0.f, 18.f, 0.f, 0.f));

		UButton* Confirm = MakeGhostButton(Tree, TEXT("DELETE PERMANENTLY"), ValhallaUIArt::Danger, ValhallaUIArt::Danger, FMargin(0.f, 16.f));
		Confirm->OnClicked.AddDynamic(this, &UValhallaCharacterSelectWidget::HandleDeleteConfirmClicked);
		UButton* Keep = MakeGoldButton(Tree, TEXT("KEEP"), 16, FMargin(40.f, 16.f));
		Keep->OnClicked.AddDynamic(this, &UValhallaCharacterSelectWidget::HandleDeleteCancelClicked);
		AddButtonsRow(Column, Confirm, Keep);
		AddModalError(Column);
	}

	// Account (B-12): change password, delete account.
	{
		TPair<UBorder*, UVerticalBox*> Card = AddCard(660.f);
		AccountCard = Card.Key->GetParent();
		UVerticalBox* Panel = Card.Value;
		auto AddBox = [&Tree, Panel](const FString& Hint, bool bPassword) -> UEditableTextBox*
		{
			UEditableTextBox* Box = MakeInput(Tree, Hint, bPassword);
			Panel->AddChildToVerticalBox(Box)->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
			return Box;
		};
		auto AddHeading = [&Tree, Panel](const FString& Heading, float TopPad, const FLinearColor& Colour)
		{
			Panel->AddChildToVerticalBox(MakeDisplay(Tree, Heading, 17, Colour, TEXT("Regular"), 200))
				->SetPadding(FMargin(0.f, TopPad, 0.f, 4.f));
		};

		AddHeading(TEXT("CHANGE PASSWORD"), 0.f, ValhallaUIArt::GoldLight);
		CurrentPasswordBox = AddBox(TEXT("Current password"), true);
		NewPasswordBox = AddBox(FString::Printf(TEXT("New password (at least %d characters)"), Valhalla::MinPasswordLength), true);
		RepeatPasswordBox = AddBox(TEXT("New password again"), true);

		UButton* ChangeButton = MakeGoldButton(Tree, TEXT("CHANGE PASSWORD"), 15, FMargin(0.f, 13.f));
		Panel->AddChildToVerticalBox(ChangeButton)->SetPadding(FMargin(0.f, 14.f, 0.f, 0.f));
		ChangeButton->OnClicked.AddDynamic(this, &UValhallaCharacterSelectWidget::HandleChangePasswordClicked);

		AddHeading(TEXT("DELETE ACCOUNT"), 28.f, ValhallaUIArt::Danger);
		UTextBlock* Warning = MakeBody(Tree,
			TEXT("Deletes the account and every character on it, with their items. This cannot be undone."), 15, ValhallaUIArt::Danger);
		Warning->SetAutoWrapText(true);
		Panel->AddChildToVerticalBox(Warning);
		DeleteAccountPasswordBox = AddBox(TEXT("Password"), true);
		DeleteAccountConfirmBox = AddBox(TEXT("Type your account name to confirm"), false);

		UButton* DeleteAccountButton = MakeGhostButton(Tree, TEXT("DELETE ACCOUNT"), ValhallaUIArt::Danger, ValhallaUIArt::Danger, FMargin(0.f, 14.f));
		DeleteAccountButton->OnClicked.AddDynamic(this, &UValhallaCharacterSelectWidget::HandleDeleteAccountClicked);
		UButton* Close = MakeGoldButton(Tree, TEXT("CLOSE"), 15, FMargin(40.f, 14.f));
		Close->OnClicked.AddDynamic(this, &UValhallaCharacterSelectWidget::HandleAccountCloseClicked);
		AddButtonsRow(Panel, DeleteAccountButton, Close);

		AccountStatusText = MakeBody(Tree, FString(), 15, ValhallaUIArt::GoldLight);
		AccountStatusText->SetAutoWrapText(true);
		Panel->AddChildToVerticalBox(AccountStatusText)->SetPadding(FMargin(0.f, 12.f, 0.f, 0.f));
		AddModalError(Panel);
	}

	// The old free-standing panels are now the cards above.
	CreatePanel = nullptr;
	DeletePanel = nullptr;
	AccountPanel = nullptr;
}

void UValhallaCharacterSelectWidget::SetCharacters(const FString& Username, const TArray<FValhallaCharacterSummary>& InCharacters)
{
	// Keep the selection across a refresh if the same character is still
	// there, because a refresh happens right after a create and losing the
	// selection would mean the character you just made is not the one
	// "Enter World" would take.
	const int32 PreviousId = Characters.IsValidIndex(SelectedIndex) ? Characters[SelectedIndex].Id : INDEX_NONE;

	Characters = InCharacters;
	SelectedIndex = INDEX_NONE;

	const int32 WantedId = PreferredCharacterId != INDEX_NONE ? PreferredCharacterId : PreviousId;
	PreferredCharacterId = INDEX_NONE;
	for (int32 Index = 0; Index < Characters.Num(); ++Index)
	{
		if (Characters[Index].Id == WantedId)
		{
			SelectedIndex = Index;
			break;
		}
	}

	if (SelectedIndex == INDEX_NONE && Characters.Num() > 0)
	{
		SelectedIndex = 0;
	}

	if (HeaderText)
	{
		HeaderText->SetText(AsText(FString::Printf(TEXT("%s   ·   %d of %d character slots"),
			*Username, Characters.Num(), Valhalla::MaxCharactersPerUser)));
	}

	RefreshRows();
}

void UValhallaCharacterSelectWidget::RefreshRows()
{
	if (!RowBox || !WidgetTree)
	{
		return;
	}

	RowBox->ClearChildren();
	RowButtons.Reset();

	if (Characters.Num() == 0)
	{
		RowBox->AddChild(MakeBody(*WidgetTree, TEXT("No characters yet. Press Create Character."), 17, ValhallaUIArt::InkDim, TEXT("Italic")));
	}

	for (int32 Index = 0; Index < Characters.Num(); ++Index)
	{
		const FValhallaCharacterSummary& Summary = Characters[Index];
		const bool bSelected = Index == SelectedIndex;

		UValhallaCharacterRowButton* Row = WidgetTree->ConstructWidget<UValhallaCharacterRowButton>(UValhallaCharacterRowButton::StaticClass());
		Row->RowIndex = Index;
		Row->Screen = this;
		Row->SetStyle(RowStyle(bSelected));
		Row->SetBackgroundColor(FLinearColor::White);
		Row->OnClicked.AddDynamic(Row, &UValhallaCharacterRowButton::HandleClicked);

		// [coin] Name / Level N Class ........ Zone
		UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		Line->AddChildToHorizontalBox(MakeClassIcon(*WidgetTree, this, Summary.ClassId, 64.f))->SetVerticalAlignment(VAlign_Center);

		UVerticalBox* Names = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		UHorizontalBoxSlot* NamesSlot = Line->AddChildToHorizontalBox(Names);
		NamesSlot->SetVerticalAlignment(VAlign_Center);
		NamesSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		NamesSlot->SetPadding(FMargin(18.f, 0.f, 12.f, 0.f));
		Names->AddChildToVerticalBox(MakeDisplay(*WidgetTree, Summary.Name, 20, ValhallaUIArt::Ink, bSelected ? TEXT("Bold") : TEXT("Regular"), 40));
		Names->AddChildToVerticalBox(MakeBody(*WidgetTree,
			FString::Printf(TEXT("Level %d %s"), Summary.Level, *ClassDisplayName(this, Summary.ClassId)), 16, ValhallaUIArt::InkSoft));

		UHorizontalBoxSlot* ZoneSlot = Line->AddChildToHorizontalBox(
			MakeBody(*WidgetTree, ZoneDisplayName(this, Summary.ZoneId), 15, ValhallaUIArt::InkDim, TEXT("Italic")));
		ZoneSlot->SetVerticalAlignment(VAlign_Center);

		if (UButtonSlot* ContentSlot = Cast<UButtonSlot>(Row->AddChild(Line)))
		{
			ContentSlot->SetPadding(FMargin(16.f, 14.f));
			ContentSlot->SetHorizontalAlignment(HAlign_Fill);
			ContentSlot->SetVerticalAlignment(VAlign_Center);
		}

		RowBox->AddChild(Row);
		if (UScrollBoxSlot* RowSlot = Cast<UScrollBoxSlot>(Row->Slot))
		{
			RowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));
		}
		RowButtons.Add(Row);
	}

	if (EnterWorldButton)
	{
		EnterWorldButton->SetIsEnabled(SelectedIndex != INDEX_NONE);
	}
	if (DeleteButton)
	{
		DeleteButton->SetIsEnabled(SelectedIndex != INDEX_NONE);
	}
	if (CreateButton)
	{
		CreateButton->SetIsEnabled(Characters.Num() < Valhalla::MaxCharactersPerUser);
	}

	UpdateSelectedDetails();
}

void UValhallaCharacterSelectWidget::UpdateSelectedDetails()
{
	const FValhallaCharacterSummary* Selected = GetSelected();
	if (SelectedNameText)
	{
		SelectedNameText->SetText(AsText(Selected ? Selected->Name : FString()));
	}
	if (SelectedLineText)
	{
		FString Line;
		if (Selected)
		{
			Line = FString::Printf(TEXT("Level %d %s"), Selected->Level, *ClassDisplayName(this, Selected->ClassId));
			const FString Zone = ZoneDisplayName(this, Selected->ZoneId);
			if (!Zone.IsEmpty())
			{
				Line += TEXT("   ·   ") + Zone;
			}
		}
		SelectedLineText->SetText(AsText(Line));
	}

	// The preview follows the selection, and hides under an open panel.
	if (AValhallaFrontEndController* Controller = Owner.Get())
	{
		Controller->ShowPreviewFor(IsModalOpen() ? nullptr : Selected);
	}
	const bool bShowPreview = Selected && !IsModalOpen() && PreviewImage && PreviewImage->GetBrush().GetResourceObject();
	const ESlateVisibility PreviewVisibility = bShowPreview ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden;
	if (PreviewImage)
	{
		PreviewImage->SetVisibility(PreviewVisibility);
	}
	for (UWidget* Dressing : PreviewDressing)
	{
		if (Dressing)
		{
			Dressing->SetVisibility(PreviewVisibility);
		}
	}
}

void UValhallaCharacterSelectWidget::SetPreviewTexture(UTextureRenderTarget2D* Target)
{
	if (!PreviewImage)
	{
		return;
	}
	FSlateBrush Brush;
	if (Target)
	{
		Brush.SetResourceObject(Target);
		Brush.ImageSize = FVector2D(Target->SizeX, Target->SizeY);
	}
	PreviewImage->SetBrush(Brush);
	UpdateSelectedDetails();
}

const FValhallaCharacterSummary* UValhallaCharacterSelectWidget::GetSelected() const
{
	return Characters.IsValidIndex(SelectedIndex) ? &Characters[SelectedIndex] : nullptr;
}

bool UValhallaCharacterSelectWidget::SelectByName(const FString& CharacterName)
{
	for (int32 Index = 0; Index < Characters.Num(); ++Index)
	{
		if (Characters[Index].Name.Equals(CharacterName, ESearchCase::IgnoreCase))
		{
			SelectedIndex = Index;
			RefreshRows();
			return true;
		}
	}

	return false;
}

void UValhallaCharacterSelectWidget::HandleRowClicked(int32 RowIndex)
{
	if (!Characters.IsValidIndex(RowIndex))
	{
		return;
	}

	// B-08a: a second click on the selected row enters the world (a double click).
	const double Now = FPlatformTime::Seconds();
	static double LastClickTime = 0.0;
	static int32 LastClickRow = INDEX_NONE;
	const bool bDouble = RowIndex == LastClickRow && RowIndex == SelectedIndex && Now - LastClickTime < 0.4;
	LastClickTime = Now;
	LastClickRow = RowIndex;

	SelectedIndex = RowIndex;
	SetError(FString());
	RefreshRows();
	SetKeyboardFocus();

	if (bDouble)
	{
		HandleEnterWorldClicked();
	}
}

void UValhallaCharacterSelectWidget::MoveSelection(int32 Delta)
{
	if (Characters.Num() == 0)
	{
		return;
	}
	const int32 From = SelectedIndex == INDEX_NONE ? 0 : SelectedIndex;
	const int32 To = FMath::Clamp(From + Delta, 0, Characters.Num() - 1);
	if (To != SelectedIndex)
	{
		SelectedIndex = To;
		SetError(FString());
		RefreshRows();
		if (RowButtons.IsValidIndex(SelectedIndex) && RowBox)
		{
			RowBox->ScrollWidgetIntoView(RowButtons[SelectedIndex], false);
		}
	}
}

FReply UValhallaCharacterSelectWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (IsModalOpen())
	{
		if (Key == EKeys::Escape)
		{
			ShowModal(nullptr);
			return FReply::Handled();
		}
		return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
	}

	if (Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up)
	{
		MoveSelection(-1);
		return FReply::Handled();
	}
	if (Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down)
	{
		MoveSelection(1);
		return FReply::Handled();
	}
	if (Key == EKeys::Enter || Key == EKeys::Gamepad_FaceButton_Bottom)
	{
		HandleEnterWorldClicked();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

bool UValhallaCharacterSelectWidget::IsModalOpen() const
{
	return ModalLayer && ModalLayer->GetVisibility() != ESlateVisibility::Collapsed;
}

void UValhallaCharacterSelectWidget::ShowModal(UWidget* Card)
{
	for (UWidget* Each : { CreateCard.Get(), DeleteCard.Get(), AccountCard.Get() })
	{
		if (Each)
		{
			Each->SetVisibility(Each == Card ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}
	}
	if (ModalLayer)
	{
		ModalLayer->SetVisibility(Card ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	// Escape closes a card; the Create card focuses its name box itself.
	if (Card != CreateCard)
	{
		SetKeyboardFocus();
	}
	UpdateSelectedDetails();
}

void UValhallaCharacterSelectWidget::HandleClassCardClicked(int32 CardIndex)
{
	if (ClassCardIds.IsValidIndex(CardIndex))
	{
		SelectedClassCard = CardIndex;
		RefreshClassCards();
	}
}

void UValhallaCharacterSelectWidget::RefreshClassCards()
{
	for (int32 Index = 0; Index < ClassCards.Num(); ++Index)
	{
		if (ClassCards[Index])
		{
			ClassCards[Index]->SetStyle(RowStyle(Index == SelectedClassCard));
			ClassCards[Index]->SetBackgroundColor(FLinearColor::White);
		}
	}
}

void UValhallaCharacterSelectWidget::SetError(const FString& Message)
{
	if (ErrorText)
	{
		ErrorText->SetText(AsText(Message));
	}
	for (UTextBlock* Line : ModalErrorTexts)
	{
		if (Line)
		{
			Line->SetText(AsText(Message));
		}
	}
}

void UValhallaCharacterSelectWidget::SetBusy(bool bBusy)
{
	if (EnterWorldButton)
	{
		EnterWorldButton->SetIsEnabled(!bBusy && SelectedIndex != INDEX_NONE);
	}
	if (CreateButton)
	{
		CreateButton->SetIsEnabled(!bBusy && Characters.Num() < Valhalla::MaxCharactersPerUser);
	}
	if (DeleteButton)
	{
		DeleteButton->SetIsEnabled(!bBusy && SelectedIndex != INDEX_NONE);
	}
}

void UValhallaCharacterSelectWidget::HandleEnterWorldClicked()
{
	const FValhallaCharacterSummary* Selected = GetSelected();
	if (!Selected)
	{
		SetError(TEXT("Pick a character first."));
		return;
	}

	if (AValhallaFrontEndController* Controller = Owner.Get())
	{
		Controller->EnterWorld(Selected->Id);
	}
}

void UValhallaCharacterSelectWidget::HandleCreateClicked()
{
	SetError(FString());
	if (NewNameBox)
	{
		NewNameBox->SetText(FText::GetEmpty());
	}
	ShowModal(CreateCard);
	if (NewNameBox)
	{
		NewNameBox->SetKeyboardFocus();
	}
}

void UValhallaCharacterSelectWidget::HandleCreateCancelClicked()
{
	ShowModal(nullptr);
}

void UValhallaCharacterSelectWidget::HandleCreateConfirmClicked()
{
	const FString Name = NewNameBox ? NewNameBox->GetText().ToString().TrimStartAndEnd() : FString();
	const FName ClassId = ClassCardIds.IsValidIndex(SelectedClassCard) ? ClassCardIds[SelectedClassCard] : NAME_None;

	// The backend validates all of this too, and its message is the one shown
	// when it does. This is only so the common mistake costs no round trip.
	if (Name.Len() < Valhalla::MinCharacterNameLength || Name.Len() > Valhalla::MaxCharacterNameLength)
	{
		SetError(FString::Printf(TEXT("Character name must be %d-%d characters."),
			Valhalla::MinCharacterNameLength, Valhalla::MaxCharacterNameLength));
		return;
	}

	if (ClassId.IsNone())
	{
		SetError(TEXT("Pick a class."));
		return;
	}

	if (AValhallaFrontEndController* Controller = Owner.Get())
	{
		ShowModal(nullptr);
		Controller->SubmitCreateCharacter(Name, ClassId);
	}
}

void UValhallaCharacterSelectWidget::HandleDeleteClicked()
{
	const FValhallaCharacterSummary* Selected = GetSelected();
	if (!Selected)
	{
		SetError(TEXT("Pick a character first."));
		return;
	}

	SetError(FString());
	DeletePrompt->SetText(AsText(FString::Printf(
		TEXT("Delete %s, level %d %s? This cannot be undone."),
		*Selected->Name, Selected->Level, *ClassDisplayName(this, Selected->ClassId))));
	ShowModal(DeleteCard);
}

void UValhallaCharacterSelectWidget::HandleDeleteCancelClicked()
{
	ShowModal(nullptr);
}

void UValhallaCharacterSelectWidget::HandleDeleteConfirmClicked()
{
	const FValhallaCharacterSummary* Selected = GetSelected();
	if (!Selected)
	{
		return;
	}

	const int32 CharacterId = Selected->Id;
	ShowModal(nullptr);

	if (AValhallaFrontEndController* Controller = Owner.Get())
	{
		Controller->SubmitDeleteCharacter(CharacterId);
	}
}

void UValhallaCharacterSelectWidget::HandleLogOutClicked()
{
	if (AValhallaFrontEndController* Controller = Owner.Get())
	{
		Controller->LogOut();
	}
}

// ── B-12: account panel ─────────────────────────────────────────────────

void UValhallaCharacterSelectWidget::SetAccountStatus(const FString& Message)
{
	if (AccountStatusText)
	{
		AccountStatusText->SetText(AsText(Message));
	}
}

void UValhallaCharacterSelectWidget::ClearAccountFields()
{
	for (UEditableTextBox* Box : { CurrentPasswordBox.Get(), NewPasswordBox.Get(), RepeatPasswordBox.Get(),
		DeleteAccountPasswordBox.Get(), DeleteAccountConfirmBox.Get() })
	{
		if (Box)
		{
			Box->SetText(FText::GetEmpty());
		}
	}
}

void UValhallaCharacterSelectWidget::HandleAccountClicked()
{
	SetError(FString());
	SetAccountStatus(FString());
	const bool bOpen = IsModalOpen() && AccountCard && AccountCard->GetVisibility() == ESlateVisibility::Visible;
	if (bOpen)
	{
		ClearAccountFields();
	}
	ShowModal(bOpen ? nullptr : AccountCard.Get());
}

void UValhallaCharacterSelectWidget::HandleAccountCloseClicked()
{
	ClearAccountFields();
	SetAccountStatus(FString());
	ShowModal(nullptr);
}

void UValhallaCharacterSelectWidget::HandleChangePasswordClicked()
{
	const FString Current = CurrentPasswordBox ? CurrentPasswordBox->GetText().ToString() : FString();
	const FString NewPassword = NewPasswordBox ? NewPasswordBox->GetText().ToString() : FString();
	const FString Repeat = RepeatPasswordBox ? RepeatPasswordBox->GetText().ToString() : FString();

	SetError(FString());
	SetAccountStatus(FString());

	// The backend checks all of this too; these just save a round trip.
	if (Current.IsEmpty())
	{
		SetError(TEXT("Enter your current password."));
		return;
	}
	if (NewPassword.Len() < Valhalla::MinPasswordLength)
	{
		SetError(FString::Printf(TEXT("The new password must be at least %d characters."), Valhalla::MinPasswordLength));
		return;
	}
	// FString's == ignores case; passwords must not.
	if (!NewPassword.Equals(Repeat, ESearchCase::CaseSensitive))
	{
		SetError(TEXT("The two new passwords don't match."));
		return;
	}

	if (AValhallaFrontEndController* Controller = Owner.Get())
	{
		Controller->SubmitChangePassword(Current, NewPassword);
	}
}

void UValhallaCharacterSelectWidget::HandleDeleteAccountClicked()
{
	AValhallaFrontEndController* Controller = Owner.Get();
	if (!Controller)
	{
		return;
	}

	const FString Password = DeleteAccountPasswordBox ? DeleteAccountPasswordBox->GetText().ToString() : FString();
	const FString Confirm = DeleteAccountConfirmBox ? DeleteAccountConfirmBox->GetText().ToString().TrimStartAndEnd() : FString();
	const FString& Username = Controller->GetSession().Username;

	SetError(FString());
	SetAccountStatus(FString());

	if (Password.IsEmpty())
	{
		SetError(TEXT("Enter your password to delete the account."));
		return;
	}
	if (!Confirm.Equals(Username, ESearchCase::IgnoreCase))
	{
		SetError(FString::Printf(TEXT("Type your account name (%s) to confirm."), *Username));
		return;
	}

	Controller->SubmitDeleteAccount(Password, Confirm);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Controller
// ─────────────────────────────────────────────────────────────────────────────

int32 AValhallaFrontEndController::FrontEndClientCounter = 0;

AValhallaFrontEndController::AValhallaFrontEndController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
}

void AValhallaFrontEndController::BeginPlay()
{
	Super::BeginPlay();

	// Dedicated servers have no viewport and no player to log in; the game mode
	// has already sent them somewhere else by now.
	if (!IsLocalController())
	{
		return;
	}

	AutoLoginSlot = FrontEndClientCounter++;

	// B-21: the in-world player session (the token the UI settings sync uses)
	// ends when the front end opens again; a new Enter World sets it anew. The
	// settings subsystem flushed its last change when the HUD went away.
	if (UValhallaBackendSubsystem* Backend = UValhallaBackendSubsystem::Get(this))
	{
		Backend->ClearPlayerSession();
	}
	if (UValhallaUserSettingsSubsystem* UserSettings = UValhallaUserSettingsSubsystem::Get(this))
	{
		UserSettings->FlushAndForget();
	}

	LoginScreen = CreateWidget<UValhallaLoginWidget>(this, UValhallaLoginWidget::StaticClass());
	if (!LoginScreen)
	{
		UE_LOG(LogValhallaFrontEnd, Error, TEXT("could not create the login screen."));
		return;
	}

	LoginScreen->Owner = this;
	LoginScreen->AddToViewport();

	SetInputMode(FInputModeUIOnly().SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock));
	bShowMouseCursor = true;

	UE_LOG(LogValhallaFrontEnd, Log, TEXT("front end ready (client %d), backend %s"),
		AutoLoginSlot, *UValhallaDataSettings::Get()->GetResolvedBackendUrl());

	SyncGameDataIfNeeded();

	// Back from a game server that kicked or banned us (B-14): say why, and do
	// not let a dev auto-login walk straight back in as if nothing happened.
	UValhallaBackendSubsystem* Backend = UValhallaBackendSubsystem::Get(this);
	const FString Notice = Backend ? Backend->TakeDisconnectNotice() : FString();
	if (!Notice.IsEmpty())
	{
		UE_LOG(LogValhallaFrontEnd, Warning, TEXT("front end (client %d): disconnected by the server: %s"), AutoLoginSlot, *Notice);
		LoginScreen->SetError(Notice);
		return;
	}

	TryAutoLoginFromCVar();
}

void AValhallaFrontEndController::SyncGameDataIfNeeded()
{
	UGameInstance* GameInstance = GetGameInstance();
	UValhallaDataSubsystem* Data = GameInstance ? GameInstance->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
	UValhallaBackendSubsystem* Backend = UValhallaBackendSubsystem::Get(this);
	if (!Data || !Backend || !Data->IsUsingDownloadedData())
	{
		return;	// reading the repo's shared/data directly (every editor session)
	}

	if (LoginScreen)
	{
		LoginScreen->SetStatusLine(TEXT("updating game data…"));
	}

	TWeakObjectPtr<AValhallaFrontEndController> WeakThis(this);
	Backend->SyncGameData(Data->GetDataRootOverride(),
		[WeakThis](bool bOk, int32 FilesUpdated, const FString& Error)
		{
			AValhallaFrontEndController* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			UGameInstance* GI = Self->GetGameInstance();
			UValhallaDataSubsystem* Data = GI ? GI->GetSubsystem<UValhallaDataSubsystem>() : nullptr;
			if (bOk && FilesUpdated > 0 && Data)
			{
				Data->Reload();
			}

			const FString Line = !bOk
				? FString::Printf(TEXT("could not update game data (%s) — using the last copy"), *Error)
				: FilesUpdated > 0 ? FString::Printf(TEXT("game data updated (%d file%s)"), FilesUpdated, FilesUpdated == 1 ? TEXT("") : TEXT("s"))
				: FString(TEXT("game data up to date"));
			UE_LOG(LogValhallaFrontEnd, Log, TEXT("%s"), *Line);
			if (Self->LoginScreen && Self->LoginScreen->IsInViewport())
			{
				Self->LoginScreen->SetStatusLine(Line);
			}
		});
}

void AValhallaFrontEndController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (PreviewStage)
	{
		PreviewStage->Destroy();
		PreviewStage = nullptr;
	}
	if (LoginScreen)
	{
		LoginScreen->RemoveFromParent();
	}
	if (SelectScreen)
	{
		SelectScreen->RemoveFromParent();
	}

	Super::EndPlay(EndPlayReason);
}

void AValhallaFrontEndController::ReportError(const FString& Message)
{
	UE_LOG(LogValhallaFrontEnd, Warning, TEXT("%s"), *Message);

	if (SelectScreen && SelectScreen->IsInViewport())
	{
		SelectScreen->SetError(Message);
	}
	else if (LoginScreen)
	{
		LoginScreen->SetError(Message);
	}

	// An auto-login that hits an error stops; it is a script, and a script that
	// carried on past a failure would report a pass for a run that did not.
	if (bAutoLoginActive)
	{
		UE_LOG(LogValhallaFrontEnd, Error, TEXT("AutoLogin stopped: %s"), *Message);
		bAutoLoginActive = false;
	}
}

void AValhallaFrontEndController::SubmitLogin(const FString& Username, const FString& Password)
{
	UValhallaBackendSubsystem* Backend = UValhallaBackendSubsystem::Get(this);
	if (!Backend)
	{
		ReportError(TEXT("No backend subsystem in this game instance."));
		return;
	}

	if (Username.IsEmpty() || Password.IsEmpty())
	{
		ReportError(TEXT("Enter a username and a password."));
		return;
	}

	if (LoginScreen)
	{
		LoginScreen->SetError(FString());
		LoginScreen->SetBusy(true);
	}

	TWeakObjectPtr<AValhallaFrontEndController> WeakThis(this);
	Backend->Login(Username, Password,
		[WeakThis, Username](bool bSuccess, const FValhallaAuthSession& InSession, const FString& Error)
		{
			AValhallaFrontEndController* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}

			if (Self->LoginScreen)
			{
				Self->LoginScreen->SetBusy(false);
			}

			if (!bSuccess)
			{
				Self->ReportError(Error);
				return;
			}

			// B-08a: "Remember username" (never the password).
			if (Self->LoginScreen && !Self->bAutoLoginActive)
			{
				Self->LoginScreen->RememberUsernameIfWanted(Username);
			}

			Self->Session = InSession;
			Self->ShowCharacterSelect();
		});
}

void AValhallaFrontEndController::SubmitRegister(const FString& Username, const FString& Password)
{
	UValhallaBackendSubsystem* Backend = UValhallaBackendSubsystem::Get(this);
	if (!Backend)
	{
		ReportError(TEXT("No backend subsystem in this game instance."));
		return;
	}

	if (Username.Len() < Valhalla::MinUsernameLength || Username.Len() > Valhalla::MaxUsernameLength)
	{
		ReportError(FString::Printf(TEXT("Username must be %d-%d characters."),
			Valhalla::MinUsernameLength, Valhalla::MaxUsernameLength));
		return;
	}

	if (Password.Len() < Valhalla::MinPasswordLength)
	{
		ReportError(FString::Printf(TEXT("Password must be at least %d characters."), Valhalla::MinPasswordLength));
		return;
	}

	if (LoginScreen)
	{
		LoginScreen->SetError(FString());
		LoginScreen->SetBusy(true);
	}

	TWeakObjectPtr<AValhallaFrontEndController> WeakThis(this);
	Backend->Register(Username, Password,
		[WeakThis, Username](bool bSuccess, const FValhallaAuthSession& InSession, const FString& Error)
		{
			AValhallaFrontEndController* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}

			if (Self->LoginScreen)
			{
				Self->LoginScreen->SetBusy(false);
			}

			if (!bSuccess)
			{
				Self->ReportError(Error);
				return;
			}

			// B-08a: "Remember username" (never the password).
			if (Self->LoginScreen && !Self->bAutoLoginActive)
			{
				Self->LoginScreen->RememberUsernameIfWanted(Username);
			}

			Self->Session = InSession;
			Self->ShowCharacterSelect();
		});
}

void AValhallaFrontEndController::ShowCharacterSelect()
{
	// B-27: the account's graphics settings, applied here on the front end so
	// the world loads with them (decision 3: they follow the login).
	if (Session.IsValid())
	{
		if (UValhallaGraphicsSettingsSubsystem* Graphics = UValhallaGraphicsSettingsSubsystem::Get(this))
		{
			Graphics->LoadForAccount(Session.UserId, Session.Token);
		}
	}

	if (LoginScreen)
	{
		LoginScreen->RemoveFromParent();
	}

	if (!SelectScreen)
	{
		SelectScreen = CreateWidget<UValhallaCharacterSelectWidget>(this, UValhallaCharacterSelectWidget::StaticClass());
		if (!SelectScreen)
		{
			UE_LOG(LogValhallaFrontEnd, Error, TEXT("could not create the character select screen."));
			return;
		}
		SelectScreen->Owner = this;
	}

	if (!SelectScreen->IsInViewport())
	{
		SelectScreen->AddToViewport();
	}

	// B-08a: the preview stage, far above anything else in L_FrontEnd.
	if (!PreviewStage && GetWorld())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		Params.Owner = this;
		PreviewStage = GetWorld()->SpawnActor<AValhallaCharacterPreviewStage>(
			AValhallaCharacterPreviewStage::StaticClass(), FVector(0.f, 0.f, 50000.f), FRotator::ZeroRotator, Params);
	}
	SelectScreen->SetPreviewTexture(PreviewStage ? PreviewStage->GetRenderTarget() : nullptr);

	SelectScreen->SetCharacters(Session.Username, Session.Characters);
	SetInputMode(FInputModeUIOnly().SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock));
	bShowMouseCursor = true;

	UE_LOG(LogValhallaFrontEnd, Log, TEXT("character select for '%s': %d character(s)"),
		*Session.Username, Session.Characters.Num());

	if (bAutoLoginActive)
	{
		// Either the character is already there, or it has to be made first.
		if (SelectScreen->SelectByName(AutoCharacterName))
		{
			const FValhallaCharacterSummary* Selected = SelectScreen->GetSelected();
			UE_LOG(LogValhallaFrontEnd, Log, TEXT("AutoLogin: '%s' exists (id %d); entering world."),
				*AutoCharacterName, Selected ? Selected->Id : 0);
			EnterWorld(Selected ? Selected->Id : 0);
		}
		else
		{
			UE_LOG(LogValhallaFrontEnd, Log, TEXT("AutoLogin: creating '%s' as %s."),
				*AutoCharacterName, *AutoClassId.ToString());
			SubmitCreateCharacter(AutoCharacterName, AutoClassId);
		}
	}
}

void AValhallaFrontEndController::RefreshCharacters()
{
	UValhallaBackendSubsystem* Backend = UValhallaBackendSubsystem::Get(this);
	if (!Backend)
	{
		return;
	}

	TWeakObjectPtr<AValhallaFrontEndController> WeakThis(this);
	Backend->ListCharacters(Session.Token,
		[WeakThis](bool bSuccess, const TArray<FValhallaCharacterSummary>& Characters, const FString& Error)
		{
			AValhallaFrontEndController* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}

			if (Self->SelectScreen)
			{
				Self->SelectScreen->SetBusy(false);
			}

			if (!bSuccess)
			{
				Self->ReportError(Error);
				return;
			}

			Self->Session.Characters = Characters;
			if (Self->SelectScreen)
			{
				Self->SelectScreen->SetCharacters(Self->Session.Username, Characters);
			}

			if (Self->bAutoLoginActive)
			{
				if (Self->SelectScreen && Self->SelectScreen->SelectByName(Self->AutoCharacterName))
				{
					const FValhallaCharacterSummary* Selected = Self->SelectScreen->GetSelected();
					Self->EnterWorld(Selected ? Selected->Id : 0);
				}
				else
				{
					Self->ReportError(FString::Printf(TEXT("AutoLogin: '%s' is still not in the list."), *Self->AutoCharacterName));
				}
			}
		});
}

void AValhallaFrontEndController::SubmitCreateCharacter(const FString& Name, FName ClassId)
{
	UValhallaBackendSubsystem* Backend = UValhallaBackendSubsystem::Get(this);
	if (!Backend)
	{
		return;
	}

	if (SelectScreen)
	{
		SelectScreen->SetError(FString());
		SelectScreen->SetBusy(true);
	}

	TWeakObjectPtr<AValhallaFrontEndController> WeakThis(this);
	Backend->CreateCharacter(Session.Token, Name, ClassId,
		[WeakThis](bool bSuccess, const FValhallaCharacterSummary& Created, const FString& Error)
		{
			AValhallaFrontEndController* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}

			if (!bSuccess)
			{
				if (Self->SelectScreen)
				{
					Self->SelectScreen->SetBusy(false);
				}
				Self->ReportError(Error);
				return;
			}

			// B-08a: the character just made is the one shown and entered.
			if (Self->SelectScreen)
			{
				Self->SelectScreen->PreferCharacter(Created.Id);
			}

			// Re-list rather than appending the returned summary: the backend
			// is what decides what this account owns, and a list that was
			// built by adding to a stale one is a list that can drift.
			Self->RefreshCharacters();
		});
}

void AValhallaFrontEndController::SubmitDeleteCharacter(int32 CharacterId)
{
	UValhallaBackendSubsystem* Backend = UValhallaBackendSubsystem::Get(this);
	if (!Backend)
	{
		return;
	}

	if (SelectScreen)
	{
		SelectScreen->SetError(FString());
		SelectScreen->SetBusy(true);
	}

	TWeakObjectPtr<AValhallaFrontEndController> WeakThis(this);
	Backend->DeleteCharacter(Session.Token, CharacterId,
		[WeakThis](bool bSuccess, const FString& Error)
		{
			AValhallaFrontEndController* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}

			if (!bSuccess)
			{
				if (Self->SelectScreen)
				{
					Self->SelectScreen->SetBusy(false);
				}
				Self->ReportError(Error);
				return;
			}

			Self->RefreshCharacters();
		});
}

void AValhallaFrontEndController::SubmitChangePassword(const FString& CurrentPassword, const FString& NewPassword)
{
	UValhallaBackendSubsystem* Backend = UValhallaBackendSubsystem::Get(this);
	if (!Backend || !Session.IsValid())
	{
		return;
	}

	if (SelectScreen)
	{
		SelectScreen->SetError(FString());
		SelectScreen->SetBusy(true);
	}

	TWeakObjectPtr<AValhallaFrontEndController> WeakThis(this);
	Backend->ChangePassword(Session.Token, CurrentPassword, NewPassword,
		[WeakThis](bool bSuccess, const FString& NewToken, const FString& Error)
		{
			AValhallaFrontEndController* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}

			if (Self->SelectScreen)
			{
				Self->SelectScreen->SetBusy(false);
			}

			if (!bSuccess)
			{
				Self->ReportError(Error);
				return;
			}

			// The old token stopped working the moment the password changed.
			Self->Session.Token = NewToken;
			UE_LOG(LogValhallaFrontEnd, Log, TEXT("password changed for '%s'."), *Self->Session.Username);

			if (Self->SelectScreen)
			{
				Self->SelectScreen->ClearAccountFields();
				Self->SelectScreen->SetAccountStatus(TEXT("Password changed. Any other session on this account was logged out."));
			}
		});
}

void AValhallaFrontEndController::SubmitDeleteAccount(const FString& Password, const FString& Confirm)
{
	UValhallaBackendSubsystem* Backend = UValhallaBackendSubsystem::Get(this);
	if (!Backend || !Session.IsValid())
	{
		return;
	}

	if (SelectScreen)
	{
		SelectScreen->SetError(FString());
		SelectScreen->SetBusy(true);
	}

	TWeakObjectPtr<AValhallaFrontEndController> WeakThis(this);
	Backend->DeleteAccount(Session.Token, Password, Confirm,
		[WeakThis](bool bSuccess, const FString& Error)
		{
			AValhallaFrontEndController* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}

			if (!bSuccess)
			{
				if (Self->SelectScreen)
				{
					Self->SelectScreen->SetBusy(false);
				}
				Self->ReportError(Error);
				return;
			}

			const FString Deleted = Self->Session.Username;
			UE_LOG(LogValhallaFrontEnd, Log, TEXT("account '%s' deleted by its owner."), *Deleted);

			if (Self->SelectScreen)
			{
				Self->SelectScreen->ClearAccountFields();
			}
			Self->LogOut();

			if (Self->LoginScreen)
			{
				Self->LoginScreen->SetCredentials(FString(), FString());
				Self->LoginScreen->SetError(FString::Printf(TEXT("Account %s was deleted."), *Deleted));
			}
		});
}

FString AValhallaFrontEndController::ResolveServerAddress() const
{
	const UWorld* World = GetWorld();

#if WITH_EDITOR
	if (World && World->WorldType == EWorldType::PIE)
	{
		return CVarPieServerAddress.GetValueOnGameThread();
	}
#endif

	return UValhallaDataSettings::Get()->GetResolvedGameServerAddress();
}

void AValhallaFrontEndController::EnterWorld(int32 CharacterId)
{
	if (CharacterId <= 0 || !Session.IsValid())
	{
		ReportError(TEXT("No character selected, or the session expired."));
		return;
	}

	// UE URL options are separated by '?', not '&' — FURL::Parse splits on the
	// former and UGameplayStatics::ParseOption reads what it produces. A JWT
	// contains neither character, so this is unambiguous; the game mode still
	// tolerates an '&' because the contract was written with one.
	const FString Address = ResolveServerAddress();
	const FString Url = FString::Printf(TEXT("%s?token=%s?characterId=%d"),
		*Address, *Session.Token, CharacterId);

	UE_LOG(LogValhallaFrontEnd, Log, TEXT("entering world: %s?token=%s?characterId=%d"),
		*Address, *UValhallaBackendSubsystem::RedactToken(Session.Token), CharacterId);

	// B-21: the travel destroys this controller and its Session; the backend
	// subsystem (per game instance, survives the travel) keeps the token and
	// the character id in memory so the in-world client can sync its own UI
	// settings (GET/PUT /api/characters/:id/settings) with its own token.
	if (UValhallaBackendSubsystem* Backend = UValhallaBackendSubsystem::Get(this))
	{
		Backend->SetPlayerSession(Session.Token, Session.UserId, CharacterId);
	}

	ClientTravel(Url, TRAVEL_Absolute);
}

void AValhallaFrontEndController::ShowPreviewFor(const FValhallaCharacterSummary* Summary)
{
	if (!PreviewStage)
	{
		return;
	}
	if (Summary)
	{
		PreviewStage->ShowCharacter(*Summary);
	}
	else
	{
		PreviewStage->Clear();
	}
}

void AValhallaFrontEndController::LogOut()
{
	Session = FValhallaAuthSession();
	if (UValhallaBackendSubsystem* Backend = UValhallaBackendSubsystem::Get(this))
	{
		Backend->ClearPlayerSession();
	}
	if (UValhallaGraphicsSettingsSubsystem* Graphics = UValhallaGraphicsSettingsSubsystem::Get(this))
	{
		Graphics->ForgetAccount();
	}
	bAutoLoginActive = false;

	if (PreviewStage)
	{
		PreviewStage->Destroy();
		PreviewStage = nullptr;
	}

	if (SelectScreen)
	{
		SelectScreen->RemoveFromParent();
	}

	if (LoginScreen && !LoginScreen->IsInViewport())
	{
		LoginScreen->AddToViewport();
		LoginScreen->SetError(FString());
		LoginScreen->SetBusy(false);
		LoginScreen->ClearPassword();
		LoginScreen->CheckServer();
	}

	UE_LOG(LogValhallaFrontEnd, Log, TEXT("logged out."));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Auto-login
// ─────────────────────────────────────────────────────────────────────────────

void AValhallaFrontEndController::AutoLogin(const FString& Username, const FString& Password, const FString& CharacterName, FName ClassId)
{
	bAutoLoginActive = true;
	AutoCharacterName = CharacterName;
	AutoClassId = ClassId.IsNone() ? FName(TEXT("warrior")) : ClassId;

	UE_LOG(LogValhallaFrontEnd, Log, TEXT("AutoLogin: user='%s' character='%s' class='%s'"),
		*Username, *CharacterName, *AutoClassId.ToString());

	if (LoginScreen)
	{
		LoginScreen->SetCredentials(Username, Password);
		LoginScreen->SetBusy(true);
	}

	UValhallaBackendSubsystem* Backend = UValhallaBackendSubsystem::Get(this);
	if (!Backend)
	{
		ReportError(TEXT("AutoLogin: no backend subsystem."));
		return;
	}

	TWeakObjectPtr<AValhallaFrontEndController> WeakThis(this);
	Backend->Login(Username, Password,
		[WeakThis, Username, Password](bool bSuccess, const FValhallaAuthSession& InSession, const FString& Error)
		{
			AValhallaFrontEndController* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}

			if (bSuccess)
			{
				Self->Session = InSession;
				if (Self->LoginScreen)
				{
					Self->LoginScreen->SetBusy(false);
				}
				Self->ShowCharacterSelect();
				return;
			}

			// A banned account exists: registering would only fail with
			// "Username already taken" and hide the ban's end date and reason.
			if (InSession.HttpStatus == 403)
			{
				if (Self->LoginScreen)
				{
					Self->LoginScreen->SetBusy(false);
				}
				Self->ReportError(Error);
				return;
			}

			// A failed login during an auto-login is assumed to be a new
			// account, not a wrong password: the accounts this is used with are
			// throwaway and created by the run that uses them. A register that
			// fails too reports the register's error, which is the useful one.
			UE_LOG(LogValhallaFrontEnd, Log, TEXT("AutoLogin: '%s' did not log in; registering."), *Username);
			Self->SubmitRegister(Username, Password);
		});
}

void AValhallaFrontEndController::TryAutoLoginFromCVar()
{
	const FString Setting = CVarAutoLogin.GetValueOnGameThread();
	if (Setting.IsEmpty() || AutoLoginSlot == INDEX_NONE)
	{
		return;
	}

	TArray<FString> Entries;
	Setting.ParseIntoArray(Entries, TEXT(","), /*InCullEmpty=*/true);
	if (Entries.Num() == 0)
	{
		return;
	}

	const FString Entry = Entries[AutoLoginSlot % Entries.Num()].TrimStartAndEnd();

	TArray<FString> Fields;
	Entry.ParseIntoArray(Fields, TEXT(":"), /*InCullEmpty=*/false);
	if (Fields.Num() < 3)
	{
		UE_LOG(LogValhallaFrontEnd, Warning,
			TEXT("valhalla.AutoLogin entry '%s' is not user:pass:charname[:class]; ignored."), *Entry);
		return;
	}

	AutoLogin(Fields[0], Fields[1], Fields[2], Fields.Num() >= 4 ? FName(*Fields[3]) : NAME_None);
}

namespace
{
	/**
	 * `valhalla.AutoLoginNow user:pass:charname[:class]` — the same flow on the
	 * focused client, for a run that did not set the cvar before Play.
	 */
	FAutoConsoleCommandWithWorldAndArgs GAutoLoginNowCommand(
		TEXT("valhalla.AutoLoginNow"),
		TEXT("Dev only. 'user:pass:charname[:class]' — runs the front-end login flow on this client now."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				if (Args.Num() < 1 || !World)
				{
					UE_LOG(LogValhallaFrontEnd, Warning, TEXT("usage: valhalla.AutoLoginNow user:pass:charname[:class]"));
					return;
				}

				TArray<FString> Fields;
				Args[0].ParseIntoArray(Fields, TEXT(":"), /*InCullEmpty=*/false);
				if (Fields.Num() < 3)
				{
					UE_LOG(LogValhallaFrontEnd, Warning, TEXT("usage: valhalla.AutoLoginNow user:pass:charname[:class]"));
					return;
				}

				AValhallaFrontEndController* Controller = Cast<AValhallaFrontEndController>(World->GetFirstPlayerController());
				if (!Controller)
				{
					UE_LOG(LogValhallaFrontEnd, Warning, TEXT("this world has no front-end controller."));
					return;
				}

				Controller->AutoLogin(Fields[0], Fields[1], Fields[2], Fields.Num() >= 4 ? FName(*Fields[3]) : NAME_None);
			}));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Game mode
// ─────────────────────────────────────────────────────────────────────────────

AValhallaFrontEndGameMode::AValhallaFrontEndGameMode()
{
	// No pawn at all: the front end is a menu, and a DefaultPawnClass here
	// would spawn a floating camera behind the login card in every PIE run.
	DefaultPawnClass = nullptr;
	PlayerControllerClass = AValhallaFrontEndController::StaticClass();
	HUDClass = nullptr;
	bStartPlayersAsSpectators = false;
	bUseSeamlessTravel = false;
}

void AValhallaFrontEndGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	// `L_FrontEnd` is a client map. PIE hands the dedicated server whatever
	// level the editor has open, so the server has to be able to find its own
	// way off it — see AValhallaFrontEndController's class comment.
	UWorld* World = GetWorld();
	if (World && World->GetNetMode() == NM_DedicatedServer && !ServerRedirectMap.IsEmpty())
	{
		UE_LOG(LogValhallaFrontEnd, Log,
			TEXT("dedicated server loaded the front end map '%s'; travelling to '%s' (listening on port %d)."),
			*MapName, *ServerRedirectMap, World->URL.Port);

		// Next tick, not now: InitGame runs inside the level load and a travel
		// issued from here is dropped.
		FTimerHandle Handle;
		const FString Destination = ServerRedirectMap;
		World->GetTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateWeakLambda(this, [World, Destination]()
			{
				// **Relative, not absolute, and that is the whole bug fix.**
				// `FURL(&LastURL, …, TRAVEL_Absolute)` throws the base URL away
				// and the new one falls back to the default port — so an
				// absolute travel here moved a PIE server that was listening on
				// 17777 (ULevelEditorPlaySettings::ServerPort) onto 7777, and
				// every client kept knocking politely on a door that had moved.
				// Nothing errored: the clients just logged "no packets received
				// yet" until they timed out. A relative travel inherits the
				// host and port, which is what "the same server, a different
				// map" actually means.
				World->ServerTravel(Destination + TEXT("?listen"), /*bAbsolute=*/false);
			}));
	}
}
