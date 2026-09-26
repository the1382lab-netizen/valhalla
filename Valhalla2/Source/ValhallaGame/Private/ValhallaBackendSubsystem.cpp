// Copyright Valhalla 2.0. All Rights Reserved.

#include "ValhallaBackendSubsystem.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "HttpModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ValhallaConstants.h"
#include "ValhallaDataSettings.h"
#include "ValhallaPlayerState.h"
#include "ValhallaStats.h"

DEFINE_LOG_CATEGORY(LogValhallaBackend);

namespace
{
	/** Pull `{"error": "..."}` out of a body, or fall back to a status line. */
	FString ErrorFromJson(const TSharedPtr<FJsonObject>& Json, int32 StatusCode)
	{
		FString Message;
		if (Json.IsValid() && Json->TryGetStringField(TEXT("error"), Message) && !Message.IsEmpty())
		{
			return Message;
		}

		if (StatusCode <= 0)
		{
			// No status at all means the request never reached anything: the
			// backend is down, the URL is wrong, or the ten seconds ran out.
			return TEXT("Cannot reach the backend.");
		}

		return FString::Printf(TEXT("Backend returned HTTP %d."), StatusCode);
	}

	/** JSON numbers are doubles; every int on the wire arrives as one. */
	int32 GetIntField(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field, int32 Default = 0)
	{
		double Value = 0.0;
		return (Json.IsValid() && Json->TryGetNumberField(Field, Value)) ? static_cast<int32>(Value) : Default;
	}

	double GetNumberField(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field, double Default = 0.0)
	{
		double Value = 0.0;
		return (Json.IsValid() && Json->TryGetNumberField(Field, Value)) ? Value : Default;
	}

	FString GetStringField(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field)
	{
		FString Value;
		return (Json.IsValid() && Json->TryGetStringField(Field, Value)) ? Value : FString();
	}

	/** One summary out of a `characters[]` entry. */
	FValhallaCharacterSummary SummaryFromJson(const TSharedPtr<FJsonObject>& Json)
	{
		FValhallaCharacterSummary Summary;
		Summary.Id      = GetIntField(Json, TEXT("id"));
		Summary.Name    = GetStringField(Json, TEXT("name"));
		Summary.ClassId = FName(*GetStringField(Json, TEXT("classId")));
		Summary.Level   = GetIntField(Json, TEXT("level"), 1);
		return Summary;
	}

	/** `characters` off a login / list body, tolerating its absence. */
	void SummariesFromJson(const TSharedPtr<FJsonObject>& Json, TArray<FValhallaCharacterSummary>& Out)
	{
		Out.Reset();

		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!Json.IsValid() || !Json->TryGetArrayField(TEXT("characters"), Array) || !Array)
		{
			return;
		}

		for (const TSharedPtr<FJsonValue>& Entry : *Array)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (Entry.IsValid() && Entry->TryGetObject(Obj) && Obj)
			{
				Out.Add(SummaryFromJson(*Obj));
			}
		}
	}

	FString JsonToString(const TSharedPtr<FJsonObject>& Json)
	{
		FString Out;
		if (Json.IsValid())
		{
			const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
			FJsonSerializer::Serialize(Json.ToSharedRef(), Writer);
		}
		return Out;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaBackendSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UValhallaDataSettings* Settings = UValhallaDataSettings::Get();
	UE_LOG(LogValhallaBackend, Log, TEXT("backend at %s (timeout %.0fs)"),
		*Settings->GetResolvedBackendUrl(), RequestTimeoutSeconds);
}

void UValhallaBackendSubsystem::Deinitialize()
{
	// Nothing to tear down: every request holds only a weak pointer to this,
	// and FHttpModule outlives the game instance either way. A request in
	// flight when PIE stops completes into a lambda that finds a dead weak
	// pointer and returns, which is the whole reason it is weak.
	Super::Deinitialize();
}

UValhallaBackendSubsystem* UValhallaBackendSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UValhallaBackendSubsystem>() : nullptr;
}

FString UValhallaBackendSubsystem::MakeUrl(const FString& Path) const
{
	return UValhallaDataSettings::Get()->GetResolvedBackendUrl() + Path;
}

FString UValhallaBackendSubsystem::RedactToken(const FString& Token)
{
	if (Token.IsEmpty())
	{
		return TEXT("(none)");
	}

	return Token.Len() <= 8 ? Token + TEXT("…") : Token.Left(8) + TEXT("…");
}

// ─────────────────────────────────────────────────────────────────────────────
//  The one request path
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaBackendSubsystem::Send(
	const FString& Verb,
	const FString& Path,
	const TSharedPtr<FJsonObject>& Body,
	const FString& BearerToken,
	bool bServerAuth,
	TFunction<void(bool, int32, const TSharedPtr<FJsonObject>&, const FString&)> OnDone)
{
	const FString Url = MakeUrl(Path);

	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(Verb);
	Request->SetTimeout(RequestTimeoutSeconds);
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));

	if (Body.IsValid())
	{
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		Request->SetContentAsString(JsonToString(Body));
	}

	if (bServerAuth)
	{
		Request->SetHeader(TEXT("X-Server-Secret"), UValhallaDataSettings::Get()->GetServerSecret());
	}
	else if (!BearerToken.IsEmpty())
	{
		Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + BearerToken);
	}

	const double StartTime = FPlatformTime::Seconds();

	UE_LOG(LogValhallaBackend, Verbose, TEXT("-> %s %s%s%s"),
		*Verb, *Url,
		bServerAuth ? TEXT(" [server secret]") : TEXT(""),
		BearerToken.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" [bearer %s]"), *RedactToken(BearerToken)));

	// Weak, so a completion that lands after PIE stopped finds nothing and
	// returns rather than calling through a destroyed game instance.
	TWeakObjectPtr<UValhallaBackendSubsystem> WeakThis(this);

	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, Verb, Url, StartTime, OnDone = MoveTemp(OnDone)]
		(FHttpRequestPtr /*Req*/, FHttpResponsePtr Response, bool bConnectedSuccessfully)
		{
			const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
			const int32 StatusCode = Response.IsValid() ? Response->GetResponseCode() : 0;

			TSharedPtr<FJsonObject> Json;
			if (Response.IsValid())
			{
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
				FJsonSerializer::Deserialize(Reader, Json);
			}

			const bool bSuccess = bConnectedSuccessfully && StatusCode >= 200 && StatusCode < 300;

			UE_LOG(LogValhallaBackend, Verbose, TEXT("<- %s %s : %d (%.0f ms)%s"),
				*Verb, *Url, StatusCode, ElapsedMs,
				bSuccess ? TEXT("") : *FString::Printf(TEXT(" %s"), *ErrorFromJson(Json, StatusCode)));

			if (!WeakThis.IsValid())
			{
				UE_LOG(LogValhallaBackend, Verbose, TEXT("   (dropped: the game instance went away)"));
				return;
			}

			if (OnDone)
			{
				OnDone(bSuccess, StatusCode, Json, bSuccess ? FString() : ErrorFromJson(Json, StatusCode));
			}
		});

	if (!Request->ProcessRequest())
	{
		UE_LOG(LogValhallaBackend, Error, TEXT("could not start %s %s."), *Verb, *Url);
		if (OnDone)
		{
			OnDone(false, 0, nullptr, TEXT("Could not start the request."));
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Client routes
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	/** Shared by Login and Register: same body, same response shape. */
	TSharedRef<FJsonObject> MakeCredentialBody(const FString& Username, const FString& Password)
	{
		const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("username"), Username);
		Body->SetStringField(TEXT("password"), Password);
		return Body;
	}
}

