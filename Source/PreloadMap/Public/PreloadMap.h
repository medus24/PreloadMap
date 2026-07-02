#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"
#include "Modules/ModuleManager.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameFramework/Actor.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "Components/SceneComponent.h"
#include "Blueprint/UserWidget.h"
#include "PreloadMap.generated.h"

class UProgressBar;
class UTextBlock;
class UPreloadStatusWidget;


USTRUCT()
struct FPreloadMapRuntimeConfig
{
	GENERATED_BODY()

	UPROPERTY()
	bool AutoPreload = true;

	UPROPERTY()
	int32 RadiusPower = 8;
	
	UPROPERTY()
	bool WholeMap = false;
};

UENUM(BlueprintType)
enum class EPreloadStatus : uint8
{
	Idle,
	Preparing,
	Streaming,
	Completed,
	Cancelled,
	Failed
};

USTRUCT()
struct FPreloadMapRuntimeState
{
	GENERATED_BODY()

	UPROPERTY()
	bool bRunning = false;

	UPROPERTY()
	bool bWaitingForStreaming = false;

	UPROPERTY()
	int32 CurrentPoint = 0;

	UPROPERTY()
	int32 TotalPoints = 0;

	UPROPERTY()
	float Progress = 0.f;

	UPROPERTY()
	EPreloadStatus Status = EPreloadStatus::Idle;
};

UCLASS()
class PRELOADMAP_API APreloadMapGhostActor : public AActor
{
	GENERATED_BODY()

public:
	APreloadMapGhostActor();

	void ConfigureRadius(float RadiusCm);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> RootSceneComponent;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UWorldPartitionStreamingSourceComponent> StreamingSourceComponent;
};

UCLASS()
class PRELOADMAP_API UPreloadMapSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UPreloadMapSubsystem* Get(const UObject* WorldContextObject);
	static UPreloadMapSubsystem* GetFromAnyWorld();
	static constexpr float MinRadiusMeters = 25.f;
	static constexpr float MaxRadiusMeters = 100000.f;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	void StartPreload(bool bManualStart);
	bool IsPreloadRunning() const;

	FPreloadMapRuntimeState GetRuntimeState() const;

	float GetProgress() const;
	EPreloadStatus GetStatus() const;

	int32 GetCurrentPoint() const;
	int32 GetTotalPoints() const;
	double GetLastElapsedSeconds() const;
	int32 GetLastVisitedPoints() const;
	FVector GetCurrentTargetLocation() const;
	double GetEstimatedRemainingSeconds() const;

	float GetRadiusMeters() const;

	float FitRadiusToWholeMap() const;

	void CancelPreload();

private:
	FPreloadMapRuntimeConfig ReadConfiguration() const;
	void TryAutoStart();
	bool ResolveMapBounds(FBox2D& OutBounds, float& OutSuggestedZ) const;
	void BuildTraversalPoints(const FBox2D& Bounds, float RadiusCm, float TravelZ, TArray<FVector>& OutPoints) const;
	void SpawnOrReuseGhost(float RadiusCm, const FVector& SpawnLocation);
	void AdvanceToNextPoint();
	void FinishPreload(EPreloadStatus Result);
	void DestroyGhost();

private:
	bool bAutoStartChecked = false;
	bool bPreloadRunning = false;
	bool bWaitingForStreaming = false;
	double PreloadStartTime = 0.0;
	double LastElapsedSeconds = 0.0;
	double AveragePointTime = 0.0;
	double LastPointStartTime = 0.0;

	double AutoStartDelayEndTime = 0.0;

	int32 LastVisitedPoints = 0;
	EPreloadStatus CurrentStatus = EPreloadStatus::Idle;


	int32 StableFrames = 0;
	int32 CurrentPointIndex = INDEX_NONE;

	TArray<FVector> TraversalPoints;

	UPROPERTY(Transient)
	TObjectPtr<APreloadMapGhostActor> GhostActor;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UPreloadStatusWidget> StatusWidgetClass;

	UPROPERTY(Transient)
	TObjectPtr<UPreloadStatusWidget> StatusWidget;
};

class FPreloadMapModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

UCLASS()
class PRELOADMAP_API UPreloadStatusWidget : public UUserWidget
{
	GENERATED_BODY()

protected:

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UProgressBar> ProgressBar;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ProgressText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ETAText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> PercentText;

public:

	virtual void NativeTick(
		const FGeometry& MyGeometry,
		float DeltaTime) override;
};