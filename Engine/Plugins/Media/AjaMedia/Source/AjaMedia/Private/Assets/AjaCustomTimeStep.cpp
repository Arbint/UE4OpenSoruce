// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "AjaCustomTimeStep.h"
#include "AjaMediaPrivate.h"
#include "AJA.h"

#include "HAL/CriticalSection.h"
#include "HAL/Event.h"

#include "Misc/App.h"
#include "Misc/ScopeLock.h"


//~ IAJASyncChannelCallbackInterface implementation
//--------------------------------------------------------------------
// Those are called from the AJA thread. There's a lock inside AJA to prevent this object from dying while in this thread.
struct UAjaCustomTimeStep::FAJACallback : public AJA::IAJASyncChannelCallbackInterface
{
	UAjaCustomTimeStep* Owner;
	FAJACallback(UAjaCustomTimeStep* InOwner)
		: Owner(InOwner)
	{}

	//~ IAJAInputCallbackInterface interface
	virtual void OnInitializationCompleted(bool bSucceed) override
	{
		Owner->bInitializationSucceed = bSucceed;
		Owner->bInitialized = true;
		if (!bSucceed)
		{
			UE_LOG(LogAjaMedia, Error, TEXT("The initialization of '%s' failed. The CustomTimeStep won't be synchronized."), *Owner->GetName());
		}
	}
};


//~ UFixedFrameRateCustomTimeStep implementation
//--------------------------------------------------------------------
UAjaCustomTimeStep::UAjaCustomTimeStep(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, TimecodeFormat(EAjaMediaTimecodeFormat::LTC)
	, bEnableOverrunDetection(false)
	, SyncChannel(nullptr)
	, SyncCallback(nullptr)
	, bWaitIsValid(false)
	, bInitialized(false)
	, bInitializationSucceed(false)
{
}

bool UAjaCustomTimeStep::Initialize(class UEngine* InEngine)
{
	bInitialized = false;
	bInitializationSucceed = false;

	check(SyncCallback == nullptr);
	SyncCallback = new FAJACallback(this);

	AJA::AJADeviceOptions DeviceOptions(MediaPort.DeviceIndex);

	//Convert Port Index to match what AJA expects
	AJA::AJASyncChannelOptions Options(*GetName(), MediaPort.PortIndex);
	Options.CallbackInterface = SyncCallback;
	Options.bUseTimecode = TimecodeFormat != EAjaMediaTimecodeFormat::None;
	Options.bUseLTCTimecode = TimecodeFormat == EAjaMediaTimecodeFormat::LTC;

	check(SyncChannel == nullptr);
	SyncChannel = new AJA::AJASyncChannel();
	if (!SyncChannel->Initialize(DeviceOptions, Options))
	{
		delete SyncChannel;
		SyncChannel = nullptr;
		delete SyncCallback;
		SyncCallback = nullptr;
		return false;
	}

	return true;
}

void UAjaCustomTimeStep::Shutdown(class UEngine* InEngine)
{
	bInitialized = false;
	ReleaseResources();
}

bool UAjaCustomTimeStep::UpdateTimeStep(class UEngine* InEngine)
{
	bool bRunEngineTimeStep = true;
	if (!bInitialized || !bInitializationSucceed || SyncChannel == nullptr)
	{
		if (bInitialized && !bInitializationSucceed)
		{
			ReleaseResources();
			bInitialized = false;
		}
	}
	else
	{
		// Updates logical last time to match logical current time from last tick
		UpdateApplicationLastTime();

		WaitForSync();

		// Use fixed delta time and update time.
		FApp::SetDeltaTime(FixedFrameRate.AsInterval());
		FApp::SetCurrentTime(FApp::GetCurrentTime() + FApp::GetDeltaTime());
		bRunEngineTimeStep = false;
	}
	return bRunEngineTimeStep;
}

//~ ITimecodeProvider implementation
//--------------------------------------------------------------------
FTimecode UAjaCustomTimeStep::GetCurrentTimecode() const
{
	return CurrentTimecode;
}

FFrameRate UAjaCustomTimeStep::GetFrameRate() const
{
	return FixedFrameRate;
}

bool UAjaCustomTimeStep::IsSynchronizing() const
{
	return SyncChannel && !bInitialized && !bWaitIsValid;
}

bool UAjaCustomTimeStep::IsSynchronized() const
{
	return SyncChannel && bInitialized && bInitializationSucceed && bWaitIsValid;
}

//~ UAjaCustomTimeStep implementation
//--------------------------------------------------------------------
void UAjaCustomTimeStep::WaitForSync()
{
	check(SyncChannel);

	if (bEnableOverrunDetection && !VSyncThread.IsValid())
	{
		TSharedPtr<IMediaIOCoreHardwareSync> HardwareSync = MakeShared<FAjaHardwareSync>(SyncChannel);
		VSyncThread = MakeUnique<FMediaIOCoreWaitVSyncThread>(HardwareSync);
		VSyncRunnableThread.Reset(FRunnableThread::Create(VSyncThread.Get(), TEXT("UAjaCustomTimeStep::FAjaMediaWaitVSyncThread"), TPri_AboveNormal));
	}

	if (VSyncThread.IsValid())
	{
		VSyncThread->Wait_GameOrRenderThread();
	}
	else
	{
		AJA::FTimecode NewTimecode;
		bWaitIsValid = SyncChannel->WaitForSync(NewTimecode);
		if (bWaitIsValid)
		{
			CurrentTimecode = FAja::ConvertAJATimecode2Timecode(NewTimecode, FixedFrameRate);
		}
	}

	if (!bWaitIsValid)
	{
		UE_LOG(LogAjaMedia, Error, TEXT("The Engine couldn't run fast enough to keep up with the CustomTimeStep Sync. The wait timeout."));
	}
}

void UAjaCustomTimeStep::ReleaseResources()
{
	if (VSyncRunnableThread.IsValid())
	{
		check(VSyncThread.IsValid());
		VSyncThread->Stop();
		VSyncRunnableThread->WaitForCompletion();  // Wait for the thread to return.
		VSyncRunnableThread.Reset();
		VSyncThread.Reset();
	}

	if (SyncChannel)
	{
		SyncChannel->Uninitialize(); // this may block, until the completion of a callback from IAJASyncChannelCallbackInterface
		delete SyncChannel;
		SyncChannel = nullptr;
		delete SyncCallback;
		SyncCallback = nullptr;
	}
}
