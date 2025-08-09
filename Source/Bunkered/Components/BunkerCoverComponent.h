// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BunkerCoverComponent.generated.h"

class UCameraComponent;
class USpringArmComponent;
class ABunkerBase;

/* Todo: ?????  is this necessary? what is this for ???? */
/*ENUM: States for cover */
UENUM(BlueprintType)
enum class ECoverState : uint8 { None, Approach, Hug, Peek };

//ENUM: desired stance for the target slot (ApplyStanceForSlot(Bunker, slotIndex))
UENUM(BlueprintType)
enum class ECoverStance : uint8 { Stand, Crouch, Prone };

/* ENUM: states for checking global bunkers or slots on the current bunker */ 
UENUM(BlueprintType)
enum class ECoverSelectionPolicy : uint8
{
	Global, // pick from all bunkers (used when not in cover)
	SameBunkerOnly // while in cover, only traverse to other free slots on the same bunker
};

UENUM(BlueprintType)
enum class ECoverApproachMode : uint8 { Nudge, Nav };

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class BUNKERED_API UBunkerCoverComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UBunkerCoverComponent();

	/* EnterCover by calling SBSS directly */
	UFUNCTION(BlueprintCallable, Category="Cover")
	bool EnterBestCover(); 

	UFUNCTION(BlueprintCallable, Category="Cover")
	bool EnterCover(ABunkerBase* Bunker, int32 SlotIndex);

	UFUNCTION(BlueprintCallable, Category="Cover")
	void ExitCover();

	UFUNCTION(BlueprintCallable, Category="Cover")
	bool TraverseBestSlotOnCurrentBunker(int32 DesiredIndex = 0);
	
	UFUNCTION(BlueprintCallable, Category="Cover")
	void SetLeanAxis(float Axis); // [-1..1], negative = left

	UFUNCTION(BlueprintPure, Category="Cover")
	bool IsInCover() const { return State != ECoverState::None; }

	// WASD traversal helpers (left/right along current bunker)
	bool FindAdjacentSlotOnCurrentBunker(int DirSign, int32& OutSlotIndex) const; // DirSign: +1 right, -1 left

	// public convenience to hook to input (A/D)
	UFUNCTION(BlueprintCallable, Category="Cover")
	bool TraverseLeft();

	UFUNCTION(BlueprintCallable, Category="Cover")
	bool TraverseRight();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTick) override;

