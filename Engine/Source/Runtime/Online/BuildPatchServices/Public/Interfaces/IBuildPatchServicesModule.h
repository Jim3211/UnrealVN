// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	IBuildPatchServicesModule.h: Declares the IBuildPatchServicesModule interface.
=============================================================================*/

#pragma once

class IAnalyticsProvider;
class FHttpServiceTracker;

/**
 * Delegates that will be accepted and fired of by the implementation
 */
DECLARE_DELEGATE( FBuildPatchDelegate );
DECLARE_DELEGATE_OneParam( FBuildPatchManifestDelegate, IBuildManifestRef );
DECLARE_DELEGATE_TwoParams( FBuildPatchBoolManifestDelegate, bool, IBuildManifestRef );
DECLARE_DELEGATE_OneParam( FBuildPatchFloatDelegate, float );

namespace ECompactifyMode
{
	enum Type
	{
		Preview,
		NoPatchDelete,
		Full
	};
}

/**
 * Interface for the services manager.
 */
class IBuildPatchServicesModule
	: public IModuleInterface
{
public:
	/**
	 * Virtual destructor.
	 */
	virtual ~IBuildPatchServicesModule( ) { }
	
	/**
	 * Loads a Build Manifest from file and returns the interface
	 * @param Filename		The file to load from
	 * @return		a shared pointer to the manifest, which will be invalid if load failed.
	 */
	virtual IBuildManifestPtr LoadManifestFromFile( const FString& Filename ) = 0;
	
	/**
	 * Constructs a Build Manifest from a data
	 * @param ManifestData		The data received from a web api
	 * @return		a shared pointer to the manifest, which will be invalid if creation failed.
	 */
	virtual IBuildManifestPtr MakeManifestFromData( const TArray<uint8>& ManifestData ) = 0;
	
	/**
	 * Saves a Build Manifest to file
	 * @param Filename		The file to save to
	 * @param Manifest		The manifest to save out
	 * @param bUseBinary	Whether to save binary format instead of json
	 * @return		If the save was successful.
	 */
	virtual bool SaveManifestToFile(const FString& Filename, IBuildManifestRef Manifest, bool bUseBinary = true) = 0;

	/**
	 * Starts an installer thread for the provided manifests
	 * @param	CurrentManifest			The manifest that the current install was generated from (if applicable)
	 * @param	InstallManifest			The manifest to be installed
	 * @param	InstallDirectory		The directory to install the App to
	 * @param	OnCompleteDelegate		The delegate to call on completion
	 * @return		An interface to the created installer. Will be an invalid ptr if error.
	 */
	virtual IBuildInstallerPtr StartBuildInstall( IBuildManifestPtr CurrentManifest, IBuildManifestPtr InstallManifest, const FString& InstallDirectory, FBuildPatchBoolManifestDelegate OnCompleteDelegate ) = 0;

	/**
	 * Sets the directory used for staging intermediate files.
	 * @param StagingDir	The staging directory
	 */
	virtual void SetStagingDirectory( const FString& StagingDir ) = 0;

	/**
	 * Sets the cloud directory where chunks and manifests will be pulled from and saved to.
	 * @param CloudDir		The cloud directory
	 */
	virtual void SetCloudDirectory( const FString& CloudDir ) = 0;

	/**
	 * Sets the backup directory where files that are being clobbered by repair/patch will be placed.
	 * @param BackupDir		The backup directory
	 */
	virtual void SetBackupDirectory( const FString& BackupDir ) = 0;

	/**
	 * Sets the Analytics provider that will be used to register errors with patch/build installs
	 * @param AnalyticsProvider		Shared ptr to an analytics interface to use. If NULL analytics will be disabled.
	 */
	virtual void SetAnalyticsProvider( TSharedPtr< IAnalyticsProvider > AnalyticsProvider ) = 0;

	/**
	 * Set the Http Service Tracker to be used for tracking Http Service responsiveness.
	 * Will only track HTTP requests, not file requests.
	 * @param HttpTracker	Shared ptr to an Http service tracker interface to use. If NULL tracking will be disabled.
	 */
	virtual void SetHttpTracker( TSharedPtr< FHttpServiceTracker > HttpTracker ) = 0;

	/**
	 * Registers an installation on this machine. This information is used to gather a list of install locations that can be used as chunk sources.
	 * @param AppManifest			Ref to the manifest for this installation
	 * @param AppInstallDirectory	The install location
	 */
	virtual void RegisterAppInstallation(IBuildManifestRef AppManifest, const FString AppInstallDirectory) = 0;

#if WITH_BUILDPATCHGENERATION
	/**
	 * Processes a Build Image to determine new chunks and produce a chunk based manifest, all saved to the cloud.
	 * NOTE: This function is blocking and will not return until finished. Don't run on main thread.
	 * @param Settings				Specifies the settings for the operation.  See FBuildPatchSettings documentation.
	 * @return		true if no file errors occurred
	 */
	virtual bool GenerateChunksManifestFromDirectory( const FBuildPatchSettings& Settings ) = 0;

	/**
	 * Processes a Build Image to determine new raw files and produce a raw file based manifest, all saved to the cloud.
	 * NOTE: This function is blocking and will not return until finished. Don't run on main thread.
	 * @param Settings				Specifies the settings for the operation.  See FBuildPatchSettings documentation.
	 * @return		true if no file errors occurred
	 */
	virtual bool GenerateFilesManifestFromDirectory( const FBuildPatchSettings& Settings ) = 0;

	/**
	 * Processes a Cloud Directory to identify and delete any orphaned chunks or files.
	 * NOTE: THIS function is blocking and will not return until finished. Don't run on main thread.
	 * @param ManifestsToKeep      If specified, these manifests will be retained, and all others will be deleted
	 * @param DataAgeThreshold     Chunks which are not referenced by a valid manifest, and which are older than this age (in days), will be deleted
	 * @param Mode      The mode that compactify will run in. If Preview, then no work will be carried out, if NoPatchData, then no patch-data will be deleted
	 * @return              true if no file errors occurred
	 */
	virtual bool CompactifyCloudDirectory(const TArray<FString>& ManifestsToKeep, const float DataAgeThreshold, const ECompactifyMode::Type Mode) = 0;
#endif // WITH_BUILDPATCHGENERATION

	/**
	 * Deprecated function, use MakeManifestFromData instead
	 */
	virtual IBuildManifestPtr MakeManifestFromJSON(const FString& ManifestJSON) = 0;
};
