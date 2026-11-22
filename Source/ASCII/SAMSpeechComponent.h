#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SAMSpeechComponent.generated.h"

class UAudioComponent;
class USoundWaveProcedural;
class USoundAttenuation;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSAMSpeechStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSAMSpeechFinished);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSAMSpeechFailed, const FString&, Error);

UCLASS(ClassGroup=(Audio), meta=(BlueprintSpawnableComponent))
class ASCII_API USAMSpeechComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USAMSpeechComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SAM", meta=(MultiLine=true))
	FString TestSpeechText = TEXT("Hello world. This is a test of the SAM speech system in Unreal Engine.");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SAM", meta=(ClampMin="0", ClampMax="255"))
	int32 Speed = 72;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SAM", meta=(ClampMin="0", ClampMax="255"))
	int32 Pitch = 64;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SAM", meta=(ClampMin="0", ClampMax="255"))
	int32 Mouth = 128;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SAM", meta=(ClampMin="0", ClampMax="255"))
	int32 Throat = 128;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SAM", meta=(ClampMin="0.0", ClampMax="4.0"))
	float Volume = 1.0f;

	/** Optional: assign a Sound Attenuation asset for 3D falloff. Empty = no attenuation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SAM")
	TObjectPtr<USoundAttenuation> Attenuation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SAM|Debug")
	bool bPrintDebug = true;

	UPROPERTY(BlueprintAssignable, Category="SAM")
	FSAMSpeechStarted OnSpeechStarted;

	UPROPERTY(BlueprintAssignable, Category="SAM")
	FSAMSpeechFinished OnSpeechFinished;

	UPROPERTY(BlueprintAssignable, Category="SAM")
	FSAMSpeechFailed OnSpeechFailed;

	UFUNCTION(BlueprintCallable, Category="SAM")
	void SpeakTestText();

	UFUNCTION(BlueprintCallable, Category="SAM")
	void Speak(const FString& Text);

	UFUNCTION(BlueprintCallable, Category="SAM")
	void SpeakWithSettings(const FString& Text, int32 InSpeed, int32 InPitch, int32 InMouth, int32 InThroat);

	UFUNCTION(BlueprintCallable, Category="SAM")
	void StopSpeaking();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void OnGenerated(int32 RequestId, TArray<int16> Pcm, FString Error);
	void OnPlaybackTimer();
	void Debug(const FString& Msg, bool bError = false) const;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> AudioComp;

	// Keeps the wave alive while it plays
	UPROPERTY(Transient)
	TObjectPtr<USoundWaveProcedural> CurrentWave;

	FTimerHandle PlaybackTimer;
	int32 CurrentRequestId = 0;
};