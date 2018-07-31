// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Apeiron/ArrayND.h"
#include "Apeiron/Complex.h"
#include "Apeiron/UniformGrid.h"

namespace Apeiron
{
template<class T, int d>
class TFFT
{
};

template<class T>
class TFFT<T, 3>
{
  public:
	static void Transform(const TUniformGrid<T, 3>& Grid, const TArrayND<TVector<T, 3>, 3>& Velocity, TArrayND<Complex<T>, 3>& u, TArrayND<Complex<T>, 3>& v, TArrayND<Complex<T>, 3>& w);
	static void InverseTransform(const TUniformGrid<T, 3>& Grid, TArrayND<TVector<T, 3>, 3>& Velocity, const TArrayND<Complex<T>, 3>& u, const TArrayND<Complex<T>, 3>& v, const TArrayND<Complex<T>, 3>& w, const bool Normalize);
	static void MakeDivergenceFree(const TUniformGrid<T, 3>& Grid, TArrayND<Complex<T>, 3>& u, TArrayND<Complex<T>, 3>& v, TArrayND<Complex<T>, 3>& w);
};
}
