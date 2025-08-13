#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BunkerCoverComponent.generated.h"

struct FBunkerCandidate;         // from SmartBunkerSelectionSubsystem
class UCameraComponent;
class USpringArmComponent;
class ABunkerBase;               // defined in Bunkers module

/** High-level cover state */
UENUM(BlueprintType)
enum class ECoverState : uint8 { None, Approaching, InSlot, Peeking };

/** Policy for choosing new slots while already in cover */
UENUM(BlueprintType)
enum class ECoverSelectionPolicy : uint8
{
	Global         UMETA(ToolTip="Pick from all bunkers"),
	SameBunkerOnly UMETA(ToolTip="Only traverse to free slots on the same bunker")
};

/** How we approach the computed hug transform */
UENUM(BlueprintType)
enum class ECoverApproachMode : uint8 { Nudge, Nav };

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class BUNKERED_API UBunkerCoverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBunkerCoverComponent();

	/** Ask the global subsystem for the best nearby slot and begin approach/entry. */
	UFUNCTION(BlueprintCallable, Category="Cover")
	bool EnterBestCover();

	/** Enter a specific bunker/slot (claims new, safely releases old). */
	UFUNCTION(BlueprintCallable, Category="Cover")
	bool EnterCover(ABunkerBase* Bunker, int32 SlotIndex);

	/** Exit cover, restore camera/movement, and release any claim. */
	UFUNCTION(BlueprintCallable, Category="Cover")
	void ExitCover();

	/** While in cover, traverse to the best free slot on this bunker. */
	UFUNCTION(BlueprintCallable, Category="Cover")
	bool TraverseBestSlotOnCurrentBunker(int32 DesiredIndex = 0);

	/** [-1..1], negative = left. Non-zero sets state to Peeking. */
	UFUNCTION(BlueprintCallable, Category="Cover")
	void SetLeanAxis(float Axis);

	UFUNCTION(BlueprintPure, Category="Cover")
	bool IsInCover() const { return State == ECoverState::InSlot || State == ECoverState::Approaching; }
	
	static FORCEINLINE float CapsuleAwareArrive(const float BaseStop, const float CapsuleRadius)
	{
		return BaseStop + CapsuleRadius * 0.75f;
	}

	// Quick helpers for WASD-style traversal
	UFUNCTION(BlueprintCallable, Category="Cover") bool TraverseLeft();
	UFUNCTION(BlueprintCallable, Category="Cover") bool TraverseRight();

	// Optional suggestion UX (kept, but not required for core flow)
	UFUNCTION(BlueprintCallable, Category="Cover|Select") bool RefreshCoverSuggestions();
	UFUNCTION(BlueprintCallable, Category="Cover|Select") bool SelectNextSuggestion();
	UFUNCTION(BlueprintCallable, Category="Cover|Select") bool SelectPreviousSuggestion();
	UFUNCTION(BlueprintCallable, Category="Cover|Select") bool ConfirmSelectedSuggestion();

	UFUNCTION(BlueprintPure, Category="Cover")
	ECoverState GetCurrentCoverState() const { return State; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTick) override;

