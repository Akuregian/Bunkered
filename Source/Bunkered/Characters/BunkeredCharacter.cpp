// BunkeredCharacter.cpp
#include "BunkeredCharacter.h"

#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/BunkerAdvisorComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "Components/BunkerCoverComponent.h"
#include "Bunkers/BunkerBase.h"
#include "Kismet/GameplayStatics.h"
#include "Utility/LoggingMacros.h"

DEFINE_LOG_CATEGORY(LogTemplateCharacter);

ABunkeredCharacter::ABunkeredCharacter()
{
    // Collision capsule
    GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);

    // Pawn rotation
    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw   = false;
    bUseControllerRotationRoll  = false;

    // Movement config
    auto* Move = GetCharacterMovement();
    Move->NavAgentProps.bCanCrouch = true;
    Move->bOrientRotationToMovement = true;
    Move->RotationRate = FRotator(0.0f, 500.0f, 0.0f);
    Move->SetCrouchedHalfHeight(44.f);
    Move->JumpZVelocity = 500.f;
    Move->AirControl = 0.35f;
    Move->MaxWalkSpeed = 500.f;
    Move->MinAnalogWalkSpeed = 20.f;
    Move->BrakingDecelerationWalking = 2000.f;
    Move->BrakingDecelerationFalling = 1500.f;

    // Camera boom & camera
    CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    CameraBoom->SetupAttachment(RootComponent);
    CameraBoom->TargetArmLength = 400.f;
    CameraBoom->bUsePawnControlRotation = true;

    FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
    FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
    FollowCamera->bUsePawnControlRotation = false;

    // Cover component
    BunkerCoverComponent = CreateDefaultSubobject<UBunkerCoverComponent>(TEXT("BunkerCoverComponent"));

    // Create & wire the BunkerAdvisorComponent if not already created elsewhere
    if (!FindComponentByClass<UBunkerAdvisorComponent>())
    {
        auto* Advisor = CreateDefaultSubobject<UBunkerAdvisorComponent>(TEXT("BunkerAdvisorComponent"));
        if (Advisor)
        {
            Advisor->OnBeginTraverseTo.AddDynamic(this, &ABunkeredCharacter::HandleBeginTraverseTo);
        }
    }
    else
    {
        if (auto* Advisor = FindComponentByClass<UBunkerAdvisorComponent>())
        {
            Advisor->OnBeginTraverseTo.AddDynamic(this, &ABunkeredCharacter::HandleBeginTraverseTo);
        }
    }

    // Defaults for vault tuning
    VaultProbeDistance  = 150.f;
    VaultMaxHeight      = 90.f;
    VaultForwardImpulse = 400.f;
    VaultUpImpulse      = 300.f;}

void ABunkeredCharacter::SetupPlayerInputComponent(UInputComponent* InputComp)
{
    Super::SetupPlayerInputComponent(InputComp);
    // Intentionally empty — PlayerController binds and forwards via the interface.
}

void ABunkeredCharacter::HandleBeginTraverseTo(ABunkerBase* TargetBunker, int32 TargetSlot)
{
    if (!TargetBunker) return;
    PendingBunker = TargetBunker;
    PendingSlot   = TargetSlot;

    const FVector Dest = TargetBunker->GetSlotWorldTransform(TargetSlot).GetLocation();
    StartMoveTo(Dest);
}

void ABunkeredCharacter::StartMoveTo(const FVector& Dest)
{
    if (AController* C = GetController())
    {
        UAIBlueprintHelperLibrary::SimpleMoveToLocation(C, Dest);
        // Poll arrival every 0.1s
        GetWorldTimerManager().SetTimer(MovePollTimer, this, &ABunkeredCharacter::PollArrivalAndEnter, 0.1f, true);
    }
}

