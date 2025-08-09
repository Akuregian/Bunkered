// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/BunkerCoverComponent.h"
#include "Bunkers/BunkerBase.h"
#include "Characters/BunkeredCharacter.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Subsystem/SmartBunkerSelectionSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Blueprint/AIBlueprintHelperLibrary.h" // .Build.cs: AIModule

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
    const FRotator FaceRot   = (SafeBackDir).Rotation();

    // 2) Vertical placement: snap by capsule sweep (more reliable than a thin ray)
    UWorld* World = GetWorld();
    if (!World)
    {
        OutLoc = PreLoc;
        OutRot = FaceRot;
        return true;
    }

    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(CoverFloorSnap), false, GetOwner());
    const FVector Start = PreLoc + FVector::UpVector * FloorSnapUp;
    const FVector End = PreLoc - FVector::UpVector * FloorSnapDown;
    const FCollisionShape Capsule = FCollisionShape::MakeCapsule(Radius, HalfHeight);

    // Ground may be static or movable; include both so we don't miss and float
    FCollisionObjectQueryParams ObjMask;
    ObjMask.AddObjectTypesToQuery(ECC_WorldStatic);
    ObjMask.AddObjectTypesToQuery(ECC_WorldDynamic);
    const bool bHit = World->SweepSingleByObjectType(
        Hit, Start, End, FQuat::Identity, ObjMask, Capsule, Params);
    
    OutLoc = bHit ? Hit.Location + FVector::UpVector * (HalfHeight + FloorSnapPadding) : PreLoc; // rare fallback
    
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

    // keep camera/controller aligned to our new facing
    AlignControllerYawTo(Rot);
}

void UBunkerCoverComponent::SetInitialShoulderFromView(const FVector& SlotNormal)
{
        if (!CachedBoom) return;
    
        const FVector Up = FVector::UpVector;
        const FVector SlotRight = FVector::CrossProduct(Up, SlotNormal).GetSafeNormal();

        FRotator ViewRot = FRotator::ZeroRotator;
        if (APawn* P = Cast<APawn>(GetOwner()))
        {
            if (AController* C = P->GetController())
            {
                ViewRot = C->GetControlRotation();
            }
            else
            {
                ViewRot = P->GetActorRotation();
            }
        }
    
        const FVector ViewRight = FRotationMatrix(ViewRot).GetUnitAxis(EAxis::Y);
        const float SideSign = (FVector::DotProduct(ViewRight, SlotRight) >= 0.f) ? +1.f : -1.f;
        CachedBoom->SocketOffset = FVector(0.f, SideSign * CoverShoulderOffset, 0.f);
}

void UBunkerCoverComponent::AlignControllerYawTo(const FRotator& FaceRot)
{
    if (APawn* P = Cast<APawn>(GetOwner()))
    {
        if (AController* C = P->GetController())
        {
            FRotator NewCtrl = C->GetControlRotation();
            NewCtrl.Yaw = FaceRot.Yaw;
            C->SetControlRotation(NewCtrl);
        }
    }
}

bool UBunkerCoverComponent::ResolveSlotXf(FTransform& OutXf, FVector& OutNormal) const{
    if (!CurrentBunker.IsValid() || CurrentSlot == INDEX_NONE) return false;    OutXf = CurrentBunker->GetSlotWorldTransform(CurrentSlot);
    OutNormal = CurrentBunker->GetSlotNormal(CurrentSlot).GetSafeNormal();
    return true;
}

