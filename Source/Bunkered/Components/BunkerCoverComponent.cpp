// Components/BunkerCoverComponent.cpp
#include "BunkerCoverComponent.h"
#include "Bunkers/BunkerBase.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Net/UnrealNetwork.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Utility/LoggingMacros.h"

UBunkerCoverComponent::UBunkerCoverComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
}

void UBunkerCoverComponent::BeginPlay()
{
    Super::BeginPlay();
    OwnerCharacter = Cast<ACharacter>(GetOwner());
}

void UBunkerCoverComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    
    DOREPLIFETIME(UBunkerCoverComponent, CurrentBunker);
    DOREPLIFETIME(UBunkerCoverComponent, CurrentSlotIndex);
    DOREPLIFETIME(UBunkerCoverComponent, Stance);
    DOREPLIFETIME(UBunkerCoverComponent, Exposure);
    DOREPLIFETIME(UBunkerCoverComponent, Peek);
}

bool UBunkerCoverComponent::IsStanceAllowedAtSlot(ECoverStance InStance, int32 SlotIndex) const
{
    if (!CurrentBunker || !CurrentBunker->GetNumSlots() || !ensure(CurrentBunker->GetNumSlots() > SlotIndex)) return false;
    const FCoverSlot& Slot = CurrentBunker->GetSlot(SlotIndex);
    return Slot.AllowedStances.Contains(InStance);
}

bool UBunkerCoverComponent::IsPeekAllowedAtSlot(EPeekDirection InPeek, int32 SlotIndex) const
{
    if (InPeek == EPeekDirection::None) return true;
    if (!CurrentBunker || !ensure(CurrentBunker->GetNumSlots() > SlotIndex)) return false;
    const FCoverSlot& Slot = CurrentBunker->GetSlot(SlotIndex);
    return Slot.AllowedPeeks.Num() == 0 || Slot.AllowedPeeks.Contains(InPeek);
}

bool UBunkerCoverComponent::TryEnterCover(ABunkerBase* Bunker, int32 SlotIndex)
{
    if (!OwnerCharacter.IsValid() || !Bunker || !Bunker->GetNumSlots() || !Bunker->GetSlot(SlotIndex).EntryRadius)
        return false;

    if (GetOwner()->HasAuthority())
    {
        CurrentBunker = Bunker;
        CurrentSlotIndex = SlotIndex;

        const FCoverSlot& Slot = Bunker->GetSlot(SlotIndex);
        Stance = Slot.AllowedStances.Num() ? Slot.AllowedStances[0] : ECoverStance::Crouch;
        Exposure = EExposureState::Hidden;
        Peek = EPeekDirection::None;

        SnapOwnerToSlot();
        
        OnRep_Bunker();
        OnRep_Slot();
        OnRep_StanceExposure();
        //OnRep_Peek();
        return true;
    }
    else
    {
        Server_TryEnterCover(Bunker, SlotIndex);
        return true;
    }
}

void UBunkerCoverComponent::ExitCover()
{
    if (GetOwner()->HasAuthority())
    {
        CurrentBunker = nullptr;
        CurrentSlotIndex = INDEX_NONE;
        Exposure = EExposureState::Hidden;
        Peek = EPeekDirection::None;
        OnRep_Bunker(); OnRep_Slot(); OnRep_StanceExposure(); OnRep_Peek();
    }
    else
    {
        Server_ExitCover();
    }
}

