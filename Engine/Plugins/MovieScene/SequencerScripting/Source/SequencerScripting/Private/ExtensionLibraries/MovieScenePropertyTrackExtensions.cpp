// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "ExtensionLibraries/MovieScenePropertyTrackExtensions.h"
#include "Tracks/MovieScenePropertyTrack.h"

FName UMovieScenePropertyTrackExtensions::GetPropertyName(UMovieScenePropertyTrack* Track)
{
	return Track->GetPropertyName();
}

FString UMovieScenePropertyTrackExtensions::GetPropertyPath(UMovieScenePropertyTrack* Track)
{
	return Track->GetPropertyPath();
}

FName UMovieScenePropertyTrackExtensions::GetUniqueTrackName(UMovieScenePropertyTrack* Track)
{
#if WITH_EDITORONLY_DATA
	return Track->UniqueTrackName;
#else
	return NAME_None;
#endif
}
