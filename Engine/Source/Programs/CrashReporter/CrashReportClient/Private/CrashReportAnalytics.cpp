// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "CrashReportClientApp.h"

#include "CrashReportAnalytics.h"
#include "EngineBuildSettings.h"
#include "Runtime/Analytics/Analytics/Public/Analytics.h"
#include "Runtime/Analytics/Analytics/Public/Interfaces/IAnalyticsProvider.h"

// WARNING! Copied from EngineAnalytics.cpp

bool FCrashReportAnalytics::bIsInitialized;
TSharedPtr<IAnalyticsProvider> FCrashReportAnalytics::Analytics;

/**
 * Engine analytics config log to initialize the engine analytics provider.
 * External code should bind this delegate if engine analytics are desired,
 * preferably in private code that won't be redistributed.
 */
FAnalytics::FProviderConfigurationDelegate& GetCrashReportAnalyticsOverrideConfigDelegate()
{
	static FAnalytics::FProviderConfigurationDelegate Delegate;
	return Delegate;
}


/**
 * On-demand construction of the singleton. 
 */
IAnalyticsProvider& FCrashReportAnalytics::GetProvider()
{
	checkf(bIsInitialized && Analytics.IsValid(), TEXT("FCrashReportAnalytics::GetProvider called outside of Initialize/Shutdown."));
	return *Analytics.Get();
}
 
void FCrashReportAnalytics::Initialize()
{
	checkf(!bIsInitialized, TEXT("FCrashReportAnalytics::Initialize called more than once."));

	// Never use analytics when running a commandlet tool
	const bool bShouldInitAnalytics = !IsRunningCommandlet();
	if( bShouldInitAnalytics )
	{
		// Setup some default engine analytics if there is nothing custom bound
		FAnalytics::FProviderConfigurationDelegate DefaultEngineAnalyticsConfig;
		DefaultEngineAnalyticsConfig.BindStatic(
			[]( const FString& KeyName, bool bIsValueRequired ) -> FString
		{
			static TMap<FString, FString> ConfigMap;
			if( ConfigMap.Num() == 0 )
			{
				ConfigMap.Add( TEXT( "ProviderModuleName" ), TEXT( "AnalyticsET" ) );
				ConfigMap.Add( TEXT( "APIServerET" ), TEXT( "http://etsource.epicgames.com/ET2/" ) );

				// We always use the "Release" analytics account unless we're running in analytics test mode (usually with
				// a command-line parameter), or we're an internal Epic build
				bool bUseReleaseAccount =
					(FAnalytics::Get().GetBuildType() == FAnalytics::Development ||
					FAnalytics::Get().GetBuildType() == FAnalytics::Release) &&
					!FEngineBuildSettings::IsInternalBuild();	// Internal Epic build

				if( FRocketSupport::IsRocket() )
				{
					const TCHAR* DevelopmentAccountAPIKeyET = TEXT( "Rocket.Dev" );
					const TCHAR* ReleaseAccountAPIKeyET = TEXT( "Rocket.Release" );
					ConfigMap.Add( TEXT( "APIKeyET" ), bUseReleaseAccount ? ReleaseAccountAPIKeyET : DevelopmentAccountAPIKeyET );
				}
				else if( FEngineBuildSettings::IsPerforceBuild() )
				{
					const TCHAR* DevelopmentAccountAPIKeyET = TEXT( "Perforce.Dev" );
					const TCHAR* ReleaseAccountAPIKeyET = TEXT( "Perforce.Release" );
					ConfigMap.Add( TEXT( "APIKeyET" ), bUseReleaseAccount ? ReleaseAccountAPIKeyET : DevelopmentAccountAPIKeyET );
				}
				else
				{
					const TCHAR* DevelopmentAccountAPIKeyET = TEXT( "UnrealEngine.Dev" );
					const TCHAR* ReleaseAccountAPIKeyET = TEXT( "UnrealEngine.Release" );
					ConfigMap.Add( TEXT( "APIKeyET" ), bUseReleaseAccount ? ReleaseAccountAPIKeyET : DevelopmentAccountAPIKeyET );
				}
			}

			// Check for overrides
			if( GetCrashReportAnalyticsOverrideConfigDelegate().IsBound() )
			{
				const FString OverrideValue = GetCrashReportAnalyticsOverrideConfigDelegate().Execute( KeyName, bIsValueRequired );
				if( !OverrideValue.IsEmpty() )
				{
					return OverrideValue;
				}
			}

			FString* ConfigValue = ConfigMap.Find( KeyName );
			return ConfigValue != NULL ? *ConfigValue : TEXT( "" );
		} );

		// Connect the engine analytics provider (if there is a configuration delegate installed)
		Analytics = FAnalytics::Get().CreateAnalyticsProvider(
			FName( *DefaultEngineAnalyticsConfig.Execute( TEXT( "ProviderModuleName" ), true ) ),
			DefaultEngineAnalyticsConfig );
		if( Analytics.IsValid() )
		{
			//const FString MachineId = FPlatformMisc::GetMachineId().ToString( EGuidFormats::Digits );
			//Analytics->SetUserID( MachineId );
			Analytics->SetUserID(FPlatformMisc::GetUniqueDeviceId());
			Analytics->StartSession();
		}
	}
	bIsInitialized = true;
}


void FCrashReportAnalytics::Shutdown()
{
	checkf(bIsInitialized, TEXT("FCrashReportAnalytics::Shutdown called outside of Initialize."));
	Analytics.Reset();
	bIsInitialized = false;
}