private:
	// current occupancy
	UPROPERTY()
	TWeakObjectPtr<ABunkerBase> CurrentBunker;

	UPROPERTY()
	int32 CurrentSlot = INDEX_NONE;

	UPROPERTY(EditAnywhere, Category="Cover|Tuning")
	ECoverSelectionPolicy SelectionPolicy = ECoverSelectionPolicy::SameBunkerOnly;

	UPROPERTY(EditAnywhere, Category="Cover|Tuning")
	float SlotTransitionTime = 0.25f; // seconds

	// Transition state
	bool  bIsTransitioning = false;
	float TransitionElapsed = 0.f;
	FVector StartLoc; FRotator StartRot;
	FVector TargetLoc; FRotator TargetRot;
	int32  PendingSlot = INDEX_NONE;

	void ApplyStanceForSlot(ABunkerBase* Bunker, int32 SlotIndex);	

	UPROPERTY()
	ECoverState State = ECoverState::None;

	// cached base pose at hug
	FVector BaseLoc = FVector::ZeroVector;
	FRotator BaseRot = FRotator::ZeroRotator;

	// lean state
	float DesiredLeanAxis = 0.f;
	float CurrentLean = 0.f;

	// ------------ Cover|Tuning ----------------
	UPROPERTY(EditAnywhere, Category="Cover|Tuning")
	float FloorSnapUp = 50.f;        // cm above target to start the trace

	UPROPERTY(EditAnywhere, Category="Cover|Tuning")
	float FloorSnapDown = 150.f;     // cm below target to search for ground

	UPROPERTY(EditAnywhere, Category="Cover|Tuning")
	float FloorSnapPadding = 1.5f;   // tiny lift to avoid resting penetration
	
	UPROPERTY(EditAnywhere, Category="Cover|Tuning")
	float CoverBackOffset = 30.f; // cm back from slot normal
	
	UPROPERTY(EditAnywhere, Category="Cover|Tuning")
	float CameraWallClearance = 12.f; // Camera offset from the bunker (TEST)

	UPROPERTY(EditAnywhere, Category="Cover|Tuning")
	float LeanLateral = 28.f; // cm side max

	UPROPERTY(EditAnywhere, Category="Cover|Tuning")
	float LeanVertical = 8.f; // cm up max

	UPROPERTY(EditAnywhere, Category="Cover|Tuning")
	float LeanSpeed = 10.f; // interp speed
	// -----------------------------------------------

	/* -------- Camera/SpringArmComponent ---------- */
	UPROPERTY()
	USpringArmComponent* CachedBoom = nullptr;
	
	UPROPERTY()
	UCameraComponent* CachedCamera = nullptr;
	// Shoulder/camera tuning
	UPROPERTY(EditAnywhere, Category="Cover|Camera")
	float CoverShoulderOffset = 55.f;   // cm to the exposed side

	UPROPERTY(EditAnywhere, Category="Cover|Camera")
	float CameraHeight   = 70.f;   // cm above pelvis

	UPROPERTY(EditAnywhere, Category="Cover|Camera")
	float ArmLengthCover = 260.f;  // spring arm length while in cover

	UPROPERTY(EditAnywhere, Category="Cover|Camera")
	float CameraInterp   = 10.f;   // camera smoothing rate

	UPROPERTY(EditAnywhere, Category="Cover|Camera")
	bool bRightShoulder  = true;   // toggle via input later

	// cached values to restore on ExitCover
	FVector SavedSocketOffset = FVector::ZeroVector;
	float   SavedArmLength = 0.f;
	bool    bSavedDoCollisionTest = true;

	// helpers
	void CacheCamera();
	bool ComputeHugTransform(const FTransform& SlotXf, const FVector& Normal, FVector& OutLoc, FRotator& OutRot) const;
	void PlaceAtHugTransform(const FVector& Loc, const FRotator& Rot);
	/** Pick initial shoulder (L/R) from player view when we enter cover */
	void SetInitialShoulderFromView(const FVector& SlotNormal);
	/** Make camera face the bunker direction right away */
	void AlignControllerYawTo(const FRotator& FaceRot);
	/*-----------------------------------------------*/
	
	bool ResolveSlotXf(FTransform& OutXf, FVector& OutNormal) const;
	void ApplyHugPose(const FTransform& SlotXf, const FVector& Normal);
	void ApplyLean(float DeltaTime, const FTransform& SlotXf, const FVector& Normal);

	UPROPERTY(EditAnywhere, Category="Cover|Tuning")
	float ApproachStopDistance = 35.f;   // how close (cm) before we snap to hug

	UPROPERTY(EditAnywhere, Category="Cover|Tuning")
	float ApproachNudgeMaxDistance = 450.f;   // if closer than this and LOS is clear, nudge

	UPROPERTY(EditAnywhere, Category="Cover|Tuning")
	float ApproachMoveSpeed = 1.0f;      // scalar for AddMovementInput while approaching

	/** Walk speed while approaching (nav or nudge) */
	UPROPERTY(EditAnywhere, Category="Cover|Tuning")
	float ApproachWalkSpeed = 220.f;

	/** Ease the last few centimeters instead of popping */
	UPROPERTY(EditAnywhere, Category="Cover|Tuning")
	float EntrySnapBlendTime = 0.18f;
	
	ECoverApproachMode ActiveApproachMode = ECoverApproachMode::Nudge;

	// cached approach target
	FVector ApproachLoc = FVector::ZeroVector;
	FRotator ApproachRot = FRotator::ZeroRotator;
	
	// restore movement speed after approach
	float SavedMaxWalkSpeed = 0.f;
};
