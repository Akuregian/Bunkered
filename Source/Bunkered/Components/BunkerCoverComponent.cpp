#include "BunkerCoverComponent.h"
#include "Bunkers/BunkerBase.h"                 // ECoverStance + slot info (in .cpp only)
#include "Characters/BunkeredCharacter.h"       // if your character lives here
#include "Subsystem/SmartBunkerSelectionSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Utility/LoggingMacros.h"
#include "DrawDebugHelpers.h"

UBunkerCoverComponent::UBunkerCoverComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

// Not used!
bool UBunkerCoverComponent::RefreshCoverSuggestions()
{
	Suggested.Reset(); SuggestedIndex = INDEX_NONE;
	AActor* Owner = GetOwner(); if (!Owner) return false;
	UWorld* W = Owner->GetWorld(); if (!W) return false;

	auto* SBSS = W->GetSubsystem<USmartBunkerSelectionSubsystem>(); if (!SBSS) return false;

	TArray<FBunkerCandidate> Top;
	SBSS->GetTopKAcrossBunkers(Owner, SuggestTopK * 2, Top); // ask a bit more, we’ll filter
	if (CurrentBunker.IsValid()) {
		ABunkerBase* Curr = CurrentBunker.Get();
		Top.RemoveAll([Curr](const FBunkerCandidate& C){ return C.Bunker == Curr; });
	}

	// Angle gate to the camera view for snappy UX
	FVector ViewFwd = Owner->GetActorForwardVector();
	if (APawn* P = Cast<APawn>(Owner)) {
		if (AController* C = P->GetController()) ViewFwd = C->GetControlRotation().Vector();
	}

	const FVector Eye = Owner->GetActorLocation();
	Top = Top.FilterByPredicate([&](const FBunkerCandidate& C){
		if (!IsValid(C.Bunker)) return false;
		const FVector P = C.Bunker->GetSlotWorldTransform(C.SlotIndex).GetLocation();
		const float Cos = FVector::DotProduct((P - Eye).GetSafeNormal(), ViewFwd);
		return Cos >= SuggestMinCosToView;
	});

	// Keep at most K
	if (Top.Num() == 0) return false;
	if (Top.Num() > SuggestTopK) Top.SetNum(SuggestTopK);

	Suggested = MoveTemp(Top);
	SuggestedIndex = 0;
	return true;
}

bool UBunkerCoverComponent::SelectNextSuggestion()
{
	if (Suggested.Num() == 0) return false;
	SuggestedIndex = (SuggestedIndex + 1) % Suggested.Num();
	return true;
}

bool UBunkerCoverComponent::SelectPreviousSuggestion()
{
	if (Suggested.Num() == 0) return false;
	SuggestedIndex = (SuggestedIndex - 1 + Suggested.Num()) % Suggested.Num();
	return true;
}

bool UBunkerCoverComponent::ConfirmSelectedSuggestion()
{
	if (!Suggested.IsValidIndex(SuggestedIndex)) return false;

	// Try current pick
	const FBunkerCandidate First = Suggested[SuggestedIndex];
	if (EnterCover(First.Bunker, First.SlotIndex)) return true;

	// Auto-advance once if it failed (stale/occupied), but don’t loop forever
	DEBUG(5.0f, FColor::Red, TEXT("BunkerComponent::CoverSuggestionConfirm - Auto Advancing due to stale or occupied suggested bunker"));
	const int32 NextIdx = (SuggestedIndex + 1) % Suggested.Num();
	if (NextIdx != SuggestedIndex)
	{
		const FBunkerCandidate& Next = Suggested[NextIdx];
		if (EnterCover(Next.Bunker, Next.SlotIndex))
		{
			SuggestedIndex = NextIdx;
			return true;
		}
	}
	return false;
}

