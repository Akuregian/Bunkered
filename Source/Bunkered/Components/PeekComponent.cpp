// Components/PeekComponent.cpp
#include "Components/PeekComponent.h"
#include "Components/BunkerCoverComponent.h"
#include "Bunkers/BunkerBase.h"
#include "Components/SphereComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/SpringArmComponent.h"
#include "Net/UnrealNetwork.h"

UPeekComponent::UPeekComponent() { PrimaryComponentTick.bCanEverTick = false; SetIsReplicatedByDefault(true); }

void UPeekComponent::BeginPlay()
{
  Super::BeginPlay();
  if (ACharacter* C = Cast<ACharacter>(GetOwner()))
  {
    Cover = C->FindComponentByClass<UBunkerCoverComponent>();
    if (Cover.IsValid())
    {
      Cover->OnSlotChanged.AddDynamic(this, &UPeekComponent::RefreshAnchorFromCover);
      RefreshAnchorFromCover(0);
    }
  }
  EnsureProxies();
}

void UPeekComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);
  DOREPLIFETIME(UPeekComponent, Net);
}

void UPeekComponent::EnsureProxies()
{
  if (HeadHit) return;
  auto* Owner = GetOwner();
  auto mk = [&](const FName& N, float R)->USphereComponent*{
    auto* S = NewObject<USphereComponent>(Owner, N);
    S->SetupAttachment(Owner->GetRootComponent());
    S->InitSphereRadius(R);
    S->SetCollisionEnabled(ECollisionEnabled::NoCollision); // enabled server-side only when peeking
    S->RegisterComponent();
    return S;
  };
  HeadHit = mk(TEXT("HeadHit"), HeadRadius);
  ShoulderHit = mk(TEXT("ShoulderHit"), HeadRadius*1.2f);
  GunHit = mk(TEXT("GunHit"), HeadRadius*0.8f);
}

void UPeekComponent::RefreshAnchorFromCover(int32 Index)
{
  if (!Cover.IsValid() || !Cover->IsInCover()) return;
  
  ABunkerBase* B = Cover->GetCurrentBunker();
  const int32 Slot = Cover->GetCurrentSlot();
  const FTransform WT = B->GetSlotWorldTransform(Slot);
  
  Anchor.SlotOrigin = WT.GetLocation();
  Anchor.EdgeRight  = WT.GetUnitAxis(EAxis::Y);      // world “right”
  Anchor.PlaneNormal= Anchor.EdgeRight;              // outward approx
  Anchor.PlaneD     = -FVector::DotProduct(Anchor.PlaneNormal, Anchor.SlotOrigin);
}

bool UPeekComponent::CanPeekNow() const
{
  const double Now = GetWorld()->GetTimeSeconds();
  if (!Cover.IsValid() || !Cover->IsInCover()) return false;
  return (Now - LastRetractTime) >= Settings.RefractorySec;
}

bool UPeekComponent::IsDirAllowed(EPeekDirection Dir) const
{
  if (!Cover.IsValid() || !Cover->IsInCover()) return false;
  return Cover->GetPeek() == EPeekDirection::None && Cover->GetCurrentBunker() &&
         Cover->GetCurrentSlot() != INDEX_NONE &&
         Cover->IsInCover(); // additional per-slot check happens server-side via CoverComp
}

void UPeekComponent::HandlePeekInput(EPeekDirection Dir, bool bPressed)
{
  if (!Cover.IsValid() || Dir==EPeekDirection::None) return;
  const double Now = GetWorld()->GetTimeSeconds();

  if (bPressed)
  {
    if (!CanPeekNow() || !IsDirAllowed(Dir)) return;

    const double& Last = (Dir==EPeekDirection::Left) ? LastPressTimeL : LastPressTimeR;
    const bool bDouble = (Now - Last) <= 0.25;
    if (Dir==EPeekDirection::Left) LastPressTimeL = Now; else LastPressTimeR = Now;

    EPeekMode Mode = bDouble ? EPeekMode::Toggle : EPeekMode::Hold;
    bHoldCandidate = !bDouble;
    HoldStartTime = Now;

    const float aReq = Settings.MinBurstOffsetCm / Settings.MaxOffsetCm;
    const uint8 Hint = (uint8)FMath::RoundToInt(255.f * aReq);
    Server_BeginPeek(Dir, Mode, Hint);
  }
  else
  {
    // release
    if (Net.Mode == EPeekMode::Toggle) return; // stays out until toggled
    const double Held = Now - HoldStartTime;
    if (bHoldCandidate && Held < 0.15) // TAP -> Burst
    {
      // Start a Burst (out to MinBurst then retract)
      Server_BeginPeek(Net.Dir, EPeekMode::Burst, (uint8)FMath::RoundToInt(255.f*(Settings.MinBurstOffsetCm/Settings.MaxOffsetCm)));
    }
    else
    {
      Server_StopPeek();
    }
    bHoldCandidate = false;
  }
}

void UPeekComponent::Server_BeginPeek_Implementation(EPeekDirection Dir, EPeekMode Mode, uint8 Hint)
{
  if (!Cover.IsValid() || !Cover->IsInCover()) return;
  if (!Cover->SetPeek(Dir, true)) return; // validates per-slot peeks & sets Exposure=Peeking (replicated) 
  Net.Dir = Dir; Net.Mode = Mode;

  const float req = (Hint/255.f);
  const float amin= ComputeAlphaMinForClearance();
  const float a   = FMath::Clamp(FMath::Max(req, amin), 0.f, 1.f);
  Net.PeekDepthQ = (uint8)FMath::RoundToInt(255.f * a);
  OnRep_Net();           // drive proxies here on server
  StartOut(Mode);        // timers drive α changes, then StartIn() for Burst
}

