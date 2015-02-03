// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SPaperEditorViewport.h"

struct FPaperExtractedSprite
{
	FString Name;
	FIntRect Rect;
};

//////////////////////////////////////////////////////////////////////////
// SPaperExtractSpritesDialog

class SPaperExtractSpritesDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPaperExtractSpritesDialog) {}
	SLATE_END_ARGS()

	// Constructs this widget with InArgs
	void Construct(const FArguments& InArgs, UTexture2D* Texture);

	// Show the dialog, returns true if successfully extracted sprites
	static bool ShowWindow(const FText& TitleText, UTexture2D* SourceTexture);

private:
	// Handler for Extract button
	FReply ExtractClicked();
	
	// Handler for Cancel button
	FReply CancelClicked();
	void OnFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent);
	void CloseContainingWindow();

	// Calculate a list of extracted sprite rects
	void PreviewExtractedSprites();

	// Actually create extracted sprites
	void CreateExtractedSprites();

private:
	// Source texture to extract from
	UTexture2D* SourceTexture;

	class UPaperExtractSpritesSettings* ExtractSpriteSettings;

	TSharedPtr<class IDetailsView> PropertyView;
	TArray<FPaperExtractedSprite> ExtractedSprites;
};


//////////////////////////////////////////////////////////////////////////
// SPaperExtractSpritesViewport

class SPaperExtractSpritesViewport : public SPaperEditorViewport
{
public:
	SLATE_BEGIN_ARGS(SPaperExtractSpritesViewport) {}
	SLATE_END_ARGS()

	~SPaperExtractSpritesViewport();
	void Construct(const FArguments& InArgs, UTexture2D* Texture, const TArray<FPaperExtractedSprite>& ExtractedSprites, const class UPaperExtractSpritesSettings* Settings, class SPaperExtractSpritesDialog* InDialog);

protected:
	// SPaperEditorViewport interface
	virtual FText GetTitleText() const override;
	// End of SPaperEditorViewport interface

private:
	TWeakObjectPtr<class UTexture2D> TexturePtr;
	TSharedPtr<class FPaperExtractSpritesViewportClient> TypedViewportClient;
	class SPaperExtractSpritesDialog* Dialog;
};