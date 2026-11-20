#include "NyrexVJSubsystem.h"

#include "Common/UdpSocketBuilder.h"
#include "Common/UdpSocketReceiver.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

#include "Core/NyrexOsc.h"
#include "Core/NyrexState.h"
#include "NyrexVJLog.h"
#include "NyrexVJRuntime.h"
#include "NyrexVJSettings.h"

namespace NyrexVJPrivate
{
	std::string ToUtf8(const FString& Text)
	{
		FTCHARToUTF8 Converted(*Text);
		return std::string(Converted.Get(), static_cast<std::size_t>(Converted.Length()));
	}

	bool ParseControlHost(const FString& Host, FIPv4Address& OutAddress)
	{
		if (Host.Equals(TEXT("localhost"), ESearchCase::IgnoreCase))
		{
			OutAddress = FIPv4Address::InternalLoopback;
			return true;
		}
		return FIPv4Address::Parse(Host, OutAddress);
	}

	bool ValidPort(int32 Port)
	{
		return Port >= 1 && Port <= 65535;
	}
}

void UNyrexVJSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Runtime = MakeShared<FNyrexVJRuntime>();

	const UNyrexVJSettings* Settings = GetDefault<UNyrexVJSettings>();
	ControlHost = Settings->ControlHost;
	ControlPort = Settings->ControlPort;

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UNyrexVJSubsystem::TickSubsystem));

	if (Settings->bAutoStart)
	{
		StartListening(0);
	}
}

void UNyrexVJSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	TickHandle.Reset();

	StopListening();
	CloseSendSocket();
	Runtime.Reset();

	Super::Deinitialize();
}

bool UNyrexVJSubsystem::StartListening(int32 Port)
{
	if (!Runtime.IsValid())
	{
		return false;
	}
	if (Port <= 0)
	{
		Port = GetDefault<UNyrexVJSettings>()->ListenPort;
	}
	if (!NyrexVJPrivate::ValidPort(Port))
	{
		UE_LOG(LogNyrexVJ, Warning, TEXT("Invalid listen port %d."), Port);
		return false;
	}
	if (ListenSocket != nullptr)
	{
		if (ActiveListenPort == Port)
		{
			return true;
		}
		StopListening();
	}

	FSocket* Socket = FUdpSocketBuilder(TEXT("NyrexVJ Listen"))
		.AsNonBlocking()
		.BoundToAddress(FIPv4Address::Any)
		.BoundToPort(Port)
		.WithReceiveBufferSize(1 << 20)
		.Build();
	if (Socket == nullptr)
	{
		UE_LOG(LogNyrexVJ, Warning, TEXT("Could not open UDP port %d. Another program may already use it."), Port);
		return false;
	}

	Receiver = new FUdpSocketReceiver(Socket, FTimespan::FromMilliseconds(100), TEXT("NyrexVJ Receiver"));
	Receiver->OnDataReceived().BindRaw(Runtime.Get(), &FNyrexVJRuntime::OnReceived);
	Receiver->Start();

	ListenSocket = Socket;
	ActiveListenPort = Port;
	UE_LOG(LogNyrexVJ, Log, TEXT("Listening for NyrexVJ on UDP port %d."), Port);
	return true;
}