void UBunkerCoverComponent::BeginPlay()
{
	Super::BeginPlay();

	// Cache camera boom if owner is our character type; otherwise fall back.
	if (const auto* Char = Cast<ABunkeredCharacter>(GetOwner()))
	{
		CachedBoom = Char->GetCameraBoom();
		CachedCamera = Char->GetFollowCamera();
	}
	if (!CachedBoom) { CacheCameraComponents(); }

	if (CachedBoom)
	{
		SavedSocketOffset     = CachedBoom->SocketOffset;
		SavedArmLength        = CachedBoom->TargetArmLength;
		bSavedDoCollisionTest = CachedBoom->bDoCollisionTest;
	}
}

void UBunkerCoverComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ExitCover();
	Super::EndPlay(EndPlayReason);
}

void UBunkerCoverComponent::ApplyStanceForSlot(ABunkerBase* Bunker, int32 SlotIndex)
{
	ACharacter* Char = Cast<ACharacter>(GetOwner());
	if (!Char || !Bunker) return;

	switch (Bunker->GetSlotPose(SlotIndex))
	{
		case ECoverStance::Crouch: Char->Crouch();   break;
		case ECoverStance::Stand:  Char->UnCrouch(); break;
		case ECoverStance::Prone:  /* hook prone here */ break;
	}
}

void UBunkerCoverComponent::CacheCameraComponents()
{
	if (CachedBoom) return;

	if (ABunkeredCharacter* BC = Cast<ABunkeredCharacter>(GetOwner()))
	{
		CachedBoom   = BC->GetCameraBoom();
		CachedCamera = BC->GetFollowCamera();
	}
	else
	{
		TArray<USpringArmComponent*> Arms; GetOwner()->GetComponents<USpringArmComponent>(Arms);
		if (Arms.Num() > 0) CachedBoom = Arms[0];
		if (CachedBoom)
		{
			TArray<UCameraComponent*> Cams; GetOwner()->GetComponents<UCameraComponent>(Cams);
			if (Cams.Num() > 0) CachedCamera = Cams[0];
		}
	}
}

bool UBunkerCoverComponent::ComputeCoverAlignmentTransform(const FTransform& SlotXf, const FVector& Normal, FVector& OutLoc, FRotator& OutRot) const
{
	// Cast Character
	const ACharacter* Char = Cast<ACharacter>(GetOwner());
	if (!Char) return false;

	float Radius=42.f, HalfHeight=88.f;
	if (UCapsuleComponent* Cap = Char->GetCapsuleComponent())
	{
		Cap->GetScaledCapsuleSize(Radius, HalfHeight);
	}

	// 1) Horizontal: back away from wall along slot normal
	const FVector SafeBackDir = Normal.GetSafeNormal();
	const float   SafeBack    = CoverBackOffset + Radius * 0.6f + CameraWallClearance;
	const FVector Slot      = SlotXf.GetLocation();
	const FVector PreLoc      = Slot - SafeBackDir * SafeBack;
	const FRotator FaceRot    = SafeBackDir.Rotation();

	DEBUG(10.0f, FColor::Magenta, TEXT("Slot: %s, PreLoc: %s"), *Slot.ToString(), *PreLoc.ToString());

	// 2) Vertical: capsule sweep to ground
	UWorld* World = GetWorld();
	if (!World)
	{
		OutLoc = PreLoc;
		OutRot = FaceRot;
		return true;
	}

	FCollisionObjectQueryParams ObjMask;
	ObjMask.AddObjectTypesToQuery(ECC_WorldStatic);
	
	// floor trace straight down
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CoverFloorSnap), false, GetOwner());
	Params.AddIgnoredActor(GetOwner());
	if (CurrentBunker.IsValid())
		Params.AddIgnoredActor(CurrentBunker.Get());

	const FVector Start = PreLoc + FVector::UpVector * FloorSnapUp;
	const FVector End   = PreLoc - FVector::UpVector * FloorSnapDown;

	// TODO: DEBUG - Remove
	DrawDebugLine(World, Start, End, FColor::Emerald, false,5.0f, 0, 2.f);

	float FloorZ = PreLoc.Z;
	float OutZ;
	if (QueryFloorZAtXY(FVector2D(PreLoc.X, PreLoc.Y), PreLoc.Z + FloorSnapUp, PreLoc.Z - FloorSnapDown, OutZ))
		FloorZ = OutZ;
	
	// Treat slot Z as the desired capsule-center height,
	// but never go below floor + HalfHeight(=44) + padding.
	constexpr float DesiredHalfHeight = 44.f;        // you said all players use 44.f
	const float FloorCenterZ = FloorZ + DesiredHalfHeight + FloorSnapPadding;
	const float SlotCenterZ  = Slot.Z;               // Slot was computed above
	const float FinalZ       = FMath::Max(SlotCenterZ, FloorCenterZ);
	
	OutLoc = FVector(PreLoc.X, PreLoc.Y, FinalZ);
	OutRot = FaceRot;

	DrawDebugSphere(World, OutLoc, 5.0f, 10, FColor::Black,false, 5.0f,0, 3.f);
	return true;
	
}

