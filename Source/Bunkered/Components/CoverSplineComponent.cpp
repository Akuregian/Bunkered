// Components/CoverSplineComponent.cpp
#include "Components/CoverSplineComponent.h"

static float ToNormalizedT(const USplineComponent* S, float InputKey)
{
  const float Dist = S->GetDistanceAlongSplineAtSplineInputKey(InputKey);
  const float Len  = FMath::Max(S->GetSplineLength(), 1e-3f);
  return FMath::Clamp(Dist / Len, 0.f, 1.f);
}

float UCoverSplineComponent::FindClosestT(const FVector& World) const
{
  const float Key = FindInputKeyClosestToWorldLocation(World);
  return ClampT(ToNormalizedT(this, Key));
}

FTransform UCoverSplineComponent::GetWorldTransformAtT(float T) const
{
  const float Len  = GetSplineLength();
  const float Dist = ClampT(T) * Len;
  return GetTransformAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::World, true);
}

FVector UCoverSplineComponent::GetTangentAtT(float T) const
{
  const float Len  = GetSplineLength();
  const float Dist = ClampT(T) * Len;
  return GetTangentAtDistanceAlongSpline(Dist, ESplineCoordinateSpace::World).GetSafeNormal();
}

FVector UCoverSplineComponent::GetOutwardAtT(float T) const
{
  const FVector Tg  = GetTangentAtT(T);
  const FVector Up  = FVector::UpVector;
  const FVector Left= Up ^ Tg; // Up × Tangent
  return (bOutwardIsLeft ? Left : -Left).GetSafeNormal();
}

bool UCoverSplineComponent::IsPeekAllowedAt(float T, EPeekDirection Dir, float& OutMaxDepthCm) const
{
  OutMaxDepthCm = 0.f;
  if (Dir == EPeekDirection::None || Dir == EPeekDirection::Over) return false;
  const float x = ClampT(T);
  bool bAllowed = false;
  for (const FPeekRegion& R : PeekRegions)
  {
    if (x >= R.TMin && x <= R.TMax)
    {
      const bool DirOk =
          (R.Allowed == Dir) ||
          (R.Allowed == EPeekDirection::Left  && Dir == EPeekDirection::Left) ||
          (R.Allowed == EPeekDirection::Right && Dir == EPeekDirection::Right);
      if (DirOk) { bAllowed = true; OutMaxDepthCm = FMath::Max(OutMaxDepthCm, R.MaxDepthCm); }
    }
  }
  return bAllowed;
}

ECoverStance UCoverSplineComponent::RequiredStanceAt(float T) const
{
  const float x = ClampT(T);
  for (const FStanceRegion& R : StanceRegions)
  {
    if (x >= R.TMin && x <= R.TMax) return R.Required;
  }
  return ECoverStance::Crouch; // default
}

#if WITH_EDITOR
void UCoverSplineComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
  Super::PostEditChangeProperty(PropertyChangedEvent);
  UpdateSpline();
}

void UCoverSplineComponent::PostEditComponentMove(bool bFinished)
{
  Super::PostEditComponentMove(bFinished);
  UpdateSpline();
}

#endif