void UValhallaBackendSubsystem::Register(const FString& Username, const FString& Password, FValhallaAuthCallback OnDone)
{
	// Note what is *not* in this log line and never will be.
	UE_LOG(LogValhallaBackend, Verbose, TEXT("register user='%s'"), *Username);

	Send(TEXT("POST"), TEXT("/api/auth/register"), MakeCredentialBody(Username, Password), FString(), /*bServerAuth=*/false,
		[OnDone = MoveTemp(OnDone)](bool bSuccess, int32 /*Status*/, const TSharedPtr<FJsonObject>& Json, const FString& Error)
		{
			FValhallaAuthSession Session;
			if (bSuccess)
			{
				Session.Token    = GetStringField(Json, TEXT("token"));
				Session.UserId   = GetIntField(Json, TEXT("userId"));
				Session.Username = GetStringField(Json, TEXT("username"));
				SummariesFromJson(Json, Session.Characters);

				UE_LOG(LogValhallaBackend, Log, TEXT("registered '%s' (userId %d, token %s)"),
					*Session.Username, Session.UserId, *RedactToken(Session.Token));
			}

			if (OnDone)
			{
				OnDone(bSuccess && Session.IsValid(), Session, Error);
			}
		});
}

void UValhallaBackendSubsystem::Login(const FString& Username, const FString& Password, FValhallaAuthCallback OnDone)
{
	UE_LOG(LogValhallaBackend, Verbose, TEXT("login user='%s'"), *Username);

	Send(TEXT("POST"), TEXT("/api/auth/login"), MakeCredentialBody(Username, Password), FString(), /*bServerAuth=*/false,
		[OnDone = MoveTemp(OnDone)](bool bSuccess, int32 Status, const TSharedPtr<FJsonObject>& Json, const FString& Error)
		{
			FValhallaAuthSession Session;
			Session.HttpStatus = Status;
			if (bSuccess)
			{
				Session.Token    = GetStringField(Json, TEXT("token"));
				Session.UserId   = GetIntField(Json, TEXT("userId"));
				Session.Username = GetStringField(Json, TEXT("username"));
				SummariesFromJson(Json, Session.Characters);

				UE_LOG(LogValhallaBackend, Log, TEXT("login ok '%s' (userId %d, token %s, %d characters)"),
					*Session.Username, Session.UserId, *RedactToken(Session.Token), Session.Characters.Num());
			}

			if (OnDone)
			{
				OnDone(bSuccess && Session.IsValid(), Session, Error);
			}
		});
}

void UValhallaBackendSubsystem::ListCharacters(const FString& Token, FValhallaCharacterListCallback OnDone)
{
	Send(TEXT("GET"), TEXT("/api/characters"), nullptr, Token, /*bServerAuth=*/false,
		[OnDone = MoveTemp(OnDone)](bool bSuccess, int32 /*Status*/, const TSharedPtr<FJsonObject>& Json, const FString& Error)
		{
			TArray<FValhallaCharacterSummary> Characters;
			if (bSuccess)
			{
				SummariesFromJson(Json, Characters);
			}

			if (OnDone)
			{
				OnDone(bSuccess, Characters, Error);
			}
		});
}

void UValhallaBackendSubsystem::CreateCharacter(const FString& Token, const FString& Name, FName ClassId, FValhallaCharacterCallback OnDone)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("name"), Name);
	Body->SetStringField(TEXT("classId"), ClassId.ToString());

	Send(TEXT("POST"), TEXT("/api/characters"), Body, Token, /*bServerAuth=*/false,
		[OnDone = MoveTemp(OnDone), Name](bool bSuccess, int32 /*Status*/, const TSharedPtr<FJsonObject>& Json, const FString& Error)
		{
			FValhallaCharacterSummary Character;
			if (bSuccess && Json.IsValid())
			{
				const TSharedPtr<FJsonObject>* Obj = nullptr;
				if (Json->TryGetObjectField(TEXT("character"), Obj) && Obj)
				{
					Character = SummaryFromJson(*Obj);
				}

				UE_LOG(LogValhallaBackend, Log, TEXT("created character '%s' id=%d class=%s"),
					*Character.Name, Character.Id, *Character.ClassId.ToString());
			}

			if (OnDone)
			{
				OnDone(bSuccess && Character.Id > 0, Character, Error);
			}
		});
}

void UValhallaBackendSubsystem::DeleteCharacter(const FString& Token, int32 CharacterId, FValhallaSimpleCallback OnDone)
{
	Send(TEXT("DELETE"), FString::Printf(TEXT("/api/characters/%d"), CharacterId), nullptr, Token, /*bServerAuth=*/false,
		[OnDone = MoveTemp(OnDone), CharacterId](bool bSuccess, int32 /*Status*/, const TSharedPtr<FJsonObject>& /*Json*/, const FString& Error)
		{
			UE_LOG(LogValhallaBackend, Log, TEXT("delete char=%d : %s"), CharacterId, bSuccess ? TEXT("ok") : *Error);

			if (OnDone)
			{
				OnDone(bSuccess, Error);
			}
		});
}