void UBunkerCoverComponent::ApplyCoverAlignmentTransform(const FVector& Loc, const FRotator& Rot)
{

	ACharacter* Char = Cast<ACharacter>(GetOwner()); if (!Char) return;

	// single sweep move
	FHitResult Hit;
	Char->SetActorLocation(Loc, /*bSweep=*/true, &Hit);
	if (Hit.bStartPenetrating)
	{
		const FVector PushBack = (-Rot.Vector()) * (Hit.PenetrationDepth + 5.f);
		Char->SetActorLocation(Char->GetActorLocation() - PushBack, /*bSweep=*/false);
	}

	BaseLoc = Loc;
	BaseRot = Rot;
	
	// NEW: remember the half-height used for this base
	if (UCapsuleComponent* Cap = Char->GetCapsuleComponent())
	{
		float R, HH;
		Cap->GetScaledCapsuleSize(R, HH);
		BaseHalfHeight = HH;
	}
}

void UBunkerCoverComponent::SetInitialShoulderFromView(const FVector& SlotNormal)
{
	if (!CachedBoom) return;

	const FVector Up = FVector::UpVector;
	const FVector SlotRight = FVector::CrossProduct(Up, SlotNormal).GetSafeNormal();

	FRotator ViewRot = FRotator::ZeroRotator;
	if (APawn* P = Cast<APawn>(GetOwner()))
	{
		if (AController* C = P->GetController()) ViewRot = C->GetControlRotation();
		else                                     ViewRot = P->GetActorRotation();
	}

	const FVector ViewRight = FRotationMatrix(ViewRot).GetUnitAxis(EAxis::Y);
	const float SideSign = (FVector::DotProduct(ViewRight, SlotRight) >= 0.f) ? +1.f : -1.f;

	CachedBoom->SocketOffset     = FVector(0.f, SideSign * CoverShoulderOffset, 0.f);
	CachedBoom->TargetArmLength  = ArmLengthCover;
	CachedBoom->bDoCollisionTest = true;
}

void UBunkerCoverComponent::AlignControllerYawTo(const FRotator& FaceRot)
{
	if (APawn* P = Cast<APawn>(GetOwner()))
	{
		if (AController* C = P->GetController())
		{
			FRotator NewCtrl = C->GetControlRotation();
			NewCtrl.Yaw = FaceRot.Yaw;
			C->SetControlRotation(NewCtrl);
		}
	}
}

bool UBunkerCoverComponent::ResolveCurrentSlotTransform(FTransform& OutTransform, FVector& OutNormal) const
{
	if (!CurrentBunker.IsValid() || CurrentSlot == INDEX_NONE) return false;
	OutTransform     = CurrentBunker->GetSlotWorldTransform(CurrentSlot);
	OutNormal = CurrentBunker->GetSlotNormal(CurrentSlot).GetSafeNormal();
	return true;
}

