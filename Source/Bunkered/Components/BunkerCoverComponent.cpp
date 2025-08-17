// Components/BunkerCoverComponent.cpp
#include "BunkerCoverComponent.h"
#include "Bunkers/BunkerBase.h"
#include "Components/CoverSplineComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "DrawDebugHelpers.h"

UBunkerCoverComponent::UBunkerCoverComponent()
{
  PrimaryComponentTick.bCanEverTick = false;
  SetIsReplicatedByDefault(true);
}

void UBunkerCoverComponent::BeginPlay()
{
  Super::BeginPlay();
  OwnerCharacter = Cast<ACharacter>(GetOwner());
}

void UBunkerCoverComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);
  DOREPLIFETIME(UBunkerCoverComponent, CurrentBunker);
  DOREPLIFETIME(UBunkerCoverComponent, Stance);
  DOREPLIFETIME(UBunkerCoverComponent, Exposure);
  DOREPLIFETIME(UBunkerCoverComponent, Peek);
  DOREPLIFETIME(UBunkerCoverComponent, CoverAlpha);
}

// ----- Core API -----

bool UBunkerCoverComponent::EnterCoverAtAlpha(ABunkerBase* Bunker, float Alpha)
{
  if (!OwnerCharacter.IsValid() || !Bunker) return false;
  if (!Bunker->GetCoverSpline()) return false;

  if (GetOwner()->HasAuthority())
  {
    CurrentBunker = Bunker;
    CoverAlpha    = FMath::Clamp(Alpha, 0.f, 1.f);
    Stance        = Bunker->GetCoverSpline()->RequiredStanceAtAlpha(CoverAlpha);
    Exposure      = EExposureState::Hidden;
    Peek          = EPeekDirection::None;

    SnapOwnerToCoverAlpha();
    OnRep_Bunker();
    OnRep_CoverAlpha();
    OnRep_StanceExposure();
    OnRep_Peek();
    return true;
  }
  else
  {
    Server_EnterCoverAtAlpha(Bunker, Alpha);
    return true;
  }
}

void UBunkerCoverComponent::ExitCover()
{
  if (GetOwner()->HasAuthority())
  {
    CurrentBunker = nullptr;
    Exposure = EExposureState::Hidden;
    Peek     = EPeekDirection::None;
    OnRep_Bunker(); OnRep_StanceExposure(); OnRep_Peek();
  }
  else
  {
    Server_ExitCover();
  }
}

bool UBunkerCoverComponent::SlideAlongCover(float DeltaAlpha)
{
  if (!IsInCover() || !CurrentBunker) return false;
  UCoverSplineComponent* S = CurrentBunker->GetCoverSpline();
  if (!S) return false;

  if (GetOwner()->HasAuthority())
  {
    const float NewAlpha = S->ClampT(CoverAlpha + DeltaAlpha);
    if (FMath::IsNearlyEqual(NewAlpha, CoverAlpha, 1e-4f)) return false;

    CoverAlpha = NewAlpha;
    // Hide while traversing; peek canceled
    Exposure = EExposureState::Hidden;
    Peek     = EPeekDirection::None;

    Stance = S->RequiredStanceAtAlpha(CoverAlpha);

    SnapOwnerToCoverAlpha();
    OnRep_CoverAlpha();
    OnRep_StanceExposure();
    OnRep_Peek();
    return true;
  }
  else
  {
    Server_SlideAlongCover(DeltaAlpha);
    return true;
  }
}

bool UBunkerCoverComponent::SetStance(ECoverStance NewStance)
{
  if (!IsInCover()) return false;
  if (!IsStanceAllowedAtAlpha(NewStance, CoverAlpha)) return false;

  if (GetOwner()->HasAuthority())
  {
    Stance = NewStance;
    OnRep_StanceExposure();
    return true;
  }
  else
  {
    Server_SetStance(NewStance);
    return true;
  }
}

bool UBunkerCoverComponent::SetPeek(EPeekDirection Direction, bool bEnable)
{
  if (!IsInCover()) return false;

  float MaxDepth = 0.f;
  if (bEnable && !IsPeekAllowedAtAlpha(Direction, CoverAlpha, MaxDepth)) return false;

  if (GetOwner()->HasAuthority())
  {
    Peek     = bEnable ? Direction : EPeekDirection::None;
    Exposure = bEnable ? EExposureState::Peeking : EExposureState::Hidden;

    OnRep_Peek();
    OnRep_StanceExposure();
    return true;
  }
  else
  {
    Server_SetPeek(Direction, bEnable);
    return true;
  }
}