void UValhallaBackendSubsystem::ChangePassword(const FString& Token, const FString& CurrentPassword, const FString& NewPassword,
	TFunction<void(bool, const FString&, const FString&)> OnDone)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("currentPassword"), CurrentPassword);
	Body->SetStringField(TEXT("newPassword"), NewPassword);

	// Passwords are never logged; the Verbose request line logs the path only.
	Send(TEXT("POST"), TEXT("/api/auth/password"), Body, Token, /*bServerAuth=*/false,
		[OnDone = MoveTemp(OnDone)](bool bSuccess, int32 /*Status*/, const TSharedPtr<FJsonObject>& Json, const FString& Error)
		{
			const FString NewToken = bSuccess ? GetStringField(Json, TEXT("token")) : FString();
			const bool bOk = bSuccess && !NewToken.IsEmpty();
			UE_LOG(LogValhallaBackend, Log, TEXT("change password : %s"), bOk ? TEXT("ok") : *Error);

			if (OnDone)
			{
				OnDone(bOk, NewToken, bOk ? FString() : (Error.IsEmpty() ? FString(TEXT("Password change failed.")) : Error));
			}
		});
}

void UValhallaBackendSubsystem::DeleteAccount(const FString& Token, const FString& Password, const FString& Confirm, FValhallaSimpleCallback OnDone)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("password"), Password);
	Body->SetStringField(TEXT("confirm"), Confirm);

	// POST rather than DELETE-with-a-body: the backend accepts both, and not
	// every HTTP stack sends a body on a DELETE.
	Send(TEXT("POST"), TEXT("/api/auth/account/delete"), Body, Token, /*bServerAuth=*/false,
		[OnDone = MoveTemp(OnDone)](bool bSuccess, int32 /*Status*/, const TSharedPtr<FJsonObject>& /*Json*/, const FString& Error)
		{
			UE_LOG(LogValhallaBackend, Log, TEXT("delete account : %s"), bSuccess ? TEXT("ok") : *Error);

			if (OnDone)
			{
				OnDone(bSuccess, Error);
			}
		});
}

// ─────────────────────────────────────────────────────────────────────────────
//  Client routes: UI settings (B-21)
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaBackendSubsystem::GetCharacterSettings(const FString& Token, int32 CharacterId, FValhallaSettingsGetCallback OnDone)
{
	Send(TEXT("GET"), FString::Printf(TEXT("/api/characters/%d/settings"), CharacterId), nullptr, Token, /*bServerAuth=*/false,
		[OnDone = MoveTemp(OnDone), CharacterId](bool bSuccess, int32 Status, const TSharedPtr<FJsonObject>& Json, const FString& Error)
		{
			TSharedPtr<FJsonObject> Ui;
			FString UpdatedAt;
			if (bSuccess && Json.IsValid())
			{
				const TSharedPtr<FJsonObject>* UiObject = nullptr;
				if (Json->TryGetObjectField(TEXT("ui"), UiObject) && UiObject)
				{
					Ui = *UiObject;
				}
				UpdatedAt = GetStringField(Json, TEXT("updatedAt"));
			}
			UE_LOG(LogValhallaBackend, Log, TEXT("settings get char=%d : %d%s"), CharacterId, Status,
				bSuccess ? (Ui.IsValid() ? TEXT(" ok") : TEXT(" (no ui object)")) : *FString::Printf(TEXT(" %s"), *Error));
			if (OnDone)
			{
				OnDone(bSuccess && Ui.IsValid(), Status, Ui, UpdatedAt, Error);
			}
		});
}

void UValhallaBackendSubsystem::PutCharacterSettings(const FString& Token, int32 CharacterId, const TSharedRef<FJsonObject>& Ui, FValhallaSettingsPutCallback OnDone)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("ui"), Ui);
	Send(TEXT("PUT"), FString::Printf(TEXT("/api/characters/%d/settings"), CharacterId), Body, Token, /*bServerAuth=*/false,
		[OnDone = MoveTemp(OnDone), CharacterId](bool bSuccess, int32 Status, const TSharedPtr<FJsonObject>& Json, const FString& Error)
		{
			const FString UpdatedAt = bSuccess ? GetStringField(Json, TEXT("updatedAt")) : FString();
			UE_LOG(LogValhallaBackend, Log, TEXT("settings put char=%d : %d%s"), CharacterId, Status,
				bSuccess ? TEXT(" ok") : *FString::Printf(TEXT(" %s"), *Error));
			if (OnDone)
			{
				OnDone(bSuccess, Status, UpdatedAt, Error);
			}
		});
}

void UValhallaBackendSubsystem::GetAccountSettings(const FString& Token, FValhallaSettingsGetCallback OnDone)
{
	Send(TEXT("GET"), TEXT("/api/account/settings"), nullptr, Token, /*bServerAuth=*/false,
		[OnDone = MoveTemp(OnDone)](bool bSuccess, int32 Status, const TSharedPtr<FJsonObject>& Json, const FString& Error)
		{
			TSharedPtr<FJsonObject> Graphics;
			FString UpdatedAt;
			if (bSuccess && Json.IsValid())
			{
				const TSharedPtr<FJsonObject>* GraphicsObject = nullptr;
				if (Json->TryGetObjectField(TEXT("graphics"), GraphicsObject) && GraphicsObject)
				{
					Graphics = *GraphicsObject;
				}
				UpdatedAt = GetStringField(Json, TEXT("updatedAt"));
			}
			UE_LOG(LogValhallaBackend, Log, TEXT("account settings get : %d%s"), Status,
				bSuccess ? (Graphics.IsValid() ? TEXT(" ok") : TEXT(" (no graphics object)")) : *FString::Printf(TEXT(" %s"), *Error));
			if (OnDone)
			{
				OnDone(bSuccess && Graphics.IsValid(), Status, Graphics, UpdatedAt, Error);
			}
		});
}

