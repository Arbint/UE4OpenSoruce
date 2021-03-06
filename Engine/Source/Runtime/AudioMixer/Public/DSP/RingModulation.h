// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DSP/Osc.h"

namespace Audio
{
	// Ring modulation effect
	// https://en.wikipedia.org/wiki/Ring_modulation
	class AUDIOMIXER_API FRingModulation
	{
	public:
		// Constructor
		FRingModulation();

		// Destructor
		~FRingModulation();

		// Initialize the equalizer
		void Init(const float InSampleRate, const int32 InNumChannels);

		// The type of modulation
		void SetModulatorWaveType(const EOsc::Type InType);

		// Set the ring modulation frequency
		void SetModulationFrequency(const float InModulationFrequency);

		// Set the ring modulation depth
		void SetModulationDepth(const float InModulationDepth);

		// Set the dry level of the ring modulation
		void SetDryLevel(const float InDryLevel) { DryLevel = InDryLevel; }

		// Set the wet level of the ring modulation
		void SetWetLevel(const float InWetLevel) { WetLevel = InWetLevel; }

		// Processes the audio frame (audio frame must have channels equal to that used during initialization)
		void ProcessAudioFrame(const float* InFrame, float* OutFrame);

		// Process audio buffer
		void ProcessAudio(const float* InBuffer, const int32 InNumSamples, float* OutBuffer);

	private:
		Audio::FOsc Osc;
		float ModulationFrequency;
		float ModulationDepth;
		float DryLevel;
		float WetLevel;
		int32 NumChannels;
	};

}