bool UBunkerCoverComponent::EnterBestCover()
{
    AActor* Owner = GetOwner(); if (!Owner) return false;
    UWorld* W = Owner->GetWorld(); if (!W) return false;
    auto* SBSS = W->GetSubsystem<USmartBunkerSelectionSubsystem>(); if (!SBSS) return false;

    TArray<FBunkerCandidate> Top;
    SBSS->GetTopK(Owner, 5, Top); // grab a few

    //  Do not pick our currently-occupied bunker 
    if (CurrentBunker.IsValid())
    {
        ABunkerBase* Curr = CurrentBunker.Get();
        Top.RemoveAll([Curr](const FBunkerCandidate& C)
        {
            return C.Bunker == Curr;   // exclude current bunker entirely
        });
    }

    if (Top.Num() == 0) return false; // nothing new to go to

    // Reserve the slot now so AI/teammates don’t steal it
    ABunkerBase* B = Top[0].Bunker; 
    const int32 S = Top[0].SlotIndex;
    if (!B || !B->TryClaimSlot(S, Owner)) return false;

    // Switch current; compute target hug transform (location we’ll approach)
    CurrentBunker = B; 
    CurrentSlot   = S;

    FTransform SlotXf; 
    FVector Normal;
    if (!ResolveSlotXf(SlotXf, Normal))
    {
        B->ReleaseSlot(S, Owner);
        CurrentBunker.Reset(); 
        CurrentSlot = INDEX_NONE; 
        return false;
    }

    if (!ComputeHugTransform(SlotXf, Normal, ApproachLoc, ApproachRot))
    {
        B->ReleaseSlot(S, Owner);
        CurrentBunker.Reset(); 
        CurrentSlot = INDEX_NONE; 
        return false;
    }

    // Approach setup (unchanged)
    ACharacter* Char = Cast<ACharacter>(Owner);
    const float Dist2D = Char ? FVector::Dist2D(Char->GetActorLocation(), ApproachLoc) : 0.f;

    bool bHasLOS = true;
    if (UWorld* World = GetWorld())
    {
        FHitResult Hit;
        FCollisionQueryParams P(SCENE_QUERY_STAT(CoverApproachLOS), false, Owner);
        const FVector EyeA = (Char ? Char->GetActorLocation() : ApproachLoc) + FVector(0,0,44.f);
        const FVector EyeB = ApproachLoc + FVector(0,0,44.f);
        bHasLOS = !World->LineTraceSingleByChannel(Hit, EyeA, EyeB, ECC_Visibility, P);
    }

    ActiveApproachMode = (Dist2D <= ApproachNudgeMaxDistance && bHasLOS)
                           ? ECoverApproachMode::Nudge
                           : ECoverApproachMode::Nav;

    State = ECoverState::Approach;

    if (Char && Char->GetCharacterMovement())
    {
        SavedMaxWalkSpeed = Char->GetCharacterMovement()->MaxWalkSpeed;
        Char->GetCharacterMovement()->MaxWalkSpeed = ApproachWalkSpeed;
    }

    AlignControllerYawTo(ApproachRot);

    if (ActiveApproachMode == ECoverApproachMode::Nav && Char && Char->GetController())
    {
        UAIBlueprintHelperLibrary::SimpleMoveToLocation(Char->GetController(), ApproachLoc);
    }

    return true;
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
        TransitionElapsed = 0.f;
        bIsTransitioning  = true;
        PendingSlot       = SlotIndex;
    }
    else
    {
        // First-time entry or cross-bunker: blend in (no teleport)
        StartLoc = Char->GetActorLocation();
        StartRot = Char->GetActorRotation();
        TargetLoc = TargetHugLoc;
        TargetRot = TargetHugRot;
        TransitionElapsed = 0.f;
        bIsTransitioning  = true;
        PendingSlot       = SlotIndex;
    }

    // Reset lean and state
    State = ECoverState::Hug;
    DesiredLeanAxis = 0.f;
    CurrentLean = 0.f;

    // Pose (stand/crouch/prone) per slot
    ApplyStanceForSlot(Bunker, SlotIndex);

    // Initial shoulder based on current player view vs slot frame
    SetInitialShoulderFromView(Normal);
    
    // Make sure controller yaw matches new bunker facing (camera looks right way)
    AlignControllerYawTo(TargetHugRot); 
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

    if (ACharacter* Char = Cast<ACharacter>(GetOwner()))
    {
        if (Char->GetCharacterMovement() && SavedMaxWalkSpeed > 0.f)
        {
            Char->GetCharacterMovement()->MaxWalkSpeed = SavedMaxWalkSpeed;
            SavedMaxWalkSpeed = 0.f;
        }
    }
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

