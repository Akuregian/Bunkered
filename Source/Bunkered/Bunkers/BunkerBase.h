#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BunkerBase.generated.h"

class USplineComponent;
class UBoxComponent;
class ABunkerBase; // self

UENUM(BlueprintType)
enum class ECoverStance : uint8 { Stand, Crouch, Prone };

/** Designer-authored cover slot description */
USTRUCT(BlueprintType)
struct FBunkerCoverSlot
{
	GENERATED_BODY()

	/** Assign a child SceneComponent (e.g., ArrowComponent) in the editor to mark this slot. */
	UPROPERTY(EditDefaultsOnly, Category="Cover Slot")
	FComponentReference SlotPoint;

	/** Desired stance for this slot. */
	UPROPERTY(EditAnywhere, Category="Cover Slot")
	ECoverStance SlotCoverPose = ECoverStance::Stand;

	/** Right-side or left-side orientation (designer semantics only). */
	UPROPERTY(EditAnywhere, Category="Cover Slot")
	bool bRightSide = false;

	/** Designer label to help identify slots in the editor. */
	UPROPERTY(EditAnywhere, Category="Cover Slot")
	FName SlotName = NAME_None;

	/** If true, someone is occupying this slot (temporary fallback). */
	UPROPERTY(VisibleAnywhere, Category="Runtime")
	bool bOccupied = false;

	/** Which actor currently holds this slot (for safety checks). */
	UPROPERTY(VisibleAnywhere, Category="Runtime")
	TWeakObjectPtr<AActor> ClaimedBy;

	/** Lateral/vertical peek hints (future). */
	UPROPERTY(EditAnywhere, Category="Cover Slot", meta=(ClampMin="0.0", UIMin="0.0"))
	float PeekLateral = 35.f;

	UPROPERTY(EditAnywhere, Category="Cover Slot", meta=(ClampMin="0.0", UIMin="0.0"))
	float PeekVertical = 15.f;
};

UCLASS(Blueprintable)
class BUNKERED_API ABunkerBase : public AActor
{
	GENERATED_BODY()

public:
	ABunkerBase();

	// ---- Accessors ----
	UFUNCTION(BlueprintCallable, Category="Bunker")
	int32 GetSlotCount() const { return Slots.Num(); }

	UFUNCTION(BlueprintCallable, Category="Bunker")
	ECoverStance GetSlotPose(int32 Index) const { return Slots.IsValidIndex(Index) ? Slots[Index].SlotCoverPose : ECoverStance::Stand; }

	UFUNCTION(BlueprintCallable, Category="Bunker")
	bool IsSlotOccupied(int32 Index) const;

	UFUNCTION(BlueprintCallable, Category="Bunker")
	FTransform GetSlotWorldTransform(int32 Index) const;

	UFUNCTION(BlueprintCallable, Category="Bunker")
	FVector GetSlotNormal(int32 Index) const;

	/** Try to claim a slot. Returns false if invalid or already occupied by someone else. */
	UFUNCTION(BlueprintCallable, Category="Bunker")
	bool ClaimSlot(int32 Index, AActor* Claimant);

	/** Release a claimed slot. Only the current claimant can release. */
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

	/** Colors for free/occupied debug. */
	UPROPERTY(EditAnywhere, Category="Debug")
	FColor FreeColor = FColor::Green;
	UPROPERTY(EditAnywhere, Category="Debug")
	FColor OccupiedColor = FColor::Red;

	/** How long to keep lines in the world (0 = one frame). */
	UPROPERTY(EditAnywhere, Category="Debug")
	float DebugDuration = 0.f;

	void DrawDebug() const;
};