void UPeekComponent::Server_StopPeek_Implementation()
{
  if (!Cover.IsValid()) return;
  Cover->SetPeek(Net.Dir, false);
  Net.Dir = EPeekDirection::None; Net.Mode = EPeekMode::None; Net.PeekDepthQ = 0;
  OnRep_Net();
  LastRetractTime = GetWorld()->GetTimeSeconds();
}

void UPeekComponent::OnRep_Net()
{
  AlphaLocal = Net.PeekDepthQ / 255.f;
  UpdateProxies(AlphaLocal);
  ApplyCameraTilt();
}

float UPeekComponent::ComputeAlphaMinForClearance() const
{
  // Approximate plane with EdgeRight outward; use gun local point
  const FVector n = Anchor.PlaneNormal.GetSafeNormal();
  const float   ndote = FVector::DotProduct(n, Anchor.EdgeRight);
  const FVector M0 = GetOwner()->GetActorTransform().TransformPosition(GunLocal);
  const float δ0 = FVector::DotProduct(n, M0) + Anchor.PlaneD;
  const float num = Settings.MarginClearanceCm - δ0;
  const float den = FMath::Max(Settings.MaxOffsetCm * ndote, 1e-3f);
  return FMath::Clamp(num/den, 0.f, 1.f);
}

void UPeekComponent::StartOut(EPeekMode Mode)
{
  GetWorld()->GetTimerManager().ClearTimer(OutHandle);
  const float T = Settings.TimeOut;
  const float Start = AlphaLocal;
  const float Target= (Mode==EPeekMode::Burst || Mode==EPeekMode::Toggle)
                    ? (Settings.MinBurstOffsetCm/Settings.MaxOffsetCm)
                    : FMath::Max(Start, Settings.MinBurstOffsetCm/Settings.MaxOffsetCm);
  const double Beg = GetWorld()->GetTimeSeconds();

  GetWorld()->GetTimerManager().SetTimer(OutHandle, [this, Beg, T, Start, Target, Mode]()
  {
    const double t = FMath::Clamp((GetWorld()->GetTimeSeconds()-Beg)/T, 0.0, 1.0);
    const float a = Start + (Target-Start) * (1 - FMath::Pow(1 - t, 3));
    Net.PeekDepthQ = (uint8)FMath::RoundToInt(255.f*a);
    OnRep_Net();

    if (t>=1.0)
    {
      GetWorld()->GetTimerManager().ClearTimer(OutHandle);
      if (Net.Mode==EPeekMode::Burst) StartIn();
    }
  }, 0.0f, true);
}

void UPeekComponent::StartIn()
{
  GetWorld()->GetTimerManager().ClearTimer(InHandle);
  const float T = Settings.TimeIn;
  const float Start = AlphaLocal;
  const double Beg = GetWorld()->GetTimeSeconds();

  GetWorld()->GetTimerManager().SetTimer(InHandle, [this, Beg, T, Start]()
  {
    const double t = FMath::Clamp((GetWorld()->GetTimeSeconds()-Beg)/T, 0.0, 1.0);
    const float a = Start * FMath::Pow(1 - t, 3);
    Net.PeekDepthQ = (uint8)FMath::RoundToInt(255.f*a);
    OnRep_Net();

    if (t>=1.0)
    {
      GetWorld()->GetTimerManager().ClearTimer(InHandle);
      Server_StopPeek(); // ends exposure + refractory
    }
  }, 0.0f, true);
}

void UPeekComponent::UpdateProxies(float a)
{
  const FVector off = Anchor.EdgeRight * (a * Settings.MaxOffsetCm);
  const FTransform T = GetOwner()->GetActorTransform();
  auto place=[&](USphereComponent* S, const FVector& local){
    if (!S) return;
    S->SetWorldLocation(T.TransformPosition(local + off));
    const bool bActive = (Net.Dir != EPeekDirection::None && a>0.f);
    S->SetCollisionEnabled( (bActive && GetOwner()->HasAuthority()) ? ECollisionEnabled::QueryOnly
                                                                    : ECollisionEnabled::NoCollision );
  };
  place(HeadHit, HeadLocal);
  place(ShoulderHit, ShoulderLocal);
  place(GunHit, GunLocal);
}

void UPeekComponent::ApplyCameraTilt()
{
  if (ACharacter* C = Cast<ACharacter>(GetOwner()))
  {
    if (USpringArmComponent* Boom = C->FindComponentByClass<USpringArmComponent>())
    {
      const float sign = (Net.Dir==EPeekDirection::Left) ? -1.f : (Net.Dir==EPeekDirection::Right?+1.f:0.f);
      const float roll = sign * Settings.CameraTiltDeg * AlphaLocal;
      FRotator R = Boom->GetRelativeRotation(); R.Roll = roll; Boom->SetRelativeRotation(R);
      // optional: lateral socket offset (visual only)
      Boom->SocketOffset = FVector(0, sign * AlphaLocal * Settings.MaxOffsetCm, 0);
    }
  }
}
