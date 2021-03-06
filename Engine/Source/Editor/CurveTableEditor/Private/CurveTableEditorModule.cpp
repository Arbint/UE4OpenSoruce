// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "CurveTableEditorModule.h"
#include "Modules/ModuleManager.h"
#include "ICurveTableEditor.h"
#include "CurveTableEditor.h"
#include "CompositeCurveTableEditor.h"
#include "Engine/CompositeCurveTable.h"

IMPLEMENT_MODULE( FCurveTableEditorModule, CurveTableEditor );


const FName FCurveTableEditorModule::CurveTableEditorAppIdentifier( TEXT( "CurveTableEditorApp" ) );

void FCurveTableEditorModule::StartupModule()
{
	MenuExtensibilityManager = MakeShareable(new FExtensibilityManager);
}


void FCurveTableEditorModule::ShutdownModule()
{
	MenuExtensibilityManager.Reset();
}


TSharedRef<ICurveTableEditor> FCurveTableEditorModule::CreateCurveTableEditor( const EToolkitMode::Type Mode, const TSharedPtr< IToolkitHost >& InitToolkitHost, UCurveTable* Table )
{
	if (Cast<UCompositeCurveTable>(Table) != nullptr)
	{
		return CreateCompositeCurveTableEditor(Mode, InitToolkitHost, Table);
	}

	return CreateStandardCurveTableEditor(Mode, InitToolkitHost, Table);
}

TSharedRef<ICurveTableEditor> FCurveTableEditorModule::CreateStandardCurveTableEditor(const EToolkitMode::Type Mode, const TSharedPtr< IToolkitHost >& InitToolkitHost, UCurveTable* Table)
{
	TSharedRef< FCurveTableEditor > NewCurveTableEditor(new FCurveTableEditor());
	NewCurveTableEditor->InitCurveTableEditor(Mode, InitToolkitHost, Table);
	return NewCurveTableEditor;
}

TSharedRef<ICurveTableEditor> FCurveTableEditorModule::CreateCompositeCurveTableEditor(const EToolkitMode::Type Mode, const TSharedPtr< IToolkitHost >& InitToolkitHost, UCurveTable* Table)
{
	TSharedRef< FCompositeCurveTableEditor > NewCurveTableEditor(new FCompositeCurveTableEditor());
	NewCurveTableEditor->InitCurveTableEditor(Mode, InitToolkitHost, Table);
	return NewCurveTableEditor;
}

