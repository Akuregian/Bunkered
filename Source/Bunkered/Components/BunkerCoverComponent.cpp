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

bool UBunkerCoverComponent::CoverSuggestRefresh()
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

bool UBunkerCoverComponent::CoverSuggestNext()
{
	if (Suggested.Num() == 0) return false;
	SuggestedIndex = (SuggestedIndex + 1) % Suggested.Num();
	return true;
}

bool UBunkerCoverComponent::CoverSuggestPrev()
{
	if (Suggested.Num() == 0) return false;
	SuggestedIndex = (SuggestedIndex - 1 + Suggested.Num()) % Suggested.Num();
	return true;
}

bool UBunkerCoverComponent::CoverSuggestConfirm()
{
	if (!Suggested.IsValidIndex(SuggestedIndex)) return false;
	const FBunkerCandidate& C = Suggested[SuggestedIndex];
	return EnterCover(C.Bunker, C.SlotIndex); // reuse your smooth entry
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
	if (!CachedBoom) { CacheCamera(); }

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

void UBunkerCoverComponent::CacheCamera()
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

bool UBunkerCoverComponent::ComputeHugTransform(const FTransform& SlotXf, const FVector& Normal, FVector& OutLoc, FRotator& OutRot) const
{
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
	const FVector Anchor      = SlotXf.GetLocation();
	const FVector PreLoc      = Anchor - SafeBackDir * SafeBack;
	const FRotator FaceRot    = SafeBackDir.Rotation();

	// 2) Vertical: capsule sweep to ground
	UWorld* World = GetWorld();
	if (!World) { OutLoc = PreLoc; OutRot = FaceRot; return true; }


	// floor trace straight down
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(CoverFloorSnap), false, GetOwner());
	Params.AddIgnoredActor(GetOwner());
	if (CurrentBunker.IsValid()) Params.AddIgnoredActor(CurrentBunker.Get());

	const FVector Start = PreLoc + FVector::UpVector * FloorSnapUp;
	const FVector End   = PreLoc - FVector::UpVector * FloorSnapDown;

	FCollisionObjectQueryParams ObjMask;
	ObjMask.AddObjectTypesToQuery(ECC_WorldStatic);

	// Try world static; fall back to visibility if needed
	bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params);
	if (!bHit) bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);

	const float FloorZ = bHit ? Hit.Location.Z : PreLoc.Z;
	OutLoc = FVector(PreLoc.X, PreLoc.Y, FloorZ + HalfHeight + FloorSnapPadding);
	OutRot = FaceRot;	
	return true;
}

