#include "PreloadMap.h"
#include "PreloadMap_ConfigurationStruct.h"

#include "FGMapFunctionLibrary.h"
#include "HAL/IConsoleManager.h"
#include "Configuration/ConfigManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Kismet/GameplayStatics.h"
#include "Algo/Reverse.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

#define LOCTEXT_NAMESPACE "PreloadMap"

DEFINE_LOG_CATEGORY_STATIC(LogPreloadMap, Log, All);

namespace
{

	UWorld* FindAnyGameWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}

		for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
		{
			UWorld* World = WorldContext.World();
			if (World != nullptr && World->IsGameWorld())
			{
				return World;
			}
		}

		return nullptr;
	}
}

APreloadMapGhostActor::APreloadMapGhostActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetReplicates(false);
	SetCanBeDamaged(false);
	SetActorHiddenInGame(true);

	RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(RootSceneComponent);

	StreamingSourceComponent = CreateDefaultSubobject<UWorldPartitionStreamingSourceComponent>(TEXT("StreamingSource"));
	StreamingSourceComponent->Priority = EStreamingSourcePriority::Highest;
}

void APreloadMapGhostActor::ConfigureRadius(float RadiusCm)
{
	if (!StreamingSourceComponent)
	{
		return;
	}

	StreamingSourceComponent->Shapes.Reset();

	FStreamingSourceShape Shape;
	Shape.bUseGridLoadingRange = false;
	Shape.Radius = RadiusCm;
	Shape.bIsSector = false;
	Shape.Location = FVector::ZeroVector;
	Shape.Rotation = FRotator::ZeroRotator;

	StreamingSourceComponent->Shapes.Add(Shape);
	StreamingSourceComponent->EnableStreamingSource();
}

UPreloadMapSubsystem* UPreloadMapSubsystem::Get(const UObject* WorldContextObject)
{
	if (WorldContextObject == nullptr)
	{
		return nullptr;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	return World->GetSubsystem<UPreloadMapSubsystem>();
}

UPreloadMapSubsystem* UPreloadMapSubsystem::GetFromAnyWorld()
{
	UWorld* World = FindAnyGameWorld();
	return World ? World->GetSubsystem<UPreloadMapSubsystem>() : nullptr;
}

void UPreloadMapSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	StatusWidgetClass = LoadClass<UPreloadStatusWidget>(
		nullptr,
		TEXT("/PreloadMap/WBP_PreloadStatus.WBP_PreloadStatus_C"));

	if (!StatusWidgetClass)
	{
		UE_LOG(LogPreloadMap, Warning,
			TEXT("Failed to load WBP_PreloadStatus"));
	}
}

void UPreloadMapSubsystem::Deinitialize()
{
	DestroyGhost();
	TraversalPoints.Empty();
	Super::Deinitialize();
}

FPreloadMapRuntimeConfig UPreloadMapSubsystem::ReadConfiguration() const
{
	FPreloadMapRuntimeConfig Result;

	const auto Config =
		FPreloadMap_ConfigurationStruct::GetActiveConfig(GetWorld());

	UE_LOG(
		LogPreloadMap,
		Display,
		TEXT("CONFIG: Auto=%d RadiusPower=%d WholeMap=%d"),
		Config.AutoPreload,
		Config.RadiusPower,
		Config.WholeMap);

	Result.AutoPreload = Config.AutoPreload;
	Result.RadiusPower = Config.RadiusPower;
	Result.WholeMap = Config.WholeMap;

	return Result;
}

void UPreloadMapSubsystem::Tick(float DeltaTime)
{
	TryAutoStart();

	if (!bPreloadRunning || GhostActor == nullptr || GhostActor->StreamingSourceComponent == nullptr)
	{
		return;
	}

	if (!bWaitingForStreaming)
	{
		return;
	}

	if (GhostActor->StreamingSourceComponent->IsStreamingCompleted())
	{
		++StableFrames;

		if (StableFrames >= 2)
		{
			const double PointTime =
				FPlatformTime::Seconds() - LastPointStartTime;

			const int32 Completed =
				CurrentPointIndex + 1;

			AveragePointTime =
				(
					AveragePointTime * FMath::Max(Completed - 1, 0)
					+ PointTime
					)
				/
				FMath::Max(Completed, 1);

			AdvanceToNextPoint();
		}
	}
	else
	{
		StableFrames = 0;
	}
}

