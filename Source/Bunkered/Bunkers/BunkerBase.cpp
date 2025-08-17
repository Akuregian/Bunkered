// Bunkers/BunkerBase.cpp
#include "BunkerBase.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/CoverSplineComponent.h"
#include "Components/SplineComponent.h"
#include "Misc/DataValidation.h"

ABunkerBase::ABunkerBase()
{
  Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
  RootComponent = Root;

  Bunker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
  Bunker->SetupAttachment(RootComponent);
  Bunker->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
  Bunker->SetCanEverAffectNavigation(false);

  CoverSpline = CreateDefaultSubobject<UCoverSplineComponent>(TEXT("CoverSpline"));
  CoverSpline->SetupAttachment(RootComponent);
}

#if WITH_EDITOR
void ABunkerBase::OnConstruction(const FTransform& Transform)
{
  Super::OnConstruction(Transform);

  if (bAutoInitSplinePoints && CoverSpline)
  {
    InitializeCoverSplineDefaults();
  }

  if (CoverSpline && !bOverrideSplineInInstance)
  {
    if (UCoverSplineComponent* TemplateSpline = Cast<UCoverSplineComponent>(CoverSpline->GetArchetype()))
    {
      CoverSpline->CopyFrom(TemplateSpline);
    }
  }

  if (CoverSpline && CoverSpline->GetNumberOfSplinePoints() == 1)
  {
    const FVector Center = GetActorLocation();
    const FVector Right  = GetActorRightVector();

    float ExtentY = 100.f;
    if (Bunker)
    {
      ExtentY = Bunker->Bounds.BoxExtent.Y; // half-width
    }

    FVector Outer = Center + Right * (ExtentY + DefaultOutwardMargin);
    Outer.Z = Center.Z + DefaultCoverPointZ;

    CoverSpline->SetLocationAtSplinePoint(0, Outer, ESplineCoordinateSpace::World, false);
    CoverSpline->UpdateSpline();
  }
}

EDataValidationResult ABunkerBase::IsDataValid(FDataValidationContext& Context) const
{
  if (CoverSpline == nullptr)
  {
    Context.AddWarning(FText::FromString(TEXT("Bunker has no CoverSpline; add one to enable cover traversal.")));
  }
  return EDataValidationResult::Valid;
}

static bool IsDefaultCenterPoint(const USplineComponent* Spline, int32 PointIdx, float Tol=10.f)
{
    const FVector P = Spline->GetLocationAtSplinePoint(PointIdx, ESplineCoordinateSpace::World);
    const FVector C = Spline->GetComponentLocation(); // where UE spawns the starter point
    return FVector::DistSquared(P, C) <= FMath::Square(Tol);
}

void ABunkerBase::InitializeCoverSplineDefaults()
{
  if (!CoverSpline) return;

  // Ensure not closed loop by default.
  CoverSpline->SetClosedLoop(false, true);

  // 1) Remove any “starter” point that sits at (or very near) actor center.
  //    Also catch the case where the designer added a point and the default center still lingers.
  {
    const int32 Num = CoverSpline->GetNumberOfSplinePoints();
    for (int32 i = Num - 1; i >= 0; --i)
    {
      if (IsDefaultCenterPoint(CoverSpline, i))
      {
        CoverSpline->RemoveSplinePoint(i, false);
      }
    }
  }

  // 2) If the spline has no points now, seed a single “outer” point.
  if (CoverSpline->GetNumberOfSplinePoints() == 0)
  {
    // Compute an outward point based on the bunker mesh bounds.
    const FVector Center  = GetActorLocation();
    const FVector Right   = GetActorRightVector();

    float ExtentY = 100.f; // fallback if no mesh
    if (Bunker)
    {
      // Bounds are world-space extents (half sizes)
      ExtentY = Bunker->Bounds.BoxExtent.Y;
    }

    FVector Outer = Center + Right * (ExtentY + DefaultOutwardMargin);
    Outer.Z = Center.Z + DefaultCoverPointZ;

    CoverSpline->AddSplinePoint(Outer, ESplineCoordinateSpace::World, false);
  }

  // 3) Normalize tangents (not strictly needed for a single point; safe no-op).
  CoverSpline->UpdateSpline();
}
#endif
