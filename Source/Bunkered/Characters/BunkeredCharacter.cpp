// BunkeredCharacter.cpp
#include "BunkeredCharacter.h"

#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/BunkerAdvisorComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PeekComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/Controller.h"
#include "Components/BunkerCoverComponent.h"
#include "Bunkers/BunkerBase.h"
#include "Components/CoverSplineComponent.h"
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

    // Bunker Advisor Component
    BunkerAdvisorComponent = CreateDefaultSubobject<UBunkerAdvisorComponent>(TEXT("BunkerAdvisorComponent"));
    BunkerAdvisorComponent->OnBeginTraverseTo.AddDynamic(this, &ABunkeredCharacter::HandleBeginTraverseTo);

    // Peek Component
    PeekComponent = CreateDefaultSubobject<UPeekComponent>(TEXT("PeekComponent"));
}

void ABunkeredCharacter::SetupPlayerInputComponent(UInputComponent* InputComp)
{
    Super::SetupPlayerInputComponent(InputComp);
}

// ===== IBunkerCoverInterface impl =====
void ABunkeredCharacter::Cover_EnterNearest_Implementation()
{
    if (!BunkerCoverComponent) return;

    if (BunkerCoverComponent->IsInCover())
    {
        BunkerCoverComponent->ExitCover();
        return;
    }

    ABunkerBase* B = nullptr; float Alpha = 0.f;
    if (FindNearbyBunkerAndAlpha(B, Alpha))
    {
        if (HasAuthority()) BunkerCoverComponent->EnterCoverAtAlpha(B, Alpha);
        else                BunkerCoverComponent->Server_EnterCoverAtAlpha(B, Alpha);
    }
}

void ABunkeredCharacter::Cover_Exit_Implementation()
{
    if (!BunkerCoverComponent) return;
    if (HasAuthority()) BunkerCoverComponent->ExitCover();
    else                BunkerCoverComponent->Server_ExitCover();
}

void ABunkeredCharacter::Cover_Slide_Implementation(float Axis)
{
    if (!BunkerCoverComponent) return;
    const float Step = SlideStepAlpha * FMath::Clamp(Axis, -1.f, +1.f);
    if (FMath::IsNearlyZero(Step)) return;

    if (HasAuthority()) BunkerCoverComponent->SlideAlongCover(Step);
    else                BunkerCoverComponent->Server_SlideAlongCover(Step);
}

void ABunkeredCharacter::Cover_Peek_Implementation(EPeekDirection Direction, bool bPressed)
{
    if (PeekComponent)
    {
        PeekComponent->HandlePeekInput(Direction, bPressed);
        return;
    }
    if (BunkerCoverComponent)
    {
        if (HasAuthority()) BunkerCoverComponent->SetPeek(Direction, bPressed);
        else                BunkerCoverComponent->Server_SetPeek(Direction, bPressed);
    }
}

void ABunkeredCharacter::Pawn_Movement_Implementation(FVector2D Move)
{
    DoMove(Move.X, Move.Y);
}

void ABunkeredCharacter::Pawn_MouseLook_Implementation(FVector2D Look)
{
    DoLook(Look.X, Look.Y);
}

void ABunkeredCharacter::Pawn_ChangeBunkerStance_Implementation(bool bCrouching)
{
    DoCrouchToggle();
}

// ===== Helpers =====
bool ABunkeredCharacter::FindNearbyBunkerAndAlpha(ABunkerBase*& OutBunker, float& OutAlpha) const
{
    OutBunker = nullptr; OutAlpha = 0.f;

    TArray<AActor*> BunkerActors;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), ABunkerBase::StaticClass(), BunkerActors);

    float BestSq = FLT_MAX;
    ABunkerBase* Best = nullptr;
    float BestA = 0.f;

    const FVector Here = GetActorLocation();

    for (AActor* A : BunkerActors)
    {
        ABunkerBase* B = Cast<ABunkerBase>(A);
        if (!B) continue;
        UCoverSplineComponent* S = B->GetCoverSpline();
        if (!S) continue;

        const float A0 = S->FindClosestAlpha(Here);
        const FVector P = S->GetWorldTransformAtAlpha(A0).GetLocation();
        const float D2 = FVector::DistSquared(P, Here);
        if (D2 < BestSq) { BestSq = D2; Best = B; BestA = A0; }
    }

    OutBunker = Best; OutAlpha = BestA;
    return (Best != nullptr);
}