void ABunkeredCharacter::PollArrivalAndEnter()
{
    {
        if (!PendingBunker.IsValid() || PendingSlot == INDEX_NONE)
        {
            GetWorldTimerManager().ClearTimer(MovePollTimer);
            return;
        }

        const FVector Here = GetActorLocation();
        const FVector Dest = PendingBunker->GetSlotWorldTransform(PendingSlot).GetLocation();
        const float Dist = FVector::Dist(Here, Dest);

        // Opportunistic vault if we’re still far but something low is in the way
        if (Dist > 200.f)
        {
            CheckAndAutoVaultToward(Dest);
        }

        const float Threshold = FMath::Max(100.f, PendingBunker->GetSlot(PendingSlot).EntryRadius + 30.f);

        if (Dist <= Threshold)
        {
            GetWorldTimerManager().ClearTimer(MovePollTimer);
            if (auto* Cover = FindComponentByClass<UBunkerCoverComponent>())
            {
                if (Cover->TryEnterCover(PendingBunker.Get(), PendingSlot))
                {
                    // Suggest the next bunker immediately
                    if (auto* Advisor = FindComponentByClass<UBunkerAdvisorComponent>())
                    {
                        Advisor->UpdateSuggestion();
                    }
                }
            }
            PendingBunker = nullptr;
            PendingSlot = INDEX_NONE;
        }
    }
}

bool ABunkeredCharacter::CheckAndAutoVaultToward(const FVector& Dest)
{
    // Quick capsule-forward probe to detect a low obstacle (e.g., snake bunker)
    const FVector Start = GetActorLocation();
    const FVector Fwd = (Dest - Start).GetSafeNormal();

    const FVector End = Start + Fwd * VaultProbeDistance;

    // Line trace at knee height and chest height to estimate obstacle top
    FHitResult HR;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(AutoVault), false, this);
    const FVector Knee = Start + FVector(0,0, 40.f);
    const FVector Chest= Start + FVector(0,0, 90.f);

    bool bKneeHit = GetWorld()->LineTraceSingleByChannel(HR, Knee, Knee + Fwd * VaultProbeDistance, ECC_Visibility, Params);
    if (!bKneeHit) return false; // no low obstacle ahead

    // If chest also hits, obstacle too tall → no auto-vault
    FHitResult HR2;
    const bool bChestHit = GetWorld()->LineTraceSingleByChannel(HR2, Chest, Chest + Fwd * VaultProbeDistance, ECC_Visibility, Params);
    if (bChestHit) return false;

    // Estimate height by tracing upward from the knee impact point until free
    float Height = 0.f;
    {
        FHitResult UpHR;
        const FVector UpStart = HR.ImpactPoint;
        const FVector UpEnd   = UpStart + FVector(0,0, VaultMaxHeight + 5.f);
        bool bUpHit = GetWorld()->LineTraceSingleByChannel(UpHR, UpStart, UpEnd, ECC_Visibility, Params);
        Height = bUpHit ? (UpHR.Distance) : VaultMaxHeight; // crude estimate
    }

    if (Height <= VaultMaxHeight)
    {
        // Trigger a simple physical vault: forward+up impulse (you can replace with a montage later)
        LaunchCharacter(FVector(Fwd * VaultForwardImpulse + FVector(0,0,VaultUpImpulse)), false, false);
        return true;
    }

    return false;
}

bool ABunkeredCharacter::FindNearbyBunkerAndSlot(ABunkerBase*& OutBunker, int32& OutSlot) const
{
    const float SearchRadius = 300.f;
    TArray<AActor*> BunkerActors;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), ABunkerBase::StaticClass(), BunkerActors);

    float BestSq = FLT_MAX;
    ABunkerBase* Best = nullptr;
    int32 BestSlot = INDEX_NONE;

    for (AActor* A : BunkerActors)
    {
        auto* Bunker = Cast<ABunkerBase>(A);
        if (!Bunker) continue;

        int32 SlotIdx = INDEX_NONE;
        const int32 Found = Bunker->FindClosestValidSlot(GetActorLocation(), SearchRadius, SlotIdx);
        if (Found != INDEX_NONE)
        {
            const float D2 = FVector::DistSquared(Bunker->GetSlotWorldTransform(SlotIdx).GetLocation(), GetActorLocation());
            if (D2 < BestSq)
            {
                BestSq  = D2;
                Best    = Bunker;
                BestSlot = SlotIdx;
            }
        }
    }

    OutBunker = Best;
    OutSlot   = BestSlot;
    return Best != nullptr;
}