bool UBunkerCoverComponent::EnterBestCover()
{
	// If already transitioning or CoverState == Approach
	if (bIsTransitionActive || State == ECoverState::Approaching) return false;

	// Force GLOBAL selection
	const ECoverSelectionPolicy PrevPolicy = SelectionPolicy;
	SelectionPolicy = ECoverSelectionPolicy::Global;

	// Gaurds
	AActor* Owner = GetOwner(); if (!Owner) return false;
	UWorld* W = Owner->GetWorld(); if (!W) return false;
	auto* SBSS = W->GetSubsystem<USmartBunkerSelectionSubsystem>(); if (!SBSS) return false;

	// Get Top Bunkers
	TArray<FBunkerCandidate> Top;
	SBSS->GetTopKAcrossBunkers(Owner, 5, Top);
	if (Top.Num() == 0) { SelectionPolicy = PrevPolicy; return false; }

	// If we're already in cover, never select the same bunker on "F"
	if (CurrentBunker.IsValid())
	{
		ABunkerBase* Curr = CurrentBunker.Get();
		Top.RemoveAll([Curr](const FBunkerCandidate& C){ return C.Bunker == Curr; });
	}
	if (Top.Num() == 0) { SelectionPolicy = PrevPolicy; return false; }

	// Get New Top Bunker
	ABunkerBase* NewBunker = Top[0].Bunker;
	const int32  NewSlot = Top[0].SlotIndex;

	// 1) Claim first
	if (!NewBunker || !NewBunker->ClaimSlot(NewSlot, Owner)) { SelectionPolicy = PrevPolicy; return false; }

	// 2) Release OLD if different
	ABunkerBase* OldBunker = CurrentBunker.Get();
	const int32  OldSlot = CurrentSlot;
	if (OldBunker && OldSlot != INDEX_NONE && (OldBunker != NewBunker || OldSlot != NewSlot))
	{ OldBunker->ReleaseSlot(OldSlot, Owner); }

	// 3) Now flip current
	CurrentBunker = NewBunker;
	CurrentSlot   = NewSlot;

	ApplyStanceForSlot(NewBunker, NewSlot);

	FTransform SlotXf; FVector Normal;
	if (!ResolveCurrentSlotTransform(SlotXf, Normal))
	{
		// undo claim on failure
		NewBunker->ReleaseSlot(NewSlot, Owner);
		CurrentBunker.Reset(); CurrentSlot = INDEX_NONE;
		SelectionPolicy = PrevPolicy;
		return false;
	}

	if (!ComputeCoverAlignmentTransform(SlotXf, Normal, ApproachLoc, ApproachRot))
	{
		NewBunker->ReleaseSlot(NewSlot, Owner);
		CurrentBunker.Reset(); CurrentSlot = INDEX_NONE;
		SelectionPolicy = PrevPolicy;
		return false;
	}

	DEBUG(10.0f, FColor::Orange, TEXT("Computed Hug Transform: %s"), *ApproachLoc.ToString());

	// Approach setup
	ACharacter* Char = Cast<ACharacter>(Owner);
	const float Dist2D = Char ? FVector::Dist2D(Char->GetActorLocation(), ApproachLoc) : 0.f;

	bool bHasLOS = true;
	if (UWorld* World = GetWorld())
	{
		FHitResult Hit;
		FCollisionQueryParams P(SCENE_QUERY_STAT(CoverApproachLOS), false, Owner);
		const FVector EyeA = (Char ? Char->GetActorLocation() : ApproachLoc) + FVector(0,0,44.f);
		const FVector EyeB = ApproachLoc + FVector(0,0,44.f);
		bHasLOS = !World->LineTraceSingleByChannel(Hit, EyeA, EyeB, ECC_Visibility, P);
	}

	ApproachMode = ECoverApproachMode::Nav;
	State = ECoverState::Approaching;

	if (Char && Char->GetCharacterMovement())
	{
		SavedMaxWalkSpeed = Char->GetCharacterMovement()->MaxWalkSpeed;
		Char->GetCharacterMovement()->MaxWalkSpeed = ApproachWalkSpeed;
	}

	AlignControllerYawTo(ApproachRot);

	if (ApproachMode == ECoverApproachMode::Nav && Char->GetController())
	{
		UAIBlueprintHelperLibrary::SimpleMoveToLocation(Char->GetController(), ApproachLoc);
	}

	SelectionPolicy = PrevPolicy;
	return true;
}

