// Components/BunkerCoverComponent.cpp
#include "BunkerCoverComponent.h"
#include "Bunkers/BunkerBase.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
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
        OnRep_Bunker(); OnRep_Slot(); OnRep_StanceExposure(); OnRep_Peek();
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
    if (!IsInCover() || Delta == 0) return false;

    if (GetOwner()->HasAuthority())
    {
        const int32 Num = CurrentBunker->GetNumSlots();
        int32 NewIdx = FMath::Clamp(CurrentSlotIndex + Delta, 0, Num - 1);

        if (NewIdx != CurrentSlotIndex)
        {
            CurrentSlotIndex = NewIdx;
            Exposure = EExposureState::Hidden;
            Peek = EPeekDirection::None;

            if (!IsStanceAllowedAtSlot(Stance, CurrentSlotIndex))
            {
                const FCoverSlot& Slot = CurrentBunker->GetSlot(CurrentSlotIndex);
                Stance = Slot.AllowedStances.Num() ? Slot.AllowedStances[0] : ECoverStance::Crouch;
            }

            SnapOwnerToSlot();
            OnRep_Slot(); OnRep_StanceExposure(); OnRep_Peek();
            return true;
        }
        return false;
    }
    else
    {
        Server_RequestSlotMoveRelative(Delta);
        return true;
    }
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

    if (GetOwner()->HasAuthority())
    {
        Peek = bEnable ? Direction : EPeekDirection::None;
        Exposure = bEnable ? EExposureState::Peeking : EExposureState::Hidden;

        OnRep_Peek(); OnRep_StanceExposure();
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
    if (!OwnerCharacter.IsValid() || !CurrentBunker) return;
    const FTransform WT = CurrentBunker->GetSlotWorldTransform(CurrentSlotIndex);

    FRotator NewRot = WT.GetRotation().Rotator();
    NewRot.Pitch = 0.f; NewRot.Roll = 0.f;

    OwnerCharacter->SetActorLocation(WT.GetLocation());
    OwnerCharacter->SetActorRotation(NewRot);
    // TODO: notify anim system via tags / montage
}

void UBunkerCoverComponent::OnRep_Bunker() { }
void UBunkerCoverComponent::OnRep_Slot()    { OnSlotChanged.Broadcast(CurrentSlotIndex); }
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
void UBunkerCoverComponent::OnRep_Peek() { OnPeekChanged.Broadcast(Peek, Peek != EPeekDirection::None); }

// === RPC impls ===
void UBunkerCoverComponent::Server_TryEnterCover_Implementation(ABunkerBase* Bunker, int32 SlotIndex){ TryEnterCover(Bunker, SlotIndex); }
void UBunkerCoverComponent::Server_ExitCover_Implementation(){ ExitCover(); }
void UBunkerCoverComponent::Server_RequestSlotMoveRelative_Implementation(int32 Delta){ RequestSlotMoveRelative(Delta); }
void UBunkerCoverComponent::Server_SetStance_Implementation(ECoverStance NewStance){ SetStance(NewStance); }
void UBunkerCoverComponent::Server_SetPeek_Implementation(EPeekDirection Direction, bool bEnable){ SetPeek(Direction, bEnable); }
void UBunkerCoverComponent::Server_SetExposure_Implementation(EExposureState NewExposure){ SetExposureState(NewExposure); }
