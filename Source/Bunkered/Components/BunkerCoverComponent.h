// Components/BunkerCoverComponent.h
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Types/CoverTypes.h"
#include "BunkerCoverComponent.generated.h"

class ABunkerBase;
class ACharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCoverSlotChanged, int32, NewSlotIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCoverStanceChanged, ECoverStance, NewStance, EExposureState, NewExposure);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCoverPeekChanged, EPeekDirection, NewPeek, bool, bIsPeeking);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class BUNKERED_API UBunkerCoverComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UBunkerCoverComponent();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // === Public API ===
    UFUNCTION(BlueprintCallable, Category="Cover")
    bool TryEnterCover(ABunkerBase* Bunker, int32 SlotIndex);

    UFUNCTION(BlueprintCallable, Category="Cover")
    void ExitCover();

    UFUNCTION(BlueprintCallable, Category="Cover")
    bool RequestSlotMoveRelative(int32 Delta); // e.g. -1 left / +1 right

    UFUNCTION(BlueprintCallable, Category="Cover")
    bool SetStance(ECoverStance NewStance);

    UFUNCTION(BlueprintCallable, Category="Cover")
    bool SetPeek(EPeekDirection Direction, bool bEnable);

    UFUNCTION(BlueprintCallable, Category="Cover")
    bool SetExposureState(EExposureState NewExposure);

    UFUNCTION(BlueprintPure, Category="Cover")
    bool IsInCover() const { return CurrentBunker != nullptr; }

    UFUNCTION(BlueprintPure, Category="Cover")
    ABunkerBase* GetCurrentBunker() const { return CurrentBunker; }

    UFUNCTION(BlueprintPure, Category="Cover")
    int32 GetCurrentSlot() const { return CurrentSlotIndex; }

    UFUNCTION(BlueprintPure, Category="Cover")
    EExposureState GetExposureState() const { return Exposure; }

    UFUNCTION(BlueprintPure, Category="Cover")
    ECoverStance GetStance() const { return Stance; }

    UFUNCTION(BlueprintPure, Category="Cover")
    EPeekDirection GetPeek() const { return Peek; }

    // Convenience wrappers (so existing code in DoMove can call TraverseLeft/Right)
    UFUNCTION(BlueprintCallable, Category="Cover")
    void TraverseLeft()  { RequestSlotMoveRelative(-1); }

    UFUNCTION(BlueprintCallable, Category="Cover")
    void TraverseRight() { RequestSlotMoveRelative(+1); }

    // Delegates for UI/Anim
    UPROPERTY(BlueprintAssignable) FCoverSlotChanged   OnSlotChanged;
    UPROPERTY(BlueprintAssignable) FCoverStanceChanged OnStanceChanged;
    UPROPERTY(BlueprintAssignable) FCoverPeekChanged   OnPeekChanged;

protected:
    virtual void BeginPlay() override;

    // === Server RPCs ===
    UFUNCTION(Server, Reliable) void Server_TryEnterCover(ABunkerBase* Bunker, int32 SlotIndex);
    UFUNCTION(Server, Reliable) void Server_ExitCover();
    UFUNCTION(Server, Reliable) void Server_RequestSlotMoveRelative(int32 Delta);
    UFUNCTION(Server, Reliable) void Server_SetStance(ECoverStance NewStance);
    UFUNCTION(Server, Reliable) void Server_SetPeek(EPeekDirection Direction, bool bEnable);
    UFUNCTION(Server, Reliable) void Server_SetExposure(EExposureState NewExposure);

    // === Replicated State ===
    UPROPERTY(ReplicatedUsing=OnRep_Bunker)
    ABunkerBase* CurrentBunker = nullptr;
    
    UPROPERTY(ReplicatedUsing=OnRep_Slot)
    int32 CurrentSlotIndex = INDEX_NONE;
    
    UPROPERTY(ReplicatedUsing=OnRep_StanceExposure)
    ECoverStance Stance = ECoverStance::Crouch;
    
    UPROPERTY(ReplicatedUsing=OnRep_StanceExposure)
    EExposureState Exposure = EExposureState::Hidden;
    
    UPROPERTY(ReplicatedUsing=OnRep_Peek)
    EPeekDirection Peek = EPeekDirection::None;

    UFUNCTION() void OnRep_Bunker();
    UFUNCTION() void OnRep_Slot();
    UFUNCTION() void OnRep_StanceExposure();
    UFUNCTION() void OnRep_Peek();

private:
    TWeakObjectPtr<ACharacter> OwnerCharacter;

    void SnapOwnerToSlot();

    bool IsStanceAllowedAtSlot(ECoverStance InStance, int32 SlotIndex) const;
    bool IsPeekAllowedAtSlot(EPeekDirection InPeek, int32 SlotIndex) const;
};