bool UBunkerCoverComponent::RequestSlotMoveRelative(int32 Delta)
{
    DEBUG(5.0f, FColor::Black, TEXT("(1) UBunkerCoverComponent::RequestSlotMoveRelative()"));
    // return if not in cover
    if (!IsInCover() || Delta == 0) return false;

    // Server
    if (GetOwner()->HasAuthority())
    {
        const int32 SlotNum = CurrentBunker->GetNumSlots();
        int32 NewSlotIndex = FMath::Clamp(CurrentSlotIndex + Delta, 0, SlotNum - 1);

        // Transition possible
        if (NewSlotIndex != CurrentSlotIndex)
        {
            CurrentSlotIndex = NewSlotIndex; // update CurrentSlotIndex to new Slot
            Exposure = EExposureState::Hidden; // Exposure: Hidden
            Peek = EPeekDirection::None; // reset peek direction

            if (!IsStanceAllowedAtSlot(Stance, CurrentSlotIndex))
            {
                const FCoverSlot& Slot = CurrentBunker->GetSlot(CurrentSlotIndex);
                Stance = Slot.AllowedStances.Num() ? Slot.AllowedStances[0] : ECoverStance::Crouch;
            }

            SnapOwnerToSlot();

            // server
            OnRep_Slot();
            OnRep_StanceExposure();
            OnRep_Peek();
            
            return true;
        }
        
        // === No transition available — check for peeking instead ===
        const FCoverSlot& Slot = CurrentBunker->GetSlot(CurrentSlotIndex);
        EPeekDirection DesiredPeek = (Delta > 0) ? EPeekDirection::Right : EPeekDirection::Left;
        bool bIsSnakeSlot = Slot.AllowedStances.Num() == 1 && Slot.AllowedStances[0] == ECoverStance::Prone;

        // Prevent inward peeking (only allow peeking outward if we're on an end slot)
        const bool bIsEndSlot =
            (CurrentSlotIndex == 0 && Delta < 0) || (CurrentSlotIndex == SlotNum - 1 && Delta > 0);

        // Peek if at end OR if snake-style slot
        bool bAllowPeek = bIsEndSlot || bIsSnakeSlot;
        if (bAllowPeek && IsPeekAllowedAtSlot(DesiredPeek, CurrentSlotIndex))
        {
            SetPeek(DesiredPeek, true);
            return true;
        }
    }
    else
    {
        if (Peek == EPeekDirection::None)
        {
            Server_RequestSlotMoveRelative(Delta);
        }
        return true;
    }
    
    return false;
}

bool UBunkerCoverComponent::SetStance(ECoverStance NewStance)
{
    if (!IsInCover()) return false;
    if (!IsStanceAllowedAtSlot(NewStance, CurrentSlotIndex)) return false;

    if (GetOwner()->HasAuthority())
    {
        Stance = NewStance;
        OnRep_StanceExposure();
        return true;
    }
    else
    {
        Server_SetStance(NewStance);
        return true;
    }
}

bool UBunkerCoverComponent::SetPeek(EPeekDirection Direction, bool bEnable)
{
    if (!IsInCover()) return false;
    if (!IsPeekAllowedAtSlot(Direction, CurrentSlotIndex)) return false;

    DEBUG(5.0f, FColor::Blue, TEXT("UBunkerCoverComponent::SetPeek: %s"), *StaticEnum<EPeekDirection>()->GetNameStringByValue(static_cast<int64>(Direction)));

    if (GetOwner()->HasAuthority())
    {
        Peek = bEnable ? Direction : EPeekDirection::None;
        Exposure = bEnable ? EExposureState::Peeking : EExposureState::Hidden;
        
        OnRep_Peek();
        OnRep_StanceExposure();
        return true;
    }
    else
    {
        Server_SetPeek(Direction, bEnable);
        return true;
    }
}

bool UBunkerCoverComponent::SetExposureState(EExposureState NewExposure)
{
    if (!IsInCover()) return false;

    if (GetOwner()->HasAuthority())
    {
        Exposure = NewExposure;
        if (Exposure == EExposureState::Hidden) { Peek = EPeekDirection::None; }
        OnRep_StanceExposure();
        return true;
    }
    else
    {
        Server_SetExposure(NewExposure);
        return true;
    }
}

void UBunkerCoverComponent::SnapOwnerToSlot()
{
    DEBUG(5.0f, FColor::Yellow, TEXT("(2) Snapping owner to Slot: [%i]"), CurrentSlotIndex);
    
    if (!OwnerCharacter.IsValid() || !CurrentBunker) return;
    const FTransform WT = CurrentBunker->GetSlotWorldTransform(CurrentSlotIndex);

    FRotator NewRot = WT.GetRotation().Rotator();
    NewRot.Pitch = 0.f; NewRot.Roll = 0.f;

    OwnerCharacter->SetActorLocation(WT.GetLocation());
    OwnerCharacter->SetActorRotation(NewRot);
    
    // TODO: notify anim system via tags / montage
}

