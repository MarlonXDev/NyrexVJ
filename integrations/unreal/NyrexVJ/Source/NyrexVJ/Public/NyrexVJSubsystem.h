#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "NyrexVJSubsystem.generated.h"

class FSocket;
class FUdpSocketReceiver;
class FNyrexVJRuntime;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNyrexBeatSignature, int32, BeatNumber, int32, BeatInBar);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNyrexOnsetSignature, int32, OnsetCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FNyrexConnectionSignature);

UCLASS(BlueprintType)
class NYREXVJ_API UNyrexVJSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "NyrexVJ|Connection")
	bool StartListening(int32 Port = 0);

	UFUNCTION(BlueprintCallable, Category = "NyrexVJ|Connection")
	void StopListening();

	UFUNCTION(BlueprintPure, Category = "NyrexVJ|Connection")
	bool IsListening() const;

	UFUNCTION(BlueprintPure, Category = "NyrexVJ|Connection")
	bool IsConnected() const;

	UFUNCTION(BlueprintPure, Category = "NyrexVJ|Connection")
	FString GetNyrexVJVersion() const;

	UFUNCTION(BlueprintPure, Category = "NyrexVJ|Connection")
	int32 GetSchema() const;

	UFUNCTION(BlueprintPure, Category = "NyrexVJ|Connection")
	int32 GetNyrexVJInputPort() const;

	UFUNCTION(BlueprintPure, Category = "NyrexVJ|Audio")
	float GetBass() const;

	UFUNCTION(BlueprintPure, Category = "NyrexVJ|Audio")
	float GetMid() const;

	UFUNCTION(BlueprintPure, Category = "NyrexVJ|Audio")
	float GetHigh() const;

	UFUNCTION(BlueprintPure, Category = "NyrexVJ|Audio")
	float GetLevel() const;

	UFUNCTION(BlueprintPure, Category = "NyrexVJ|Audio")
	float GetKick() const;

	UFUNCTION(BlueprintPure, Category = "NyrexVJ|Audio")
	TArray<float> GetSpectrum() const;

	UFUNCTION(BlueprintPure, Category = "NyrexVJ|Audio")
	float GetSpectrumBand(int32 Index) const;

	UFUNCTION(BlueprintPure, Category = "NyrexVJ|Audio")
	int32 GetOnsetCount() const;

	UFUNCTION(BlueprintPure, Category = "NyrexVJ|Tempo")
	float GetBpm() const;

	UFUNCTION(BlueprintPure, Category = "NyrexVJ|Tempo")
	int32 GetBeatNumber() const;

	UFUNCTION(BlueprintPure, Category = "NyrexVJ|Tempo")
	float GetBeatPhase() const;

	UFUNCTION(BlueprintPure, Category = "NyrexVJ|Tempo")
	int32 GetBeatInBar() const;

	UFUNCTION(BlueprintPure, Category = "NyrexVJ|Parameters")
	bool GetParameter(const FString& Name, float& Value) const;

	UFUNCTION(BlueprintPure, Category = "NyrexVJ|Parameters")
	TArray<FString> GetParameterNames() const;

	UFUNCTION(BlueprintCallable, Category = "NyrexVJ|Control")
	bool SetControlTarget(const FString& Host, int32 Port);

	UFUNCTION(BlueprintCallable, Category = "NyrexVJ|Control")
	bool SendControlValue(const FString& Address, float Value);

	UFUNCTION(BlueprintCallable, Category = "NyrexVJ|Control")
	bool SendControlTrigger(const FString& Address);

	UFUNCTION(BlueprintCallable, Category = "NyrexVJ|Control")
	bool SetMasterLevel(float Value);

	UFUNCTION(BlueprintCallable, Category = "NyrexVJ|Control")
	bool SetLayerOpacity(int32 Layer, float Value);

	UFUNCTION(BlueprintCallable, Category = "NyrexVJ|Control")
	bool TriggerColumn(int32 Column);

	UFUNCTION(BlueprintCallable, Category = "NyrexVJ|Control")
	bool SwitchDeck(int32 Deck);

	UFUNCTION(BlueprintCallable, Category = "NyrexVJ|Control")
	bool Tap();

	UPROPERTY(BlueprintAssignable, Category = "NyrexVJ|Events")
	FNyrexBeatSignature OnBeat;

	UPROPERTY(BlueprintAssignable, Category = "NyrexVJ|Events")
	FNyrexOnsetSignature OnOnset;

	UPROPERTY(BlueprintAssignable, Category = "NyrexVJ|Events")
	FNyrexConnectionSignature OnConnected;

	UPROPERTY(BlueprintAssignable, Category = "NyrexVJ|Events")
	FNyrexConnectionSignature OnDisconnected;

private:
	bool TickSubsystem(float DeltaTime);
	bool SendControlMessage(const FString& Address, bool bWithValue, float Value);
	void CloseSendSocket();

	TSharedPtr<FNyrexVJRuntime> Runtime;
	FSocket* ListenSocket = nullptr;
	FUdpSocketReceiver* Receiver = nullptr;
	FSocket* SendSocket = nullptr;
	FTSTicker::FDelegateHandle TickHandle;
	int32 ActiveListenPort = 0;
	FString ControlHost;
	int32 ControlPort = 0;
};