bool UBunkerCoverComponent::SetExposureState(EExposureState NewExposure)
{
  if (!IsInCover()) return false;

  if (GetOwner()->HasAuthority())
  {
    Exposure = NewExposure;
    if (Exposure == EExposureState::Hidden) { Peek = EPeekDirection::None; }
    OnRep_StanceExposure();
    return true;
  }
  else
  {
    Server_SetExposure(NewExposure);
    return true;
  }
}

// ----- Helpers -----

void UBunkerCoverComponent::SnapOwnerToCoverAlpha()
{
  if (!OwnerCharacter.IsValid() || !CurrentBunker) return;
  UCoverSplineComponent* S = CurrentBunker->GetCoverSpline();
  if (!S) return;

  const FTransform WT = S->GetWorldTransformAtAlpha(CoverAlpha);
  FRotator YawOnly = WT.Rotator(); YawOnly.Pitch = 0.f; YawOnly.Roll = 0.f;
  OwnerCharacter->SetActorLocation(WT.GetLocation());
  OwnerCharacter->SetActorRotation(YawOnly);
}

bool UBunkerCoverComponent::IsStanceAllowedAtAlpha(ECoverStance InStance, float Alpha) const
{
  if (!CurrentBunker) return false;
  if (UCoverSplineComponent* S = CurrentBunker->GetCoverSpline())
  {
    return (S->RequiredStanceAtAlpha(Alpha) == InStance);
  }
  return true;
}

bool UBunkerCoverComponent::IsPeekAllowedAtAlpha(EPeekDirection InPeek, float Alpha, float& OutMaxDepthCm) const
{
  OutMaxDepthCm = 0.f;
  if (InPeek == EPeekDirection::None) return true;
  if (!CurrentBunker) return false;
  if (UCoverSplineComponent* S = CurrentBunker->GetCoverSpline())
  {
    return S->IsPeekAllowedAtAlpha(Alpha, InPeek, OutMaxDepthCm);
  }
  return false;
}

// ----- RepNotifies -----

void UBunkerCoverComponent::OnRep_Bunker()
{
  // no-op; animation/gameplay systems can listen to other notifies
}

void UBunkerCoverComponent::OnRep_CoverAlpha()
{
  OnCoverAlphaChanged.Broadcast(CoverAlpha);
  SnapOwnerToCoverAlpha();
}

void UBunkerCoverComponent::OnRep_StanceExposure()
{
  OnStanceChanged.Broadcast(Stance, Exposure);
}

void UBunkerCoverComponent::OnRep_Peek()
{
  OnPeekChanged.Broadcast(Peek, Peek != EPeekDirection::None);

  // Debug visualize peek outward axis (optional)
  if (CurrentBunker && CurrentBunker->GetCoverSpline())
  {
    const FTransform WT = CurrentBunker->GetCoverSpline()->GetWorldTransformAtAlpha(CoverAlpha);
    const FVector P = WT.GetLocation();
    const FVector Out = CurrentBunker->GetCoverSpline()->GetOutwardAtAlpha(CoverAlpha);
    const float Sign = (Peek == EPeekDirection::Left) ? -1.f : (Peek == EPeekDirection::Right ? +1.f : 0.f);
    DrawDebugLine(GetWorld(), P, P + Out * (Sign * 40.f), FColor::Red, false, 2.f, 0, 2.f);
  }
}

// ----- RPC impls -----

void UBunkerCoverComponent::Server_EnterCoverAtAlpha_Implementation(ABunkerBase* Bunker, float Alpha)
{
  EnterCoverAtAlpha(Bunker, Alpha);
}

void UBunkerCoverComponent::Server_ExitCover_Implementation()
{
  ExitCover();
}

void UBunkerCoverComponent::Server_SlideAlongCover_Implementation(float DeltaAlpha)
{
  SlideAlongCover(DeltaAlpha);
}

void UBunkerCoverComponent::Server_SetStance_Implementation(ECoverStance NewStance)
{
  SetStance(NewStance);
}

void UBunkerCoverComponent::Server_SetPeek_Implementation(EPeekDirection Direction, bool bEnable)
{
  SetPeek(Direction, bEnable);
}

void UBunkerCoverComponent::Server_SetExposure_Implementation(EExposureState NewExposure)
{
  SetExposureState(NewExposure);
}