void UValhallaBackendSubsystem::PutAccountSettings(const FString& Token, const TSharedRef<FJsonObject>& Graphics, FValhallaSettingsPutCallback OnDone)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetObjectField(TEXT("graphics"), Graphics);
	Send(TEXT("PUT"), TEXT("/api/account/settings"), Body, Token, /*bServerAuth=*/false,
		[OnDone = MoveTemp(OnDone)](bool bSuccess, int32 Status, const TSharedPtr<FJsonObject>& Json, const FString& Error)
		{
			const FString UpdatedAt = bSuccess ? GetStringField(Json, TEXT("updatedAt")) : FString();
			UE_LOG(LogValhallaBackend, Log, TEXT("account settings put : %d%s"), Status,
				bSuccess ? TEXT(" ok") : *FString::Printf(TEXT(" %s"), *Error));
			if (OnDone)
			{
				OnDone(bSuccess, Status, UpdatedAt, Error);
			}
		});
}

void UValhallaBackendSubsystem::SetPlayerSession(const FString& Token, int32 UserId, int32 CharacterId)
{
	PlayerToken = Token;
	PlayerUserId = UserId;
	PlayerCharacterId = CharacterId;
	UE_LOG(LogValhallaBackend, Log, TEXT("player session: userId %d, character %d, token %s (memory only)"),
		UserId, CharacterId, *RedactToken(Token));
}

void UValhallaBackendSubsystem::ClearPlayerSession()
{
	if (!PlayerToken.IsEmpty() || PlayerCharacterId != 0)
	{
		UE_LOG(LogValhallaBackend, Log, TEXT("player session cleared (character %d)."), PlayerCharacterId);
	}
	PlayerToken.Reset();
	PlayerUserId = 0;
	PlayerCharacterId = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Server routes
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaBackendSubsystem::Verify(const FString& Token, FValhallaVerifyCallback OnDone)
{
	const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("token"), Token);

	UE_LOG(LogValhallaBackend, Verbose, TEXT("verify token %s"), *RedactToken(Token));

	Send(TEXT("POST"), TEXT("/api/auth/verify"), Body, FString(), /*bServerAuth=*/true,
		[OnDone = MoveTemp(OnDone), Redacted = RedactToken(Token)]
		(bool bSuccess, int32 /*Status*/, const TSharedPtr<FJsonObject>& Json, const FString& Error)
		{
			FValhallaVerifiedToken Verified;
			if (bSuccess)
			{
				Verified.UserId    = GetIntField(Json, TEXT("userId"));
				Verified.Username  = GetStringField(Json, TEXT("username"));
				Verified.ExpiresAt = GetNumberField(Json, TEXT("expiresAt"));
			}

			const bool bOk = bSuccess && Verified.IsValid();
			UE_LOG(LogValhallaBackend, Log, TEXT("verify %s : %s"),
				*Redacted, bOk ? *FString::Printf(TEXT("userId %d ('%s')"), Verified.UserId, *Verified.Username) : *Error);

			if (OnDone)
			{
				OnDone(bOk, Verified, bOk ? FString() : (Error.IsEmpty() ? TEXT("Invalid or expired token.") : Error));
			}
		});
}

void UValhallaBackendSubsystem::LoadCharacter(int32 CharacterId, int32 UserId, FValhallaLoadCallback OnDone)
{
	const FString Path = FString::Printf(TEXT("/api/characters/%d/load?userId=%d"), CharacterId, UserId);

	Send(TEXT("GET"), Path, nullptr, FString(), /*bServerAuth=*/true,
		[OnDone = MoveTemp(OnDone), CharacterId](bool bSuccess, int32 /*Status*/, const TSharedPtr<FJsonObject>& Json, const FString& Error)
		{
			FValhallaLoadedCharacter Character;
			bool bParsed = false;

			if (bSuccess && Json.IsValid())
			{
				// The contract wraps the row in `{ "character": … }`; a route
				// that returned the row bare is still readable, and a load that
				// works either way is one fewer thing to coordinate across two
				// repos.
				const TSharedPtr<FJsonObject>* Obj = nullptr;
				const TSharedPtr<FJsonObject> Row = Json->TryGetObjectField(TEXT("character"), Obj) && Obj ? *Obj : Json;
				bParsed = LoadedCharacterFromJson(Row, Character);
			}

			if (bParsed)
			{
				UE_LOG(LogValhallaBackend, Log,
					TEXT("loaded char=%d '%s' [%s lv%d] xp=%d zone=%s pos=(%.0f, %.0f) inv=%d equip=%s"),
					Character.Id, *Character.Name, *Character.ClassId.ToString(), Character.Level,
					Character.Xp, *Character.ZoneId.ToString(), Character.PositionX, Character.PositionY,
					Character.Inventory.Num(), Character.HasAnyEquipment() ? TEXT("some") : TEXT("none"));
			}

			if (OnDone)
			{
				OnDone(bParsed, Character, bParsed ? FString()
					: (Error.IsEmpty() ? FString::Printf(TEXT("Character %d could not be loaded."), CharacterId) : Error));
			}
		});
}

void UValhallaBackendSubsystem::SaveCharacter(int32 CharacterId, const FValhallaSaveData& Data, FValhallaSimpleCallback OnDone)
{
	const FString Path = FString::Printf(TEXT("/api/characters/%d/save"), CharacterId);

	Send(TEXT("PUT"), Path, SaveDataToJson(Data), FString(), /*bServerAuth=*/true,
		[OnDone = MoveTemp(OnDone), CharacterId, Level = Data.Level, Xp = Data.Xp, ZoneId = Data.ZoneId]
		(bool bSuccess, int32 /*Status*/, const TSharedPtr<FJsonObject>& /*Json*/, const FString& Error)
		{
			if (bSuccess)
			{
				UE_LOG(LogValhallaBackend, Log, TEXT("saved char=%d lv%d xp=%d zone=%s"),
					CharacterId, Level, Xp, *ZoneId.ToString());
			}
			else
			{
				UE_LOG(LogValhallaBackend, Warning, TEXT("save char=%d failed: %s"), CharacterId, *Error);
			}

			if (OnDone)
			{
				OnDone(bSuccess, Error);
			}
		});
}

void UValhallaBackendSubsystem::Health(FValhallaSimpleCallback OnDone)
{
	Send(TEXT("GET"), TEXT("/api/health"), nullptr, FString(), /*bServerAuth=*/true,
		[OnDone = MoveTemp(OnDone)](bool bSuccess, int32 /*Status*/, const TSharedPtr<FJsonObject>& /*Json*/, const FString& Error)
		{
			if (OnDone)
			{
				OnDone(bSuccess, Error);
			}
		});
}