void UPreloadStatusWidget::NativeTick(
	const FGeometry& MyGeometry,
	float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);

	UPreloadMapSubsystem* S = UPreloadMapSubsystem::Get(this);

	if (!S)
	{
		return;
	}

	ProgressBar->SetPercent(S->GetProgress());

	PercentText->SetText(
		FText::FromString(
			FString::Printf(TEXT("%.1f%%"),
				S->GetProgress() * 100.f)));

	ProgressText->SetText(
		FText::FromString(
			FString::Printf(TEXT("%d / %d"),
				S->GetCurrentPoint(),
				S->GetTotalPoints())));

	ETAText->SetText(
		FText::FromString(
			FString::Printf(TEXT("%.0fs"),
				S->GetEstimatedRemainingSeconds())));
}

TStatId UPreloadMapSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPreloadMapSubsystem, STATGROUP_Tickables);
}

bool UPreloadMapSubsystem::IsPreloadRunning() const
{
	return bPreloadRunning;
}

float UPreloadMapSubsystem::GetProgress() const
{
	if (TraversalPoints.Num() == 0)
	{
		return 0.0f;
	}

	return FMath::Clamp(
		static_cast<float>(CurrentPointIndex + 1) /
		static_cast<float>(TraversalPoints.Num()),
		0.0f,
		1.0f);
}

int32 UPreloadMapSubsystem::GetCurrentPoint() const
{
	return FMath::Max(CurrentPointIndex + 1, 0);
}

FVector UPreloadMapSubsystem::GetCurrentTargetLocation() const
{
	if (TraversalPoints.IsValidIndex(CurrentPointIndex))
	{
		return TraversalPoints[CurrentPointIndex];
	}

	return FVector::ZeroVector;
}

double UPreloadMapSubsystem::GetEstimatedRemainingSeconds() const
{
	const int32 Remaining =
		GetTotalPoints() - GetCurrentPoint();

	return Remaining * AveragePointTime;
}

int32 UPreloadMapSubsystem::GetTotalPoints() const
{
	return TraversalPoints.Num();
}

double UPreloadMapSubsystem::GetLastElapsedSeconds() const
{
	return LastElapsedSeconds;
}

int32 UPreloadMapSubsystem::GetLastVisitedPoints() const
{
	return LastVisitedPoints;
}

void UPreloadMapSubsystem::TryAutoStart()
{
	if (bAutoStartChecked || bPreloadRunning)
	{
		return;
	}

	const FPreloadMapRuntimeConfig Config = ReadConfiguration();

	if (!Config.AutoPreload)
	{
		bAutoStartChecked = true;
		return;
	}

	APlayerController* PlayerController =
		UGameplayStatics::GetPlayerController(GetWorld(), 0);

	if (!PlayerController)
	{
		return;
	}

	APawn* Pawn = PlayerController->GetPawn();

	if (!Pawn)
	{
		return;
	}

	if (!Pawn->HasActorBegunPlay())
	{
		return;
	}

	if (!PlayerController->PlayerState)
	{
		return;
	}

	if (AutoStartDelayEndTime == 0.0)
	{
		AutoStartDelayEndTime = FPlatformTime::Seconds() + 3.0;
		return;
	}

	if (FPlatformTime::Seconds() < AutoStartDelayEndTime)
	{
		return;
	}

	bAutoStartChecked = true;
	AutoStartDelayEndTime = 0.0;
	StartPreload(false);
}
bool UPreloadMapSubsystem::ResolveMapBounds(FBox2D& OutBounds, float& OutSuggestedZ) const
{
	FVector2D Min;
	FVector2D Max;

	UFGMapFunctionLibrary::GetWorldBounds(GetWorld(), Min, Max);


	if (Min.Equals(Max))
	{
		UE_LOG(LogPreloadMap, Error, TEXT("GetWorldBounds returned invalid bounds."));
		return false;
	}

	OutBounds = FBox2D(Min, Max);

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			OutSuggestedZ = Pawn->GetActorLocation().Z;
		}
		else
		{
			OutSuggestedZ = 0.f;
		}
	}
	else
	{
		OutSuggestedZ = 0.f;
	}

	UE_LOG(
		LogPreloadMap,
		Display,
		TEXT("World bounds: Min=(%.0f, %.0f) Max=(%.0f, %.0f)"),
		Min.X,
		Min.Y,
		Max.X,
		Max.Y);

	return true;
}

