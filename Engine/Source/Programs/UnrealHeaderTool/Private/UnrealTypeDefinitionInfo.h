// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

// Forward declarations.
class FUnrealSourceFile;

/**
 * Class that stores information about type (USTRUCT/UCLASS) definition.
 */
class FUnrealTypeDefinitionInfo
{
public:
	// Constructor
	FUnrealTypeDefinitionInfo(FUnrealSourceFile& SourceFile, int32 LineNumber)
		: SourceFile(SourceFile), LineNumber(LineNumber)
	{}

	/**
	 * Gets the line number in source file this type was defined in.
	 */
	int32 GetLineNumber() const
	{
		return LineNumber;
	}

	/**
	 * Gets the reference to FUnrealSourceFile object that stores information about
	 * source file this type was defined in.
	 */
	FUnrealSourceFile& GetUnrealSourceFile()
	{
		return SourceFile;
	}

private:
	FUnrealSourceFile& SourceFile;
	int32 LineNumber;
};