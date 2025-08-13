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

        if (ToggleCoverAction)    EIC->BindAction(ToggleCoverAction,    ETriggerEvent::Started, this, &ABunkeredPlayerController::OnEnterSlotOnBunker);
        if (MoveSlotLeftAction)   EIC->BindAction(MoveSlotLeftAction,   ETriggerEvent::Started, this, &ABunkeredPlayerController::OnMoveSlotLeft);
        if (MoveSlotRightAction)  EIC->BindAction(MoveSlotRightAction,  ETriggerEvent::Started, this, &ABunkeredPlayerController::OnMoveSlotRight);

        if (PeekLeftAction)
        {
            EIC->BindAction(PeekLeftAction,  ETriggerEvent::Started,  this, &ABunkeredPlayerController::OnPeekLeft_Pressed);
            EIC->BindAction(PeekLeftAction,  ETriggerEvent::Completed,this, &ABunkeredPlayerController::OnPeekLeft_Released);
        }
        if (PeekRightAction)
        {
            EIC->BindAction(PeekRightAction, ETriggerEvent::Started,  this, &ABunkeredPlayerController::OnPeekRight_Pressed);
            EIC->BindAction(PeekRightAction, ETriggerEvent::Completed,this, &ABunkeredPlayerController::OnPeekRight_Released);
        }
        if (PeekOverAction)
        {
            EIC->BindAction(PeekOverAction,  ETriggerEvent::Started,  this, &ABunkeredPlayerController::OnPeekOver_Pressed);
            EIC->BindAction(PeekOverAction,  ETriggerEvent::Completed,this, &ABunkeredPlayerController::OnPeekOver_Released);
        }

        if (StandAction)  EIC->BindAction(StandAction,  ETriggerEvent::Started, this, &ABunkeredPlayerController::OnStand);
        if (CrouchAction) EIC->BindAction(CrouchAction, ETriggerEvent::Started, this, &ABunkeredPlayerController::OnCrouch);
        if (ProneAction)  EIC->BindAction(ProneAction,  ETriggerEvent::Started, this, &ABunkeredPlayerController::OnProne);
        if (SuggestAction)          EIC->BindAction(SuggestAction,          ETriggerEvent::Started, this, &ABunkeredPlayerController::OnSuggest);
        if (AcceptSuggestionAction) EIC->BindAction(AcceptSuggestionAction, ETriggerEvent::Started, this, &ABunkeredPlayerController::OnAccept);

    }
}

void ABunkeredPlayerController::OnSuggest()
{
    if (AActor* A = Cast<AActor>(GetPawnObject()))
    {
        if (UActorComponent* C = A->GetComponentByClass(UBunkerAdvisorComponent::StaticClass()))
        {
            if (auto* Adv = Cast<UBunkerAdvisorComponent>(C))
            {
                Adv->UpdateSuggestion();
            }
        }
    }
}

void ABunkeredPlayerController::OnAccept()
{
    if (AActor* A = Cast<AActor>(GetPawnObject()))
    {
        if (UActorComponent* C = A->GetComponentByClass(UBunkerAdvisorComponent::StaticClass()))
        {
            if (auto* Adv = Cast<UBunkerAdvisorComponent>(C))
            {
                // Ensure we have a suggestion, then accept it
                if (!Adv->UpdateSuggestion() || !Adv->AcceptSuggestion())
                {
                    // (Optional) fall back to your old nearest-enter flow via interface if you keep it
                    DEBUG(3.f, FColor::Yellow, TEXT("No next bunker available."));
                }
            }
        }
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
    if (UObject* P = GetPawnObject())
    {
        // 1) Try advisor: suggest then accept
        AActor* ActorPawn = Cast<AActor>(P);
        if (ActorPawn)
        {
            if (UActorComponent* C = ActorPawn->GetComponentByClass(UBunkerAdvisorComponent::StaticClass()))
            {
                if (auto* Nav = Cast<UBunkerAdvisorComponent>(C))
                {
                    // If we don't already have a suggestion, compute one:
                    Nav->UpdateSuggestion();
                    if (Nav->AcceptSuggestion())
                    {
                        DEBUG(3.0f, FColor::Cyan, TEXT("Accepting bunker suggestion"));
                        return;
                    }
                }
            }
        }
        
        // 2) Fallback: old "enter nearest" behavior
        IBunkerCoverInterface::Execute_EnterSlotOnBunker(P);
    }
}
void ABunkeredPlayerController::OnMoveSlotLeft()
{
    if (UObject* P = GetPawnObject())
    {
        IBunkerCoverInterface::Execute_SlotTransition(P, -1);
    }
}
void ABunkeredPlayerController::OnMoveSlotRight()
{
    if (UObject* P = GetPawnObject())
    {
        IBunkerCoverInterface::Execute_SlotTransition(P, +1);
    }
}

void ABunkeredPlayerController::OnPeekLeft_Pressed()
{
    if (UObject* P = GetPawnObject())
    {
        IBunkerCoverInterface::Execute_SlotPeek(P, EPeekDirection::Left, true);
    }
}
void ABunkeredPlayerController::OnPeekLeft_Released()
{
    if (UObject* P = GetPawnObject())
    {
        IBunkerCoverInterface::Execute_SlotPeek(P, EPeekDirection::Left, false);
    }
}
void ABunkeredPlayerController::OnPeekRight_Pressed()
{
    if (UObject* P = GetPawnObject())
    {
        IBunkerCoverInterface::Execute_SlotPeek(P, EPeekDirection::Right, true);
    }
}
void ABunkeredPlayerController::OnPeekRight_Released()
{
    if (UObject* P = GetPawnObject())
    {
        IBunkerCoverInterface::Execute_SlotPeek(P, EPeekDirection::Right, false);
    }
}
void ABunkeredPlayerController::OnPeekOver_Pressed()
{
    if (UObject* P = GetPawnObject())
    {
        IBunkerCoverInterface::Execute_SlotPeek(P, EPeekDirection::Over, true);
    }
}
void ABunkeredPlayerController::OnPeekOver_Released()
{
    if (UObject* P = GetPawnObject())
    {
        IBunkerCoverInterface::Execute_SlotPeek(P, EPeekDirection::Over, false);
    }
}

void ABunkeredPlayerController::OnStand()
{
    if (UObject* P = GetPawnObject())
    {
        IBunkerCoverInterface::Execute_SetSlotStance(P, ECoverStance::Stand);
    }
}
void ABunkeredPlayerController::OnCrouch()
{
    if (UObject* P = GetPawnObject())
    {
        DEBUG(5.0f, FColor::Green, TEXT("Crouching"));
        IBunkerCoverInterface::Execute_SetSlotStance(P, ECoverStance::Crouch);
    }
}
void ABunkeredPlayerController::OnProne()
{
    if (UObject* P = GetPawnObject())
    {
        IBunkerCoverInterface::Execute_SetSlotStance(P, ECoverStance::Prone);
    }
}
