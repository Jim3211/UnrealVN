﻿// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ExpandableArea.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnExpandableAreaExpansionChanged, bool, bIsExpanded);

/**
 * 
 */
UCLASS(Experimental)
class UMG_API UExpandableArea : public UWidget, public INamedSlotInterface
{
	GENERATED_UCLASS_BODY()

public:
	/**  */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Expansion")
	bool bIsExpanded;

	/** The maximum height of the area */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Expansion")
	float MaxHeight;
	
	/**  */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Expansion")
	FMargin AreaPadding;

	/** A bindable delegate for the IsChecked. */
	UPROPERTY(BlueprintAssignable, Category="ExpandableArea|Event")
	FOnExpandableAreaExpansionChanged OnExpansionChanged;

public:

	UFUNCTION(BlueprintCallable, Category="Expansion")
	bool GetIsExpanded() const;

	UFUNCTION(BlueprintCallable, Category="Expansion")
	void SetIsExpanded(bool IsExpanded);
	
	// Begin INamedSlotInterface
	virtual void GetSlotNames(TArray<FName>& SlotNames) const;
	virtual UWidget* GetContentForSlot(FName SlotName) const;
	virtual void SetContentForSlot(FName SlotName, UWidget* Content);
	// End INamedSlotInterface

public:
	
	// UWidget interface
	virtual void SynchronizeProperties() override;
	// End of UWidget interface

	// UVisual interface
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	// End of UVisual interface

#if WITH_EDITOR
	virtual const FSlateBrush* GetEditorIcon() override;
	virtual const FText GetPaletteCategory() override;
	virtual void OnDescendantSelected(UWidget* DescendantWidget) override;
	virtual void OnDescendantDeselected(UWidget* DescendantWidget) override;
#endif

protected:
	// UWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	// End of UWidget interface

	void SlateExpansionChanged(bool NewState);

protected:
	UPROPERTY()
	UWidget* HeaderContent;

	UPROPERTY()
	UWidget* BodyContent;

	TSharedPtr<SExpandableArea> MyExpandableArea;
};
