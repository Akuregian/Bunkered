// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/BunkerCoverComponent.h"
#include "Bunkers/BunkerBase.h"
#include "Characters/BunkeredCharacter.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Subsystem/SmartBunkerSelectionSubsystem.h"

UBunkerCoverComponent::UBunkerCoverComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UBunkerCoverComponent::BeginPlay()
{
    Super::BeginPlay();
        // Cache camera boom if the owner is our character type
    if (const auto* Char = Cast<ABunkeredCharacter>(GetOwner()))
    {
        CachedBoom = Char->GetCameraBoom();
        if (CachedBoom)
        {
            SavedSocketOffset = CachedBoom->SocketOffset;
            bSavedDoCollisionTest = CachedBoom->bDoCollisionTest;
        }
    } 
}

void UBunkerCoverComponent::EndPlay(const EEndPlayReason::Type EndPlayReason){
    ExitCover();
    Super::EndPlay(EndPlayReason);
}

void UBunkerCoverComponent::ApplyStanceForSlot(ABunkerBase* Bunker, int32 SlotIndex)
{
    ACharacter* Char = Cast<ACharacter>(GetOwner());
    if (!Char || !Bunker) return;

    // Todo: 
    
    switch (Bunker->GetSlotPose(SlotIndex))
    {
    case ECoverStance::Crouch:
        Char->Crouch(); break;
    case ECoverStance::Stand:
        Char->UnCrouch(); break;
    case ECoverStance::Prone:
        // Call into your movement component / ability system to enter prone
        // e.g., MyMovement->SetCustomProne(true);
        break;
    }
}

void UBunkerCoverComponent::CacheCamera()
{
    if (CachedBoom) return;

    if (ABunkeredCharacter* BC = Cast<ABunkeredCharacter>(GetOwner()))
    {
        CachedBoom = BC->GetCameraBoom();
        CachedCamera    = BC->GetFollowCamera();
    }
    else
    {
        // Fallback: first spring arm on the actor
        TArray<USpringArmComponent*> Arms;
        GetOwner()->GetComponents<USpringArmComponent>(Arms);
        if (Arms.Num() > 0) CachedBoom = Arms[0];
        if (CachedBoom)
        {
            TArray<UCameraComponent*> Cams;
            GetOwner()->GetComponents<UCameraComponent>(Cams);
            if (Cams.Num() > 0) CachedCamera = Cams[0];
        }
    }
}

bool UBunkerCoverComponent::ComputeHugTransform(const FTransform& SlotXf, const FVector& Normal, FVector& OutLoc,
    FRotator& OutRot) const
{
    const ACharacter* Char = Cast<ACharacter>(GetOwner());
    if (!Char) return false;

    float Radius, HalfHeight;
    Char->GetCapsuleComponent()->GetScaledCapsuleSize(Radius, HalfHeight);

    // 1) Horizontal placement: back away from wall along slot normal
    const FVector SafeBackDir = Normal.GetSafeNormal();
    const float  SafeBack    = CoverBackOffset + Radius * 0.6f + CameraWallClearance;
    const FVector Anchor     = SlotXf.GetLocation();
    const FVector PreLoc     = Anchor - SafeBackDir * SafeBack;
    const FRotator FaceRot   = (-SafeBackDir).Rotation();

    // 2) Vertical placement: snap to walkable floor
    UWorld* World = GetWorld();
    if (!World) { OutLoc = PreLoc; OutRot = FaceRot; return true; }

    const FVector Start = PreLoc + FVector::UpVector * FloorSnapUp;
    const FVector End   = PreLoc - FVector::UpVector * FloorSnapDown;

    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(CoverFloorSnap), false, GetOwner());
    const bool bHit = World->LineTraceSingleByChannel(
        Hit, Start, End, ECC_Visibility, Params
    );

    if (bHit)
    {
        // Put capsule bottom on the floor, plus a tiny padding
        OutLoc = Hit.Location + FVector::UpVector * (HalfHeight + FloorSnapPadding);
    }
    else
    {
        // No floor? Fall back to precomputed Z (should rarely happen)
        OutLoc = PreLoc;
    }

    OutRot = FaceRot;
    return true;
}

void UBunkerCoverComponent::PlaceAtHugTransform(const FVector& Loc, const FRotator& Rot)
{
    ACharacter* Char = Cast<ACharacter>(GetOwner());
    if (!Char) return;

    Char->TeleportTo(Loc, Rot, false, true);

    // one swept correction pass (not duplicated elsewhere)
    FHitResult Hit;
    Char->SetActorLocationAndRotation(Loc, Rot, /*bSweep=*/true, &Hit);
    if (Hit.bStartPenetrating)
    {
        const FVector PushBack = (-Rot.Vector()) * (Hit.PenetrationDepth + 5.f);
        Char->SetActorLocation(Char->GetActorLocation() - PushBack, /*bSweep=*/false);
    }

    BaseLoc = Loc;
    BaseRot = Rot;
}