bool UBunkerCoverComponent::EnterCover(ABunkerBase* Bunker, int32 SlotIndex)
{
	if (!Bunker) return false;
	if (bIsTransitionActive || State == ECoverState::Approaching) return false;

	AActor* Owner = GetOwner(); if (!Owner) return false;

	// If already there, nothing to do
	if (CurrentBunker.IsValid() && CurrentBunker.Get() == Bunker && CurrentSlot == SlotIndex)
	{
		State = ECoverState::InSlot; // be explicit
		return true;
	}

	// 1) Claim new first
	if (!Bunker->ClaimSlot(SlotIndex, Owner)) return false;
	const bool bSameBunker = (CurrentBunker.IsValid() && CurrentBunker.Get() == Bunker);

	// 2) Release any old after new succeeds
	if (CurrentBunker.IsValid() && CurrentSlot != INDEX_NONE)
	{
		CurrentBunker->ReleaseSlot(CurrentSlot, Owner);
	}

	CurrentBunker = Bunker;
	CurrentSlot   = SlotIndex;
	ApplyStanceForSlot(Bunker, SlotIndex);

	// Compute target hug transform from the slot itself
	const FTransform SlotXf = Bunker->GetSlotWorldTransform(SlotIndex);
	const FVector    Normal = Bunker->GetSlotNormal(SlotIndex).GetSafeNormal();

	FVector  HugLoc; FRotator HugRot;
	if (!ComputeCoverAlignmentTransform(SlotXf, Normal, HugLoc, HugRot))
	{
		Bunker->ReleaseSlot(SlotIndex, Owner);
		CurrentBunker.Reset(); CurrentSlot = INDEX_NONE;
		return false;
	}

	// Smooth transition (works for same-bunker and cross-bunker)
	ACharacter* Char = Cast<ACharacter>(Owner); if (!Char) return false;
	StartLoc = Char->GetActorLocation();
	StartRot = Char->GetActorRotation();
	TargetLoc = HugLoc;
	TargetRot = HugRot;
	TransitionElapsed = 0.f;
	bIsTransitionActive = true;
	DesiredLeanAxis = 0.f; CurrentLean = 0.f;

	// Auto-scale transition time by distance for consistent feel
	const float Dist2D = FVector::Dist2D(StartLoc, TargetLoc);
	const float SlideSpeed = 1200.f; // cm/s — tune
	SlotTransitionTime = FMath::Max(0.08f, Dist2D / SlideSpeed);

	SetInitialShoulderFromView(Normal);
	AlignControllerYawTo(TargetRot);
	State = ECoverState::InSlot;
	return true;}

void UBunkerCoverComponent::ExitCover()
{
	// Restore camera
	if (CachedBoom)
	{
		CachedBoom->SocketOffset     = SavedSocketOffset;
		if (SavedArmLength > 0.f) { CachedBoom->TargetArmLength = SavedArmLength; }
		CachedBoom->bDoCollisionTest = bSavedDoCollisionTest;
	}

	// Release slot
	if (CurrentBunker.IsValid() && CurrentSlot != INDEX_NONE)
	{
		CurrentBunker->ReleaseSlot(CurrentSlot, GetOwner());
	}
	CurrentBunker.Reset(); CurrentSlot = INDEX_NONE; State = ECoverState::None;

	// Restore movement
	if (ACharacter* Char = Cast<ACharacter>(GetOwner()))
	{
		if (Char->GetCharacterMovement() && SavedMaxWalkSpeed > 0.f)
		{
			Char->GetCharacterMovement()->MaxWalkSpeed = SavedMaxWalkSpeed;
			SavedMaxWalkSpeed = 0.f;
		}
	}
	// Reset variables
	SelectionPolicy = ECoverSelectionPolicy::Global;
	bIsTransitionActive = false;
	TransitionElapsed = 0.f;
}

