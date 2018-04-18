// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "GeometryCacheComponent.h"
#include "GeometryCache.h"
#include "Logging/MessageLog.h"
#include "ContentStreaming.h"

#include "GeometryCacheSceneProxy.h"

#include "GeometryCacheTrack.h"
#include "GeometryCacheMeshData.h"
#include "GeometryCacheStreamingManager.h"
#include "GeometryCacheModule.h"

#define LOCTEXT_NAMESPACE "GeometryCacheComponent"

DECLARE_CYCLE_STAT(TEXT("Component Tick"), STAT_GeometryCacheComponent_TickComponent, STATGROUP_GeometryCache);

UGeometryCacheComponent::UGeometryCacheComponent(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	ElapsedTime = 0.0f;
	bRunning = true;
	bLooping = true;
	PlayDirection = 1.0f;
	StartTimeOffset = 0.0f;
	PlaybackSpeed = 1.0f;
	Duration = 0.0f;
}

void UGeometryCacheComponent::BeginDestroy()
{
	Super::BeginDestroy();
	ReleaseResources();	
}

void UGeometryCacheComponent::FinishDestroy()
{
	Super::FinishDestroy();
}

void UGeometryCacheComponent::PostLoad()
{
	Super::PostLoad();
}

void UGeometryCacheComponent::OnRegister()
{
	ClearTrackData();
	SetupTrackData();
	IGeometryCacheStreamingManager::Get().AddStreamingComponent(this);
	Super::OnRegister();
}

void UGeometryCacheComponent::ClearTrackData()
{
	NumTracks = 0;
	TrackSections.Empty();
}

void UGeometryCacheComponent::SetupTrackData()
{
	if (GeometryCache != nullptr)
	{
		// Refresh NumTracks and clear Index Arrays
		NumTracks = GeometryCache->Tracks.Num();

		Duration = 0.0f;
		// Create mesh sections for each GeometryCacheTrack
		for (int32 TrackIndex = 0; TrackIndex < NumTracks; ++TrackIndex)
		{
			// First time so create rather than update the mesh sections
			CreateTrackSection(TrackIndex);

			const float TrackMaxSampleTime = GeometryCache->Tracks[TrackIndex]->GetMaxSampleTime();
			Duration = (Duration > TrackMaxSampleTime) ? Duration : TrackMaxSampleTime;
		}
	}
	UpdateLocalBounds();
}

void UGeometryCacheComponent::OnUnregister()
{
	IGeometryCacheStreamingManager::Get().RemoveStreamingComponent(this);
	Super::OnUnregister();
	ClearTrackData();
}

void UGeometryCacheComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	SCOPE_CYCLE_COUNTER(STAT_GeometryCacheComponent_TickComponent);
	if (GeometryCache && bRunning)
	{
		// Increase total elapsed time since BeginPlay according to PlayDirection and speed
		ElapsedTime += (DeltaTime * PlayDirection * GetPlaybackSpeed());

		if (ElapsedTime < 0.0f && bLooping)
		{
			ElapsedTime += Duration;
		}

		// Game thread update:
		// This mainly just updates the matrix and bounding boxes. All render state (meshes) is done on the render thread
		bool bUpdatedBoundsOrMatrix = false;
		for (int32 TrackIndex = 0; TrackIndex < NumTracks; ++TrackIndex)
		{
			bUpdatedBoundsOrMatrix |= UpdateTrackSection(TrackIndex);
		}

		if (bUpdatedBoundsOrMatrix)
		{
			UpdateLocalBounds();
			// Mark the transform as dirty, so the bounds are updated and sent to the render thread
			MarkRenderTransformDirty();
		}

		// The actual current playback speed. The PlaybackSpeed variable contains the speed it would
		// play back at if it were running regardless of if we're running or not. The renderer
		// needs the actual playback speed if not a paused animation with explicit motion vectors
		// would just keep on blurring as if it were moving even when paused.
		float ActualPlaybackSpeed = (bRunning) ? PlaybackSpeed : 0.0f;

		// Schedule an update on the render thread
		if (FGeometryCacheSceneProxy* CastedProxy = static_cast<FGeometryCacheSceneProxy*>(SceneProxy))
		{
			ENQUEUE_UNIQUE_RENDER_COMMAND_FIVEPARAMETER(
				FGeometryCacheUpdateAnimation,
				FGeometryCacheSceneProxy*, SceneProxy, CastedProxy,
				float, AnimationTime, GetAnimationTime(),
				bool, bLooping, IsLooping(),
				bool, bIsPlayingBackwards, IsPlayingReversed(),
				float, PlaybackSpeed, ActualPlaybackSpeed,
				{
					SceneProxy->UpdateAnimation(AnimationTime, bLooping, bIsPlayingBackwards,PlaybackSpeed);
				});
		}
	}
}

