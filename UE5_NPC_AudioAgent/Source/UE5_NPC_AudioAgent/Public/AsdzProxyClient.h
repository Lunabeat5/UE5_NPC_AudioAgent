#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interfaces/IHttpRequest.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Sound/SoundWaveProcedural.h"
#include "AsdzProxyClient.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAsdzTextAskDone, USoundWave*, Wave, int32, StatusCode);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAsdzError, FString, Message, int32, StatusCode);

/**
 * AsdzProxyClient – leichter HTTP-Client für GPTProxy (ohne VaRest)
 * - POST /text-ask   -> audio/wav (binär)
 * Optional später: /stt-ask (multipart)
 */
UCLASS(ClassGroup=(ASDZ), meta=(BlueprintSpawnableComponent))
class UE5_NPC_AUDIOAGENT_API UAsdzProxyClient : public UActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="ASDZ|Proxy")
	FString BaseUrl = TEXT("http://127.0.0.1:3000");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="ASDZ|Proxy")
	FString ClientToken = TEXT("dev-ue-proxy-token");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="ASDZ|Proxy")
	FString SessionId = TEXT("ue5-session-1");

	/** Fired when /text-ask returns a WAV successfully */
	UPROPERTY(BlueprintAssignable, Category="ASDZ|Proxy")
	FOnAsdzTextAskDone OnTextAskDone;

	/** Fired on any HTTP or parsing error */
	UPROPERTY(BlueprintAssignable, Category="ASDZ|Proxy")
	FOnAsdzError OnError;

	/** Text -> TTS (WAV zurück), mode = "fast" oder "accurate" */
	UFUNCTION(BlueprintCallable, Category="ASDZ|Proxy")
	void TextAsk(const FString& Text, const FString& Mode = TEXT("fast"))
	{
		if (Text.IsEmpty())
		{
			OnError.Broadcast(TEXT("TextAsk: empty text"), 0);
			return;
		}

		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
		Req->SetURL(BaseUrl + TEXT("/text-ask"));
		Req->SetVerb(TEXT("POST"));
		Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		if (!ClientToken.IsEmpty()) Req->SetHeader(TEXT("X-Client-Token"), ClientToken);
		if (!SessionId.IsEmpty())   Req->SetHeader(TEXT("X-Session-Id"), SessionId);

		const FString EscText = Text.ReplaceCharWithEscapedChar();
		const FString EscMode = (Mode.IsEmpty() ? TEXT("fast") : Mode).ReplaceCharWithEscapedChar();
		const FString Payload = FString::Printf(TEXT("{\"text\":\"%s\",\"mode\":\"%s\"}"), *EscText, *EscMode);


		Req->SetContentAsString(Payload);
		Req->OnProcessRequestComplete().BindUObject(this, &UAsdzProxyClient::HandleTextAskResponse);
		Req->ProcessRequest();
	}

private:
	// ---------- HTTP callback ----------
	void HandleTextAskResponse(FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bOk)
	{
		const int32 Code = Resp.IsValid() ? Resp->GetResponseCode() : 0;
		if (!bOk || !Resp.IsValid())
		{
			OnError.Broadcast(TEXT("Request failed or no response"), Code);
			return;
		}
		if (Code != 200)
		{
			const FString Msg = Resp->GetContentType().Contains(TEXT("application/json"))
				? Resp->GetContentAsString()
				: FString::Printf(TEXT("HTTP %d (non-OK)"), Code);
			OnError.Broadcast(Msg, Code);
			return;
		}

		const TArray<uint8>& WavBytes = Resp->GetContent();

		TArray<uint8> PCM16;
		int32 SampleRate = 0, Channels = 0;
		if (!WavToPCM16(WavBytes, PCM16, SampleRate, Channels))
		{
			OnError.Broadcast(TEXT("WAV parse failed (expecting PCM16)"), 415);
			return;
		}

		USoundWaveProcedural* Wave = MakeWave(SampleRate, Channels, PCM16);
		if (!Wave)
		{
			OnError.Broadcast(TEXT("Failed to create SoundWave"), 500);
			return;
		}
		OnTextAskDone.Broadcast(Wave, 200);
	}

	// ---------- Minimal WAV (PCM16) parser ----------
	static uint32 R32(const uint8* p){ return (uint32)p[0] | ((uint32)p[1]<<8) | ((uint32)p[2]<<16) | ((uint32)p[3]<<24); }
	static uint16 R16(const uint8* p){ return (uint16)p[0] | ((uint16)p[1]<<8); }

	bool WavToPCM16(const TArray<uint8>& InWav, TArray<uint8>& OutPCM, int32& OutSR, int32& OutNC) const
	{
		const int32 N = InWav.Num();
		if (N < 44) return false;
		const uint8* D = InWav.GetData();

		if (FMemory::Memcmp(D, "RIFF", 4)!=0 || FMemory::Memcmp(D+8, "WAVE", 4)!=0)
			return false;

		int32 Off = 12;
		uint16 AudioFmt=0, NumCh=0, Bits=0;
		uint32 SR=0, DataSize=0;
		int32 DataOff=-1;

		while (Off + 8 <= N)
		{
			const bool IsFmt  = FMemory::Memcmp(D+Off, "fmt ", 4)==0;
			const bool IsData = FMemory::Memcmp(D+Off, "data", 4)==0;
			const uint32 CSize = R32(D+Off+4);
			const int32 Next = Off + 8 + (int32)CSize;

			if (IsFmt)
			{
				if (Off + 8 + 16 > N) return false;
				AudioFmt = R16(D+Off+8+0);
				NumCh    = R16(D+Off+8+2);
				SR       = R32(D+Off+8+4);
				Bits     = R16(D+Off+8+14);
			}
			else if (IsData)
			{
				DataOff  = Off + 8;
				DataSize = CSize;
			}
			Off = Next;
			if (Off > N) break;
		}

		if (AudioFmt != 1 || Bits != 16 || DataOff < 0) return false;
		if (DataOff + (int32)DataSize > N) return false;

		OutPCM.Reset();
		OutPCM.Append(D + DataOff, DataSize);
		OutSR = (int32)SR;
		OutNC = (int32)NumCh;
		return true;
	}

	// ---------- Create USoundWave from PCM16 ----------
	USoundWaveProcedural* MakeWave(int32 SR, int32 NC, const TArray<uint8>& PCM16) const
	{
		if (SR <= 0 || (NC != 1 && NC != 2) || PCM16.Num() == 0) return nullptr;

		USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(GetTransientPackage());
		if (!Wave) return nullptr;

		Wave->bLooping = false;
		Wave->SoundGroup = SOUNDGROUP_Voice;
#if ENGINE_MAJOR_VERSION >= 5
		Wave->SetSampleRate(SR);
#else
		Wave->SampleRate = SR;
#endif
		Wave->NumChannels = NC;

		// Alles in einem Rutsch einreihen – abspielbereit
		Wave->QueueAudio(PCM16.GetData(), PCM16.Num());
		return Wave;
	}
};