FPreloadMapRuntimeState UPreloadMapSubsystem::GetRuntimeState() const
{
	FPreloadMapRuntimeState State;

	State.bRunning = bPreloadRunning;
	State.bWaitingForStreaming = bWaitingForStreaming;

	State.CurrentPoint = GetCurrentPoint();
	State.TotalPoints = GetTotalPoints();

	State.Progress = GetProgress();

	State.Status = GetStatus();

	return State;
}

EPreloadStatus UPreloadMapSubsystem::GetStatus() const
{
	return CurrentStatus;
}

float UPreloadMapSubsystem::GetRadiusMeters() const
{
	const FPreloadMapRuntimeConfig Config = ReadConfiguration();

	return static_cast<float>(1 << Config.RadiusPower);
}

float UPreloadMapSubsystem::FitRadiusToWholeMap() const
{
	FBox2D Bounds;
	float Z;

	if (!ResolveMapBounds(Bounds, Z))
	{
		return GetRadiusMeters();
	}

	const FVector2D Center = (Bounds.Min + Bounds.Max) * 0.5f;

	const float RadiusCm = FVector2D::Distance(Center, Bounds.Max);

	return RadiusCm / 100.0f;
}

void UPreloadMapSubsystem::CancelPreload()
{
	if (!bPreloadRunning)
	{
		return;
	}

	FinishPreload(EPreloadStatus::Cancelled);
}

void UPreloadMapSubsystem::BuildTraversalPoints(const FBox2D& Bounds, float RadiusCm, float TravelZ, TArray<FVector>& OutPoints) const
{
	OutPoints.Reset();

	const FVector2D Center = (Bounds.Min + Bounds.Max) * 0.5f;
	const float ClampedRadiusCm = FMath::Max(RadiusCm, 1000.0f);

	const FVector2D InnerMin = Bounds.Min + FVector2D(ClampedRadiusCm, ClampedRadiusCm);
	const FVector2D InnerMax = Bounds.Max - FVector2D(ClampedRadiusCm, ClampedRadiusCm);

	if (InnerMin.X >= InnerMax.X || InnerMin.Y >= InnerMax.Y)
	{
		OutPoints.Add(FVector(Center.X, Center.Y, TravelZ));
		return;
	}

	const float HorizontalStep = ClampedRadiusCm * 1.75f;
	const float VerticalStep = ClampedRadiusCm * 1.50f;

	int32 RowIndex = 0;
	for (float Y = InnerMin.Y; Y <= InnerMax.Y + KINDA_SMALL_NUMBER; Y += VerticalStep, ++RowIndex)
	{
		const float RowOffset = (RowIndex % 2 == 0) ? 0.0f : HorizontalStep * 0.5f;
		const float StartX = InnerMin.X + RowOffset;

		TArray<FVector> RowPoints;
		for (float X = StartX; X <= InnerMax.X + KINDA_SMALL_NUMBER; X += HorizontalStep)
		{
			RowPoints.Add(FVector(X, Y, TravelZ));
		}

		if (RowPoints.Num() == 0)
		{
			RowPoints.Add(FVector(Center.X, Y, TravelZ));
		}

		if (RowIndex % 2 != 0)
		{
			Algo::Reverse(RowPoints);
		}

		OutPoints.Append(RowPoints);
	}

	if (OutPoints.Num() == 0)
	{
		OutPoints.Add(FVector(Center.X, Center.Y, TravelZ));
	}
}