// ─────────────────────────────────────────────────────────────────────────────
//  Account admin
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	TSharedRef<FJsonObject> MakeAccountBody(int32 UserId, const FString& Username)
	{
		const TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		if (UserId > 0)
		{
			Body->SetNumberField(TEXT("userId"), UserId);
		}
		else
		{
			Body->SetStringField(TEXT("username"), Username.TrimStartAndEnd());
		}
		return Body;
	}
}

void UValhallaBackendSubsystem::BanAccount(int32 UserId, const FString& Username, double Minutes, const FString& Reason, const FString& By, FValhallaJsonCallback OnDone)
{
	const TSharedRef<FJsonObject> Body = MakeAccountBody(UserId, Username);
	if (Minutes > 0.0)
	{
		Body->SetNumberField(TEXT("minutes"), Minutes);
	}
	Body->SetStringField(TEXT("reason"), Reason);
	Body->SetStringField(TEXT("by"), By);

	Send(TEXT("POST"), TEXT("/api/accounts/ban"), Body, FString(), /*bServerAuth=*/true,
		[OnDone = MoveTemp(OnDone), UserId, Username](bool bSuccess, int32 Status, const TSharedPtr<FJsonObject>& Json, const FString& Error)
		{
			UE_LOG(LogValhallaBackend, Log, TEXT("ban account %s: %s"),
				UserId > 0 ? *FString::Printf(TEXT("#%d"), UserId) : *Username, bSuccess ? TEXT("ok") : *Error);
			if (OnDone)
			{
				OnDone(bSuccess, Status, Json, Error);
			}
		});
}

void UValhallaBackendSubsystem::UnbanAccount(int32 UserId, const FString& Username, FValhallaJsonCallback OnDone)
{
	Send(TEXT("POST"), TEXT("/api/accounts/unban"), MakeAccountBody(UserId, Username), FString(), /*bServerAuth=*/true,
		[OnDone = MoveTemp(OnDone), UserId, Username](bool bSuccess, int32 Status, const TSharedPtr<FJsonObject>& Json, const FString& Error)
		{
			UE_LOG(LogValhallaBackend, Log, TEXT("unban account %s: %s"),
				UserId > 0 ? *FString::Printf(TEXT("#%d"), UserId) : *Username, bSuccess ? TEXT("ok") : *Error);
			if (OnDone)
			{
				OnDone(bSuccess, Status, Json, Error);
			}
		});
}

void UValhallaBackendSubsystem::ListBans(FValhallaJsonCallback OnDone)
{
	Send(TEXT("GET"), TEXT("/api/accounts/bans"), nullptr, FString(), /*bServerAuth=*/true, MoveTemp(OnDone));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Game data sync (B-03)
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	FString Sha1Hex(const TArray<uint8>& Bytes)
	{
		uint8 Digest[20];
		FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num(), Digest);
		return BytesToHex(Digest, 20).ToLower();
	}

	/** What one sync is waiting for. Shared by every download's completion. */
	struct FDataSyncState
	{
		FString TargetDir;
		FValhallaDataSyncCallback OnDone;
		TMap<FString, FString> ExpectedSha1;	// name -> manifest hash, for the files being fetched
		TMap<FString, TArray<uint8>> Received;
		int32 Outstanding = 0;
		FString FirstError;
	};

	void FinishDataSync(const TSharedRef<FDataSyncState>& State)
	{
		if (!State->FirstError.IsEmpty())
		{
			UE_LOG(LogValhallaBackend, Warning, TEXT("data sync failed, nothing written: %s"), *State->FirstError);
			if (State->OnDone) { State->OnDone(false, 0, State->FirstError); }
			return;
		}

		IFileManager::Get().MakeDirectory(*State->TargetDir, /*Tree=*/true);
		for (const TPair<FString, TArray<uint8>>& File : State->Received)
		{
			const FString Final = FPaths::Combine(State->TargetDir, File.Key);
			const FString Temp = Final + TEXT(".download");
			if (!FFileHelper::SaveArrayToFile(File.Value, *Temp) || !IFileManager::Get().Move(*Final, *Temp, /*Replace=*/true))
			{
				const FString Error = FString::Printf(TEXT("could not write %s"), *Final);
				UE_LOG(LogValhallaBackend, Error, TEXT("data sync: %s"), *Error);
				if (State->OnDone) { State->OnDone(false, 0, Error); }
				return;
			}
		}

		UE_LOG(LogValhallaBackend, Log, TEXT("data sync: %d file(s) updated in %s"), State->Received.Num(), *State->TargetDir);
		if (State->OnDone) { State->OnDone(true, State->Received.Num(), FString()); }
	}
}

