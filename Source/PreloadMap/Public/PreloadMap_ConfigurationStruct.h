#pragma once
#include "CoreMinimal.h"
#include "Configuration/ConfigManager.h"
#include "Engine/Engine.h"
#include "PreloadMap_ConfigurationStruct.generated.h"

/* Struct generated from Mod Configuration Asset '/PreloadMap/PreloadMap_Configuration' */
USTRUCT(BlueprintType)
struct FPreloadMap_ConfigurationStruct {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite)
    bool AutoPreload{};

    UPROPERTY(BlueprintReadWrite)
    int32 RadiusPower{};

    UPROPERTY(BlueprintReadWrite)
    int32 PostStreamDelayMs{};

    UPROPERTY(BlueprintReadWrite)
    int32 StartupDelay{};

    UPROPERTY(BlueprintReadWrite)
    bool WholeMap{};

    /* Retrieves active configuration value and returns object of this struct containing it */
    static FPreloadMap_ConfigurationStruct GetActiveConfig(UObject* WorldContext) {
        FPreloadMap_ConfigurationStruct ConfigStruct{};
        FConfigId ConfigId{"PreloadMap", ""};
        if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)) {
            UConfigManager* ConfigManager = World->GetGameInstance()->GetSubsystem<UConfigManager>();
            ConfigManager->FillConfigurationStruct(ConfigId, FDynamicStructInfo{FPreloadMap_ConfigurationStruct::StaticStruct(), &ConfigStruct});
        }
        return ConfigStruct;
    }
};