void UPreloadMapSubsystem::SpawnOrReuseGhost(float RadiusCm, const FVector& SpawnLocation)
{
	if (GhostActor == nullptr)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = TEXT("PreloadMapGhostActor");
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		GhostActor = GetWorld()->SpawnActor<APreloadMapGhostActor>(SpawnLocation, FRotator::ZeroRotator, SpawnParameters);
	}

	if (GhostActor != nullptr)
	{
		GhostActor->ConfigureRadius(RadiusCm);
		GhostActor->SetActorLocation(SpawnLocation, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void UPreloadMapSubsystem::StartPreload(bool bManualStart)
{
	if (bPreloadRunning)
	{
		UE_LOG(LogPreloadMap, Display, TEXT("Preload already running. Ignoring new request."));
		return;
	}

	const FPreloadMapRuntimeConfig Config = ReadConfiguration();

	float RadiusMeters = GetRadiusMeters();

	if (Config.WholeMap)
	{
		RadiusMeters = FitRadiusToWholeMap();
		UE_LOG(
			LogPreloadMap,
			Display,
			TEXT("Whole Map enabled. Calculated radius = %.0f m"),
			RadiusMeters);
	}

	const float RadiusCm = RadiusMeters * 100.0f;

	FBox2D MapBounds;
	float SuggestedZ = 0.0f;
	if (!ResolveMapBounds(MapBounds, SuggestedZ))
	{
		UE_LOG(LogPreloadMap, Warning, TEXT("Failed to resolve map bounds. Preload cannot start."));
		return;
	}

	float TravelZ = SuggestedZ;
	CurrentStatus = EPreloadStatus::Preparing;

	BuildTraversalPoints(MapBounds, RadiusCm, TravelZ, TraversalPoints);
	if (TraversalPoints.Num() == 0)
	{
		UE_LOG(LogPreloadMap, Warning, TEXT("No traversal points were generated. Preload cannot start."));
		return;
	}

	SpawnOrReuseGhost(RadiusCm, TraversalPoints[0]);
	if (GhostActor == nullptr || GhostActor->StreamingSourceComponent == nullptr)
	{
		UE_LOG(LogPreloadMap, Warning, TEXT("Failed to spawn preload ghost actor."));
		CurrentStatus = EPreloadStatus::Failed;
		return;
	}

	CurrentPointIndex = INDEX_NONE;
	StableFrames = 0;

	PreloadStartTime = FPlatformTime::Seconds();
	LastElapsedSeconds = 0.0;
	LastVisitedPoints = 0;

	bPreloadRunning = true;

	if (!StatusWidget && StatusWidgetClass)
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
		{
			StatusWidget = CreateWidget<UPreloadStatusWidget>(
				PC,
				StatusWidgetClass);

			if (StatusWidget)
			{
				StatusWidget->AddToViewport();
			}
		}
	}

	bWaitingForStreaming = false;

	UE_LOG(
		LogPreloadMap,
		Display,
		TEXT("%s preload started. Radius=%.2fm, Points=%d, BoundsMin=(%.0f, %.0f), BoundsMax=(%.0f, %.0f)"),
		bManualStart ? TEXT("Manual") : TEXT("Automatic"),
		RadiusMeters,
		TraversalPoints.Num(),
		MapBounds.Min.X,
		MapBounds.Min.Y,
		MapBounds.Max.X,
		MapBounds.Max.Y);

	AdvanceToNextPoint();
}

void UPreloadMapSubsystem::AdvanceToNextPoint()
{
	if (!bPreloadRunning || GhostActor == nullptr)
	{
		return;
	}

	++CurrentPointIndex;
	if (!TraversalPoints.IsValidIndex(CurrentPointIndex))
	{
		FinishPreload(EPreloadStatus::Completed);
		return;
	}

	const FVector TargetLocation = TraversalPoints[CurrentPointIndex];
	LastPointStartTime = FPlatformTime::Seconds();
	GhostActor->SetActorLocation(TargetLocation, false, nullptr, ETeleportType::TeleportPhysics);

	StableFrames = 0;
	bWaitingForStreaming = true;
	CurrentStatus = EPreloadStatus::Streaming;

	UE_LOG(
		LogPreloadMap,
		Display,
		TEXT("[%d/%d | %.1f%%] ETA %.1fs                X=%.0f Y=%.0f Z=%.0f"),
		GetCurrentPoint(),
		GetTotalPoints(),
		GetProgress() * 100.f,
		GetEstimatedRemainingSeconds(),
		TargetLocation.X,
		TargetLocation.Y,
		TargetLocation.Z);
}

void UPreloadMapSubsystem::FinishPreload(EPreloadStatus Result)
{
	LastElapsedSeconds =
		FPlatformTime::Seconds() - PreloadStartTime;

	LastVisitedPoints =
		FMath::Max(CurrentPointIndex + 1, 0);

	CurrentStatus = Result;
	UE_LOG(
		LogPreloadMap,
		Display,
		TEXT("Preload %s in %.2fs (%d locations)."),
		Result == EPreloadStatus::Completed ? TEXT("completed") :
		Result == EPreloadStatus::Cancelled ? TEXT("cancelled") :
		TEXT("failed"),
		LastElapsedSeconds,
		LastVisitedPoints);

	bPreloadRunning = false;
	bWaitingForStreaming = false;
	StableFrames = 0;
	CurrentPointIndex = INDEX_NONE;
	TraversalPoints.Empty();

	DestroyGhost();

	if (StatusWidget)
	{
		StatusWidget->RemoveFromParent();
		StatusWidget = nullptr;
	}
}

void UPreloadMapSubsystem::DestroyGhost()
{
	if (GhostActor != nullptr)
	{
		GhostActor->Destroy();
		GhostActor = nullptr;
	}
}

void FPreloadMapModule::StartupModule()
{
	UE_LOG(LogPreloadMap, Display, TEXT("PreloadMap module started."));
}

void FPreloadMapModule::ShutdownModule()
{
	UE_LOG(LogPreloadMap, Display, TEXT("PreloadMap module shut down."));
}

#undef LOCTEXT_NAMESPACE

static FAutoConsoleCommand CmdPreloadStart(
	TEXT("PreloadMap.Start"),
	TEXT("Starts map preloading."),
	FConsoleCommandDelegate::CreateLambda([]()
		{
			if (UPreloadMapSubsystem* Subsystem =
				UPreloadMapSubsystem::GetFromAnyWorld())
			{
				Subsystem->StartPreload(true);
			}
		}));

static FAutoConsoleCommand CmdPreloadCancel(
	TEXT("PreloadMap.Cancel"),
	TEXT("Cancels map preloading."),
	FConsoleCommandDelegate::CreateLambda([]()
		{
			if (UPreloadMapSubsystem* Subsystem =
				UPreloadMapSubsystem::GetFromAnyWorld())
			{
				Subsystem->CancelPreload();
			}
		}));

static FAutoConsoleCommand CmdPreloadStatus(
	TEXT("PreloadMap.Status"),
	TEXT("Prints current preload status."),
	FConsoleCommandDelegate::CreateLambda([]()
		{
			if (UPreloadMapSubsystem* S =
				UPreloadMapSubsystem::GetFromAnyWorld())
			{
				UE_LOG(
					LogPreloadMap,
					Display,
					TEXT("Status=%d Running=%d Progress=%.1f%% (%d/%d) ETA=%.1fs"),
					(int32)S->GetStatus(),
					S->IsPreloadRunning(),
					S->GetProgress() * 100.f,
					S->GetCurrentPoint(),
					S->GetTotalPoints(),
					S->GetEstimatedRemainingSeconds());
			}
		}));

IMPLEMENT_MODULE(FPreloadMapModule, PreloadMap)