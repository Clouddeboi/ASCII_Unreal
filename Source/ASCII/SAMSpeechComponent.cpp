#include "SAMSpeechComponent.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "Sound/SoundAttenuation.h"
#include "Async/Async.h"
#include "Engine/Engine.h"
#include "TimerManager.h"
#include "GameFramework/Actor.h"

extern "C"
{
#include "ThirdParty/SAM/sam.h"
#include "ThirdParty/SAM/reciter.h"
}

DEFINE_LOG_CATEGORY_STATIC(LogSAM, Log, All);

namespace
{
	constexpr int32 SamSampleRate = 22050;
	constexpr int32 MaxChunkChars = 200;

	// SAM uses global state, so only one generation may run at a time
	FCriticalSection GSamLock;

	void SplitIntoChunks(const FString& Text, TArray<FString>& OutChunks)
	{
		FString Current;
		for (TCHAR C : Text)
		{
			// SAM only handles basic ASCII
			if (C > 126 || (C < 32)) { C = TEXT(' '); }
			Current.AppendChar(FChar::ToUpper(C));
			const bool bEnd = (C == '.' || C == '!' || C == '?' || C == ',' || C == ';');
			if ((bEnd && Current.Len() > 20) || Current.Len() >= MaxChunkChars)
			{
				OutChunks.Add(Current.TrimStartAndEnd());
				Current.Reset();
			}
		}
		if (!Current.TrimStartAndEnd().IsEmpty())
		{
			OutChunks.Add(Current.TrimStartAndEnd());
		}
	}

	bool GenerateChunk(const FString& Chunk, int32 Speed, int32 Pitch, int32 Mouth, int32 Throat,
		TArray<int16>& OutPcm, FString& OutError)
	{
		FScopeLock Lock(&GSamLock);

		unsigned char Input[256];
		FMemory::Memzero(Input, sizeof(Input));

		FTCHARToUTF8 Utf8(*Chunk);
		const int32 Len = FMath::Min<int32>(Utf8.Length(), MaxChunkChars);
		FMemory::Memcpy(Input, Utf8.Get(), Len);
		Input[Len] = '[';   // reciter end marker

		if (!TextToPhonemes(Input))
		{
			OutError = FString::Printf(TEXT("SAM reciter failed on: %s"), *Chunk);
			return false;
		}

		SetInput(reinterpret_cast<char*>(Input));
		SetSpeed(static_cast<unsigned char>(Speed));
		SetPitch(static_cast<unsigned char>(Pitch));
		SetMouth(static_cast<unsigned char>(Mouth));
		SetThroat(static_cast<unsigned char>(Throat));

		if (!SAMMain())
		{
			OutError = FString::Printf(TEXT("SAM render failed on: %s"), *Chunk);
			return false;
		}

		const char* Buf = GetBuffer();
		const int32 NumSamples = GetBufferLength() / 50;   // SAM's length unit
		if (!Buf || NumSamples <= 0)
		{
			OutError = TEXT("SAM produced an empty buffer");
			return false;
		}

		// 8-bit unsigned -> 16-bit signed
		OutPcm.Reserve(OutPcm.Num() + NumSamples);
		for (int32 i = 0; i < NumSamples; ++i)
		{
			const int32 S = static_cast<int32>(static_cast<unsigned char>(Buf[i])) - 128;
			OutPcm.Add(static_cast<int16>(S * 256));
		}
		return true;
	}
}

USAMSpeechComponent::USAMSpeechComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void USAMSpeechComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* Owner = GetOwner())
	{
		AudioComp = NewObject<UAudioComponent>(Owner, TEXT("SAMAudio"));
		AudioComp->bAutoActivate = false;
		AudioComp->bAllowSpatialization = (Attenuation != nullptr);
		AudioComp->AttenuationSettings = Attenuation;
		AudioComp->SetupAttachment(Owner->GetRootComponent());
		AudioComp->RegisterComponent();
	}
}

void USAMSpeechComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopSpeaking();
	Super::EndPlay(EndPlayReason);
}

void USAMSpeechComponent::Debug(const FString& Msg, bool bError) const
{
	if (bError) { UE_LOG(LogSAM, Error, TEXT("%s"), *Msg); }
	else { UE_LOG(LogSAM, Log, TEXT("%s"), *Msg); }

	if (bPrintDebug && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 6.f, bError ? FColor::Red : FColor::Green, TEXT("[SAM] ") + Msg);
	}
}