FBoxSphereBounds UGeometryCacheComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	return LocalBounds.TransformBy(LocalToWorld);
}

/**
	Update the local bounds of this component based on the bounds of all the tracks in this component.
	This is used to accelerate CalcBounds above.
*/
void UGeometryCacheComponent::UpdateLocalBounds()
{
	FBox LocalBox(ForceInit);

	for (const FTrackRenderData& Section : TrackSections)
	{
		// Use World matrix per section for correct bounding box
		LocalBox += (Section.BoundingBox.TransformBy(Section.Matrix));
	}

	LocalBounds = LocalBox.IsValid ? FBoxSphereBounds(LocalBox) : FBoxSphereBounds(FVector(0, 0, 0), FVector(0, 0, 0), 0); // fall back to reset box sphere bounds
	
	// This calls CalcBounds above and finally stores the world bounds in the "Bounds" member variable
	UpdateBounds();
}

FPrimitiveSceneProxy* UGeometryCacheComponent::CreateSceneProxy()
{
	return new FGeometryCacheSceneProxy(this);
}

#if WITH_EDITOR
void UGeometryCacheComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	InvalidateTrackSampleIndices();
	MarkRenderStateDirty();
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

int32 UGeometryCacheComponent::GetNumMaterials() const
{
	return GeometryCache ? GeometryCache->Materials.Num() : 0;
}

UMaterialInterface* UGeometryCacheComponent::GetMaterial(int32 MaterialIndex) const
{
	// If we have a base materials array, use that
	if (OverrideMaterials.IsValidIndex(MaterialIndex) && OverrideMaterials[MaterialIndex])
	{
		return OverrideMaterials[MaterialIndex];
	}
	// Otherwise get it from the geometry cache
	else
	{
		return GeometryCache ? ( GeometryCache->Materials.IsValidIndex(MaterialIndex) ? GeometryCache->Materials[MaterialIndex] : nullptr) : nullptr;
	}
}

void UGeometryCacheComponent::CreateTrackSection(int32 TrackIndex)
{
	// Ensure sections array is long enough
	if (TrackSections.Num() <= TrackIndex)
	{
		TrackSections.SetNum(TrackIndex + 1, false);
	}

	UpdateTrackSection(TrackIndex);
	MarkRenderStateDirty(); // Recreate scene proxy
}

bool UGeometryCacheComponent::UpdateTrackSection(int32 TrackIndex)
{
	checkf(TrackIndex < TrackSections.Num() && GeometryCache != nullptr && TrackIndex < GeometryCache->Tracks.Num(), TEXT("Invalid SectionIndex") );

	UGeometryCacheTrack* Track = GeometryCache->Tracks[TrackIndex];
	FTrackRenderData& UpdateSection = TrackSections[TrackIndex];

	FMatrix Matrix;
	FBox TrackBounds;
	const bool bUpdateMatrix = Track->UpdateMatrixData(GetAnimationTime(), bLooping, UpdateSection.MatrixSampleIndex, Matrix);
	const bool bUpdateBounds = Track->UpdateBoundsData(GetAnimationTime(), bLooping, (PlayDirection < 0.0f) ? true : false, UpdateSection.BoundsSampleIndex, TrackBounds);

	if (bUpdateBounds)
	{
		UpdateSection.BoundingBox = TrackBounds;
	}

	if (bUpdateMatrix)
	{
		UpdateSection.Matrix = Matrix;
	}

	// Update sections according what is required
	if (bUpdateMatrix || bUpdateBounds)
	{
		return true;
	}

	return false;
}

void UGeometryCacheComponent::OnObjectReimported(UGeometryCache* ImportedGeometryCache)
{
	if (GeometryCache == ImportedGeometryCache)
	{
		ReleaseResources();
		DetachFence.Wait();

		GeometryCache = ImportedGeometryCache;
		MarkRenderStateDirty();
	}
}

void UGeometryCacheComponent::Play()
{
	bRunning = true;
	PlayDirection = 1.0f;
	IGeometryCacheStreamingManager::Get().PrefetchData(this);
}

void UGeometryCacheComponent::PlayFromStart()
{
	ElapsedTime = 0.0f;
	bRunning = true;
	PlayDirection = 1.0f;
	IGeometryCacheStreamingManager::Get().PrefetchData(this);
}