bool UBunkerCoverComponent::TraverseBestSlotOnCurrentBunker(int32 /*DesiredIndex*/)
{
	SelectionPolicy = ECoverSelectionPolicy::SameBunkerOnly;
	AActor* Owner = GetOwner(); if (!Owner) return false;
	if (!CurrentBunker.IsValid() || CurrentSlot == INDEX_NONE) return false;
	UWorld* W = Owner->GetWorld(); if (!W) return false;

	auto* SBSS = W->GetSubsystem<USmartBunkerSelectionSubsystem>(); if (!SBSS) return false;

	TArray<FBunkerCandidate> Top;
	SBSS->GetTopKWithinBunker(Owner, CurrentBunker.Get(), 3, Top);
	Top.RemoveAll([this](const FBunkerCandidate& C){ return C.SlotIndex == CurrentSlot; });
	if (Top.Num() == 0) return false;

	return EnterCover(Top[0].Bunker, Top[0].SlotIndex);
}

void UBunkerCoverComponent::SetLeanAxis(float Axis)
{
	DesiredLeanAxis = FMath::Clamp(Axis, -1.f, 1.f);
	if (State != ECoverState::None) State = (FMath::IsNearlyZero(DesiredLeanAxis)) ? ECoverState::InSlot : ECoverState::Peeking;
}

bool UBunkerCoverComponent::FindAdjacentFreeSlotOnCurrentBunker(int DirSign, int32& OutSlotIndex) const
{
	OutSlotIndex = INDEX_NONE;
	if (!CurrentBunker.IsValid() || CurrentSlot == INDEX_NONE) return false;

	// Current frame
	const FTransform CurrXf = CurrentBunker->GetSlotWorldTransform(CurrentSlot);
	const FVector CurrP = CurrXf.GetLocation();
	const FVector Normal = CurrentBunker->GetSlotNormal(CurrentSlot).GetSafeNormal();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Normal).GetSafeNormal();

	float BestAlong = -FLT_MAX;
	float BestDist  = FLT_MAX;

	const int32 Count = CurrentBunker->GetSlotCount();
	for (int32 i = 0; i < Count; ++i)
	{
		if (i == CurrentSlot || CurrentBunker->IsSlotOccupied(i)) continue;
		const FVector P = CurrentBunker->GetSlotWorldTransform(i).GetLocation();
		const FVector Delta = P - CurrP;

		const float Along = FVector::DotProduct(Delta, Right) * DirSign; // positive means in requested direction
		if (Along <= 5.f) continue;

		const float Dist = Delta.Size2D();
		// prefer bigger Along, then nearest distance
		if (Along > BestAlong || (FMath::IsNearlyEqual(Along, BestAlong, 1.f) && Dist < BestDist))
		{
			BestAlong = Along; BestDist = Dist; OutSlotIndex = i;
		}
	}
	return OutSlotIndex != INDEX_NONE;
}

bool UBunkerCoverComponent::QueryFloorZAtXY(const FVector2D& XY, float TopZ, float BottomZ, float& OutZ) const
{
	UWorld* W = GetWorld(); if (!W) return false;
	FHitResult Hit;
	FCollisionQueryParams P(SCENE_QUERY_STAT(CoverFloorQuery), false, GetOwner());
	if (CurrentBunker.IsValid()) P.AddIgnoredActor(CurrentBunker.Get());

	const FVector Start(XY.X, XY.Y, TopZ);
	const FVector End  (XY.X, XY.Y, BottomZ);

	bool bHit = W->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, P);
	if (!bHit) bHit = W->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, P);

	if (bHit && Hit.ImpactNormal.Z >= 0.5f) { OutZ = Hit.Location.Z; return true; }
	return false;
}

bool UBunkerCoverComponent::TraverseLeft()
{
	SelectionPolicy = ECoverSelectionPolicy::SameBunkerOnly;
	int32 Next = INDEX_NONE;
	if (!FindAdjacentFreeSlotOnCurrentBunker(-1, Next)) return false;
	return EnterCover(CurrentBunker.Get(), Next);
}

