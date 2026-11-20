#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NyrexVJBlueprintLibrary.generated.h"

class UNyrexVJSubsystem;

UCLASS()
class NYREXVJ_API UNyrexVJBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "NyrexVJ", meta = (WorldContext = "WorldContextObject"))
	static UNyrexVJSubsystem* GetNyrexVJ(const UObject* WorldContextObject);
};