void UGeometryCacheComponent::Pause()
{
	bRunning = !bRunning;
}

void UGeometryCacheComponent::Stop()
{
	bRunning = false;
}

bool UGeometryCacheComponent::IsPlaying() const
{
	return bRunning;
}

bool UGeometryCacheComponent::IsLooping() const
{
	return bLooping;
}

void UGeometryCacheComponent::SetLooping(const bool bNewLooping)
{
	bLooping = bNewLooping;
}

bool UGeometryCacheComponent::IsPlayingReversed() const
{
	return FMath::IsNearlyEqual( PlayDirection, -1.0f );
}

float UGeometryCacheComponent::GetPlaybackSpeed() const
{
	return FMath::Clamp(PlaybackSpeed, 0.0f, 512.0f);
}

void UGeometryCacheComponent::SetPlaybackSpeed(const float NewPlaybackSpeed)
{
	// Currently only positive play back speeds are supported
	PlaybackSpeed = FMath::Clamp( NewPlaybackSpeed, 0.0f, 512.0f );
}

bool UGeometryCacheComponent::SetGeometryCache(UGeometryCache* NewGeomCache)
{
	// Do nothing if we are already using the supplied geometry cache
	if (NewGeomCache == GeometryCache)
	{
		return false;
	}

	// Don't allow changing static meshes if "static" and registered
	AActor* Owner = GetOwner();
	if (!AreDynamicDataChangesAllowed() && Owner != nullptr)
	{
		FMessageLog("PIE").Warning(FText::Format(LOCTEXT("SetGeometryCache", "Calling SetGeometryCache on '{0}' but Mobility is Static."),
			FText::FromString(GetPathName())));
		return false;
	}

	ReleaseResources();
	DetachFence.Wait();

	GeometryCache = NewGeomCache;

	ClearTrackData();
	SetupTrackData();

	// This will cause us to prefetch the new data which is needed by the render state creation
	IGeometryCacheStreamingManager::Get().PrefetchData(this);

	// Need to send this to render thread at some point
	MarkRenderStateDirty();

	// Update physics representation right away
	RecreatePhysicsState();
	
	// Notify the streaming system. Don't use Update(), because this may be the first time the geometry cache has been set
	// and the component may have to be added to the streaming system for the first time.
	IStreamingManager::Get().NotifyPrimitiveAttached(this, DPT_Spawned);

	// Since we have new tracks, we need to update bounds
	UpdateBounds();
	return true;
}

UGeometryCache* UGeometryCacheComponent::GetGeometryCache() const
{
	return GeometryCache;
}

float UGeometryCacheComponent::GetStartTimeOffset() const
{
	return StartTimeOffset;
}

void UGeometryCacheComponent::SetStartTimeOffset(const float NewStartTimeOffset)
{
	StartTimeOffset = NewStartTimeOffset;
	MarkRenderStateDirty();
}

float UGeometryCacheComponent::GetAnimationTime()
{
	const float ClampedStartTimeOffset = FMath::Clamp(StartTimeOffset, -14400.0f, 14400.0f);
	return ElapsedTime + ClampedStartTimeOffset;
}

float UGeometryCacheComponent::GetPlaybackDirection()
{
	return PlayDirection;
}

void UGeometryCacheComponent::PlayReversedFromEnd()
{
	ElapsedTime = Duration;
	PlayDirection = -1.0f;
	bRunning = true;
	IGeometryCacheStreamingManager::Get().PrefetchData(this);
}

void UGeometryCacheComponent::PlayReversed()
{
	PlayDirection = -1.0f;
	bRunning = true;
	IGeometryCacheStreamingManager::Get().PrefetchData(this);
}

void UGeometryCacheComponent::InvalidateTrackSampleIndices()
{
	for (FTrackRenderData& Track : TrackSections)
	{
		Track.MatrixSampleIndex = -1;
		Track.BoundsSampleIndex = -1;
	}
}

void UGeometryCacheComponent::ReleaseResources()
{
	GeometryCache = nullptr;
	NumTracks = 0;
	TrackSections.Empty();
	DetachFence.BeginFence();
}

#if WITH_EDITOR
void UGeometryCacheComponent::PreEditUndo()
{
	InvalidateTrackSampleIndices();
	MarkRenderStateDirty();
}

void UGeometryCacheComponent::PostEditUndo()
{
	InvalidateTrackSampleIndices();
	MarkRenderStateDirty();
}
#endif

#undef LOCTEXT_NAMESPACE