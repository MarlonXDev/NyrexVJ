#pragma once

#include "CoreMinimal.h"
#include "Common/UdpSocketReceiver.h"
#include "HAL/CriticalSection.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Core/NyrexLink.h"
#include "Core/NyrexState.h"

class FNyrexVJRuntime
{
public:
	static constexpr int32 MaxPendingEvents = 512;

	void OnReceived(const FArrayReaderPtr& Data, const FIPv4Endpoint& Sender);

	void Collect(TArray<TPair<int32, int32>>& OutBeats, TArray<int32>& OutOnsets, bool& bOutWasConnected, bool& bOutIsConnected, int32& OutUnsupportedSchema);

	void SetListening(bool bListening);

	bool IsConnected() const;

	bool TryBeginPing(double IntervalSeconds, int32& OutId);

	NyrexCore::LinkStats ReadLinkStats() const;

	NyrexCore::LatencyStats ReadLatencyStats() const;

	void ResetLatency();

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
	NyrexCore::LinkMonitor Link;
	NyrexCore::LatencyTracker Latency;
	bool bConnected = false;
};
