// Components/BunkerCoverComponent.cpp
#include "BunkerCoverComponent.h"
#include "Bunkers/BunkerBase.h"
#include "Components/CoverSplineComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "DrawDebugHelpers.h"
#include "Components/CapsuleComponent.h"
#include "Utility/LoggingMacros.h"

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
  if (!GetOwner()->HasAuthority()) { Server_EnterCoverAtAlpha(Bunker, Alpha); return true; }
  if (!Bunker) return false;

  UCoverSplineComponent* S = Bunker->GetCoverSpline();
  if (!S) return false;

  const float ClampedAlpha = S->ClampT(Alpha);

  // Early-out if nothing would change (prevents extra snaps/net traffic)
  if (CurrentBunker == Bunker && FMath::IsNearlyEqual(CoverAlpha, ClampedAlpha, 1e-4f))
    return false;

  ABunkerBase* Old = CurrentBunker;
  CurrentBunker = Bunker;
  CoverAlpha    = ClampedAlpha;
  Stance        = S->RequiredStanceAtAlpha(CoverAlpha);
  Exposure      = EExposureState::Hidden;
  Peek          = EPeekDirection::None;

  HandleBunkerChanged(Old, CurrentBunker, /*bServerSide=*/true);
  HandleCoverAlphaChanged();
  HandleStanceExposureChanged();
  HandlePeekChanged();

  GetOwner()->ForceNetUpdate(); // one-shot: makes enter feel crisp on clients
  return true;
}

void UBunkerCoverComponent::ExitCover()
{
  if (!GetOwner()->HasAuthority()) { Server_ExitCover(); return; }

  ABunkerBase* Old = CurrentBunker;
  CurrentBunker = nullptr;
  Stance   = ECoverStance::Stand;   // <- reset
  Exposure = EExposureState::Hidden;
  Peek     = EPeekDirection::None;

  HandleBunkerChanged(Old, nullptr, /*bServerSide=*/true);
  HandleStanceExposureChanged();
  HandlePeekChanged();

  GetOwner()->ForceNetUpdate(); // one-shot: crisp replication of exit
}

bool UBunkerCoverComponent::SlideAlongCover(float DeltaAlpha)
{
  if (!IsInCover() || !CurrentBunker) return false;
  UCoverSplineComponent* S = CurrentBunker->GetCoverSpline();
  if (!S) return false;

  if (!GetOwner()->HasAuthority())
  {
    Server_SlideAlongCover(DeltaAlpha);
    return true; // (Optional: add client prediction later)
  }

  const float NewAlpha = S->ClampT(CoverAlpha + DeltaAlpha);
  if (FMath::IsNearlyEqual(NewAlpha, CoverAlpha, 1e-4f)) return false;

  const float OldAlpha = CoverAlpha;
  CoverAlpha = NewAlpha;

  // Update stance from spline (no forced exposure/peek changes here)
  const ECoverStance NewStance = S->RequiredStanceAtAlpha(CoverAlpha);
  if (Stance != NewStance)
  {
    Stance = NewStance;
    HandleStanceExposureChanged();
  }

  HandleCoverAlphaChanged(); // broadcasts + snaps owner

  // NOTE: do NOT call OnRep_* here; clients will get real OnRep via replication.
  // Also avoid ForceNetUpdate() for every slide – thass too chatty for the server.

  return true;
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
  const FVector Out = S->GetOutwardAtAlpha(CoverAlpha); // face normal
  const float CapsuleR = OwnerCharacter->GetCapsuleComponent()->GetScaledCapsuleRadius();
  const FVector Target = WT.GetLocation() + Out * (CapsuleR * 0.9f);
  
  FRotator YawOnly = WT.Rotator();
  YawOnly.Pitch = 0.f;
  YawOnly.Roll = 0.f;
  FHitResult Hit;
  
  OwnerCharacter->SetActorLocation(Target, true /*bSweep*/, &Hit, ETeleportType::None);
  OwnerCharacter->SetActorRotation(YawOnly);
  
  // (Optional) if Hit.bBlockingHit, nudge a bit more along outward or back off slightly}
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
  HandleBunkerChanged(LastReplicatedBunker, CurrentBunker, /*bServerSide=*/false);
}

void UBunkerCoverComponent::OnRep_CoverAlpha()
{
  HandleCoverAlphaChanged();
}

void UBunkerCoverComponent::OnRep_StanceExposure()
{
  HandleStanceExposureChanged();
}

void UBunkerCoverComponent::OnRep_Peek()
{
  HandlePeekChanged();
}

void UBunkerCoverComponent::HandleBunkerChanged(ABunkerBase* OldBunker, ABunkerBase* NewBunker, bool bServerSide)
{
  DEBUG(5.0f, FColor::Magenta, TEXT("UBunkerCoverComponent::HandleBunkerChanged()"));

  // Broadcast for UI/Anim/other systems
  OnBunkerChanged.Broadcast(OldBunker, NewBunker);

  // Server can snap immediately (authoritative location).
  // Client must NOT snap here due to replication-order ambiguity—OnRep_CoverAlpha will snap.
  if (bServerSide && NewBunker)
  {
    SnapOwnerToCoverAlpha();
  }

  LastReplicatedBunker = NewBunker; // keep for client Old/New pairs
}

void UBunkerCoverComponent::HandleCoverAlphaChanged()
{
  OnCoverAlphaChanged.Broadcast(CoverAlpha);
  SnapOwnerToCoverAlpha();
}

void UBunkerCoverComponent::HandleStanceExposureChanged()
{
  OnStanceChanged.Broadcast(Stance, Exposure);
}

void UBunkerCoverComponent::HandlePeekChanged()
{
  OnPeekChanged.Broadcast(Peek, Peek != EPeekDirection::None);
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