bool UBunkerCoverComponent::ResolveSlotXf(FTransform& OutXf, FVector& OutNormal) const
{
    if (!CurrentBunker.IsValid() || CurrentSlot == INDEX_NONE) return false;
    OutXf = CurrentBunker->GetSlotWorldTransform(CurrentSlot);
    OutNormal = CurrentBunker->GetSlotNormal(CurrentSlot).GetSafeNormal();
    return true;
}

bool UBunkerCoverComponent::EnterBestCover()
{
    AActor* Owner = GetOwner(); if (!Owner) return false;
    UWorld* W = Owner->GetWorld(); if (!W) return false;
    auto* SBSS = W->GetSubsystem<USmartBunkerSelectionSubsystem>(); if (!SBSS) return false;

    TArray<FBunkerCandidate> Top;
    SBSS->GetTopK(Owner, 3, Top);
    
    if (Top.Num() == 0) return false;
    return EnterCover(Top[0].Bunker, Top[0].SlotIndex);
}

bool UBunkerCoverComponent::EnterCover(ABunkerBase* Bunker, int32 SlotIndex)
{
    if (!Bunker) return false;

    // Already there?
    if (CurrentBunker.IsValid() && CurrentBunker.Get() == Bunker && CurrentSlot == SlotIndex) return true;

    AActor* Owner = GetOwner(); if (!Owner) return false;

    // Claim new first
    if (!Bunker->TryClaimSlot(SlotIndex, Owner)) return false;

    const bool bSameBunker = (CurrentBunker.IsValid() && CurrentBunker.Get() == Bunker);

    // Release previous after successful new claim
    if (CurrentBunker.IsValid() && CurrentSlot != INDEX_NONE)
    {
        CurrentBunker->ReleaseSlot(CurrentSlot, Owner);
    }

    // Switch current
    CurrentBunker = Bunker;
    CurrentSlot   = SlotIndex;

    // Resolve target transform
    FTransform SlotXf; FVector Normal;
    if (!ResolveSlotXf(SlotXf, Normal))
    {
        Bunker->ReleaseSlot(SlotIndex, Owner);
        CurrentBunker.Reset(); CurrentSlot = INDEX_NONE;
        return false;
    }

    // Debugging
    if (CurrentBunker.IsValid())
    {
        // If the slot point is missing, GetSlotWorldTransform returned the actor transform.
        // Detect this by comparing to actor transform (approx).
        const FTransform BunkerXf = CurrentBunker->GetActorTransform();
        if (SlotXf.Equals(BunkerXf, 0.1f))
        {
            UE_LOG(LogTemp, Warning, TEXT("EnterCover: Bunker %s Slot %d has no SlotPoint; refusing to place at bunker origin."),
                *CurrentBunker->GetName(), CurrentSlot);
            CurrentBunker->ReleaseSlot(CurrentSlot, Owner);
            CurrentBunker.Reset(); CurrentSlot = INDEX_NONE;
            return false;
        }
    }

    ACharacter* Char = Cast<ACharacter>(Owner);
    if (!Char)
    {
        Bunker->ReleaseSlot(SlotIndex, Owner);
        CurrentBunker.Reset();
        CurrentSlot = INDEX_NONE;
        return false;
    }

    // Compute hugging transform at target
    FVector TargetHugLoc;
    FRotator TargetHugRot;
    if (!ComputeHugTransform(SlotXf, Normal, TargetHugLoc, TargetHugRot))
    {
        Bunker->ReleaseSlot(SlotIndex, Owner);
        CurrentBunker.Reset();
        CurrentSlot = INDEX_NONE;
        return false;
    }

    // Debugging
    if (UWorld* W = GetWorld())
    {
        DrawDebugSphere(W, SlotXf.GetLocation(), 10.f, 8, FColor::Cyan, false, 2.f);
        DrawDebugLine(W, SlotXf.GetLocation(), TargetHugLoc, FColor::Cyan, false, 2.f, 0, 1.f);
    }

    if (bSameBunker && SlotTransitionTime > KINDA_SMALL_NUMBER)
    {
        // Smooth transition between slots on the same bunker
        StartLoc = Char->GetActorLocation();
        StartRot = Char->GetActorRotation();
        TargetLoc = TargetHugLoc;
        TargetRot = TargetHugRot;
        BaseLoc   = TargetHugLoc; // keep Base for lean relative to target
        BaseRot   = TargetHugRot;
        TransitionElapsed = 0.f;
        bIsTransitioning  = true;
        PendingSlot       = SlotIndex;
    }
    else
    {
        PlaceAtHugTransform(TargetHugLoc, TargetHugRot);
        PendingSlot = INDEX_NONE;
        bIsTransitioning = false;
    }

    // Reset lean and state
    State = ECoverState::Hug;
    DesiredLeanAxis = 0.f;
    CurrentLean = 0.f;

    // Pose (stand/crouch/prone) per slot
    ApplyStanceForSlot(Bunker, SlotIndex);

    // Camera shoulder offset while in cover (simple lateral nudge away from wall)
    if (CachedBoom)
    {
        CachedBoom->SocketOffset = FVector(0.f, CoverShoulderOffset, 0.f);
        // Leave collision enabled; the extra clearance + shoulder offset should be enough
    }

    return true;
}