void UValhallaBackendSubsystem::SyncGameData(const FString& TargetDir, FValhallaDataSyncCallback OnDone)
{
	TWeakObjectPtr<UValhallaBackendSubsystem> WeakThis(this);

	Send(TEXT("GET"), TEXT("/api/data/manifest"), nullptr, FString(), /*bServerAuth=*/false,
		[WeakThis, TargetDir, OnDone = MoveTemp(OnDone)](bool bSuccess, int32 /*Status*/, const TSharedPtr<FJsonObject>& Json, const FString& Error) mutable
		{
			UValhallaBackendSubsystem* Self = WeakThis.Get();
			const TArray<TSharedPtr<FJsonValue>>* Files = nullptr;
			if (!Self || !bSuccess || !Json.IsValid() || !Json->TryGetArrayField(TEXT("files"), Files) || !Files)
			{
				const FString Why = Error.IsEmpty() ? FString(TEXT("the backend's data manifest was unreadable")) : Error;
				UE_LOG(LogValhallaBackend, Warning, TEXT("data sync: %s"), *Why);
				if (OnDone) { OnDone(false, 0, Why); }
				return;
			}

			double DataVersion = 0.0;
			Json->TryGetNumberField(TEXT("dataVersion"), DataVersion);

			const TSharedRef<FDataSyncState> State = MakeShared<FDataSyncState>();
			State->TargetDir = TargetDir;
			State->OnDone = MoveTemp(OnDone);

			for (const TSharedPtr<FJsonValue>& Value : *Files)
			{
				const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
				FString Name, Sha1;
				if (!Entry.IsValid() || !Entry->TryGetStringField(TEXT("name"), Name) || !Entry->TryGetStringField(TEXT("sha1"), Sha1) || Sha1.IsEmpty())
				{
					continue;	// a file missing on the server: keep whatever copy we have
				}
				// Only plain file names: the manifest never gets to write outside TargetDir.
				if (Name.Contains(TEXT("/")) || Name.Contains(TEXT("\\")) || Name.Contains(TEXT("..")))
				{
					continue;
				}

				TArray<uint8> Local;
				const bool bHaveLocal = FFileHelper::LoadFileToArray(Local, *FPaths::Combine(TargetDir, Name), FILEREAD_Silent);
				if (bHaveLocal && Sha1Hex(Local).Equals(Sha1, ESearchCase::IgnoreCase))
				{
					continue;
				}
				State->ExpectedSha1.Add(Name, Sha1.ToLower());
			}

			UE_LOG(LogValhallaBackend, Log, TEXT("data sync: manifest v%.0f, %d file(s) to download"), DataVersion, State->ExpectedSha1.Num());

			if (State->ExpectedSha1.Num() == 0)
			{
				FinishDataSync(State);
				return;
			}

			State->Outstanding = State->ExpectedSha1.Num();
			for (const TPair<FString, FString>& Wanted : State->ExpectedSha1)
			{
				const FString Name = Wanted.Key;
				const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
				Request->SetURL(Self->MakeUrl(TEXT("/api/data/") + Name));
				Request->SetVerb(TEXT("GET"));
				Request->SetTimeout(RequestTimeoutSeconds);
				Request->OnProcessRequestComplete().BindLambda(
					[State, Name](FHttpRequestPtr /*Req*/, FHttpResponsePtr Response, bool bConnected)
					{
						const int32 Code = Response.IsValid() ? Response->GetResponseCode() : 0;
						if (!bConnected || Code != 200)
						{
							if (State->FirstError.IsEmpty())
							{
								State->FirstError = FString::Printf(TEXT("%s: HTTP %d"), *Name, Code);
							}
						}
						else
						{
							const TArray<uint8>& Bytes = Response->GetContent();
							if (!Sha1Hex(Bytes).Equals(State->ExpectedSha1[Name], ESearchCase::IgnoreCase))
							{
								if (State->FirstError.IsEmpty())
								{
									State->FirstError = FString::Printf(TEXT("%s changed on the server during the download; try again"), *Name);
								}
							}
							else
							{
								State->Received.Add(Name, Bytes);
							}
						}

						if (--State->Outstanding == 0)
						{
							FinishDataSync(State);
						}
					});
				if (!Request->ProcessRequest())
				{
					if (State->FirstError.IsEmpty())
					{
						State->FirstError = FString::Printf(TEXT("could not start the download of %s"), *Name);
					}
					if (--State->Outstanding == 0)
					{
						FinishDataSync(State);
					}
				}
			}
		});
}

// ─────────────────────────────────────────────────────────────────────────────
//  JSON
// ─────────────────────────────────────────────────────────────────────────────

TSharedRef<FJsonObject> UValhallaBackendSubsystem::SaveDataToJson(const FValhallaSaveData& Data)
{
	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();

	// hp, mana, xp and level are **integers** on the wire. The `characters`
	// table's columns are integers and `internal.ts`'s `requireInt` rejects
	// anything else with a 400 — so a hp of 173.5, which is an entirely
	// ordinary mid-regen value, would fail every save it appeared in and the
	// only symptom would be a character that quietly stopped persisting.
	// Rounding here, at the boundary, is the whole of the fix: the pools stay
	// floats everywhere the game reasons about them.
	Json->SetNumberField(TEXT("hp"), FMath::Max(0, FMath::RoundToInt32(Data.Hp)));
	Json->SetNumberField(TEXT("mana"), FMath::Max(0, FMath::RoundToInt32(Data.Mana)));
	Json->SetNumberField(TEXT("xp"), FMath::Max(0, Data.Xp));
	Json->SetNumberField(TEXT("level"), FMath::Max(1, Data.Level));
	Json->SetNumberField(TEXT("positionX"), Data.PositionX);
	Json->SetNumberField(TEXT("positionY"), Data.PositionY);

	// The column is NOT NULL and the route rejects an empty string. A player
	// state with no zone is a bug elsewhere, and writing "grasslands" rather
	// than failing the save is the kinder of the two wrong answers.
	Json->SetStringField(TEXT("zoneId"), Data.ZoneId.IsNone() ? TEXT("grasslands") : Data.ZoneId.ToString());
	Json->SetBoolField(TEXT("alive"), Data.bAlive);

	// `slotIndex` is the array index, because the 2.0 inventory is dense and
	// its position *is* its index — see AValhallaPlayerState::Inventory.
	TArray<TSharedPtr<FJsonValue>> InventoryArray;
	for (int32 Index = 0; Index < Data.Inventory.Num() && Index < Valhalla::InventoryMaxSlots; ++Index)
	{
		const FValhallaInventorySlot& Slot = Data.Inventory[Index];
		if (Slot.IsEmpty())
		{
			continue;
		}

		const TSharedRef<FJsonObject> SlotJson = MakeShared<FJsonObject>();
		SlotJson->SetNumberField(TEXT("slotIndex"), Index);
		SlotJson->SetStringField(TEXT("itemId"), Slot.ItemId.ToString());
		SlotJson->SetNumberField(TEXT("quantity"), Slot.Quantity);
		InventoryArray.Add(MakeShared<FJsonValueObject>(SlotJson));
	}
	Json->SetArrayField(TEXT("inventory"), InventoryArray);

	// `[{slotType, itemId}]`, which is the shape `saveCharacter` writes into
	// `character_equipment` and `loadCharacter` reads back. An empty slot is an
	// absent row, not a row with an empty string.
	TArray<TSharedPtr<FJsonValue>> EquipmentArray;
	for (int32 Index = 0; Index < ValhallaEquipSlotCount; ++Index)
	{
		const FName ItemId = Data.Equipment.IsValidIndex(Index) ? Data.Equipment[Index] : NAME_None;
		if (ItemId.IsNone())
		{
			continue;
		}

		const EValhallaEquipSlot Slot = ValhallaEquipSlotFromIndex(Index);
		const TSharedRef<FJsonObject> EquipJson = MakeShared<FJsonObject>();
		EquipJson->SetStringField(TEXT("slotType"), UValhallaInventoryLibrary::EquipSlotToName(Slot).ToString());
		EquipJson->SetStringField(TEXT("itemId"), ItemId.ToString());
		EquipmentArray.Add(MakeShared<FJsonValueObject>(EquipJson));
	}
	Json->SetArrayField(TEXT("equipment"), EquipmentArray);

	// Eight slots, no more: `internal.ts` refuses a longer bar, and the bar 2.0
	// carries is whatever the load handed it — so a backend that ever grew a
	// ninth slot must not be able to make 2.0 the thing that rejects it.
	constexpr int32 ActionBarSlots = 8;
	TArray<TSharedPtr<FJsonValue>> ActionBarArray;
	for (int32 Index = 0; Index < Data.ActionBar.Num() && Index < ActionBarSlots; ++Index)
	{
		ActionBarArray.Add(MakeShared<FJsonValueString>(Data.ActionBar[Index]));
	}
	Json->SetArrayField(TEXT("actionBar"), ActionBarArray);

	return Json;
}

