#include "NyrexVJBlueprintLibrary.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "NyrexVJSubsystem.h"

UNyrexVJSubsystem* UNyrexVJBlueprintLibrary::GetNyrexVJ(const UObject* WorldContextObject)
{
	if (GEngine == nullptr || WorldContextObject == nullptr)
	{
		return nullptr;
	}
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	return GameInstance != nullptr ? GameInstance->GetSubsystem<UNyrexVJSubsystem>() : nullptr;
}
