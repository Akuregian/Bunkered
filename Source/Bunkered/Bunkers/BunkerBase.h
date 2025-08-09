// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SmartObjectComponent.h"
#include "Engine/EngineTypes.h"
#include "Components/BunkerCoverComponent.h"
#include "BunkerBase.generated.h"

class UBunkerMetaData;
enum class ECoverStance : uint8;

/*
 * Base class for all bunkers in the game
 * they should all derive from this class.
 */

class USplineComponent;
class UBoxComponent;

USTRUCT(BlueprintType)
struct FBunkerCoverSlot {
	GENERATED_BODY()
	
	/** Assign a child SceneComponent (e.g., ArrowComponent) in the editor to mark this slot. */
	UPROPERTY(EditDefaultsOnly, Category="Cover Slot")
	FComponentReference SlotPoint;

	UPROPERTY(EditAnywhere, Category="Cover Slot")
	ECoverStance SlotCoverPose = ECoverStance::Stand;

	/** Right-side or left-side orientation (designer semantics only). */
	UPROPERTY(EditAnywhere, Category="Cover Slot")
	bool bRightSide = false;

	/** Designer label to help you identify in the editor. */
	UPROPERTY(EditAnywhere, Category="Cover Slot")
	FName SlotName = NAME_None;

	/** If true, someone is occupying this slot (temporary until SmartObjects hook-up). */
	UPROPERTY(VisibleAnywhere, Category="Runtime")
	bool bOccupied = false;

	/** Lateral peek offset (in cm) applied during lean checks (for future use). */
	UPROPERTY(EditAnywhere, Category="Cover Slot", meta=(ClampMin="0.0", UIMin="0.0"))
	float PeekLateral = 35.f;

	/** Vertical peek offset (in cm) applied during head peeks (for future use). */
	UPROPERTY(EditAnywhere, Category="Cover Slot", meta=(ClampMin="0.0", UIMin="0.0"))
	float PeekVertical = 15.f;
};

UCLASS(Blueprintable)
class BUNKERED_API ABunkerBase : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ABunkerBase();
	
	// ---- Accessors ----
	UFUNCTION(BlueprintCallable, Category="Bunker")
	int32 GetSlotCount() const { return Slots.Num(); }
	
	// returns the Slots Default Pose ie. Stand, Crouch, Prone
	ECoverStance GetSlotPose(int32 Index) const { return Slots.IsValidIndex(Index) ? Slots[Index].SlotCoverPose : ECoverStance::Stand; }

	UFUNCTION(BlueprintCallable, Category="Bunker")
	bool IsSlotOccupied(int32 Index) const;

	UFUNCTION(BlueprintCallable, Category="Bunker")
	FTransform GetSlotWorldTransform(int32 Index) const;

	UFUNCTION(BlueprintCallable, Category="Bunker")
	FVector GetSlotNormal(int32 Index) const;

	/** Try to claim a slot. Returns false if invalid or already occupied. */
	UFUNCTION(BlueprintCallable, Category="Bunker")
	bool TryClaimSlot(int32 Index, AActor* Claimant);

	/** Release a claimed slot (no validation yet). */
	UFUNCTION(BlueprintCallable, Category="Bunker")
	void ReleaseSlot(int32 Index, AActor* Claimant);

	/** For quick testing: finds the nearest free slot to a given point. Returns -1 if none. */
	int32 FindNearestFreeSlot(const FVector& FromLocation, float MaxDist = 5000.f) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
#if WITH_EDITOR
	virtual void OnConstruction(const FTransform& Transform) override;
#endif

private:
	UPROPERTY(VisibleAnywhere, Category="Components")
	TObjectPtr<USceneComponent> RootSceneComponent;

	UPROPERTY(EditAnywhere, Category="Components")
	TObjectPtr<UStaticMeshComponent> BunkerMesh;

public:
	/** Place child SceneComponents (e.g., ArrowComponents) and assign them here. */
	UPROPERTY(EditAnywhere, Category="Bunker")
	TArray<FBunkerCoverSlot> Slots;

	/** Toggle to visualize slot positions/normals in PIE. */
	UPROPERTY(EditAnywhere, Category="Debug")
	bool bDrawDebug = true;

	/** Color for free/occupied slots. */
	UPROPERTY(EditAnywhere, Category="Debug")
	FColor FreeColor = FColor::Green;
	UPROPERTY(EditAnywhere, Category="Debug")
	FColor OccupiedColor = FColor::Red;

	/** How long to keep lines in the world (0 = one frame). */
	UPROPERTY(EditAnywhere, Category="Debug")
	float DebugDuration = 0.f;

	void DrawDebug() const;

};