bool UBunkerCoverComponent::TraverseRight()
{
	SelectionPolicy = ECoverSelectionPolicy::SameBunkerOnly;
	int32 Next = INDEX_NONE;
	if (!FindAdjacentFreeSlotOnCurrentBunker(+1, Next)) return false;
	return EnterCover(CurrentBunker.Get(), Next);
}

void UBunkerCoverComponent::ApplySlotIndexPose(const FTransform& SlotTransform, const FVector& Normal)
{
	ACharacter* Char = Cast<ACharacter>(GetOwner()); if (!Char) return;
	FVector HugLoc; FRotator HugRot;
	if (ComputeCoverAlignmentTransform(SlotTransform, Normal, HugLoc, HugRot)) { ApplyCoverAlignmentTransform(HugLoc, HugRot); }
}

void UBunkerCoverComponent::ApplyLean(float DeltaTime, const FTransform& /*SlotXf*/, const FVector& /*Normal*/)
{
	if (State == ECoverState::None) return;
	
	ACharacter* Char = Cast<ACharacter>(GetOwner()); if (!Char) return;

	if (UCapsuleComponent* Cap = Char->GetCapsuleComponent())
	{
		float R, HH;
		Cap->GetScaledCapsuleSize(R, HH);
		if (!FMath::IsNearlyEqual(HH, BaseHalfHeight))
		{
			// Shift center so the feet stay on the same floor
			const float Delta = HH - BaseHalfHeight; // negative when crouching
			BaseLoc.Z += Delta;
			BaseHalfHeight = HH;
		}
	}

	// Smooth the lean
	CurrentLean = FMath::FInterpTo(CurrentLean, DesiredLeanAxis, DeltaTime, LeanSpeed);

	// Lean along base right; add a bit of vertical lift
	const FVector Up = FVector::UpVector;
	const FRotationMatrix BaseM(BaseRot);
	const FVector Right = BaseM.GetScaledAxis(EAxis::Y).GetSafeNormal();

	const FVector LeanOffset = Right*(CurrentLean*LeanLateral) + Up*(FMath::Abs(CurrentLean)*LeanVertical);
	const FVector NewLoc = BaseLoc + LeanOffset;
	Char->SetActorLocation(NewLoc, /*bSweep=*/true);
	Char->SetActorRotation(BaseRot);
}

