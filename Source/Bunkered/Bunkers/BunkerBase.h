// Bunkers/BunkerBase.h
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Types/CoverTypes.h"
#include "BunkerBase.generated.h"

UCLASS(Blueprintable)
class BUNKERED_API ABunkerBase : public AActor
{
    GENERATED_BODY()

public:
    ABunkerBase();

    UFUNCTION(BlueprintCallable, Category="Cover")
    int32 FindClosestValidSlot(const FVector& WorldLocation, float MaxDist, int32& OutExactIndex) const;

    UFUNCTION(BlueprintCallable, Category="Cover")
    FTransform GetSlotWorldTransform(int32 SlotIndex) const;

    UFUNCTION(BlueprintCallable, Category="Cover")
    const FCoverSlot& GetSlot(int32 SlotIndex) const { return Slots[SlotIndex]; }

    UFUNCTION(BlueprintCallable, Category="Cover")
    int32 GetNumSlots() const { return Slots.Num(); }

#if WITH_EDITOR
    /** Scans child components with SlotTag and rebuilds Slots to reference them. */
    UFUNCTION(CallInEditor, Category="Cover|Authoring")
    void RebuildSlotsFromChildren();

    /** Adds SlotTag to all Arrow children (non-destructive; only adds if missing). */
    UFUNCTION(CallInEditor, Category="Cover|Authoring")
    void TagAllArrowChildrenAsSlots();
#endif

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    USceneComponent* Root;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    UStaticMeshComponent* Bunker;

    /** Designer-authored cover slots (may reference child components). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cover")
    TArray<FCoverSlot> Slots;
    
    /** If true, OnConstruction will auto-rebuild slots from tagged children. */
    UPROPERTY(EditAnywhere, Category="Cover|Authoring")
    bool bAutoSyncSlotsFromChildren = true;

    /** Tag used to identify child components that should act as slots. */
    UPROPERTY(EditAnywhere, Category="Cover|Authoring")
    FName SlotTag = TEXT("CoverSlot");

    /** If no tagged components are found, also harvest ALL Arrow children as slots. */
    UPROPERTY(EditAnywhere, Category="Cover|Authoring")
    bool bFallbackUseAllArrowChildren = true;

    /** Also auto-tag any Arrow children that are missing the tag. */
    UPROPERTY(EditAnywhere, Category="Cover|Authoring")
    bool bAutoTagArrowChildren = true;

#if WITH_EDITOR
    virtual void OnConstruction(const FTransform& Transform) override;

    /** Editor data validation hook (Window → Developer Tools → Data Validation). */
    virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