void UNyrexVJSubsystem::StopListening()
{
	if (Receiver != nullptr)
	{
		Receiver->Stop();
		delete Receiver;
		Receiver = nullptr;
	}
	if (ListenSocket != nullptr)
	{
		ListenSocket->Close();
		if (ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
		{
			SocketSubsystem->DestroySocket(ListenSocket);
		}
		ListenSocket = nullptr;
	}
	ActiveListenPort = 0;
	if (Runtime.IsValid())
	{
		Runtime->MarkStopped();
	}
}

bool UNyrexVJSubsystem::IsListening() const
{
	return ListenSocket != nullptr;
}

bool UNyrexVJSubsystem::IsConnected() const
{
	return Runtime.IsValid() && Runtime->IsConnected();
}

FString UNyrexVJSubsystem::GetNyrexVJVersion() const
{
	return Runtime.IsValid() ? Runtime->ReadVersion() : FString();
}

int32 UNyrexVJSubsystem::GetSchema() const
{
	return Runtime.IsValid() ? static_cast<int32>(Runtime->Read(&NyrexCore::NyrexState::schema)) : 0;
}

int32 UNyrexVJSubsystem::GetNyrexVJInputPort() const
{
	return Runtime.IsValid() ? static_cast<int32>(Runtime->Read(&NyrexCore::NyrexState::inputPort)) : 0;
}

float UNyrexVJSubsystem::GetBass() const
{
	return Runtime.IsValid() ? Runtime->Read(&NyrexCore::NyrexState::bass) : 0.0f;
}

float UNyrexVJSubsystem::GetMid() const
{
	return Runtime.IsValid() ? Runtime->Read(&NyrexCore::NyrexState::mid) : 0.0f;
}

float UNyrexVJSubsystem::GetHigh() const
{
	return Runtime.IsValid() ? Runtime->Read(&NyrexCore::NyrexState::high) : 0.0f;
}

float UNyrexVJSubsystem::GetLevel() const
{
	return Runtime.IsValid() ? Runtime->Read(&NyrexCore::NyrexState::level) : 0.0f;
}

float UNyrexVJSubsystem::GetKick() const
{
	return Runtime.IsValid() ? Runtime->Read(&NyrexCore::NyrexState::kick) : 0.0f;
}

TArray<float> UNyrexVJSubsystem::GetSpectrum() const
{
	if (Runtime.IsValid())
	{
		return Runtime->ReadSpectrum();
	}
	TArray<float> Empty;
	Empty.Init(0.0f, NyrexCore::kSpectrumBands);
	return Empty;
}

float UNyrexVJSubsystem::GetSpectrumBand(int32 Index) const
{
	if (Index < 0 || Index >= NyrexCore::kSpectrumBands)
	{
		return 0.0f;
	}
	return GetSpectrum()[Index];
}

int32 UNyrexVJSubsystem::GetOnsetCount() const
{
	return Runtime.IsValid() ? static_cast<int32>(Runtime->Read(&NyrexCore::NyrexState::onsetCount)) : 0;
}

float UNyrexVJSubsystem::GetBpm() const
{
	return Runtime.IsValid() ? Runtime->Read(&NyrexCore::NyrexState::bpm) : 0.0f;
}

int32 UNyrexVJSubsystem::GetBeatNumber() const
{
	return Runtime.IsValid() ? static_cast<int32>(Runtime->Read(&NyrexCore::NyrexState::beat)) : 0;
}

float UNyrexVJSubsystem::GetBeatPhase() const
{
	return Runtime.IsValid() ? Runtime->Read(&NyrexCore::NyrexState::beatPhase) : 0.0f;
}

int32 UNyrexVJSubsystem::GetBeatInBar() const
{
	return Runtime.IsValid() ? static_cast<int32>(Runtime->Read(&NyrexCore::NyrexState::beatInBar)) : 0;
}

bool UNyrexVJSubsystem::GetParameter(const FString& Name, float& Value) const
{
	Value = 0.0f;
	if (!Runtime.IsValid())
	{
		return false;
	}
	const std::string Key = NyrexVJPrivate::ToUtf8(Name);
	if (!NyrexCore::validParameterName(Key))
	{
		return false;
	}
	return Runtime->ReadParameter(Key, Value);
}

TArray<FString> UNyrexVJSubsystem::GetParameterNames() const
{
	return Runtime.IsValid() ? Runtime->ReadParameterNames() : TArray<FString>();
}

bool UNyrexVJSubsystem::SetControlTarget(const FString& Host, int32 Port)
{
	FIPv4Address Parsed;
	if (!NyrexVJPrivate::ParseControlHost(Host, Parsed) || !NyrexVJPrivate::ValidPort(Port))
	{
		UE_LOG(LogNyrexVJ, Warning, TEXT("Invalid control target %s:%d. Use an IPv4 address or localhost."), *Host, Port);
		return false;
	}
	ControlHost = Host;
	ControlPort = Port;
	return true;
}

bool UNyrexVJSubsystem::SendControlValue(const FString& Address, float Value)
{
	return SendControlMessage(Address, true, Value);
}

bool UNyrexVJSubsystem::SendControlTrigger(const FString& Address)
{
	return SendControlMessage(Address, false, 0.0f);
}

bool UNyrexVJSubsystem::SetMasterLevel(float Value)
{
	return SendControlMessage(UTF8_TO_TCHAR(NyrexCore::kMasterLevelAddress), true, Value);
}

bool UNyrexVJSubsystem::SetLayerOpacity(int32 Layer, float Value)
{
	const std::string Address = NyrexCore::layerOpacityAddress(Layer);
	if (Address.empty())
	{
		return false;
	}
	return SendControlMessage(FString(UTF8_TO_TCHAR(Address.c_str())), true, Value);
}

bool UNyrexVJSubsystem::TriggerColumn(int32 Column)
{
	const std::string Address = NyrexCore::columnAddress(Column);
	if (Address.empty())
	{
		return false;
	}
	return SendControlMessage(FString(UTF8_TO_TCHAR(Address.c_str())), false, 0.0f);
}

bool UNyrexVJSubsystem::SwitchDeck(int32 Deck)
{
	const std::string Address = NyrexCore::deckAddress(Deck);
	if (Address.empty())
	{
		return false;
	}
	return SendControlMessage(FString(UTF8_TO_TCHAR(Address.c_str())), false, 0.0f);
}

bool UNyrexVJSubsystem::Tap()
{
	return SendControlMessage(UTF8_TO_TCHAR(NyrexCore::kTapAddress), false, 0.0f);
}

bool UNyrexVJSubsystem::TickSubsystem(float DeltaTime)
{
	if (!Runtime.IsValid())
	{
		return true;
	}

	TArray<TPair<int32, int32>> Beats;
	TArray<int32> Onsets;
	bool bWasConnected = false;
	bool bIsConnected = false;
	int32 UnsupportedSchema = 0;
	Runtime->Collect(Beats, Onsets, bWasConnected, bIsConnected, UnsupportedSchema);

	if (UnsupportedSchema != 0)
	{
		UE_LOG(LogNyrexVJ, Warning, TEXT("NyrexVJ announced OSC schema %d. This plugin only understands schema %d and ignores the data."), UnsupportedSchema, NyrexCore::kSupportedSchema);
	}

	if (!bWasConnected && bIsConnected)
	{
		OnConnected.Broadcast();
	}
	for (const TPair<int32, int32>& Beat : Beats)
	{
		OnBeat.Broadcast(Beat.Key, Beat.Value);
	}
	for (const int32 Onset : Onsets)
	{
		OnOnset.Broadcast(Onset);
	}
	if (bWasConnected && !bIsConnected)
	{
		OnDisconnected.Broadcast();
	}
	return true;
}

bool UNyrexVJSubsystem::SendControlMessage(const FString& Address, bool bWithValue, float Value)
{
	const std::string AddressUtf8 = NyrexVJPrivate::ToUtf8(Address);
	if (!NyrexCore::validControlAddress(AddressUtf8))
	{
		UE_LOG(LogNyrexVJ, Warning, TEXT("Refused control address %s."), *Address);
		return false;
	}

	NyrexCore::OscPacketMessage Message;
	Message.address = AddressUtf8;
	if (bWithValue)
	{
		if (!FMath::IsFinite(Value))
		{
			return false;
		}
		Message.values.push_back(NyrexCore::OscValue::fromNumber(Value));
	}

	std::vector<std::uint8_t> Bytes;
	if (!NyrexCore::encodeMessage(Message, Bytes))
	{
		return false;
	}

	FIPv4Address Ip;
	if (!NyrexVJPrivate::ParseControlHost(ControlHost, Ip) || !NyrexVJPrivate::ValidPort(ControlPort))
	{
		UE_LOG(LogNyrexVJ, Warning, TEXT("Invalid control target %s:%d. Use an IPv4 address or localhost."), *ControlHost, ControlPort);
		return false;
	}

	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (SocketSubsystem == nullptr)
	{
		return false;
	}
	if (SendSocket == nullptr)
	{
		SendSocket = SocketSubsystem->CreateSocket(NAME_DGram, TEXT("NyrexVJ Send"));
		if (SendSocket == nullptr)
		{
			return false;
		}
	}

	const TSharedRef<FInternetAddr> Target = FIPv4Endpoint(Ip, static_cast<uint16>(ControlPort)).ToInternetAddr();
	int32 BytesSent = 0;
	const int32 Length = static_cast<int32>(Bytes.size());
	return SendSocket->SendTo(Bytes.data(), Length, BytesSent, *Target) && BytesSent == Length;
}

void UNyrexVJSubsystem::CloseSendSocket()
{
	if (SendSocket != nullptr)
	{
		SendSocket->Close();
		if (ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
		{
			SocketSubsystem->DestroySocket(SendSocket);
		}
		SendSocket = nullptr;
	}
}