void UBunkerCoverComponent::PlaceAtHugTransform(const FVector& Loc, const FRotator& Rot)
{

	ACharacter* Char = Cast<ACharacter>(GetOwner()); if (!Char) return;

	// single sweep move
	FHitResult Hit;
	Char->SetActorLocationAndRotation(Loc, Rot, /*bSweep=*/true, &Hit);
	if (Hit.bStartPenetrating)
	{
		const FVector PushBack = (-Rot.Vector()) * (Hit.PenetrationDepth + 5.f);
		Char->SetActorLocation(Char->GetActorLocation() - PushBack, /*bSweep=*/false);
	}

	BaseLoc = Loc; BaseRot = Rot;
	AlignControllerYawTo(Rot);
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

bool UBunkerCoverComponent::ResolveSlotXf(FTransform& OutXf, FVector& OutNormal) const
{
	if (!CurrentBunker.IsValid() || CurrentSlot == INDEX_NONE) return false;
	OutXf     = CurrentBunker->GetSlotWorldTransform(CurrentSlot);
	OutNormal = CurrentBunker->GetSlotNormal(CurrentSlot).GetSafeNormal();
	return true;
}

bool UBunkerCoverComponent::EnterBestCover()
{
	if (bIsTransitioning || State == ECoverState::Approach)
	{
		DEBUG(5.0f, FColor::Red, TEXT("Currently Transtioning, ABORTING!"));
		return false;
	}

	AActor* Owner = GetOwner(); if (!Owner) return false;
	UWorld* W = Owner->GetWorld(); if (!W) return false;
	auto* SBSS = W->GetSubsystem<USmartBunkerSelectionSubsystem>(); if (!SBSS) return false;

	TArray<FBunkerCandidate> Top;
	SBSS->GetTopKAcrossBunkers(Owner, 5, Top);

	// Don’t pick our current bunker to avoid trivial "reselect"
	if (CurrentBunker.IsValid())
	{
		ABunkerBase* Curr = CurrentBunker.Get();
		Top.RemoveAll([Curr](const FBunkerCandidate& C){ return C.Bunker == Curr; });
	}
	if (Top.Num() == 0) return false;

	ABunkerBase* B = Top[0].Bunker;
	const int32   S = Top[0].SlotIndex;

	// Reserve early so AI/teammates don’t steal it
	if (!B || !B->ClaimSlot(S, Owner)) return false;

	CurrentBunker = B; CurrentSlot = S;
	ApplyStanceForSlot(B, S); // so ComputeHugTransform sees crouch height

	FTransform SlotXf; FVector Normal;
	if (!ResolveSlotXf(SlotXf, Normal)) { B->ReleaseSlot(S, Owner); CurrentBunker.Reset(); CurrentSlot = INDEX_NONE; return false; }
	if (!ComputeHugTransform(SlotXf, Normal, ApproachLoc, ApproachRot)) { B->ReleaseSlot(S, Owner); CurrentBunker.Reset(); CurrentSlot = INDEX_NONE; return false; }

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

	ApproachMode = (Dist2D <= ApproachNudgeMaxDistance && bHasLOS) ? ECoverApproachMode::Nudge : ECoverApproachMode::Nav;
	State = ECoverState::Approach;

	if (Char && Char->GetCharacterMovement())
	{
		SavedMaxWalkSpeed = Char->GetCharacterMovement()->MaxWalkSpeed;
		Char->GetCharacterMovement()->MaxWalkSpeed = ApproachWalkSpeed;
	}

	AlignControllerYawTo(ApproachRot);

	if (ApproachMode == ECoverApproachMode::Nav && Char && Char->GetController())
	{
		UAIBlueprintHelperLibrary::SimpleMoveToLocation(Char->GetController(), ApproachLoc);
	}
	return true;
}

bool UBunkerCoverComponent::EnterCover(ABunkerBase* Bunker, int32 SlotIndex)
{

	if (bIsTransitioning || State == ECoverState::Approach)
	{
		DEBUG(5.0f, FColor::Red, TEXT("Currently Transtioning, ABORTING!"));
		return false;
	}
	
	if (!Bunker) return false;
	
	// Already there?
	if (CurrentBunker.IsValid() && CurrentBunker.Get() == Bunker && CurrentSlot == SlotIndex) return true;

	AActor* Owner = GetOwner(); if (!Owner) return false;

	// Claim new first
	if (!Bunker->ClaimSlot(SlotIndex, Owner)) return false;
	const bool bSameBunker = (CurrentBunker.IsValid() && CurrentBunker.Get() == Bunker);

	// Release old after new succeeds
	if (CurrentBunker.IsValid() && CurrentSlot != INDEX_NONE)
	{
		CurrentBunker->ReleaseSlot(CurrentSlot, Owner);
	}

	CurrentBunker = Bunker;
	CurrentSlot = SlotIndex;
	
	ApplyStanceForSlot(Bunker, SlotIndex);

	FTransform SlotXf; FVector Normal;
	if (!ResolveSlotXf(SlotXf, Normal)) { Bunker->ReleaseSlot(SlotIndex, Owner); CurrentBunker.Reset(); CurrentSlot = INDEX_NONE; return false; }

	// Guard: missing slot component -> GetSlotWorldTransform returns actor xf; refuse to place at origin
	if (SlotXf.Equals(CurrentBunker->GetActorTransform(), 0.1f))
	{
		UE_LOG(LogTemp, Warning, TEXT("EnterCover: %s Slot %d has no SlotPoint; refusing to place at bunker origin."), *CurrentBunker->GetName(), CurrentSlot);
		CurrentBunker->ReleaseSlot(CurrentSlot, Owner); CurrentBunker.Reset(); CurrentSlot = INDEX_NONE; return false;
	}

	ACharacter* Char = Cast<ACharacter>(Owner); if (!Char) { Bunker->ReleaseSlot(SlotIndex, Owner); CurrentBunker.Reset(); CurrentSlot = INDEX_NONE; return false; }

	// Compute target hug transform
	FVector  TargetHugLoc; FRotator TargetHugRot;
	if (!ComputeHugTransform(SlotXf, Normal, TargetHugLoc, TargetHugRot)) { Bunker->ReleaseSlot(SlotIndex, Owner); CurrentBunker.Reset(); CurrentSlot = INDEX_NONE; return false; }

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (UWorld* W = GetWorld())
	{
		DrawDebugSphere(W, SlotXf.GetLocation(), 10.f, 8, FColor::Cyan, false, 2.f);
		DrawDebugLine  (W, SlotXf.GetLocation(), TargetHugLoc, FColor::Cyan, false, 2.f, 0, 1.f);
	}
#endif

	// Smooth transition (works for same-bunker and cross-bunker)
	StartLoc = Char->GetActorLocation(); StartRot = Char->GetActorRotation();
	TargetLoc = TargetHugLoc;            TargetRot = TargetHugRot;
	TransitionElapsed = 0.f; bIsTransitioning = true; PendingSlot = SlotIndex;

	// Reset lean and state
	State = ECoverState::Hug; DesiredLeanAxis = 0.f; CurrentLean = 0.f;

	SetInitialShoulderFromView(Normal);
	AlignControllerYawTo(TargetHugRot);
	return true;
}

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
}

