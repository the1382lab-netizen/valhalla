// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaFrontEnd.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Styling/CoreStyle.h"
#include "ValhallaBackendSubsystem.h"
#include "ValhallaConstants.h"
#include "ValhallaDataSettings.h"
#include "ValhallaDataSubsystem.h"

DEFINE_LOG_CATEGORY(LogValhallaFrontEnd);

namespace ValhallaUI
{
	const FLinearColor Background(0.020f, 0.024f, 0.035f, 1.0f);
	const FLinearColor Panel(0.055f, 0.062f, 0.085f, 1.0f);
	const FLinearColor PanelSelected(0.130f, 0.110f, 0.045f, 1.0f);
	const FLinearColor Ink(0.900f, 0.910f, 0.930f, 1.0f);
	const FLinearColor InkDim(0.480f, 0.510f, 0.570f, 1.0f);
	const FLinearColor Error(0.930f, 0.380f, 0.330f, 1.0f);
	const FLinearColor Accent(0.930f, 0.760f, 0.320f, 1.0f);
}

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

	FSlateFontInfo Font(int32 Size, const TCHAR* Style = TEXT("Regular"))
	{
		return FCoreStyle::GetDefaultFontStyle(Style, Size);
	}

	/** A label. */
	UTextBlock* MakeText(UWidgetTree& Tree, const FString& Content, int32 Size, const FLinearColor& Color, const TCHAR* Style = TEXT("Regular"))
	{
		UTextBlock* Text = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Text->SetText(AsText(Content));
		Text->SetFont(Font(Size, Style));
		Text->SetColorAndOpacity(FSlateColor(Color));
		return Text;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Row button
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaCharacterRowButton::HandleClicked()
{
	if (UValhallaCharacterSelectWidget* Widget = Screen.Get())
	{
		Widget->HandleRowClicked(RowIndex);
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

	UBorder* Root = Tree.ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Root"));
	Root->SetBrushColor(ValhallaUI::Background);
	Root->SetHorizontalAlignment(HAlign_Center);
	Root->SetVerticalAlignment(VAlign_Center);
	Tree.RootWidget = Root;

	USizeBox* Sizer = Tree.ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Sizer->SetWidthOverride(460.f);
	Root->SetContent(Sizer);

	UBorder* Card = Tree.ConstructWidget<UBorder>(UBorder::StaticClass());
	Card->SetBrushColor(ValhallaUI::Panel);
	Card->SetPadding(FMargin(28.f, 26.f));
	Sizer->SetContent(Card);

	UVerticalBox* Column = Tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Card->SetContent(Column);

	auto AddRow = [&Column](UWidget* Widget, float TopPad, EHorizontalAlignment HAlign = HAlign_Fill) -> UVerticalBoxSlot*
	{
		UVerticalBoxSlot* RowSlot = Column->AddChildToVerticalBox(Widget);
		RowSlot->SetPadding(FMargin(0.f, TopPad, 0.f, 0.f));
		RowSlot->SetHorizontalAlignment(HAlign);
		return RowSlot;
	};

	AddRow(MakeText(Tree, TEXT("VALHALLA"), 34, ValhallaUI::Accent, TEXT("Bold")), 0.f, HAlign_Center);
	AddRow(MakeText(Tree, TEXT("Sign in to choose a character"), 12, ValhallaUI::InkDim), 2.f, HAlign_Center);

	// The backend URL, on screen, for the reason in the class comment.
	StatusText = MakeText(Tree,
		FString::Printf(TEXT("backend  %s"), *UValhallaDataSettings::Get()->GetResolvedBackendUrl()),
		10, ValhallaUI::InkDim);
	AddRow(StatusText, 10.f, HAlign_Center);

	UsernameBox = Tree.ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
	UsernameBox->SetHintText(AsText(TEXT("Username")));
	UsernameBox->OnTextCommitted.AddDynamic(this, &UValhallaLoginWidget::HandleTextCommitted);
	AddRow(UsernameBox, 20.f);

	PasswordBox = Tree.ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
	PasswordBox->SetHintText(AsText(TEXT("Password")));
	PasswordBox->SetIsPassword(true);
	PasswordBox->OnTextCommitted.AddDynamic(this, &UValhallaLoginWidget::HandleTextCommitted);
	AddRow(PasswordBox, 8.f);

	UHorizontalBox* Buttons = Tree.ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	AddRow(Buttons, 16.f);

	auto AddButton = [&Tree, Buttons](const FString& Caption, const FLinearColor& Tint, TObjectPtr<UButton>& Out)
	{
		Out = Tree.ConstructWidget<UButton>(UButton::StaticClass());
		Out->SetBackgroundColor(Tint);
		Out->AddChild(MakeText(Tree, Caption, 14, FLinearColor::Black, TEXT("Bold")));

		UHorizontalBoxSlot* ButtonSlot = Buttons->AddChildToHorizontalBox(Out);
		ButtonSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ButtonSlot->SetPadding(FMargin(3.f, 0.f));
		ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
	};

	AddButton(TEXT("Log In"), ValhallaUI::Accent, LoginButton);
	AddButton(TEXT("Register"), ValhallaUI::InkDim, RegisterButton);

	LoginButton->OnClicked.AddDynamic(this, &UValhallaLoginWidget::HandleLoginClicked);
	RegisterButton->OnClicked.AddDynamic(this, &UValhallaLoginWidget::HandleRegisterClicked);

	ErrorText = MakeText(Tree, FString(), 12, ValhallaUI::Error);
	ErrorText->SetAutoWrapText(true);
	AddRow(ErrorText, 14.f);
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
		StatusText->SetText(AsText(bBusy
			? TEXT("contacting the backend…")
			: FString::Printf(TEXT("backend  %s"), *UValhallaDataSettings::Get()->GetResolvedBackendUrl())));
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
		Controller->SubmitLogin(UsernameBox->GetText().ToString(), PasswordBox->GetText().ToString());
	}
}

void UValhallaLoginWidget::HandleRegisterClicked()
{
	if (AValhallaFrontEndController* Controller = Owner.Get())
	{
		Controller->SubmitRegister(UsernameBox->GetText().ToString(), PasswordBox->GetText().ToString());
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

void UValhallaCharacterSelectWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildUi();
}

void UValhallaCharacterSelectWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildUi();
	SetError(FString());
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

	UBorder* Root = Tree.ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Root"));
	Root->SetBrushColor(ValhallaUI::Background);
	Root->SetHorizontalAlignment(HAlign_Center);
	Root->SetVerticalAlignment(VAlign_Center);
	Tree.RootWidget = Root;

	USizeBox* Sizer = Tree.ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Sizer->SetWidthOverride(520.f);
	Root->SetContent(Sizer);

	UBorder* Card = Tree.ConstructWidget<UBorder>(UBorder::StaticClass());
	Card->SetBrushColor(ValhallaUI::Panel);
	Card->SetPadding(FMargin(26.f, 24.f));
	Sizer->SetContent(Card);

	UVerticalBox* Column = Tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Card->SetContent(Column);

	auto AddRow = [&Column](UWidget* Widget, float TopPad, EHorizontalAlignment HAlign = HAlign_Fill) -> UVerticalBoxSlot*
	{
		UVerticalBoxSlot* RowSlot = Column->AddChildToVerticalBox(Widget);
		RowSlot->SetPadding(FMargin(0.f, TopPad, 0.f, 0.f));
		RowSlot->SetHorizontalAlignment(HAlign);
		return RowSlot;
	};

	AddRow(MakeText(Tree, TEXT("CHOOSE YOUR CHARACTER"), 22, ValhallaUI::Accent, TEXT("Bold")), 0.f, HAlign_Center);

	HeaderText = MakeText(Tree, FString(), 11, ValhallaUI::InkDim);
	AddRow(HeaderText, 4.f, HAlign_Center);

	RowBox = Tree.ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
	UVerticalBoxSlot* ListSlot = AddRow(RowBox, 16.f);
	ListSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	// ── actions ─────────────────────────────────────────────────────────
	UHorizontalBox* Actions = Tree.ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	AddRow(Actions, 14.f);

	auto AddAction = [&Tree, Actions](const FString& Caption, const FLinearColor& Tint, TObjectPtr<UButton>& Out)
	{
		Out = Tree.ConstructWidget<UButton>(UButton::StaticClass());
		Out->SetBackgroundColor(Tint);
		Out->AddChild(MakeText(Tree, Caption, 13, FLinearColor::Black, TEXT("Bold")));

		UHorizontalBoxSlot* ActionSlot = Actions->AddChildToHorizontalBox(Out);
		ActionSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ActionSlot->SetPadding(FMargin(3.f, 0.f));
		ActionSlot->SetHorizontalAlignment(HAlign_Fill);
	};

	TObjectPtr<UButton> LogOutButton = nullptr;
	AddAction(TEXT("Enter World"), ValhallaUI::Accent, EnterWorldButton);
	AddAction(TEXT("Create"), ValhallaUI::InkDim, CreateButton);
	AddAction(TEXT("Delete"), ValhallaUI::Error, DeleteButton);
	AddAction(TEXT("Log Out"), ValhallaUI::InkDim, LogOutButton);

	EnterWorldButton->OnClicked.AddDynamic(this, &UValhallaCharacterSelectWidget::HandleEnterWorldClicked);
	CreateButton->OnClicked.AddDynamic(this, &UValhallaCharacterSelectWidget::HandleCreateClicked);
	DeleteButton->OnClicked.AddDynamic(this, &UValhallaCharacterSelectWidget::HandleDeleteClicked);
	LogOutButton->OnClicked.AddDynamic(this, &UValhallaCharacterSelectWidget::HandleLogOutClicked);

	// ── create panel, collapsed until Create is pressed ─────────────────
	CreatePanel = Tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	CreatePanel->SetVisibility(ESlateVisibility::Collapsed);
	AddRow(CreatePanel, 14.f);

	{
		UVerticalBoxSlot* HeadingSlot = CreatePanel->AddChildToVerticalBox(MakeText(Tree, TEXT("New character"), 13, ValhallaUI::Ink, TEXT("Bold")));
		HeadingSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

		NewNameBox = Tree.ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
		NewNameBox->SetHintText(AsText(FString::Printf(TEXT("Name (%d-%d characters)"),
			Valhalla::MinCharacterNameLength, Valhalla::MaxCharacterNameLength)));
		CreatePanel->AddChildToVerticalBox(NewNameBox)->SetPadding(FMargin(0.f, 2.f));

		// Every class classes.json knows about, and not a hard-coded six.
		NewClassBox = Tree.ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass());
		if (const UGameInstance* GameInstance = GetGameInstance())
		{
			if (const UValhallaDataSubsystem* Data = GameInstance->GetSubsystem<UValhallaDataSubsystem>())
			{
				for (const FName& ClassId : Data->GetAllClassIds())
				{
					NewClassBox->AddOption(ClassId.ToString());
				}
			}
		}
		if (NewClassBox->GetOptionCount() > 0)
		{
			NewClassBox->SetSelectedOption(NewClassBox->GetOptionAtIndex(0));
		}
		CreatePanel->AddChildToVerticalBox(NewClassBox)->SetPadding(FMargin(0.f, 2.f));

		UHorizontalBox* CreateActions = Tree.ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		CreatePanel->AddChildToVerticalBox(CreateActions)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));

		UButton* Confirm = Tree.ConstructWidget<UButton>(UButton::StaticClass());
		Confirm->SetBackgroundColor(ValhallaUI::Accent);
		Confirm->AddChild(MakeText(Tree, TEXT("Create Character"), 13, FLinearColor::Black, TEXT("Bold")));
		CreateActions->AddChildToHorizontalBox(Confirm)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Confirm->OnClicked.AddDynamic(this, &UValhallaCharacterSelectWidget::HandleCreateConfirmClicked);

		UButton* Cancel = Tree.ConstructWidget<UButton>(UButton::StaticClass());
		Cancel->SetBackgroundColor(ValhallaUI::InkDim);
		Cancel->AddChild(MakeText(Tree, TEXT("Cancel"), 13, FLinearColor::Black));
		CreateActions->AddChildToHorizontalBox(Cancel)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Cancel->OnClicked.AddDynamic(this, &UValhallaCharacterSelectWidget::HandleCreateCancelClicked);
	}

	// ── delete confirm, collapsed until Delete is pressed ───────────────
	DeletePanel = Tree.ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	DeletePanel->SetVisibility(ESlateVisibility::Collapsed);
	AddRow(DeletePanel, 14.f);

	{
		DeletePrompt = MakeText(Tree, FString(), 13, ValhallaUI::Error);
		DeletePrompt->SetAutoWrapText(true);
		DeletePanel->AddChildToVerticalBox(DeletePrompt);

		UHorizontalBox* DeleteActions = Tree.ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		DeletePanel->AddChildToVerticalBox(DeleteActions)->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));

		UButton* Confirm = Tree.ConstructWidget<UButton>(UButton::StaticClass());
		Confirm->SetBackgroundColor(ValhallaUI::Error);
		Confirm->AddChild(MakeText(Tree, TEXT("Delete Permanently"), 13, FLinearColor::Black, TEXT("Bold")));
		DeleteActions->AddChildToHorizontalBox(Confirm)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Confirm->OnClicked.AddDynamic(this, &UValhallaCharacterSelectWidget::HandleDeleteConfirmClicked);

		UButton* Cancel = Tree.ConstructWidget<UButton>(UButton::StaticClass());
		Cancel->SetBackgroundColor(ValhallaUI::InkDim);
		Cancel->AddChild(MakeText(Tree, TEXT("Keep"), 13, FLinearColor::Black));
		DeleteActions->AddChildToHorizontalBox(Cancel)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Cancel->OnClicked.AddDynamic(this, &UValhallaCharacterSelectWidget::HandleDeleteCancelClicked);
	}

	ErrorText = MakeText(Tree, FString(), 12, ValhallaUI::Error);
	ErrorText->SetAutoWrapText(true);
	AddRow(ErrorText, 12.f);
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

	for (int32 Index = 0; Index < Characters.Num(); ++Index)
	{
		if (Characters[Index].Id == PreviousId)
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
		HeaderText->SetText(AsText(FString::Printf(TEXT("%s — %d of %d character slots used"),
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
		RowBox->AddChild(MakeText(*WidgetTree, TEXT("No characters yet. Press Create."), 13, ValhallaUI::InkDim));
	}

	for (int32 Index = 0; Index < Characters.Num(); ++Index)
	{
		const FValhallaCharacterSummary& Summary = Characters[Index];

		UValhallaCharacterRowButton* Row = WidgetTree->ConstructWidget<UValhallaCharacterRowButton>(UValhallaCharacterRowButton::StaticClass());
		Row->RowIndex = Index;
		Row->Screen = this;
		Row->SetBackgroundColor(Index == SelectedIndex ? ValhallaUI::PanelSelected : ValhallaUI::Panel);
		Row->OnClicked.AddDynamic(Row, &UValhallaCharacterRowButton::HandleClicked);

		Row->AddChild(MakeText(*WidgetTree,
			FString::Printf(TEXT("%s   [%s lv%d]"), *Summary.Name, *Summary.ClassId.ToString(), Summary.Level),
			15, Index == SelectedIndex ? ValhallaUI::Accent : ValhallaUI::Ink,
			Index == SelectedIndex ? TEXT("Bold") : TEXT("Regular")));

		RowBox->AddChild(Row);
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
	if (Characters.IsValidIndex(RowIndex))
	{
		SelectedIndex = RowIndex;
		SetError(FString());
		RefreshRows();
	}
}

void UValhallaCharacterSelectWidget::SetError(const FString& Message)
{
	if (ErrorText)
	{
		ErrorText->SetText(AsText(Message));
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
	DeletePanel->SetVisibility(ESlateVisibility::Collapsed);
	CreatePanel->SetVisibility(ESlateVisibility::Visible);
}

void UValhallaCharacterSelectWidget::HandleCreateCancelClicked()
{
	CreatePanel->SetVisibility(ESlateVisibility::Collapsed);
}

void UValhallaCharacterSelectWidget::HandleCreateConfirmClicked()
{
	const FString Name = NewNameBox ? NewNameBox->GetText().ToString().TrimStartAndEnd() : FString();
	const FString ClassName = NewClassBox ? NewClassBox->GetSelectedOption() : FString();

	// The backend validates all of this too, and its message is the one shown
	// when it does. This is only so the common mistake costs no round trip.
	if (Name.Len() < Valhalla::MinCharacterNameLength || Name.Len() > Valhalla::MaxCharacterNameLength)
	{
		SetError(FString::Printf(TEXT("Character name must be %d-%d characters."),
			Valhalla::MinCharacterNameLength, Valhalla::MaxCharacterNameLength));
		return;
	}

	if (ClassName.IsEmpty())
	{
		SetError(TEXT("Pick a class."));
		return;
	}

	if (AValhallaFrontEndController* Controller = Owner.Get())
	{
		CreatePanel->SetVisibility(ESlateVisibility::Collapsed);
		Controller->SubmitCreateCharacter(Name, FName(*ClassName));
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
	CreatePanel->SetVisibility(ESlateVisibility::Collapsed);
	DeletePrompt->SetText(AsText(FString::Printf(
		TEXT("Delete %s [%s lv%d]? This cannot be undone."),
		*Selected->Name, *Selected->ClassId.ToString(), Selected->Level)));
	DeletePanel->SetVisibility(ESlateVisibility::Visible);
}

void UValhallaCharacterSelectWidget::HandleDeleteCancelClicked()
{
	DeletePanel->SetVisibility(ESlateVisibility::Collapsed);
}

void UValhallaCharacterSelectWidget::HandleDeleteConfirmClicked()
{
	const FValhallaCharacterSummary* Selected = GetSelected();
	if (!Selected)
	{
		return;
	}

	const int32 CharacterId = Selected->Id;
	DeletePanel->SetVisibility(ESlateVisibility::Collapsed);

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
		[WeakThis](bool bSuccess, const FValhallaAuthSession& InSession, const FString& Error)
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
		[WeakThis](bool bSuccess, const FValhallaAuthSession& InSession, const FString& Error)
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

			Self->Session = InSession;
			Self->ShowCharacterSelect();
		});
}

void AValhallaFrontEndController::ShowCharacterSelect()
{
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
		[WeakThis](bool bSuccess, const FValhallaCharacterSummary& /*Character*/, const FString& Error)
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

	ClientTravel(Url, TRAVEL_Absolute);
}

void AValhallaFrontEndController::LogOut()
{
	Session = FValhallaAuthSession();
	bAutoLoginActive = false;

	if (SelectScreen)
	{
		SelectScreen->RemoveFromParent();
	}

	if (LoginScreen && !LoginScreen->IsInViewport())
	{
		LoginScreen->AddToViewport();
		LoginScreen->SetError(FString());
		LoginScreen->SetBusy(false);
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
		[WeakThis, Username, Password](bool bSuccess, const FValhallaAuthSession& InSession, const FString& /*Error*/)
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
