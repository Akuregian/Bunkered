// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/BunkerCoverComponent.h"
#include "Bunkers/BunkerBase.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Subsystem/SmartBunkerSelectionSubsystem.h"

UBunkerCoverComponent::UBunkerCoverComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UBunkerCoverComponent::BeginPlay()
{
    Super::BeginPlay();
}

void UBunkerCoverComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ExitCover();
    Super::EndPlay(EndPlayReason);
}

bool UBunkerCoverComponent::ResolveSlotXf(FTransform& OutXf, FVector& OutNormal) const
{
    if (!CurrentBunker.IsValid() || CurrentSlot == INDEX_NONE) return false;
    OutXf = CurrentBunker->GetSlotWorldTransform(CurrentSlot);
    OutNormal = CurrentBunker->GetSlotNormal(CurrentSlot).GetSafeNormal();
    return true;
}

bool UBunkerCoverComponent::EnterCoverFromSBSS(int32 DesiredIndex)
{
    AActor* Owner = GetOwner();
    if (!Owner) return false;

    UWorld* W = Owner->GetWorld();
    if (!W) return false;

    auto* SBSS = W->GetSubsystem<USmartBunkerSelectionSubsystem>();
    if (!SBSS) return false;

    TArray<FBunkerCandidate> Top;
    SBSS->GetTopK(Owner, 3, Top);
    if (!Top.IsValidIndex(DesiredIndex)) return false;

    return EnterCover(Top[DesiredIndex].Bunker, Top[DesiredIndex].SlotIndex);
}

bool UBunkerCoverComponent::EnterCover(ABunkerBase* Bunker, int32 SlotIndex)
{
    if (!Bunker) return false;
    if (!Bunker->TryClaimSlot(SlotIndex, GetOwner())) return false;

    CurrentBunker = Bunker;
    CurrentSlot = SlotIndex;

    FTransform SlotXf; FVector Normal;
    if (!ResolveSlotXf(SlotXf, Normal))
    {
        ExitCover();
        return false;
    }

    ApplyHugPose(SlotXf, Normal);
    State = ECoverState::Hug;
    LeanAxis = 0.f;
    CurrentLean = 0.f;
    return true;
}

void UBunkerCoverComponent::ExitCover()
{
    if (CurrentBunker.IsValid() && CurrentSlot != INDEX_NONE)
    {
        CurrentBunker->ReleaseSlot(CurrentSlot, GetOwner());
    }
    CurrentBunker.Reset();
    CurrentSlot = INDEX_NONE;
    State = ECoverState::None;
}

void UBunkerCoverComponent::SetLeanAxis(float Axis)
{
    LeanAxis = FMath::Clamp(Axis, -1.f, 1.f);
    if (State != ECoverState::None) State = (FMath::IsNearlyZero(LeanAxis)) ? ECoverState::Hug : ECoverState::Peek;
}

void UBunkerCoverComponent::ApplyHugPose(const FTransform& SlotXf, const FVector& Normal)
{
    ACharacter* Char = Cast<ACharacter>(GetOwner());
    if (!Char) return;

    float Radius, HalfHeight;
    Char->GetCapsuleComponent()->GetScaledCapsuleSize(Radius, HalfHeight);

    const FVector SlotLoc = SlotXf.GetLocation();
    const FVector Up = FVector::UpVector;

    // Position: pull back along normal so the capsule touches cover
    const FVector HugLoc = SlotLoc - Normal * (HugBackOffset + Radius * 0.6f);

    // Face along -Normal (into the bunker) to make side = cross(Up, Normal)
    const FVector FaceDir = (-Normal).GetSafeNormal();
    const FRotator HugRot = FaceDir.Rotation();

    Char->TeleportTo(HugLoc, HugRot, /*bIsATest=*/false, /*bNoCheck=*/true);
    BaseLoc = HugLoc;
    BaseRot = HugRot;
}

void UBunkerCoverComponent::ApplyLean(float DeltaTime, const FTransform& SlotXf, const FVector& Normal)
{
    if (State == ECoverState::None) return;

    ACharacter* Char = Cast<ACharacter>(GetOwner());
    if (!Char) return;

    // Smooth the lean
    CurrentLean = FMath::FInterpTo(CurrentLean, LeanAxis, DeltaTime, LeanSpeed);

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

    ApplyLean(DeltaTime, SlotXf, Normal);

    // Optional: draw a tiny marker at the slot
    // DrawDebugSphere(GetWorld(), SlotXf.GetLocation(), 8.f, 8, FColor::Cyan, false, 0.f, 0, 1.f);
}
