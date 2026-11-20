#include "NyrexVJRuntime.h"

#include "HAL/PlatformTime.h"
#include "Serialization/ArrayReader.h"

void FNyrexVJRuntime::OnReceived(const FArrayReaderPtr& Data, const FIPv4Endpoint& Sender)
{
	if (!Data.IsValid() || Data->Num() <= 0)
	{
		return;
	}

	NyrexCore::NyrexEvents Events;
	FScopeLock Lock(&Mutex);
	NyrexCore::applyPacket(State, Data->GetData(), static_cast<std::size_t>(Data->Num()), Events);

	if (Events.hello)
	{
		LastHelloSeconds = FPlatformTime::Seconds();
		bWireConnected = State.schemaSupported;
		if (State.schemaSupported)
		{
			bSchemaWarned = false;
		}
	}
	if (Events.bye)
	{
		bWireConnected = false;
	}
	for (const std::pair<std::int32_t, std::int32_t>& Beat : Events.beats)
	{
		if (PendingBeats.Num() < MaxPendingEvents)
		{
			PendingBeats.Emplace(static_cast<int32>(Beat.first), static_cast<int32>(Beat.second));
		}
	}
	for (const std::int32_t Onset : Events.onsets)
	{
		if (PendingOnsets.Num() < MaxPendingEvents)
		{
			PendingOnsets.Add(static_cast<int32>(Onset));
		}
	}
}

void FNyrexVJRuntime::Collect(TArray<TPair<int32, int32>>& OutBeats, TArray<int32>& OutOnsets, bool& bOutWasConnected, bool& bOutIsConnected, int32& OutUnsupportedSchema)
{
	FScopeLock Lock(&Mutex);

	const double Now = FPlatformTime::Seconds();
	bOutWasConnected = bConnected;
	bConnected = bWireConnected && (Now - LastHelloSeconds) < ConnectionTimeoutSeconds;
	bOutIsConnected = bConnected;
	OutUnsupportedSchema = 0;

	if (bConnected)
	{
		OutBeats = MoveTemp(PendingBeats);
		OutOnsets = MoveTemp(PendingOnsets);
	}
	PendingBeats.Reset();
	PendingOnsets.Reset();

	if (bOutWasConnected && !bConnected)
	{
		ResetDataKeepingIdentity();
	}

	if (State.helloSeen && !State.schemaSupported && !bSchemaWarned)
	{
		bSchemaWarned = true;
		OutUnsupportedSchema = static_cast<int32>(State.schema);
	}
}

void FNyrexVJRuntime::MarkStopped()
{
	FScopeLock Lock(&Mutex);
	bWireConnected = false;
}

bool FNyrexVJRuntime::IsConnected() const
{
	FScopeLock Lock(&Mutex);
	return bConnected;
}

TArray<float> FNyrexVJRuntime::ReadSpectrum() const
{
	TArray<float> Result;
	Result.Init(0.0f, NyrexCore::kSpectrumBands);
	FScopeLock Lock(&Mutex);
	if (bConnected)
	{
		for (int32 Index = 0; Index < NyrexCore::kSpectrumBands; ++Index)
		{
			Result[Index] = State.spectrum[static_cast<std::size_t>(Index)];
		}
	}
	return Result;
}

bool FNyrexVJRuntime::ReadParameter(const std::string& Name, float& OutValue) const
{
	FScopeLock Lock(&Mutex);
	if (!bConnected)
	{
		return false;
	}
	const auto Found = State.parameters.find(Name);
	if (Found == State.parameters.end())
	{
		return false;
	}
	OutValue = Found->second;
	return true;
}

TArray<FString> FNyrexVJRuntime::ReadParameterNames() const
{
	TArray<FString> Names;
	FScopeLock Lock(&Mutex);
	if (bConnected)
	{
		for (const auto& Entry : State.parameters)
		{
			Names.Add(FString(UTF8_TO_TCHAR(Entry.first.c_str())));
		}
	}
	Names.Sort();
	return Names;
}

FString FNyrexVJRuntime::ReadVersion() const
{
	FScopeLock Lock(&Mutex);
	return bConnected ? FString(UTF8_TO_TCHAR(State.appVersion.c_str())) : FString();
}

void FNyrexVJRuntime::ResetDataKeepingIdentity()
{
	NyrexCore::NyrexState Fresh;
	Fresh.helloSeen = State.helloSeen;
	Fresh.schemaSupported = State.schemaSupported;
	Fresh.appName = State.appName;
	Fresh.appVersion = State.appVersion;
	Fresh.schema = State.schema;
	Fresh.inputPort = State.inputPort;
	State = MoveTemp(Fresh);
}