// ===== Interface execution =====
void ABunkeredCharacter::EnterSlotOnBunker_Implementation()
{
    if (!BunkerCoverComponent) return;

    if (BunkerCoverComponent->IsInCover())
    {
        DEBUG(5.0f, FColor::Red, TEXT("Ignoring EnterSlotOnBunker"));
        BunkerCoverComponent->ExitCover();
    }
    else
    {
        ABunkerBase* B = nullptr; int32 Slot = INDEX_NONE;
        if (FindNearbyBunkerAndSlot(B, Slot))
        {
            if (BunkerCoverComponent->TryEnterCover(B, Slot))
            {
                if (BunkerAdvisorComponent)
                {
                    BunkerAdvisorComponent->UpdateSuggestion();
                }
            }
        }
    }
}

void ABunkeredCharacter::SlotTransition_Implementation(int32 Delta)
{
    if (BunkerCoverComponent)
    {
        if (BunkerCoverComponent->RequestSlotMoveRelative(Delta))
        {
            if (BunkerAdvisorComponent)
            {
                BunkerAdvisorComponent->UpdateSuggestion();
            }
        }
    }
}

void ABunkeredCharacter::SetSlotStance_Implementation(ECoverStance Stance)
{
    if (BunkerCoverComponent) BunkerCoverComponent->SetStance(Stance);
}

void ABunkeredCharacter::SlotPeek_Implementation(EPeekDirection Direction, bool bPressed)
{
    if (BunkerCoverComponent) BunkerCoverComponent->SetPeek(Direction, bPressed);
}

void ABunkeredCharacter::Pawn_Movement_Implementation(FVector2D Move)
{
    // If in cover, allow horizontal traverse via DoMove (keeps your existing behavior)
    DoMove(Move.X, Move.Y);
}

void ABunkeredCharacter::Pawn_MouseLook_Implementation(FVector2D Look)
{
    DoLook(Look.X, Look.Y);
}

// ===== Optional helpers =====
void ABunkeredCharacter::DoMove(float Right, float Forward)
{
    // If we are in cover, traverse Left or Right
    if (BunkerCoverComponent && BunkerCoverComponent->IsInCover())
    {
        if (Right > 0.6f)  { BunkerCoverComponent->TraverseRight(); return; }
        if (Right < -0.6f) { BunkerCoverComponent->TraverseLeft();  return; }
        return; // swallow normal movement while in cover
    }

    if (Controller)
    {
        const FRotator Rotation = Controller->GetControlRotation();
        const FRotator YawRotation(0, Rotation.Yaw, 0);
        const FVector ForwardDir = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
        const FVector RightDir   = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

        AddMovementInput(ForwardDir, Forward);
        AddMovementInput(RightDir,   Right);
    }
}

void ABunkeredCharacter::DoLook(float Yaw, float Pitch)
{
    if (Controller)
    {
        AddControllerYawInput(Yaw);
        AddControllerPitchInput(Pitch);
    }
}

void ABunkeredCharacter::DoCrouchToggle()
{
    DEBUG(5.0f, FColor::Yellow, TEXT("DoCrouchToggle"));
    if (GetCharacterMovement()->IsCrouching()) { UnCrouch(); }
    else                                        { Crouch();  }
}

bool ABunkeredCharacter::Nav_UpdateSuggestion()
{
    return (BunkerAdvisorComponent) ? BunkerAdvisorComponent->UpdateSuggestion() : false;
}

// Todo: Considering binding this this to PC for button presses
bool ABunkeredCharacter::Nav_AcceptSuggestion()
{
    return (BunkerAdvisorComponent) ? BunkerAdvisorComponent->AcceptSuggestion() : false;
}