void USAMSpeechComponent::SpeakTestText()
{
	Speak(TestSpeechText);
}

void USAMSpeechComponent::Speak(const FString& Text)
{
	SpeakWithSettings(Text, Speed, Pitch, Mouth, Throat);
}

void USAMSpeechComponent::SpeakWithSettings(const FString& Text, int32 InSpeed, int32 InPitch, int32 InMouth, int32 InThroat)
{
	StopSpeaking();

	if (Text.TrimStartAndEnd().IsEmpty())
	{
		Debug(TEXT("Empty text, nothing to say"), true);
		OnSpeechFailed.Broadcast(TEXT("Empty text"));
		return;
	}

	InSpeed = FMath::Clamp(InSpeed, 0, 255);
	InPitch = FMath::Clamp(InPitch, 0, 255);
	InMouth = FMath::Clamp(InMouth, 0, 255);
	InThroat = FMath::Clamp(InThroat, 0, 255);

	Debug(FString::Printf(TEXT("Generating: \"%s\" (Speed=%d Pitch=%d Mouth=%d Throat=%d Volume=%.2f)"),
		*Text, InSpeed, InPitch, InMouth, InThroat, Volume));

	const int32 RequestId = ++CurrentRequestId;
	TWeakObjectPtr<USAMSpeechComponent> WeakThis(this);

	Async(EAsyncExecution::ThreadPool, [=]()
		{
			TArray<FString> Chunks;
			SplitIntoChunks(Text, Chunks);

			TArray<int16> Pcm;
			FString Error;
			for (const FString& Chunk : Chunks)
			{
				if (!GenerateChunk(Chunk, InSpeed, InPitch, InMouth, InThroat, Pcm, Error))
				{
					Pcm.Reset();
					break;
				}
			}

			AsyncTask(ENamedThreads::GameThread, [WeakThis, RequestId, Pcm = MoveTemp(Pcm), Error]() mutable
				{
					if (USAMSpeechComponent* This = WeakThis.Get())
					{
						This->OnGenerated(RequestId, MoveTemp(Pcm), Error);
					}
				});
		});
}

void USAMSpeechComponent::OnGenerated(int32 RequestId, TArray<int16> Pcm, FString Error)
{
	if (RequestId != CurrentRequestId)
	{
		return;   // cancelled or superseded
	}

	if (!Error.IsEmpty() || Pcm.Num() == 0 || !AudioComp)
	{
		const FString Msg = !Error.IsEmpty() ? Error : TEXT("No audio generated or audio component missing");
		Debug(Msg, true);
		OnSpeechFailed.Broadcast(Msg);
		return;
	}

	const float Seconds = static_cast<float>(Pcm.Num()) / SamSampleRate;
	Debug(FString::Printf(TEXT("SAM generated %d samples (%.2fs). Starting playback."), Pcm.Num(), Seconds));

	CurrentWave = NewObject<USoundWaveProcedural>(this);
	CurrentWave->SetSampleRate(SamSampleRate);
	CurrentWave->NumChannels = 1;
	CurrentWave->Duration = Seconds;
	CurrentWave->SoundGroup = SOUNDGROUP_Default;
	CurrentWave->bLooping = false;
	CurrentWave->QueueAudio(reinterpret_cast<const uint8*>(Pcm.GetData()), Pcm.Num() * sizeof(int16));

	AudioComp->SetSound(CurrentWave);
	AudioComp->SetVolumeMultiplier(Volume);
	AudioComp->Play();

	OnSpeechStarted.Broadcast();

	GetWorld()->GetTimerManager().SetTimer(PlaybackTimer, this, &USAMSpeechComponent::OnPlaybackTimer, Seconds + 0.15f, false);
}

void USAMSpeechComponent::OnPlaybackTimer()
{
	Debug(TEXT("Playback finished"));
	OnSpeechFinished.Broadcast();
}

void USAMSpeechComponent::StopSpeaking()
{
	++CurrentRequestId;   // invalidates any in-flight generation
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PlaybackTimer);
	}
	if (AudioComp && AudioComp->IsPlaying())
	{
		AudioComp->Stop();
	}
}