void UBunkerCoverComponent::ExitCover()
{
    // Restore camera
    if (CachedBoom)
    {
        CachedBoom->SocketOffset = SavedSocketOffset;
        CachedBoom->bDoCollisionTest = bSavedDoCollisionTest;
    }
    
    if (CurrentBunker.IsValid() && CurrentSlot != INDEX_NONE)
    {
        CurrentBunker->ReleaseSlot(CurrentSlot, GetOwner());
    }
    CurrentBunker.Reset();
    CurrentSlot = INDEX_NONE;
    State = ECoverState::None;
}

bool UBunkerCoverComponent::TraverseBestSlotOnCurrentBunker(int32 DesiredIndex)
{
    AActor* Owner = GetOwner();
    if (!Owner) return false;
    
    if (!CurrentBunker.IsValid() || CurrentSlot == INDEX_NONE) return false;
    UWorld* W = Owner->GetWorld();
    if (!W) return false;
    
    auto* SBSS = W->GetSubsystem<USmartBunkerSelectionSubsystem>();
    if (!SBSS) return false;

    TArray<FBunkerCandidate> Top;
    SBSS->GetTopKForBunker(Owner, CurrentBunker.Get(), 3, Top);
    Top.RemoveAll([this](const FBunkerCandidate& C) { return C.SlotIndex == CurrentSlot; });
    
    if (Top.Num() == 0) return false;
    
    return EnterCover(Top[0].Bunker, Top[0].SlotIndex);
}

void UBunkerCoverComponent::SetLeanAxis(float Axis)
{
    DesiredLeanAxis = FMath::Clamp(Axis, -1.f, 1.f);
    if (State != ECoverState::None) State = (FMath::IsNearlyZero(DesiredLeanAxis)) ? ECoverState::Hug : ECoverState::Peek;
}

void UBunkerCoverComponent::ApplyHugPose(const FTransform& SlotXf, const FVector& Normal)
{
    ACharacter* Char = Cast<ACharacter>(GetOwner());
    if (!Char) return;
    
    FVector HugLoc;
    FRotator HugRot;
    if (ComputeHugTransform(SlotXf, Normal, HugLoc, HugRot))
    {
        PlaceAtHugTransform(HugLoc, HugRot);
    }
}

void UBunkerCoverComponent::ApplyLean(float DeltaTime, const FTransform& SlotXf, const FVector& Normal)
{
    if (State == ECoverState::None) return;

    ACharacter* Char = Cast<ACharacter>(GetOwner());
    if (!Char) return;

    // Smooth the lean
    CurrentLean = FMath::FInterpTo(CurrentLean, DesiredLeanAxis, DeltaTime, LeanSpeed);

    const FVector Up = FVector::UpVector;
    const FVector Right = FVector::CrossProduct(Up, Normal).GetSafeNormal(); // right is +lean

    const FVector LeanOffset = Right * (CurrentLean * LeanLateral) + Up * (FMath::Abs(CurrentLean) * LeanVertical);

    const FVector NewLoc = BaseLoc + LeanOffset;
    Char->SetActorLocation(NewLoc, /*bSweep=*/true);
    // Keep rotation locked to base for now (we can add shoulder swap later)
    Char->SetActorRotation(BaseRot);
}

void UBunkerCoverComponent::TickComponent(float DeltaTime, ELevelTick, FActorComponentTickFunction*)
{
    if (State == ECoverState::None) return;

    FTransform SlotXf; FVector Normal;
    if (!ResolveSlotXf(SlotXf, Normal)) { ExitCover(); return; }

    ACharacter* Char = Cast<ACharacter>(GetOwner());
    if (!Char) return;

    if (bIsTransitioning)
    {
        TransitionElapsed += DeltaTime;
        const float Alpha = FMath::Clamp(TransitionElapsed / FMath::Max(SlotTransitionTime, KINDA_SMALL_NUMBER), 0.f, 1.f);

        const FVector NewLoc = FMath::Lerp(StartLoc, TargetLoc, Alpha);
        const FRotator NewRot = FMath::Lerp(StartRot, TargetRot, Alpha);

        Char->SetActorLocationAndRotation(NewLoc, NewRot, /*bSweep=*/true);

        if (Alpha >= 1.f - SMALL_NUMBER)
        {
            bIsTransitioning = false;
            PendingSlot = INDEX_NONE;
        }
    }
    else
    {
        // Normal lean behavior around BaseLoc/Rot
        ApplyLean(DeltaTime, SlotXf, Normal);
        Char->SetActorRotation(BaseRot);
    }

    // 4) Camera: flip shoulder with lean side (simple)
    if (CachedBoom)
    {
        const float Side = (CurrentLean >= 0.f) ? +1.f : -1.f;
        CachedBoom->SocketOffset = FVector(0.f, Side * CoverShoulderOffset, 0.f);
    }
}

