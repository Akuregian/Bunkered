// BunkeredPlayerController.cpp
#include "BunkeredPlayerController.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Characters/BunkeredCharacter.h"
#include "Components/BunkerAdvisorComponent.h"
#include "Components/PeekComponent.h"
#include "Interface/BunkerCoverInterface.h"
#include "Types/CoverTypes.h"
#include "Utility/LoggingMacros.h"

ABunkeredPlayerController::ABunkeredPlayerController()
{
    bShowMouseCursor = false;
}

void ABunkeredPlayerController::BeginPlay()
{
    Super::BeginPlay();
}

void ABunkeredPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();

    if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
        ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
    {
        for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
        {
            Subsystem->AddMappingContext(CurrentContext, 0);
        }
    }

    if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
    {
        if (MoveAction)  EIC->BindAction(MoveAction,  ETriggerEvent::Triggered, this, &ABunkeredPlayerController::OnMove);
        if (LookAction)  EIC->BindAction(LookAction,  ETriggerEvent::Triggered, this, &ABunkeredPlayerController::OnLook);

        if (EnterSplineOnBunkerAction)
            EIC->BindAction(EnterSplineOnBunkerAction, ETriggerEvent::Started, this, &ABunkeredPlayerController::OnEnterCoverAction);

        if (ChangeStanceAction)
            EIC->BindAction(ChangeStanceAction, ETriggerEvent::Started, this, &ABunkeredPlayerController::OnStanceChange);

        // Peeking
        if (PeekLeftAction)  { EIC->BindAction(PeekLeftAction,  ETriggerEvent::Started,   this, &ABunkeredPlayerController::OnPeekLeftStart);
                               EIC->BindAction(PeekLeftAction,  ETriggerEvent::Completed, this, &ABunkeredPlayerController::OnPeekLeftStop); }
        if (PeekRightAction) { EIC->BindAction(PeekRightAction, ETriggerEvent::Started,   this, &ABunkeredPlayerController::OnPeekRightStart);
                               EIC->BindAction(PeekRightAction, ETriggerEvent::Completed, this, &ABunkeredPlayerController::OnPeekRightStop); }
    }
}

// ===== Helpers =====
void ABunkeredPlayerController::OnMove(const FInputActionValue& Value)
{
    if (UObject* P = GetPawnObject())
    {
        const FVector2D V = Value.Get<FVector2D>();
        IBunkerCoverInterface::Execute_Pawn_Movement(P, V);
        IBunkerCoverInterface::Execute_Cover_Slide(P, V.X); // Test: If works keep this
    }
}

void ABunkeredPlayerController::OnLook(const FInputActionValue& Value)
{
    if (UObject* P = GetPawnObject())
    {
        const FVector2D V = Value.Get<FVector2D>();
        IBunkerCoverInterface::Execute_Pawn_MouseLook(P, V);
    }
}

void ABunkeredPlayerController::OnEnterCoverAction()
{
    if (AActor* PawnActor = Cast<AActor>(GetPawnObject()))
    {
        if (UBunkerAdvisorComponent* Adv = PawnActor->FindComponentByClass<UBunkerAdvisorComponent>())
        {
            Adv->UpdateSuggestion();
            if (Adv->AcceptSuggestion())
            {
                DEBUG(3.0f, FColor::Cyan, TEXT("Moving to suggested bunker (spline)."));
                return;
            }
        }

        // Fallback: pawn computes nearest bunker+alpha
        IBunkerCoverInterface::Execute_Cover_EnterNearest(PawnActor);
    }
}

void ABunkeredPlayerController::OnStanceChange()
{
    if (UObject* P = GetPawnObject())
    {
        IBunkerCoverInterface::Execute_Pawn_ChangeBunkerStance(P, true);
    }
}

void ABunkeredPlayerController::OnPeekLeftStart()
{
    if (UObject* P = GetPawnObject())
        IBunkerCoverInterface::Execute_Cover_Peek(P, EPeekDirection::Left, true);
}

void ABunkeredPlayerController::OnPeekLeftStop()
{
    if (UObject* P = GetPawnObject())
        IBunkerCoverInterface::Execute_Cover_Peek(P, EPeekDirection::Left, false);
}

void ABunkeredPlayerController::OnPeekRightStart()
{
    if (UObject* P = GetPawnObject())
        IBunkerCoverInterface::Execute_Cover_Peek(P, EPeekDirection::Right, true);
}

void ABunkeredPlayerController::OnPeekRightStop()
{
    if (UObject* P = GetPawnObject())
        IBunkerCoverInterface::Execute_Cover_Peek(P, EPeekDirection::Right, false);
}