namespace
{
	/**
	 * `inventory` in either spelling. Missing indices are filled forward, so an
	 * array with no `slotIndex` at all still comes back in its own order.
	 */
	void ParseInventory(const TSharedPtr<FJsonObject>& Json, TArray<FValhallaInventorySlot>& Out)
	{
		Out.Reset();

		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (!Json.IsValid() || !Json->TryGetArrayField(TEXT("inventory"), Array) || !Array)
		{
			return;
		}

		// Index -> slot, so a sparse `slotIndex` set survives and is compacted
		// at the end. The 1.0 table permits holes; the 2.0 array does not.
		TMap<int32, FValhallaInventorySlot> ByIndex;
		int32 NextFreeIndex = 0;

		for (const TSharedPtr<FJsonValue>& Entry : *Array)
		{
			const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
			if (!Entry.IsValid() || !Entry->TryGetObject(ObjPtr) || !ObjPtr)
			{
				continue;
			}

			const TSharedPtr<FJsonObject>& Obj = *ObjPtr;

			const FString ItemId = GetStringField(Obj, TEXT("itemId"));
			if (ItemId.IsEmpty())
			{
				continue;
			}

			double SlotNumber = 0.0;
			int32 SlotIndex = NextFreeIndex;
			if (Obj->TryGetNumberField(TEXT("slotIndex"), SlotNumber) || Obj->TryGetNumberField(TEXT("slot"), SlotNumber))
			{
				SlotIndex = static_cast<int32>(SlotNumber);
			}

			const int32 Quantity = FMath::Max(1, GetIntField(Obj, TEXT("quantity"), 1));
			ByIndex.Add(SlotIndex, FValhallaInventorySlot(FName(*ItemId), Quantity));
			NextFreeIndex = FMath::Max(NextFreeIndex, SlotIndex + 1);
		}

		ByIndex.KeySort([](int32 A, int32 B) { return A < B; });
		for (const TPair<int32, FValhallaInventorySlot>& Pair : ByIndex)
		{
			if (Out.Num() >= Valhalla::InventoryMaxSlots)
			{
				UE_LOG(LogValhallaBackend, Warning,
					TEXT("load: more than %d inventory rows; the rest were dropped."), Valhalla::InventoryMaxSlots);
				break;
			}
			Out.Add(Pair.Value);
		}
	}

	/** `equipment` as either an array of rows or an object map. */
	void ParseEquipment(const TSharedPtr<FJsonObject>& Json, TArray<FName>& Out)
	{
		Out.Reset();
		Out.SetNum(ValhallaEquipSlotCount);

		if (!Json.IsValid())
		{
			return;
		}

		auto Assign = [&Out](const FString& SlotName, const FString& ItemId)
		{
			if (ItemId.IsEmpty())
			{
				return;
			}

			const EValhallaEquipSlot Slot = UValhallaInventoryLibrary::ParseEquipSlotName(FName(*SlotName));
			const int32 Index = ValhallaEquipSlotToIndex(Slot);
			if (Index == INDEX_NONE)
			{
				UE_LOG(LogValhallaBackend, Warning, TEXT("load: unknown equip slot '%s'; skipped."), *SlotName);
				return;
			}

			Out[Index] = FName(*ItemId);
		};

		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (Json->TryGetArrayField(TEXT("equipment"), Array) && Array)
		{
			for (const TSharedPtr<FJsonValue>& Entry : *Array)
			{
				const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
				if (!Entry.IsValid() || !Entry->TryGetObject(ObjPtr) || !ObjPtr)
				{
					continue;
				}

				Assign(GetStringField(*ObjPtr, TEXT("slotType")), GetStringField(*ObjPtr, TEXT("itemId")));
			}
			return;
		}

		const TSharedPtr<FJsonObject>* MapPtr = nullptr;
		if (Json->TryGetObjectField(TEXT("equipment"), MapPtr) && MapPtr)
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*MapPtr)->Values)
			{
				FString ItemId;
				if (Pair.Value.IsValid() && Pair.Value->TryGetString(ItemId))
				{
					Assign(Pair.Key, ItemId);
				}
			}
		}
	}
}

bool UValhallaBackendSubsystem::SaveDataFromJson(const TSharedPtr<FJsonObject>& Json, FValhallaSaveData& Out)
{
	if (!Json.IsValid())
	{
		return false;
	}

	Out = FValhallaSaveData();

	Out.Hp        = static_cast<float>(GetNumberField(Json, TEXT("hp")));
	Out.Mana      = static_cast<float>(GetNumberField(Json, TEXT("mana")));
	Out.Xp        = GetIntField(Json, TEXT("xp"));
	Out.Level     = FMath::Max(1, GetIntField(Json, TEXT("level"), 1));
	Out.PositionX = GetNumberField(Json, TEXT("positionX"));
	Out.PositionY = GetNumberField(Json, TEXT("positionY"));
	Out.ZoneId    = FName(*GetStringField(Json, TEXT("zoneId")));

	bool bAlive = true;
	Json->TryGetBoolField(TEXT("alive"), bAlive);
	Out.bAlive = bAlive;

	ParseInventory(Json, Out.Inventory);
	ParseEquipment(Json, Out.Equipment);

	Out.ActionBar.Reset();
	const TArray<TSharedPtr<FJsonValue>>* BarArray = nullptr;
	if (Json->TryGetArrayField(TEXT("actionBar"), BarArray) && BarArray)
	{
		for (const TSharedPtr<FJsonValue>& Entry : *BarArray)
		{
			FString SkillId;
			if (Entry.IsValid() && Entry->TryGetString(SkillId))
			{
				Out.ActionBar.Add(SkillId);
			}
		}
	}

	return true;
}

