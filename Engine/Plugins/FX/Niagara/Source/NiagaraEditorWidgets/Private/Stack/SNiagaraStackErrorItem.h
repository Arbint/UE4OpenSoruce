// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SNiagaraStackEntryWidget.h"

class UNiagaraStackErrorItem;
class UNiagaraStackErrorItemLongDescription;
class UNiagaraStackErrorItemFix;
class UNiagaraStackErrorItemDismiss;

class SNiagaraStackErrorItem: public SNiagaraStackEntryWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraStackErrorItem) { }
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UNiagaraStackErrorItem* InErrorItem, UNiagaraStackViewModel* InStackViewModel);

private:
	UNiagaraStackErrorItem* ErrorItem;
};

class SNiagaraStackErrorItemFix : public SNiagaraStackEntryWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraStackErrorItemFix) { }
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UNiagaraStackErrorItemFix* InErrorItem, UNiagaraStackViewModel* InStackViewModel);

private:
	UNiagaraStackErrorItemFix* ErrorItem;
};