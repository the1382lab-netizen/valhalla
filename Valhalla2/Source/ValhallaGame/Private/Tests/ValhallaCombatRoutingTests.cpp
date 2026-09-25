// Copyright Valhalla 2.0. All Rights Reserved.
//
// B-27 Phase 3, who is sent a combat event:
//
//   Valhalla.Game.CombatPerf.Routing — AValhallaGameState::RouteCombatEvent:
//     the player's own events and a party member's in the same zone are
//     guaranteed; a party member's in another zone are not sent; someone else's
//     fight is sent (unreliably) only when one of its actors is on the player's
//     client; a player on a B-24 loading screen gets only the guaranteed ones.
//
// The per-connection half (an actor channel being open) is checked in the
// combat benchmark (Tools/perf): the client's Valhalla/CombatEventsUnresolved
// CSV stat, the events that arrived naming no actor it has, should be 0.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "GameFramework/Character.h"
#include "GameFramework/PlayerState.h"
#include "Misc/AutomationTest.h"
#include "ValhallaGameState.h"
#include "ValhallaNPC.h"
#include "ValhallaPlayerState.h"

#ifndef VALHALLA_GAME_TEST_FLAGS
#define VALHALLA_GAME_TEST_FLAGS ( \
	EAutomationTestFlags::EditorContext \
	| EAutomationTestFlags::ClientContext \
	| EAutomationTestFlags::ServerContext \
	| EAutomationTestFlags::CommandletContext \
	| EAutomationTestFlags::ProductFilter)
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FValhallaCombatRoutingTest, "Valhalla.Game.CombatPerf.Routing", VALHALLA_GAME_TEST_FLAGS)