void UBunkerCoverComponent::OnRep_Bunker() { }
void UBunkerCoverComponent::OnRep_Slot()
{
    OnSlotChanged.Broadcast(CurrentSlotIndex);

    DEBUG(5.0f, FColor::Magenta, TEXT("Broadcasting -> OnSlotChanged: [%i]"), CurrentSlotIndex);
}
void UBunkerCoverComponent::OnRep_StanceExposure()
{
    OnStanceChanged.Broadcast(Stance, Exposure);

    const FString StanceName   = StaticEnum<ECoverStance>()
        ? StaticEnum<ECoverStance>()->GetDisplayNameTextByValue(static_cast<int64>(Stance)).ToString()
        : TEXT("Invalid");
    const FString ExposureName = StaticEnum<EExposureState>()
        ? StaticEnum<EExposureState>()->GetDisplayNameTextByValue(static_cast<int64>(Exposure)).ToString()
        : TEXT("Invalid");

    DEBUG(5.0f, FColor::Cyan, TEXT("[Cover] %s Stance=%s Exposure=%s"),
        *GetNameSafe(GetOwner()), *StanceName, *ExposureName);
}
void UBunkerCoverComponent::OnRep_Peek()
{
   OnPeekChanged.Broadcast(Peek, Peek != EPeekDirection::None);

    const FCoverSlot& Slot = CurrentBunker->GetSlot(CurrentSlotIndex);
    FVector LocalOffset = FVector::ZeroVector; // slot-local (X fwd, Y right, Z up)

    switch (Peek)
    {
    case EPeekDirection::Left:  LocalOffset.Y = -Slot.LateralPeekOffset; break;
    case EPeekDirection::Right: LocalOffset.Y = +Slot.LateralPeekOffset; break;
    case EPeekDirection::Over:  LocalOffset.Z = +Slot.VerticalPeekOffset; break;
    default: break;
    }

    if (OwnerCharacter.IsValid())
    {
        // 1) Camera: shift sideways/up using SpringArm socket offset (local to arm)
        if (USpringArmComponent* Boom = OwnerCharacter->FindComponentByClass<USpringArmComponent>())
        {
            // X is forward/back along arm; Y is lateral; Z is vertical for socket offset
            const FVector SocketOffset = (Peek == EPeekDirection::None)
                ? FVector::ZeroVector
                : FVector(0.f, LocalOffset.Y, LocalOffset.Z);

            Boom->SocketOffset = SocketOffset;

            // 2) Camera lean: small roll for visual feedback
            const float LeanRollDeg =
                (Peek == EPeekDirection::Left)  ? -10.f :
                (Peek == EPeekDirection::Right) ? +10.f : 0.f;

            FRotator R = Boom->GetRelativeRotation();
            R.Roll = LeanRollDeg;
            Boom->SetRelativeRotation(R);
        }

        const FTransform SlotWT = CurrentBunker->GetSlotWorldTransform(CurrentSlotIndex);
        const FVector WorldOffset = SlotWT.TransformVector(LocalOffset); // rotate by slot yaw
        const FVector Target = SlotWT.GetLocation() + WorldOffset;

        // Use a swept move to respect collisions
        OwnerCharacter->SetActorLocation(Target, /*bSweep=*/true);
    }

    // ---- Debug (designer aid): Slot -> Peek line & sphere (already good) ----
    if (CurrentBunker)
    {
        const FTransform SlotWT = CurrentBunker->GetSlotWorldTransform(CurrentSlotIndex);
        const FVector PeekPointWS = SlotWT.TransformPosition(LocalOffset);
        const FVector SlotWS      = SlotWT.GetLocation();

        DrawDebugLine(GetWorld(), SlotWS, PeekPointWS, FColor::Red, false, 5.f, 0, 2.f);
        DrawDebugSphere(GetWorld(), PeekPointWS, 5.f, 12, FColor::Green, false, 5.f);
    }

    DEBUG(5.0f, FColor::Emerald, TEXT("OnRep_Peek --><-- Peek=%s OffsetLocal=%s"),
        *StaticEnum<EPeekDirection>()->GetNameStringByValue((int64)Peek),
        *LocalOffset.ToString());}

// === RPC impls ===
void UBunkerCoverComponent::Server_TryEnterCover_Implementation(ABunkerBase* Bunker, int32 SlotIndex){ TryEnterCover(Bunker, SlotIndex); }
void UBunkerCoverComponent::Server_ExitCover_Implementation(){ ExitCover(); }
void UBunkerCoverComponent::Server_RequestSlotMoveRelative_Implementation(int32 Delta){ RequestSlotMoveRelative(Delta); }
void UBunkerCoverComponent::Server_SetStance_Implementation(ECoverStance NewStance){ SetStance(NewStance); }
void UBunkerCoverComponent::Server_SetPeek_Implementation(EPeekDirection Direction, bool bEnable){ SetPeek(Direction, bEnable); }
void UBunkerCoverComponent::Server_SetExposure_Implementation(EExposureState NewExposure){ SetExposureState(NewExposure); }
