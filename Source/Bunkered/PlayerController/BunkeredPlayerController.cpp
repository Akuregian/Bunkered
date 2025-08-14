// BunkeredPlayerController.cpp
#include "BunkeredPlayerController.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Components/BunkerAdvisorComponent.h"
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

    // Add IMCs
    if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
        ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
    {
        for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
        {
            if (CurrentContext) Subsystem->AddMappingContext(CurrentContext, 0);
        }
    }

    if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
    {
        if (MoveAction) EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ABunkeredPlayerController::OnMove);
        if (LookAction) EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &ABunkeredPlayerController::OnLook);
        if (EnterSlotOnBunkerAction)    EIC->BindAction(EnterSlotOnBunkerAction,    ETriggerEvent::Started, this, &ABunkeredPlayerController::OnEnterSlotOnBunker);
        if (CrouchAction) EIC->BindAction(CrouchAction, ETriggerEvent::Started, this, &ABunkeredPlayerController::OnCrouch);
    }
}

// ===== Helpers =====
void ABunkeredPlayerController::OnMove(const FInputActionValue& Value)
{
    if (UObject* P = GetPawnObject())
    {
        const FVector2D V = Value.Get<FVector2D>();
        IBunkerCoverInterface::Execute_Pawn_Movement(P, V);
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

void ABunkeredPlayerController::OnEnterSlotOnBunker()
{
    if (AActor* PawnActor = Cast<AActor>(GetPawnObject()))
    {
        if (UBunkerAdvisorComponent* Adv = PawnActor->FindComponentByClass<UBunkerAdvisorComponent>())
        {
            // If no suggestion or player wants to override, update first
            Adv->UpdateSuggestion();
            if (Adv->AcceptSuggestion())
            {
                DEBUG(3.0f, FColor::Cyan, TEXT("Moving to suggested bunker slot"));
                return;
            }
        }

        // Fallback: enter nearest cover
        IBunkerCoverInterface::Execute_EnterSlotOnBunker(PawnActor);
    }
}
void ABunkeredPlayerController::OnMoveSlotLeft()
{
    if (UObject* P = GetPawnObject())
    {
        DEBUG(3.0f, FColor::Orange, TEXT("Left Slot Transition <----"));
        IBunkerCoverInterface::Execute_SlotTransition(P, -1);
    }
}
void ABunkeredPlayerController::OnMoveSlotRight()
{
    if (UObject* P = GetPawnObject())
    {
        DEBUG(3.0f, FColor::Emerald, TEXT("Right Slot Transition ---->"));
        IBunkerCoverInterface::Execute_SlotTransition(P, +1);
    }
}

// TODO: Add Slot Peeking using IBunkerCoverInterface::Execute_SlotPeek(P, EPeekDirection::Left, true);
//void ABunkeredPlayerController::OnPeekLeft_Pressed()
//{
//    if (UObject* P = GetPawnObject())
//    {
//        IBunkerCoverInterface::Execute_SlotPeek(P, EPeekDirection::Left, true);
//    }
//}

void ABunkeredPlayerController::OnStand()
{
    if (UObject* P = GetPawnObject())
    {
        DEBUG(5.0f, FColor::Green, TEXT("Setting Slot Stance to: [Stand]"));
        IBunkerCoverInterface::Execute_SetSlotStance(P, ECoverStance::Stand);
    }
}
void ABunkeredPlayerController::OnCrouch()
{
    if (UObject* P = GetPawnObject())
    {
        DEBUG(5.0f, FColor::Green, TEXT("Setting Slot Stance to: [Crouch]"));
        IBunkerCoverInterface::Execute_SetSlotStance(P, ECoverStance::Crouch);
    }
}
void ABunkeredPlayerController::OnProne()
{
    if (UObject* P = GetPawnObject())
    {
        DEBUG(5.0f, FColor::Green, TEXT("Setting Slot Stance to: [Prone]"));
        IBunkerCoverInterface::Execute_SetSlotStance(P, ECoverStance::Prone);
    }
}