bool FValhallaCombatRoutingTest::RunTest(const FString& /*Parameters*/)
{
	// The rule only compares pointers, so any distinct objects stand in for
	// the actors and player states.
	const AActor* MyPawn = GetDefault<ACharacter>();
	const AActor* PartyPawn = GetDefault<APawn>();
	const AActor* StrangerPawn = GetDefault<AActor>();
	const AActor* Npc = GetDefault<AValhallaNPC>();
	const APlayerState* MyState = GetDefault<AValhallaPlayerState>();
	const APlayerState* PartyState = GetDefault<APlayerState>();
	// A stranger's player state only matters for "is it mine", which it never is.
	const APlayerState* StrangerState = nullptr;
	const FName Grasslands(TEXT("grasslands"));
	const FName Eldmoor(TEXT("eldmoor"));

	FValhallaCombatEventViewer Me;
	Me.Pawn = MyPawn;
	Me.PlayerState = MyState;
	Me.ZoneId = Eldmoor;
	Me.PartyId = 7;

	auto Player = [](const AActor* Pawn, const APlayerState* State, FName Zone, int32 Party, bool bOnClient)
	{
		FValhallaCombatEventActor Out;
		Out.Actor = Pawn;
		Out.PlayerState = State;
		Out.ZoneId = Zone;
		Out.PartyId = Party;
		Out.bOnClient = bOnClient;
		return Out;
	};
	auto Monster = [Npc](bool bOnClient)
	{
		FValhallaCombatEventActor Out;
		Out.Actor = Npc;
		Out.bOnClient = bOnClient;
		return Out;
	};
	const FValhallaCombatEventActor None;

	auto Check = [this](const TCHAR* What, EValhallaCombatEventRoute Got, EValhallaCombatEventRoute Want)
	{
		TestEqual(What, static_cast<int32>(Got), static_cast<int32>(Want));
	};
	using ERoute = EValhallaCombatEventRoute;

	// Self.
	Check(TEXT("I hit a goblin"), AValhallaGameState::RouteCombatEvent(Me, Monster(true), Player(MyPawn, MyState, Eldmoor, 7, true), NAME_None), ERoute::Guaranteed);
	Check(TEXT("a goblin hits me"), AValhallaGameState::RouteCombatEvent(Me, Player(MyPawn, MyState, Eldmoor, 7, true), Monster(true), NAME_None), ERoute::Guaranteed);
	Check(TEXT("I hit a goblin the client lost"), AValhallaGameState::RouteCombatEvent(Me, Monster(false), Player(MyPawn, MyState, Eldmoor, 7, true), NAME_None), ERoute::Guaranteed);
	Check(TEXT("my old body (respawned): matched by player state"), AValhallaGameState::RouteCombatEvent(Me, Player(StrangerPawn, MyState, Eldmoor, 7, false), None, NAME_None), ERoute::Guaranteed);

	// Party.
	Check(TEXT("party member in my zone, out of sight"), AValhallaGameState::RouteCombatEvent(Me, Monster(false), Player(PartyPawn, PartyState, Eldmoor, 7, true), NAME_None), ERoute::Guaranteed);
	Check(TEXT("party member in another zone"), AValhallaGameState::RouteCombatEvent(Me, Monster(false), Player(PartyPawn, PartyState, Grasslands, 7, true), NAME_None), ERoute::Skip);
	Check(TEXT("party member in another zone, next to a goblin I can see"), AValhallaGameState::RouteCombatEvent(Me, Monster(true), Player(PartyPawn, PartyState, Grasslands, 7, true), NAME_None), ERoute::Seen);

	// Someone else's fight.
	Check(TEXT("stranger's fight in view"), AValhallaGameState::RouteCombatEvent(Me, Monster(true), Player(StrangerPawn, StrangerState, Eldmoor, 3, true), NAME_None), ERoute::Seen);
	Check(TEXT("stranger in view, goblin not"), AValhallaGameState::RouteCombatEvent(Me, Monster(false), Player(StrangerPawn, StrangerState, Eldmoor, 3, true), NAME_None), ERoute::Seen);
	Check(TEXT("stranger's fight out of sight"), AValhallaGameState::RouteCombatEvent(Me, Monster(false), Player(StrangerPawn, StrangerState, Eldmoor, 3, false), NAME_None), ERoute::Skip);
	Check(TEXT("stranger across the zone border, in view"), AValhallaGameState::RouteCombatEvent(Me, Monster(true), Player(StrangerPawn, StrangerState, Grasslands, 0, true), NAME_None), ERoute::Seen);
	Check(TEXT("no party is not the same party"), AValhallaGameState::RouteCombatEvent(FValhallaCombatEventViewer{ MyPawn, MyState, Eldmoor, 0, false }, Monster(false), Player(StrangerPawn, StrangerState, Eldmoor, 0, false), NAME_None), ERoute::Skip);

	// An event with no actor: by the zone at its location.
	Check(TEXT("no actor, my zone"), AValhallaGameState::RouteCombatEvent(Me, None, None, Eldmoor), ERoute::Seen);
	Check(TEXT("no actor, another zone"), AValhallaGameState::RouteCombatEvent(Me, None, None, Grasslands), ERoute::Skip);
	Check(TEXT("no actor, no zone"), AValhallaGameState::RouteCombatEvent(Me, None, None, NAME_None), ERoute::Skip);

	// B-24: on a loading screen.
	FValhallaCombatEventViewer Loading = Me;
	Loading.bInTransit = true;
	Check(TEXT("in transit: my own"), AValhallaGameState::RouteCombatEvent(Loading, Monster(false), Player(MyPawn, MyState, Eldmoor, 7, true), NAME_None), ERoute::Guaranteed);
	Check(TEXT("in transit: party in my zone"), AValhallaGameState::RouteCombatEvent(Loading, Monster(false), Player(PartyPawn, PartyState, Eldmoor, 7, true), NAME_None), ERoute::Guaranteed);
	Check(TEXT("in transit: a fight in view"), AValhallaGameState::RouteCombatEvent(Loading, Monster(true), Player(StrangerPawn, StrangerState, Eldmoor, 3, true), NAME_None), ERoute::Skip);
	Check(TEXT("in transit: no actor"), AValhallaGameState::RouteCombatEvent(Loading, None, None, Eldmoor), ERoute::Skip);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