bool UBunkerCoverComponent::FindAdjacentSlotOnCurrentBunker(int DirSign, int32& OutSlotIndex) const
{
    OutSlotIndex = INDEX_NONE;
    if (!CurrentBunker.IsValid() || CurrentSlot == INDEX_NONE) return false;

    // Current frame
    const FTransform CurrXf = CurrentBunker->GetSlotWorldTransform(CurrentSlot);
    const FVector CurrP = CurrXf.GetLocation();
    const FVector Normal = CurrentBunker->GetSlotNormal(CurrentSlot).GetSafeNormal();
    const FVector Right = FVector::CrossProduct(FVector::UpVector, Normal).GetSafeNormal();

    float BestAlong = -FLT_MAX;   // how far along the desired side (+Right or -Right)
    float BestDist = FLT_MAX;

    const int32 Count = CurrentBunker->GetSlotCount();
    for (int32 i = 0; i < Count; ++i)
    {
        if (i == CurrentSlot || CurrentBunker->IsSlotOccupied(i)) continue;
        const FVector P = CurrentBunker->GetSlotWorldTransform(i).GetLocation();
        const FVector Delta = P - CurrP;

        const float Along = FVector::DotProduct(Delta, Right) * DirSign; // positive means in requested direction
        if (Along <= 5.f) continue; // must be meaningfully in that direction

        const float Dist = Delta.Size2D();
        // prefer bigger Along, then nearest distance
        if (Along > BestAlong || (FMath::IsNearlyEqual(Along, BestAlong, 1.f) && Dist < BestDist))
        {
            BestAlong = Along;
            BestDist = Dist;
            OutSlotIndex = i;
        }
    }
    return OutSlotIndex != INDEX_NONE;
}

bool UBunkerCoverComponent::TraverseLeft()
{
    int32 Next = INDEX_NONE;
    if (!FindAdjacentSlotOnCurrentBunker(-1, Next)) return false;
    return EnterCover(CurrentBunker.Get(), Next);
}

