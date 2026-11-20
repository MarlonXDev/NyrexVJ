#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "NyrexVJSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "NyrexVJ"))
class NYREXVJ_API UNyrexVJSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UNyrexVJSettings();

	UPROPERTY(Config, EditAnywhere, Category = "Receive")
	bool bAutoStart = true;

	UPROPERTY(Config, EditAnywhere, Category = "Receive", meta = (ClampMin = "1", ClampMax = "65535"))
	int32 ListenPort = 9011;

	UPROPERTY(Config, EditAnywhere, Category = "Control")
	FString ControlHost = TEXT("127.0.0.1");

	UPROPERTY(Config, EditAnywhere, Category = "Control", meta = (ClampMin = "1", ClampMax = "65535"))
	int32 ControlPort = 9010;
};
