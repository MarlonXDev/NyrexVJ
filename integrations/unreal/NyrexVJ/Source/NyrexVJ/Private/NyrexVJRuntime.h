#pragma once

#include "CoreMinimal.h"
#include "Common/UdpSocketReceiver.h"
#include "HAL/CriticalSection.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Core/NyrexState.h"

class FNyrexVJRuntime
{
public:
	static constexpr double ConnectionTimeoutSeconds = 5.0;
	static constexpr int32 MaxPendingEvents = 512;

	void OnReceived(const FArrayReaderPtr& Data, const FIPv4Endpoint& Sender);

	void Collect(TArray<TPair<int32, int32>>& OutBeats, TArray<int32>& OutOnsets, bool& bOutWasConnected, bool& bOutIsConnected, int32& OutUnsupportedSchema);

	void MarkStopped();

	bool IsConnected() const;

	TArray<float> ReadSpectrum() const;

	bool ReadParameter(const std::string& Name, float& OutValue) const;

	TArray<FString> ReadParameterNames() const;

	FString ReadVersion() const;

	template <typename T>
	T Read(T NyrexCore::NyrexState::*Member) const
	{
		FScopeLock Lock(&Mutex);
		return bConnected ? State.*Member : T();
	}

private:
	void ResetDataKeepingIdentity();

	mutable FCriticalSection Mutex;
	NyrexCore::NyrexState State;
	TArray<TPair<int32, int32>> PendingBeats;
	TArray<int32> PendingOnsets;
	double LastHelloSeconds = 0.0;
	bool bWireConnected = false;
	bool bConnected = false;
	bool bSchemaWarned = false;
};