private:
	// ---- Current occupancy ----
	UPROPERTY() TWeakObjectPtr<ABunkerBase> CurrentBunker;
	UPROPERTY() int32 CurrentSlot = INDEX_NONE;

	// --- Behavior & timings ---
	UPROPERTY(EditAnywhere, Category="Cover|Tuning",
			  meta=(ToolTip="When selecting a new slot while already in cover:\n• Global = consider all bunkers\n• SameBunkerOnly = only traverse to free slots on the current bunker"))
	ECoverSelectionPolicy SelectionPolicy = ECoverSelectionPolicy::SameBunkerOnly;

	/** Nominal time for lateral slot slides; auto-scaled by distance. */
	UPROPERTY(EditAnywhere, Category="Cover|Tuning", meta=(ClampMin="0.0", Units="s"))
	float SlotTransitionTime = 0.22f;

	// Transition state
	bool  bIsTransitionActive = false;
	float TransitionElapsed = 0.f;
	FVector  StartLoc;   FRotator StartRot;
	FVector  TargetLoc;  FRotator TargetRot;

	// State
	UPROPERTY() ECoverState State = ECoverState::None;

	// Cached base pose at Hug (lean is applied relative to this)
	FVector  BaseLoc = FVector::ZeroVector;
	FRotator BaseRot = FRotator::ZeroRotator;
	float    BaseHalfHeight = 0.f; // capsule half-height captured at BaseLoc

	// Lean
	float DesiredLeanAxis = 0.f;
	float CurrentLean     = 0.f;

	// ------------ Cover | Tuning ----------------
	UPROPERTY(EditAnywhere, Category="Cover|Tuning", meta=(Units="cm")) float FloorSnapUp      = 50.f;
	UPROPERTY(EditAnywhere, Category="Cover|Tuning", meta=(Units="cm")) float FloorSnapDown    = 150.f;
	UPROPERTY(EditAnywhere, Category="Cover|Tuning", meta=(Units="cm")) float FloorSnapPadding = 1.5f;
	UPROPERTY(EditAnywhere, Category="Cover|Tuning", meta=(Units="cm")) float CoverBackOffset  = 30.f;
	UPROPERTY(EditAnywhere, Category="Cover|Tuning", meta=(Units="cm")) float CameraWallClearance = 12.f;
	UPROPERTY(EditAnywhere, Category="Cover|Tuning", meta=(Units="cm")) float LeanLateral = 28.f;
	UPROPERTY(EditAnywhere, Category="Cover|Tuning", meta=(Units="cm")) float LeanVertical = 8.f;
	UPROPERTY(EditAnywhere, Category="Cover|Tuning", meta=(ClampMin="0.0")) float LeanSpeed = 10.f;

	// Camera / spring arm (cover view)
	UPROPERTY() USpringArmComponent* CachedBoom   = nullptr;
	UPROPERTY() UCameraComponent*    CachedCamera = nullptr;
	UPROPERTY(EditAnywhere, Category="Cover|Camera", meta=(Units="cm")) float CoverShoulderOffset = 55.f;
	UPROPERTY(EditAnywhere, Category="Cover|Camera", meta=(Units="cm")) float ArmLengthCover = 260.f;
	// NOTE: default shoulder side can be implemented later if desired.
	// bool bRightShoulder = true; // <— removed (unused)

	// Cached values to restore on ExitCover
	FVector SavedSocketOffset = FVector::ZeroVector;
	float   SavedArmLength    = 0.f;
	bool    bSavedDoCollisionTest = true;

	// ---- Approach (pre-hug) -------------------
	UPROPERTY(EditAnywhere, Category="Cover|Tuning", meta=(Units="cm")) float ApproachStopDistance     = 35.f;
	UPROPERTY(EditAnywhere, Category="Cover|Tuning", meta=(Units="cm")) float ApproachNudgeMaxDistance = 450.f;
	UPROPERTY(EditAnywhere, Category="Cover|Tuning", meta=(ClampMin="0.0")) float ApproachMoveSpeed = 1.0f;
	UPROPERTY(EditAnywhere, Category="Cover|Tuning", meta=(Units="cm/s")) float ApproachWalkSpeed = 350.f;
	UPROPERTY(EditAnywhere, Category="Cover|Tuning", meta=(Units="s"))   float EntrySnapBlendTime = 0.18f;

	// ---- Suggest / selection ------------------
	UPROPERTY(Transient) TArray<FBunkerCandidate> Suggested;
	int32 SuggestedIndex = INDEX_NONE;
	UPROPERTY(EditAnywhere, Category="Cover|Select", meta=(ClampMin="1", ClampMax="5")) int32 SuggestTopK = 3;
	UPROPERTY(EditAnywhere, Category="Cover|Select", meta=(ClampMin="0.0", ClampMax="1.0")) float SuggestMinCosToView = 0.25f;

	ECoverApproachMode ApproachMode = ECoverApproachMode::Nudge; // auto-chosen per distance/LOS
	FVector  ApproachLoc = FVector::ZeroVector;
	FRotator ApproachRot = FRotator::ZeroRotator;
	float    SavedMaxWalkSpeed = 0.f;

	// ---- Helpers ----
	void   CacheCameraComponents();
	void   ApplyStanceForSlot(ABunkerBase* Bunker, int32 SlotIndex);
	bool   ComputeCoverAlignmentTransform(const FTransform& SlotXf, const FVector& Normal, FVector& OutLoc, FRotator& OutRot) const;
	void   ApplyCoverAlignmentTransform(const FVector& Loc, const FRotator& Rot);
	void   SetInitialShoulderFromView(const FVector& SlotNormal);
	void   AlignControllerYawTo(const FRotator& FaceRot);
	bool   ResolveCurrentSlotTransform(FTransform& OutTransform, FVector& OutNormal) const;
	void   ApplySlotIndexPose(const FTransform& SlotTransform, const FVector& Normal);
	void   ApplyLean(float DeltaTime, const FTransform& SlotTransform, const FVector& Normal);
	bool   FindAdjacentFreeSlotOnCurrentBunker(int DirSign, int32& OutSlotIndex) const; // DirSign: -1=left, +1=right
	bool QueryFloorZAtXY(const FVector2D& XY, float TopZ, float BottomZ, float& OutZ) const;

};