// ===== Advisor traversal glue =====
void ABunkeredCharacter::HandleBeginTraverseTo(ABunkerBase* TargetBunker, float TargetAlpha)
{
    if (!TargetBunker) return;
    PendingBunker = TargetBunker;
    PendingAlpha  = TargetAlpha;

    const FVector Dest = TargetBunker->GetCoverSpline()->GetWorldTransformAtAlpha(TargetAlpha).GetLocation();
    StartMoveTo(Dest);
}

void ABunkeredCharacter::StartMoveTo(const FVector& Dest)
{
    if (AController* C = GetController())
    {
        UAIBlueprintHelperLibrary::SimpleMoveToLocation(C, Dest);
        GetWorldTimerManager().SetTimer(MovePollTimer, this, &ABunkeredCharacter::PollArrivalAndEnter, 0.1f, true);
    }
}

void ABunkeredCharacter::PollArrivalAndEnter()
{
    if (!PendingBunker.IsValid())
    {
        GetWorldTimerManager().ClearTimer(MovePollTimer);
        return;
    }

    const FVector Here = GetActorLocation();
    const FVector Dest = PendingBunker->GetCoverSpline()->GetWorldTransformAtAlpha(PendingAlpha).GetLocation();
    const float Dist = FVector::Dist(Here, Dest);

    if (Dist > 200.f)
    {
        CheckAndAutoVaultToward(Dest);
    }

    if (Dist <= EnterDistanceThreshold)
    {
        GetWorldTimerManager().ClearTimer(MovePollTimer);
        if (BunkerCoverComponent)
        {
            if (HasAuthority()) BunkerCoverComponent->EnterCoverAtAlpha(PendingBunker.Get(), PendingAlpha);
            else                BunkerCoverComponent->Server_EnterCoverAtAlpha(PendingBunker.Get(), PendingAlpha);

            if (BunkerAdvisorComponent) BunkerAdvisorComponent->UpdateSuggestion();
        }
        PendingBunker = nullptr;
    }
}

bool ABunkeredCharacter::CheckAndAutoVaultToward(const FVector& Dest)
{
    const FVector Start = GetActorLocation();
    const FVector Fwd = (Dest - Start).GetSafeNormal();

    const FVector Knee  = Start + FVector(0,0, 40.f);
    const FVector Chest = Start + FVector(0,0, 90.f);

    FHitResult HR, HR2;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(AutoVault), false, this);

    const bool bKneeHit  = GetWorld()->LineTraceSingleByChannel(HR,  Knee,  Knee  + Fwd * VaultProbeDistance, ECC_Visibility, Params);
    if (!bKneeHit) return false;

    const bool bChestHit = GetWorld()->LineTraceSingleByChannel(HR2, Chest, Chest + Fwd * VaultProbeDistance, ECC_Visibility, Params);
    if (bChestHit) return false;

    // estimate height
    float Height = 0.f;
    {
        FHitResult UpHR;
        const FVector UpStart = HR.ImpactPoint;
        const FVector UpEnd   = UpStart + FVector(0,0, VaultMaxHeight + 5.f);
        bool bUpHit = GetWorld()->LineTraceSingleByChannel(UpHR, UpStart, UpEnd, ECC_Visibility, Params);
        Height = bUpHit ? (UpHR.Distance) : VaultMaxHeight;
    }

    if (Height <= VaultMaxHeight)
    {
        LaunchCharacter(FVector(Fwd * VaultForwardImpulse + FVector(0,0,VaultUpImpulse)), false, false);
        return true;
    }
    return false;
}

// ===== Optional helpers =====
void ABunkeredCharacter::DoMove(float Right, float Forward)
{
    if (BunkerCoverComponent && BunkerCoverComponent->IsInCover())
    {
        if (Right >  0.6f) { Cover_Slide_Implementation(+1.f); return; }
        if (Right < -0.6f) { Cover_Slide_Implementation(-1.f); return; }
        return; // swallow free-move while in cover
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
    if (GetCharacterMovement()->IsCrouching()) UnCrouch();
    else                                       Crouch();
}

bool ABunkeredCharacter::Nav_UpdateSuggestion()
{
    return (BunkerAdvisorComponent) ? BunkerAdvisorComponent->UpdateSuggestion() : false;
}

bool ABunkeredCharacter::Nav_AcceptSuggestion()
{
    return (BunkerAdvisorComponent) ? BunkerAdvisorComponent->AcceptSuggestion() : false;
}