bool UValhallaBackendSubsystem::LoadedCharacterFromJson(const TSharedPtr<FJsonObject>& Json, FValhallaLoadedCharacter& Out)
{
	if (!Json.IsValid())
	{
		return false;
	}

	Out = FValhallaLoadedCharacter();

	Out.Id     = GetIntField(Json, TEXT("id"));
	Out.UserId = GetIntField(Json, TEXT("userId"));
	Out.Name   = GetStringField(Json, TEXT("name"));

	const FString ClassIdString = GetStringField(Json, TEXT("classId"));
	Out.ClassId = FName(*ClassIdString);

	Out.Level = FMath::Max(1, GetIntField(Json, TEXT("level"), 1));
	Out.Xp    = GetIntField(Json, TEXT("xp"));
	Out.Hp    = static_cast<float>(GetNumberField(Json, TEXT("hp")));
	Out.Mana  = static_cast<float>(GetNumberField(Json, TEXT("mana")));

	// `x`/`y` are what the contract calls them; `positionX`/`positionY` are
	// what the TypeScript interface calls them. Both are the same two columns.
	Out.PositionX = Json->HasField(TEXT("positionX"))
		? GetNumberField(Json, TEXT("positionX")) : GetNumberField(Json, TEXT("x"));
	Out.PositionY = Json->HasField(TEXT("positionY"))
		? GetNumberField(Json, TEXT("positionY")) : GetNumberField(Json, TEXT("y"));

	Out.ZoneId = FName(*GetStringField(Json, TEXT("zoneId")));

	bool bAlive = true;
	Json->TryGetBoolField(TEXT("alive"), bAlive);
	Out.bAlive = bAlive;

	ParseInventory(Json, Out.Inventory);
	ParseEquipment(Json, Out.Equipment);

	Out.ActionBar.Reset();
	const TArray<TSharedPtr<FJsonValue>>* BarArray = nullptr;
	if (Json->TryGetArrayField(TEXT("actionBar"), BarArray) && BarArray)
	{
		for (const TSharedPtr<FJsonValue>& Entry : *BarArray)
		{
			FString SkillId;
			if (Entry.IsValid() && Entry->TryGetString(SkillId))
			{
				Out.ActionBar.Add(SkillId);
			}
		}
	}

	// A row with no id is not a row. Everything else may be defaulted.
	return Out.Id > 0 && !ClassIdString.IsEmpty();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Apply & save shapes
// ─────────────────────────────────────────────────────────────────────────────

void UValhallaBackendSubsystem::ResolveLoadedCharacter(
	const FValhallaLoadedCharacter& Loaded,
	const FValhallaClassTemplate& ClassTemplate,
	FValhallaItemLookup FindItem,
	FValhallaAppliedCharacter& Out)
{
	Out = FValhallaAppliedCharacter();

	Out.CharacterName = Loaded.Name;
	Out.ClassId       = ClassTemplate.Id;
	Out.Level         = FMath::Clamp(Loaded.Level, 1, Valhalla::MaxLevel);
	Out.Xp            = FMath::Max(0, Loaded.Xp);
	Out.ZoneId        = Loaded.ZoneId;
	Out.bAlive        = Loaded.bAlive;

	Out.Inventory = Loaded.Inventory;
	Out.Equipment = Loaded.Equipment;
	Out.Equipment.SetNum(ValhallaEquipSlotCount);

	// CharacterService.ts:167 already grants the kit at creation, so this is
	// true only for a row that predates that path. See the declaration.
	Out.bNeedsStartingItems =
		Out.Inventory.Num() == 0 && !Loaded.HasAnyEquipment() && Out.Level <= 1;

	// The same summation AValhallaPlayerState::RecomputeStats does, and it has
	// to be *this* one: resolving the class alone and then equipping would put
	// the pools a frame out of step with the gear.
	Out.Stats = UValhallaInventoryLibrary::ComputeStatsWithEquipment(ClassTemplate, Out.Level, Out.Equipment, FindItem);

	Out.MaxHp       = Out.Stats.MaxHp;
	Out.MaxMana     = Out.Stats.MaxMana;
	Out.MaxEnergy   = Out.Stats.MaxEnergy;
	Out.VisionRange = ClassTemplate.VisionRange;

	// The clamp the declaration promises. A saved hp of 0 on a live character
	// is 1.0's "dead but alive flag not written yet"; the pool is clamped, not
	// revived, and Phase 2b's respawn is what stands them up.
	Out.Hp   = FMath::Clamp(Loaded.Hp, 0.f, Out.MaxHp);
	Out.Mana = FMath::Clamp(Loaded.Mana, 0.f, Out.MaxMana);

	// Energy is not a saved column in 1.0 — `characters` has hp and mana and
	// nothing else — so an energy class comes back with a full bar. That is
	// what 1.0 did (`GameRoom.onJoin` filled it) and it is the generous
	// reading, which is the right one for a resource that regenerates in
	// seconds.
	Out.Energy = Out.MaxEnergy;
}

FValhallaSaveData UValhallaBackendSubsystem::BuildSaveData(
	const AValhallaPlayerState& PlayerState,
	const FVector2D& ZoneLocalCm,
	const TArray<FString>& ActionBar)
{
	FValhallaSaveData Data;

	Data.Hp        = PlayerState.Hp;
	Data.Mana      = PlayerState.Mana;
	Data.Xp        = PlayerState.Xp;
	Data.Level     = PlayerState.Level;
	Data.PositionX = ZoneLocalCm.X;
	Data.PositionY = ZoneLocalCm.Y;
	Data.ZoneId    = PlayerState.ZoneId;
	Data.bAlive    = PlayerState.IsAlive();
	Data.Inventory = PlayerState.Inventory;
	Data.Equipment = PlayerState.GatherEquipment();
	Data.ActionBar = ActionBar;

	return Data;
}