void UBunkerCoverComponent::TickComponent(float DeltaTime, ELevelTick, FActorComponentTickFunction*)
{
	if (State == ECoverState::None) return;
	
	ACharacter* Char = Cast<ACharacter>(GetOwner());
	if (!Char) { ExitCover(); return; }

	// 1) Handle approach first — no need to resolve slot yet
	if (State == ECoverState::Approaching)
	{
		const FVector To = (ApproachLoc - Char->GetActorLocation());
		float R=42.f, HH=88.f;
		if (const UCapsuleComponent* Cap = Char->GetCapsuleComponent()) Cap->GetScaledCapsuleSize(R, HH);
		const float ArriveDist = CapsuleAwareArrive(ApproachStopDistance, R);

		if (To.Size2D() > ArriveDist)
		{
			if (ApproachMode == ECoverApproachMode::Nudge)
				Char->AddMovementInput(To.GetSafeNormal2D(), ApproachMoveSpeed);
			// Nav mode continues via SimpleMoveToLocation
			return;
		}

		// Arrived — stop nav, kick the short entry blend
		if (Char->GetCharacterMovement()) Char->GetCharacterMovement()->StopMovementImmediately();
		StartLoc = Char->GetActorLocation();  StartRot = Char->GetActorRotation();
		TargetLoc = ApproachLoc;              TargetRot = ApproachRot;
		TransitionElapsed = 0.f;  bIsTransitionActive = true;
		DesiredLeanAxis = 0.f;    CurrentLean = 0.f;
		State = ECoverState::InSlot; // we’re effectively entering the slot
	}
	
	// -- Transition blend (slot switch or entry) --
	if (bIsTransitionActive)
	{
		TransitionElapsed += DeltaTime;
		const bool bEntryEase = (State == ECoverState::InSlot && TransitionElapsed <= EntrySnapBlendTime + SMALL_NUMBER);
		
		const float Duration = bEntryEase
			? FMath::Max(EntrySnapBlendTime, KINDA_SMALL_NUMBER)
			: FMath::Max(SlotTransitionTime,   KINDA_SMALL_NUMBER);		const float Alpha     = FMath::Clamp(TransitionElapsed / Duration, 0.f, 1.f);

		// Lerp XY
		const FVector StartXY (StartLoc.X,  StartLoc.Y,  0.f);
		const FVector TargetXY(TargetLoc.X, TargetLoc.Y, 0.f);
		const FVector NewXY = FMath::Lerp(StartXY, TargetXY, Alpha);

		// Lerp rotation
		const FRotator NewRot = FMath::Lerp(StartRot, TargetRot, Alpha);

		// Ground-snap Z at NewXY via capsule sweep
		float Radius=42.f, HalfHeight=88.f;
		if (UCapsuleComponent* Cap = Char->GetCapsuleComponent())
		{
			Cap->GetScaledCapsuleSize(Radius, HalfHeight);
		}

		// 1) Get ground Z with a vertical line trace at NewXY
		FHitResult DownHit;
		FCollisionQueryParams DownParams(SCENE_QUERY_STAT(CoverSlideFloor), false, GetOwner());
		DownParams.AddIgnoredActor(GetOwner());
		if (CurrentBunker.IsValid()) DownParams.AddIgnoredActor(CurrentBunker.Get());

		const FVector DownStart(NewXY.X, NewXY.Y, FMath::Max(StartLoc.Z, TargetLoc.Z) + FloorSnapUp);
		const FVector DownEnd  (NewXY.X, NewXY.Y, FMath::Min(StartLoc.Z, TargetLoc.Z) - FloorSnapDown);

		float FloorZ;
		const FVector2D XY(NewXY.X, NewXY.Y);
		if (!QueryFloorZAtXY(XY,
				FMath::Max(StartLoc.Z, TargetLoc.Z) + FloorSnapUp,
				FMath::Min(StartLoc.Z, TargetLoc.Z) - FloorSnapDown,
				FloorZ))
		{
			FloorZ = FMath::Min(StartLoc.Z, TargetLoc.Z) - FloorSnapDown;
		}
		
		// 2) Clamp: stay at the slot-anchored Target Z unless that would put us below the floor
		const float FloorClampZ = FloorZ + HalfHeight + FloorSnapPadding;

		const float TargetZ = TargetLoc.Z;     // from ComputeHugTransform (slot Z clamped to ground)
		const float NewZ    = FMath::Max(TargetZ, FloorClampZ);

		// 3) Apply new location
		const FVector NewLoc(NewXY.X, NewXY.Y, NewZ);
		Char->SetActorLocation(NewLoc, /*bSweep=*/true);

		if (Alpha >= 1.f - KINDA_SMALL_NUMBER)
		{
			DEBUG(5.0f, FColor::Green, TEXT("Reached Destination!"));
			bIsTransitionActive = false;
			
			// Snap exactly to hug transform to avoid small residuals
			ApplyCoverAlignmentTransform(NewLoc, NewRot);
		}
		return;
	}
	
	FTransform SlotXf; FVector Normal;
	if (!ResolveCurrentSlotTransform(SlotXf, Normal)) { ExitCover(); return; }

	// Maintain base pose and apply lean
	ApplyLean(DeltaTime, SlotXf, Normal);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (Suggested.IsValidIndex(SuggestedIndex))
	{
		const FBunkerCandidate& C = Suggested[SuggestedIndex];
		if (IsValid(C.Bunker))
		{
			const FVector P = C.Bunker->GetSlotWorldTransform(C.SlotIndex).GetLocation();
			DrawDebugSphere(GetWorld(), P, 20.f, 12, FColor::Purple, false, 0.f, 0, 2.f);
		}
	}
#endif
}