bool UBunkerCoverComponent::TraverseBestSlotOnCurrentBunker(int32 /*DesiredIndex*/)
{
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
	if (State != ECoverState::None) State = (FMath::IsNearlyZero(DesiredLeanAxis)) ? ECoverState::Hug : ECoverState::Peek;
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

bool UBunkerCoverComponent::TraverseLeft()
{
	int32 Next = INDEX_NONE;
	if (!FindAdjacentFreeSlotOnCurrentBunker(-1, Next)) return false;
	return EnterCover(CurrentBunker.Get(), Next);
}

bool UBunkerCoverComponent::TraverseRight()
{
	int32 Next = INDEX_NONE;
	if (!FindAdjacentFreeSlotOnCurrentBunker(+1, Next)) return false;
	return EnterCover(CurrentBunker.Get(), Next);
}

void UBunkerCoverComponent::ApplyHugPose(const FTransform& SlotXf, const FVector& Normal)
{
	ACharacter* Char = Cast<ACharacter>(GetOwner()); if (!Char) return;
	FVector HugLoc; FRotator HugRot;
	if (ComputeHugTransform(SlotXf, Normal, HugLoc, HugRot)) { PlaceAtHugTransform(HugLoc, HugRot); }
}

void UBunkerCoverComponent::ApplyLean(float DeltaTime, const FTransform& /*SlotXf*/, const FVector& /*Normal*/)
{
	if (State == ECoverState::None) return;
	ACharacter* Char = Cast<ACharacter>(GetOwner()); if (!Char) return;

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

	FTransform SlotXf; FVector Normal;
	if (!ResolveSlotXf(SlotXf, Normal)) { ExitCover(); return; }

	ACharacter* Char = Cast<ACharacter>(GetOwner()); if (!Char) { ExitCover(); return; }

	// -- Approach phase (Nudge or Nav) --
	if (State == ECoverState::Approach)
	{
		const FVector To = (ApproachLoc - Char->GetActorLocation());
		const float Dist = To.Size2D();

		// Face the wall as we approach
		const FRotator YawOnly(0.f, ApproachRot.Yaw, 0.f);
		Char->SetActorRotation(YawOnly);
		AlignControllerYawTo(ApproachRot);

		if (Dist > ApproachStopDistance)
		{
			if (ApproachMode == ECoverApproachMode::Nudge)
			{
				Char->AddMovementInput(To.GetSafeNormal2D(), ApproachMoveSpeed);
			}
			// Nav mode continues via SimpleMoveToLocation
			return; // still approaching
		}

		if (ApproachMode == ECoverApproachMode::Nav)
		{
			Char->GetCharacterMovement()->StopMovementImmediately();
		}

		// Blend last few centimeters into perfect hug pose
		StartLoc = Char->GetActorLocation(); StartRot = Char->GetActorRotation();
		TargetLoc = ApproachLoc; TargetRot = ApproachRot;
		TransitionElapsed = 0.f;
		bIsTransitioning = true;

		DesiredLeanAxis = 0.f; CurrentLean = 0.f;

		// when starting a slot transition (where you set StartLoc/TargetLoc):
		const float Dist2D = FVector::Dist2D(StartLoc, TargetLoc);
		const float SlideSpeed = 1200.f; // tweak
		SlotTransitionTime = FMath::Max(0.08f, Dist2D / SlideSpeed);
		
		SetInitialShoulderFromView(Normal);
		if (CurrentBunker.IsValid() && CurrentSlot != INDEX_NONE) { ApplyStanceForSlot(CurrentBunker.Get(), CurrentSlot); }
		State = ECoverState::Hug;

		// Restore walking speed now that we arrived
		if (Char->GetCharacterMovement() && SavedMaxWalkSpeed > 0.f) { Char->GetCharacterMovement()->MaxWalkSpeed = SavedMaxWalkSpeed; SavedMaxWalkSpeed = 0.f; }
	}

	// -- Transition blend (slot switch or entry) --
	if (bIsTransitioning)
	{
		TransitionElapsed += DeltaTime;
		const bool bEntryEase = (State == ECoverState::Hug && TransitionElapsed <= EntrySnapBlendTime + SMALL_NUMBER);
		const float Duration  = bEntryEase ? EntrySnapBlendTime : FMath::Max(SlotTransitionTime, KINDA_SMALL_NUMBER);
		const float Alpha     = FMath::Clamp(TransitionElapsed / Duration, 0.f, 1.f);

		// Lerp XY
		const FVector StartXY (StartLoc.X,  StartLoc.Y,  0.f);
		const FVector TargetXY(TargetLoc.X, TargetLoc.Y, 0.f);
		const FVector NewXY = FMath::Lerp(StartXY, TargetXY, Alpha);

		// Lerp rotation
		const FRotator NewRot = FMath::Lerp(StartRot, TargetRot, Alpha);

		// Ground-snap Z at NewXY via capsule sweep
		float Radius=42.f, HalfHeight=88.f;
		if (UCapsuleComponent* Cap = Char->GetCapsuleComponent()) { Cap->GetScaledCapsuleSize(Radius, HalfHeight); }

		const FVector SweepStart(NewXY.X, NewXY.Y, FMath::Max(StartLoc.Z, TargetLoc.Z) + FloorSnapUp);
		const FVector SweepEnd  (NewXY.X, NewXY.Y, FMath::Min(StartLoc.Z, TargetLoc.Z) - FloorSnapDown);

		FHitResult SweepHit;
		FCollisionQueryParams SweepParams(SCENE_QUERY_STAT(CoverSlideSnap), false, GetOwner());
		SweepParams.AddIgnoredActor(GetOwner());
		if (CurrentBunker.IsValid()) SweepParams.AddIgnoredActor(CurrentBunker.Get());
		
		FCollisionObjectQueryParams ObjMask;
		ObjMask.AddObjectTypesToQuery(ECC_WorldStatic);

		const bool bHit = GetWorld()->SweepSingleByObjectType(SweepHit, SweepStart, SweepEnd, FQuat::Identity,
			ObjMask, FCollisionShape::MakeCapsule(Radius, HalfHeight), SweepParams);

		const float FloorZ = bHit ? SweepHit.ImpactPoint.Z : FMath::Lerp(StartLoc.Z, TargetLoc.Z, Alpha);
		const float NewZ   = FloorZ + HalfHeight + FloorSnapPadding;		const FVector NewLoc(NewXY.X, NewXY.Y, NewZ);

		Char->SetActorLocationAndRotation(NewLoc, NewRot, /*bSweep=*/true);

		if (Alpha >= 1.f - KINDA_SMALL_NUMBER)
		{
			bIsTransitioning = false; PendingSlot = INDEX_NONE;
			// Snap exactly to hug transform to avoid small residuals
			PlaceAtHugTransform(NewLoc, NewRot);
		}
		return;
	}

	// -- Hug / Peek: camera shoulder follows lean side (simple rule) --
	if (CachedBoom)
	{
		const float Side = (CurrentLean >= 0.f) ? +1.f : -1.f;
		CachedBoom->SocketOffset = FVector(0.f, Side * CoverShoulderOffset, 0.f);
	}

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
