#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BunkerCoverComponent.generated.h"

struct FBunkerCandidate;
class UCameraComponent;
class USpringArmComponent;
class ABunkerBase; // defined in Bunkers module

/** High-level cover state */
UENUM(BlueprintType)
enum class ECoverState : uint8 { None, Approach, Hug, Peek };

/** Policy for choosing new slots while already in cover */
UENUM(BlueprintType)
enum class ECoverSelectionPolicy : uint8
{
	Global         UMETA(ToolTip="Pick from all bunkers"),
	SameBunkerOnly UMETA(ToolTip="Only traverse to free slots on the same bunker")
};

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

	/** Enter a specific bunker slot (claims slot; releases old one). */
	UFUNCTION(BlueprintCallable, Category="Cover")
	bool EnterCover(ABunkerBase* Bunker, int32 SlotIndex);

	UFUNCTION(BlueprintCallable, Category="Cover")
	void ExitCover();

	/** While in cover, traverse to the best free slot on this bunker. */
	UFUNCTION(BlueprintCallable, Category="Cover")
	bool TraverseBestSlotOnCurrentBunker(int32 DesiredIndex = 0);

	/** [-1..1], negative = left. Non-zero sets state to Peek. */
	UFUNCTION(BlueprintCallable, Category="Cover")
	void SetLeanAxis(float Axis);

	UFUNCTION(BlueprintPure, Category="Cover")
	bool IsInCover() const { return State != ECoverState::None; }

	// Quick helpers for WASD-style traversal
	UFUNCTION(BlueprintCallable, Category="Cover") bool TraverseLeft();
	UFUNCTION(BlueprintCallable, Category="Cover") bool TraverseRight();

	UFUNCTION(BlueprintCallable, Category="Cover|Select")
	bool CoverSuggestRefresh();

	UFUNCTION(BlueprintCallable, Category="Cover|Select")
	bool CoverSuggestNext();   // cycles +1

	UFUNCTION(BlueprintCallable, Category="Cover|Select")
	bool CoverSuggestPrev();   // cycles -1

	UFUNCTION(BlueprintCallable, Category="Cover|Select")
	bool CoverSuggestConfirm(); // EnterCover(selected)

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

	UPROPERTY(EditAnywhere, Category="Cover|Tuning", meta=(ClampMin="0.0", Units="s",
			  ToolTip="Time to slide between slots on the SAME bunker once a traverse begins.\nSet small (~0.1–0.3s) for snappy feel; 0 = instant snap."))
	float SlotTransitionTime = 0.25f;
	
	// Transition state
	bool  bIsTransitioning = false;
	float TransitionElapsed = 0.f;
	FVector  StartLoc;   FRotator StartRot;
	FVector  TargetLoc;  FRotator TargetRot;
	int32    PendingSlot = INDEX_NONE;

	// State
	UPROPERTY() ECoverState State = ECoverState::None;

	// Cached base pose at Hug (lean is applied relative to this)
	FVector  BaseLoc = FVector::ZeroVector;
	FRotator BaseRot = FRotator::ZeroRotator;

	// Lean
	float DesiredLeanAxis = 0.f;
	float CurrentLean     = 0.f;

	// ------------ Cover | Tuning ----------------
	UPROPERTY(EditAnywhere, Category="Cover|Tuning", meta=(Units="cm",
			  ToolTip="How far ABOVE the computed hug point to start the floor trace.\nKeeps the trace from starting inside uneven ground."))
	float FloorSnapUp = 50.f;

	UPROPERTY(EditAnywhere, Category="Cover|Tuning", meta=(Units="cm",
			  ToolTip="How far BELOW the computed hug point to search for ground.\nIncrease on tall props or steep terrain."))
	float FloorSnapDown = 150.f;

	UPROPERTY(EditAnywhere, Category="Cover|Tuning", meta=(Units="cm",
			  ToolTip="Extra Z we add on top of the detected floor to avoid z-fighting and micro-penetration.\nUsually 0.5–2 cm."))
	float FloorSnapPadding = 1.5f;

	UPROPERTY(EditAnywhere, Category="Cover|Tuning", meta=(Units="cm",
			  ToolTip="How far to pull the capsule BACK from the slot normal when hugging the bunker.\nPrevents the capsule from intersecting the wall. Roughly <= capsule radius."))
	float CoverBackOffset = 30.f;

	UPROPERTY(EditAnywhere, Category="Cover|Tuning", meta=(Units="cm",
			  ToolTip="Minimum spring-arm probe size while in cover.\nLarger values reduce camera clipping but make the camera push in sooner near tight walls."))
	float CameraWallClearance = 12.f;

	UPROPERTY(EditAnywhere, Category="Cover|Tuning", meta=(Units="cm",
			  ToolTip="Maximum sideways offset applied when leaning (A/D).\nControls how far the body shifts laterally from the hug anchor."))
	float LeanLateral = 28.f;

	UPROPERTY(EditAnywhere, Category="Cover|Tuning", meta=(Units="cm",
			  ToolTip="Maximum vertical rise applied during a lean (small head pop on tall bunkers)."))
	float LeanVertical = 8.f;

	UPROPERTY(EditAnywhere, Category="Cover|Tuning", meta=(ClampMin="0.0",
			  ToolTip="How quickly the current lean interpolates toward the input axis per second.\nHigher = snappier lean response."))
	float LeanSpeed = 10.f;
	// --------------------------------------------	// --------------------------------------------

	// Camera / spring arm
	UPROPERTY() USpringArmComponent* CachedBoom   = nullptr;
	UPROPERTY() UCameraComponent*    CachedCamera = nullptr;

	// ---- Camera / spring arm (cover view) ----
	UPROPERTY(EditAnywhere, Category="Cover|Camera", meta=(Units="cm",
			  ToolTip="Horizontal camera offset from the actor, to the exposed shoulder (derived from slot normal).\nBigger = more over-the-shoulder; smaller = tighter, less obstruction."))
	float CoverShoulderOffset = 55.f;

	UPROPERTY(EditAnywhere, Category="Cover|Camera", meta=(Units="cm",
			  ToolTip="Spring-arm length while in cover. Shorter reduces wall pushing but zooms the camera in."))
	float ArmLengthCover = 260.f;

	UPROPERTY(EditAnywhere, Category="Cover|Camera",
			  meta=(ToolTip="Default camera shoulder while in cover.\nTrue = right shoulder; False = left shoulder. Can be toggled at runtime."))
	bool bRightShoulder = true;
	
	// Cached values to restore on ExitCover
	FVector SavedSocketOffset = FVector::ZeroVector;
	float   SavedArmLength    = 0.f;
	bool    bSavedDoCollisionTest = true;

	// ---- Approach (pre-hug) -------------------
	UPROPERTY(EditAnywhere, Category="Cover|Tuning", meta=(Units="cm",
			  ToolTip="Stop distance for the pre-hug nudge phase.\nWhen approaching a slot, we stop this far from the target before snapping into the hug pose."))
	float ApproachStopDistance = 35.f;

	UPROPERTY(EditAnywhere, Category="Cover|Tuning", meta=(Units="cm",
			  ToolTip="Max distance within which we use a simple XY nudge instead of requesting a full nav move.\nKeeps short approaches snappy."))
	float ApproachNudgeMaxDistance = 450.f;

	UPROPERTY(EditAnywhere, Category="Cover|Tuning", meta=(ClampMin="0.0",
			  ToolTip="Multiplier applied to movement during the pre-hug approach when we nudge.\n1.0 uses current walk speed; lower slows the approach."))
	float ApproachMoveSpeed = 1.0f;

	UPROPERTY(EditAnywhere, Category="Cover|Tuning", meta=(Units="cm/s",
			  ToolTip="Temporary CharacterMovement MaxWalkSpeed while approaching cover (saved and restored when finished)."))
	float ApproachWalkSpeed = 220.f;

	UPROPERTY(EditAnywhere, Category="Cover|Tuning", meta=(Units="s",
			  ToolTip="Blend time for the final snap from approach position to the exact hug transform.\nTiny values feel crisp; larger values feel more animated."))
	float EntrySnapBlendTime = 0.18f;

	// ---- Suggest / selection ------------------
	UPROPERTY(Transient) TArray<FBunkerCandidate> Suggested;
	int32 SuggestedIndex = INDEX_NONE;
	
	UPROPERTY(EditAnywhere, Category="Cover|Select",
			  meta=(ClampMin="1", ClampMax="5",
			  ToolTip="How many bunker suggestions to keep from SBSS for cycling/selecting.\nKeep small (2–3) for clarity and performance."))
	int32 SuggestTopK = 3;

	UPROPERTY(EditAnywhere, Category="Cover|Select",
			  meta=(ClampMin="0.0", ClampMax="1.0",
			  ToolTip="View-cone gate for suggestions. Slots must lie within this cosine angle of the camera forward.\n0.25 ≈ 75°, 0.5 ≈ 60°, 0.87 ≈ 30°."))
	float SuggestMinCosToView = 0.25f;

	ECoverApproachMode ApproachMode = ECoverApproachMode::Nudge;
	FVector  ApproachLoc = FVector::ZeroVector;
	FRotator ApproachRot = FRotator::ZeroRotator;
	float    SavedMaxWalkSpeed = 0.f;

	// ---- Helpers ----
	void   CacheCamera();
	void   ApplyStanceForSlot(ABunkerBase* Bunker, int32 SlotIndex);
	bool   ComputeHugTransform(const FTransform& SlotXf, const FVector& Normal, FVector& OutLoc, FRotator& OutRot) const;
	void   PlaceAtHugTransform(const FVector& Loc, const FRotator& Rot);
	void   SetInitialShoulderFromView(const FVector& SlotNormal);
	void   AlignControllerYawTo(const FRotator& FaceRot);
	bool   ResolveSlotXf(FTransform& OutXf, FVector& OutNormal) const;
	void   ApplyHugPose(const FTransform& SlotXf, const FVector& Normal);
	void   ApplyLean(float DeltaTime, const FTransform& SlotXf, const FVector& Normal);
	bool   FindAdjacentFreeSlotOnCurrentBunker(int DirSign, int32& OutSlotIndex) const; // DirSign: -1=left, +1=right
};
