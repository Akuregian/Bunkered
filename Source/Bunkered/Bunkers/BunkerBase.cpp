// Bunkers/BunkerBase.cpp
#include "BunkerBase.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/ArrowComponent.h"
#include "Engine/Engine.h"
#include "Misc/DataValidation.h"

ABunkerBase::ABunkerBase()
{
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent = Root;

    Bunker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
    Bunker->SetupAttachment(RootComponent);
    Bunker->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Bunker->SetCanEverAffectNavigation(false);
}

int32 ABunkerBase::FindClosestValidSlot(const FVector& WorldLocation, float MaxDist, int32& OutExactIndex) const
{
    OutExactIndex = INDEX_NONE;

    const float MaxDistSq = FMath::Square(MaxDist);
    float BestSq = MaxDistSq;
    int32 BestIdx = INDEX_NONE;

    for (int32 i = 0; i < Slots.Num(); ++i)
    {
        const FTransform WT = GetSlotWorldTransform(i);
        const float DistSq = FVector::DistSquared(WT.GetLocation(), WorldLocation);
        if (DistSq < BestSq)
        {
            BestSq = DistSq;
            BestIdx = i;
        }
    }

    OutExactIndex = BestIdx;
    return BestIdx;
}

FTransform ABunkerBase::GetSlotWorldTransform(int32 SlotIndex) const
{
    check(Slots.IsValidIndex(SlotIndex));
    const FCoverSlot& Slot = Slots[SlotIndex];

    if (Slot.bUseComponentTransform)
    {
        // Resolve the referenced component on this actor
        if (UActorComponent* AC = Slot.SlotPoint.GetComponent(const_cast<ABunkerBase*>(this)))
        {
            if (USceneComponent* SC = Cast<USceneComponent>(AC))
            {
                return SC->GetComponentTransform();
            }
        }
    }

    // Fallback: local anchor relative to bunker actor
    return Slot.LocalAnchor * GetActorTransform();
}

#if WITH_EDITOR

static void LogEditorNote(const FString& Msg)
{
#if !NO_LOGGING
    UE_LOG(LogTemp, Warning, TEXT("%s"), *Msg);
#endif
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Yellow, Msg);
    }
}

void ABunkerBase::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    // 1) Optional: auto-tag Arrow children so designers don’t forget
    if (bAutoTagArrowChildren)
    {
        TArray<UArrowComponent*> Arrows;
        GetComponents<UArrowComponent>(Arrows);
        for (UArrowComponent* Arrow : Arrows)
        {
            if (!Arrow) continue;
            if (!SlotTag.IsNone() && !Arrow->ComponentHasTag(SlotTag))
            {
                Arrow->ComponentTags.Add(SlotTag);
            }
        }
    }

    // 2) Auto-sync slots array from tagged children
    if (bAutoSyncSlotsFromChildren)
    {
        RebuildSlotsFromChildren();
    }
}

void ABunkerBase::TagAllArrowChildrenAsSlots()
{
    TArray<UArrowComponent*> Arrows;
    GetComponents<UArrowComponent>(Arrows);

    int32 Tagged = 0;
    for (UArrowComponent* Arrow : Arrows)
    {
        if (!Arrow) continue;
        if (!SlotTag.IsNone() && !Arrow->ComponentHasTag(SlotTag))
        {
            Arrow->ComponentTags.Add(SlotTag);
            ++Tagged;
        }
    }

    LogEditorNote(FString::Printf(TEXT("[Bunker] Tagged %d Arrow component(s) with %s"), Tagged, *SlotTag.ToString()));
}

void ABunkerBase::RebuildSlotsFromChildren()
{
    TArray<USceneComponent*> ChildrenSlots;
    GetComponents<USceneComponent>(ChildrenSlots);

    TArray<FCoverSlot> NewSlots;
    bool bFoundTagged = false;

    // First pass: tagged components only
    if (!SlotTag.IsNone())
    {
        for (USceneComponent* ChildSlot : ChildrenSlots)
        {
            if (!ChildSlot || ChildSlot == RootComponent) continue;
            if (ChildSlot->ComponentHasTag(SlotTag))
            {
                bFoundTagged = true;

                FCoverSlot S;
                S.SlotPoint.OtherActor = this;               // resolve on self
                S.SlotPoint.ComponentProperty = ChildSlot->GetFName();
                S.bUseComponentTransform = true;

                // Sensible defaults (tweak as needed)
                S.AllowedStances = { ECoverStance::Crouch };
                S.AllowedPeeks   = {};    // empty = all peeks allowed
                S.EntryRadius    = 75.f;
                S.SlotName       = ChildSlot->GetFName();

                NewSlots.Add(S);
            }
        }
    }

    // Fallback: if none tagged but allowed to fallback, harvest all Arrow children
    if (!bFoundTagged && bFallbackUseAllArrowChildren)
    {
        TArray<UArrowComponent*> Arrows;
        GetComponents<UArrowComponent>(Arrows);
        for (UArrowComponent* Arrow : Arrows)
        {
            if (!Arrow) continue;

            FCoverSlot S;
            S.SlotPoint.OtherActor = this;
            S.SlotPoint.ComponentProperty = Arrow->GetFName();
            S.bUseComponentTransform = true;

            S.AllowedStances = { ECoverStance::Crouch };
            S.AllowedPeeks   = {};
            S.EntryRadius    = 75.f;
            S.SlotName       = Arrow->GetFName();

            NewSlots.Add(S);
        }

        if (Arrows.Num() > 0)
        {
            LogEditorNote(TEXT("[Bunker] No tagged components; harvested all Arrow children as slots (fallback)."));
        }
    }

    Slots = MoveTemp(NewSlots);

    if (Slots.Num() == 0)
    {
        LogEditorNote(TEXT("[Bunker] No slot sources found. Add child ArrowComponents and tag them with CoverSlot, or enable fallback/auto-tag."));
    }
}

EDataValidationResult ABunkerBase::IsDataValid(FDataValidationContext& Context) const
{
    // Validate that the bunker will produce usable slots
    TArray<USceneComponent*> ChildrenSlots;
    const_cast<ABunkerBase*>(this)->GetComponents<USceneComponent>(ChildrenSlots);

    bool bHasTagged = false;
    bool bHasArrows = false;

    for (USceneComponent* ChildSlot : ChildrenSlots)
    {
        if (!ChildSlot || ChildSlot == RootComponent) continue;
        if (!SlotTag.IsNone() && ChildSlot->ComponentHasTag(SlotTag))
        {
            bHasTagged = true;
            break;
        }
    }

    if (!bHasTagged)
    {
        TArray<UArrowComponent*> Arrows;
        const_cast<ABunkerBase*>(this)->GetComponents<UArrowComponent>(Arrows);
        bHasArrows = (Arrows.Num() > 0);
    }

    if (!bHasTagged && !(bFallbackUseAllArrowChildren && bHasArrows))
    {
        Context.AddError(FText::FromString(TEXT("Bunker has no tagged slot components and fallback is disabled (or no Arrow children found).")));
        return EDataValidationResult::Invalid;
    }

    if (Slots.Num() == 0)
    {
        Context.AddWarning(FText::FromString(("Bunker Slots array is empty. Use 'Rebuild Slots From Children' or enable Auto Sync.")));
        return EDataValidationResult::Valid;
    }

    return EDataValidationResult::Valid;
}
#endif // WITH_EDITOR