bool UBunkerCoverComponent::TraverseRight()
{
    int32 Next = INDEX_NONE;
    if (!FindAdjacentSlotOnCurrentBunker(+1, Next)) return false;
    return EnterCover(CurrentBunker.Get(), Next);
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

    FTransform SlotXf;
    FVector Normal;
    if (!ResolveSlotXf(SlotXf, Normal))
    {
        ExitCover();
        return;
    }

    ACharacter* Char = Cast<ACharacter>(GetOwner());
    if (!Char)
    {
        ExitCover();
        return;
    }

    // Approach: either Nudge (AddMovementInput) or Nav (SimpleMoveToLocation already active)
    if (State == ECoverState::Approach)
    {
        const FVector To = (ApproachLoc - Char->GetActorLocation());
        const float Dist = To.Size2D();

        // Face the wall orientation as we approach
        const FRotator YawOnly(0.f, ApproachRot.Yaw, 0.f);
        Char->SetActorRotation(YawOnly);
        AlignControllerYawTo(ApproachRot);

        // Walk toward target (no AI controller required)
        if (Dist > ApproachStopDistance)
        {
            if (ActiveApproachMode == ECoverApproachMode::Nudge)
            {
                const FVector Dir = To.GetSafeNormal2D();
                Char->AddMovementInput(Dir, ApproachMoveSpeed);
            }

            // Nav mode: controller keeps moving via SimpleMoveToLocation
            return; // still approaching; skip the rest of cover logic this frame
        }

        if (ActiveApproachMode == ECoverApproachMode::Nav)
        {
            Char->GetCharacterMovement()->StopMovementImmediately();
        }

        // Close enough: ease the last few cm into perfect hug pose
        StartLoc = Char->GetActorLocation();
        StartRot = Char->GetActorRotation();
        TargetLoc = ApproachLoc;
        TargetRot = ApproachRot;
        TransitionElapsed = 0.f;

        bIsTransitioning = true;

        // Reset lean & camera shoulder default (away from wall)
        DesiredLeanAxis = 0.f;
        CurrentLean = 0.f;

        // pick initial shoulder from current view
        SetInitialShoulderFromView(/*SlotNormal*/ Normal);

        // Stance
        if (CurrentBunker.IsValid() && CurrentSlot != INDEX_NONE)
        {
            ApplyStanceForSlot(CurrentBunker.Get(), CurrentSlot);
        }

        State = ECoverState::Hug;

        // Restore walk speed now that we arrived
        if (Char->GetCharacterMovement() && SavedMaxWalkSpeed > 0.f)
        {
            Char->GetCharacterMovement()->MaxWalkSpeed = SavedMaxWalkSpeed;
            SavedMaxWalkSpeed = 0.f;
        }

        // fall through to Hug logic below next tick
    }

    if (bIsTransitioning)
    {
        TransitionElapsed += DeltaTime;

        // Use entry blend time when just finishing the approach; otherwise use slot transition time.
        const bool bEntryEase = (State == ECoverState::Hug &&
            TransitionElapsed <= EntrySnapBlendTime + SMALL_NUMBER &&
            !StartLoc.IsNearlyZero() && !TargetLoc.IsNearlyZero());

        const float Duration = bEntryEase
                                   ? EntrySnapBlendTime
                                   : FMath::Max(SlotTransitionTime, KINDA_SMALL_NUMBER);
        const float Alpha = FMath::Clamp(TransitionElapsed / Duration, 0.f, 1.f);

        // --- 1) Lerp XY only ---
        const FVector StartXY(StartLoc.X, StartLoc.Y, 0.f);
        const FVector TargetXY(TargetLoc.X, TargetLoc.Y, 0.f);
        const FVector NewXY = FMath::Lerp(StartXY, TargetXY, Alpha);

        // --- 2) Lerp rotation normally ---
        const FRotator NewRot = FMath::Lerp(StartRot, TargetRot, Alpha);

        // --- 3) Ground-snap Z at NewXY with a capsule sweep (static + dynamic ground) ---
        float Radius = 42.f, HalfHeight = 88.f;
        if (ACharacter* C = Cast<ACharacter>(GetOwner()))
        {
            if (UCapsuleComponent* Cap = C->GetCapsuleComponent())
            {
                Cap->GetScaledCapsuleSize(Radius, HalfHeight);
            }
        }

        const FVector SweepStart(NewXY.X, NewXY.Y, FMath::Max(StartLoc.Z, TargetLoc.Z) + FloorSnapUp);
        const FVector SweepEnd(NewXY.X, NewXY.Y, FMath::Min(StartLoc.Z, TargetLoc.Z) - FloorSnapDown);

        FHitResult SweepHit;
        FCollisionQueryParams SweepParams(SCENE_QUERY_STAT(CoverSlideSnap), false, GetOwner());
        FCollisionObjectQueryParams ObjMask;
        ObjMask.AddObjectTypesToQuery(ECC_WorldStatic);
        ObjMask.AddObjectTypesToQuery(ECC_WorldDynamic);

        const FCollisionShape Capsule = FCollisionShape::MakeCapsule(Radius, HalfHeight);

        float NewZ;
        if (GetWorld()->SweepSingleByObjectType(SweepHit, SweepStart, SweepEnd, FQuat::Identity, ObjMask, Capsule,
                                                SweepParams))
        {
            NewZ = SweepHit.Location.Z + HalfHeight + FloorSnapPadding;
        }
        else
        {
            // Rare fallback if no hit (keeps motion stable).
            NewZ = FMath::Lerp(StartLoc.Z, TargetLoc.Z, Alpha);
        }

        const FVector NewLoc(NewXY.X, NewXY.Y, NewZ);
        Char->SetActorLocationAndRotation(NewLoc, NewRot, /*bSweep=*/true);

        if (Alpha >= 1.f - SMALL_NUMBER)
        {
            bIsTransitioning = false;
            PendingSlot = INDEX_NONE;

            // Finalize exactly on target to avoid sub-mm drift.
            PlaceAtHugTransform(TargetLoc, TargetRot);
        }

        return; // We handled this frame’s movement.
    }
    // 4) Camera: flip shoulder with lean side (simple)
    if (CachedBoom)
    {
        const float Side = (CurrentLean >= 0.f) ? +1.f : -1.f;
        CachedBoom->SocketOffset = FVector(0.f, Side * CoverShoulderOffset, 0.f);
    }
}

