// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "CorePrivatePCH.h"

/*-----------------------------------------------------------------------------
	Global
-----------------------------------------------------------------------------*/

DECLARE_THREAD_SINGLETON( FThreadIdleStats );

struct FStats2Globals
{
	static void Get()
	{
#if	STATS
		FStartupMessages::Get();
		IStatGroupEnableManager::Get();
#endif // STATS
	}
};

static struct FForceInitAtBootFStats2 : public TForceInitAtBoot<FStats2Globals>
{} FForceInitAtBootFStats2;

DECLARE_FLOAT_COUNTER_STAT( TEXT("Seconds Per Cycle"), STAT_SecondsPerCycle, STATGROUP_Engine );
DECLARE_DWORD_COUNTER_STAT( TEXT("Frame Packets Received"),STAT_StatFramePacketsRecv,STATGROUP_StatSystem);

DECLARE_CYCLE_STAT(TEXT("WaitForStats"),STAT_WaitForStats,STATGROUP_Engine);
DECLARE_CYCLE_STAT(TEXT("StatsNew Tick"),STAT_StatsNewTick,STATGROUP_StatSystem);
DECLARE_CYCLE_STAT(TEXT("Parse Meta"),STAT_StatsNewParseMeta,STATGROUP_StatSystem);
DECLARE_CYCLE_STAT(TEXT("Scan For Advance"),STAT_ScanForAdvance,STATGROUP_StatSystem);
DECLARE_CYCLE_STAT(TEXT("Add To History"),STAT_StatsNewAddToHistory,STATGROUP_StatSystem);
DECLARE_CYCLE_STAT(TEXT("Flush Raw Stats"),STAT_FlushRawStats,STATGROUP_StatSystem);

DECLARE_MEMORY_STAT( TEXT("Stats Descriptions"), STAT_StatDescMemory, STATGROUP_StatSystem );

DEFINE_STAT(STAT_FrameTime);

/*-----------------------------------------------------------------------------
	FStats2
-----------------------------------------------------------------------------*/

int32 FStats::GameThreadStatsFrame = 1;

void FStats::AdvanceFrame( bool bDiscardCallstack, const FOnAdvanceRenderingThreadStats& AdvanceRenderingThreadStatsDelegate /*= FOnAdvanceRenderingThreadStats()*/ )
{
#if STATS
	check( IsInGameThread() );
	static int32 MasterDisableChangeTagStartFrame = -1;
	FPlatformAtomics::InterlockedIncrement(&GameThreadStatsFrame);

	int64 Frame = GameThreadStatsFrame;
	if( bDiscardCallstack )
	{
		FThreadStats::FrameDataIsIncomplete(); // we won't collect call stack stats this frame
	}
	if( MasterDisableChangeTagStartFrame == -1 )
	{
		MasterDisableChangeTagStartFrame = FThreadStats::MasterDisableChangeTag();
	}
	if( !FThreadStats::IsCollectingData() || MasterDisableChangeTagStartFrame != FThreadStats::MasterDisableChangeTag() )
	{
		Frame = -GameThreadStatsFrame; // mark this as a bad frame
	}

	// Update the seconds per cycle.
	SET_FLOAT_STAT( STAT_SecondsPerCycle, FPlatformTime::GetSecondsPerCycle() );

	static FStatNameAndInfo Adv( NAME_AdvanceFrame, "", "", TEXT( "" ), EStatDataType::ST_int64, true, false );
	FThreadStats::AddMessage( Adv.GetEncodedName(), EStatOperation::AdvanceFrameEventGameThread, Frame ); // we need to flush here if we aren't collecting stats to make sure the meta data is up to date
	if( FPlatformProperties::IsServerOnly() )
	{
		FThreadStats::AddMessage( Adv.GetEncodedName(), EStatOperation::AdvanceFrameEventRenderThread, Frame ); // we need to flush here if we aren't collecting stats to make sure the meta data is up to date
	}

	if( AdvanceRenderingThreadStatsDelegate.IsBound() )
	{
		AdvanceRenderingThreadStatsDelegate.Execute( bDiscardCallstack, GameThreadStatsFrame, MasterDisableChangeTagStartFrame );
	}
	else
	{
		// There is no rendering thread, so this message is sufficient to make stats happy and don't leak memory.
		FThreadStats::AddMessage( Adv.GetEncodedName(), EStatOperation::AdvanceFrameEventRenderThread, Frame );
	}

	FThreadStats::ExplicitFlush( bDiscardCallstack );
	FThreadStats::WaitForStats();
	MasterDisableChangeTagStartFrame = FThreadStats::MasterDisableChangeTag();
#endif
}


/* Todo

FStatsThreadState::FStatsThreadState(FString const& Filename) - needs to be more careful about files that are "cut off". Should look for < 16 bytes left OR next 8 bytes are zero. Bad files will end with zeros and it can't be a valid record.

ST_int64 combined with IsPackedCCAndDuration is fairly bogus, ST_uint32_pair should probably be a first class type. IsPackedCCAndDuration can stay, it says what kind of data is in the pair. remove FromPackedCallCountDuration_Duration et al

averages stay as int64, it might be better if they were floats, but then we would probably want a ST_float_pair too.

//@todo merge these two after we have redone the user end

set a DEBUG_STATS define that allows this all to be debugged. Otherwise, inline and get rid of checks to the MAX. Also maybe just turn off stats in debug or debug game.

It should be possible to load stats data without STATS

//@todo this probably goes away after we redo the programmer interface

We are only saving condensed frames. For the "core view", we want a single frame capture of FStatsThreadState::History. That is the whole enchillada including start and end time for every scope.

//@todo Legacy API, phase out after we have changed the programmer-facing APU

stats2.h, stats2.cpp, statsrender2.cpp - get rid of the 2s.

//@todo, delegate under a global bool?

//@todo The rest is all horrid hacks to bridge the game between the old and the new system while we finish the new system

//@todo split header

//@todo metadata probably needs a different queue, otherwise it would be possible to load a module, run code in a thread and have the events arrive before the meta data

delete "wait for render commands", and generally be on the look out for stats that are never used, also stat system stuff

sweep INC type things for dump code that calls INC in a loop instead of just calling INC_BY once

FORCEINLINE_STATS void Start(FName InStatId)
{
check(InStatId != NAME_None);

^^^should be a checkstats


*/

#if STATS

#include "TaskGraphInterfaces.h"
#include "StatsData.h"
#include "StatsFile.h"
#include "StatsMallocProfilerProxy.h"

TStatIdData TStatId::TStatId_NAME_None;

/*-----------------------------------------------------------------------------
	FStartupMessages
-----------------------------------------------------------------------------*/

void FStartupMessages::AddThreadMetadata( const FName InThreadName, uint32 InThreadID )
{
	// Make unique name.
	const FString ThreadName = FStatsUtils::BuildUniqueThreadName( InThreadID );

	FStartupMessages::AddMetadata( InThreadName, *ThreadName, STAT_GROUP_TO_FStatGroup( STATGROUP_Threads )::GetGroupName(), STAT_GROUP_TO_FStatGroup( STATGROUP_Threads )::GetGroupCategory(), STAT_GROUP_TO_FStatGroup( STATGROUP_Threads )::GetDescription(), true, EStatDataType::ST_int64, true );
}


void FStartupMessages::AddMetadata( FName InStatName, const TCHAR* InStatDesc, const char* InGroupName, const char* InGroupCategory, const TCHAR* InGroupDesc, bool bShouldClearEveryFrame, EStatDataType::Type InStatType, bool bCycleStat, FPlatformMemory::EMemoryCounterRegion InMemoryRegion /*= FPlatformMemory::MCR_Invalid*/ )
{
	FScopeLock Lock( &CriticalSection );

	new (DelayedMessages)FStatMessage( InGroupName, EStatDataType::ST_None, "Groups", InGroupCategory, InGroupDesc, false, false );
	new (DelayedMessages)FStatMessage( InStatName, InStatType, InGroupName, InGroupCategory, InStatDesc, bShouldClearEveryFrame, bCycleStat, InMemoryRegion );
}


FStartupMessages& FStartupMessages::Get()
{
	static FStartupMessages* Messages = NULL;
	if( !Messages )
	{
		check( IsInGameThread() );
		Messages = new FStartupMessages;
	}
	return *Messages;
}

/*-----------------------------------------------------------------------------
	FThreadSafeStaticStatBase
-----------------------------------------------------------------------------*/

void FThreadSafeStaticStatBase::DoSetup(const char* InStatName, const TCHAR* InStatDesc, const char* InGroupName, const char* InGroupCategory, const TCHAR* InGroupDesc, bool bDefaultEnable, bool bShouldClearEveryFrame, EStatDataType::Type InStatType, bool bCycleStat, FPlatformMemory::EMemoryCounterRegion InMemoryRegion) const
{
	FName TempName(InStatName);

	// send meta data, we don't use normal messages because the stats thread might not be running yet
	FStartupMessages::Get().AddMetadata(TempName, InStatDesc, InGroupName, InGroupCategory, InGroupDesc, bShouldClearEveryFrame, InStatType, bCycleStat, InMemoryRegion);

	TStatIdData const* LocalHighPerformanceEnable(IStatGroupEnableManager::Get().GetHighPerformanceEnableForStat(FName(InStatName), InGroupName, InGroupCategory, bDefaultEnable, bShouldClearEveryFrame, InStatType, InStatDesc, bCycleStat, InMemoryRegion).GetRawPointer());
	TStatIdData const* OldHighPerformanceEnable = (TStatIdData const*)FPlatformAtomics::InterlockedCompareExchangePointer((void**)&HighPerformanceEnable, (void*)LocalHighPerformanceEnable, NULL);
	check(!OldHighPerformanceEnable || HighPerformanceEnable == OldHighPerformanceEnable); // we are assigned two different groups?
}

/*-----------------------------------------------------------------------------
	FStatGroupEnableManager
-----------------------------------------------------------------------------*/

DEFINE_LOG_CATEGORY_STATIC(LogStatGroupEnableManager, Log, All);

class FStatGroupEnableManager : public IStatGroupEnableManager
{
	struct FGroupEnable
	{
		TMap<FName, TStatIdData *> NamesInThisGroup;
		bool DefaultEnable; 
		bool CurrentEnable; 

		FGroupEnable(bool InDefaultEnable)
			: DefaultEnable(InDefaultEnable)
			, CurrentEnable(InDefaultEnable)
		{
		}
	};

	enum 
	{
		/** Number of stats pointer allocated per block. */
		NUM_PER_BLOCK = 16384,
	};


	TMap<FName, FGroupEnable> HighPerformanceEnable;

	/** Used to synchronize the access to the high performance stats groups. */
	FCriticalSection SynchronizationObject;
	
	/** Pointer to the long name in the names block. */
	TStatIdData* PendingStatIds;

	/** Pending count of the name in the names block. */
	int32 PendingCount;

	/** Holds the amount of memory allocated for the stats descriptions. */
	FThreadSafeCounter MemoryCounter;

	// these control what happens to groups that haven't been registered yet
	TMap<FName, bool> EnableForNewGroup;
	bool EnableForNewGroups;
	bool UseEnableForNewGroups;

	void EnableStat(FName StatName, TStatIdData* DisablePtr)
	{
		// This is all complicated to ensure an atomic 8 byte write
		static_assert(sizeof(FMinimalName) == sizeof(uint64), "FMinimalName should have the same size of uint64.");
		check(UPTRINT(&DisablePtr->Name) % sizeof(FMinimalName) == 0);
		MS_ALIGN(8) struct FAligner
		{
			FMinimalName Temp;
		} GCC_ALIGN(8);
		FAligner Align;
		check(UPTRINT(&Align.Temp) % sizeof(FMinimalName) == 0);

		Align.Temp = NameToMinimalName(StatName);
		*(uint64*)&DisablePtr->Name = *(uint64 const*)&Align.Temp;
	}

	void DisableStat(TStatIdData* DisablePtr)
	{
		static_assert(sizeof(FMinimalName) == sizeof(uint64), "FMinimalName should have the same size of uint64.");
		check(UPTRINT(&DisablePtr->Name) % sizeof(FMinimalName) == 0);
		*(uint64*)&DisablePtr->Name = 0;
	}

public:
	FStatGroupEnableManager()
		: PendingStatIds(NULL)
		, PendingCount(0)
		, EnableForNewGroups(false)
		, UseEnableForNewGroups(false)
	{
		check(IsInGameThread());
	}

	virtual void UpdateMemoryUsage() override
	{
		// Update the stats descriptions memory usage.
		const int32 MemoryUsage = MemoryCounter.GetValue();
		SET_MEMORY_STAT( STAT_StatDescMemory, MemoryUsage );
	}

	virtual void SetHighPerformanceEnableForGroup(FName Group, bool Enable) override
	{
		FScopeLock ScopeLock(&SynchronizationObject);
		FThreadStats::MasterDisableChangeTagLockAdd();
		FGroupEnable* Found = HighPerformanceEnable.Find(Group);
		if (Found)
		{
			Found->CurrentEnable = Enable;
			if (Enable)
			{
				for (auto It = Found->NamesInThisGroup.CreateIterator(); It; ++It)
				{
					EnableStat(It.Key(), It.Value());
				}
			}
			else
			{
				for (auto It = Found->NamesInThisGroup.CreateIterator(); It; ++It)
				{
					DisableStat(It.Value());
				}
			}
		}
		FThreadStats::MasterDisableChangeTagLockSubtract();
	}

	virtual void SetHighPerformanceEnableForAllGroups(bool Enable) override
	{
		FScopeLock ScopeLock(&SynchronizationObject);
		FThreadStats::MasterDisableChangeTagLockAdd();
		for (auto It = HighPerformanceEnable.CreateIterator(); It; ++It)
		{
			It.Value().CurrentEnable = Enable;
			if (Enable)
			{
				for (auto ItInner = It.Value().NamesInThisGroup.CreateConstIterator(); ItInner; ++ItInner)
				{
					EnableStat(ItInner.Key(), ItInner.Value());
				}
			}
			else
			{
				for (auto ItInner = It.Value().NamesInThisGroup.CreateConstIterator(); ItInner; ++ItInner)
				{
					DisableStat(ItInner.Value());
				}
			}
		}
		FThreadStats::MasterDisableChangeTagLockSubtract();
	}
	virtual void ResetHighPerformanceEnableForAllGroups() override
	{
		FScopeLock ScopeLock(&SynchronizationObject);
		FThreadStats::MasterDisableChangeTagLockAdd();
		for (auto It = HighPerformanceEnable.CreateIterator(); It; ++It)
		{
			It.Value().CurrentEnable = It.Value().DefaultEnable;
			if (It.Value().DefaultEnable)
			{
				for (auto ItInner = It.Value().NamesInThisGroup.CreateConstIterator(); ItInner; ++ItInner)
				{
					EnableStat(ItInner.Key(), ItInner.Value());
				}
			}
			else
			{
				for (auto ItInner = It.Value().NamesInThisGroup.CreateConstIterator(); ItInner; ++ItInner)
				{
					DisableStat(ItInner.Value());
				}
			}
		}
		FThreadStats::MasterDisableChangeTagLockSubtract();
	}

	virtual TStatId GetHighPerformanceEnableForStat(FName StatShortName, const char* InGroup, const char* InCategory, bool bDefaultEnable, bool bShouldClearEveryFrame, EStatDataType::Type InStatType, TCHAR const* InDescription, bool bCycleStat, FPlatformMemory::EMemoryCounterRegion MemoryRegion = FPlatformMemory::MCR_Invalid) override
	{
		FScopeLock ScopeLock(&SynchronizationObject);

		FStatNameAndInfo LongName(StatShortName, InGroup, InCategory, InDescription, InStatType, bShouldClearEveryFrame, bCycleStat, MemoryRegion);

		FName Stat = LongName.GetEncodedName();

		FName Group(InGroup);
		FGroupEnable* Found = HighPerformanceEnable.Find(Group);
		if (Found)
		{
			if (Found->DefaultEnable != bDefaultEnable)
			{
				UE_LOG(LogStatGroupEnableManager, Fatal, TEXT("Stat group %s was was defined both on and off by default."), *Group.ToString());
			}
			TStatIdData** StatFound = Found->NamesInThisGroup.Find(Stat);
			if (StatFound)
			{
				return TStatId(*StatFound);
			}
		}
		else
		{
			Found = &HighPerformanceEnable.Add(Group, FGroupEnable(bDefaultEnable));

			// this was set up before we saw the group, so set the enable now
			if (EnableForNewGroup.Contains(Group))
			{
				Found->CurrentEnable = EnableForNewGroup.FindChecked(Group);
				EnableForNewGroup.Remove(Group); // by definition, we will never need this again
			}
			else if (UseEnableForNewGroups)
			{
				Found->CurrentEnable = EnableForNewGroups;
			}
		}
		if (PendingCount < 1)
		{
			PendingStatIds = new TStatIdData[NUM_PER_BLOCK];
			FMemory::Memzero( PendingStatIds, NUM_PER_BLOCK * sizeof( TStatIdData ) );
			PendingCount = NUM_PER_BLOCK;
		}
		--PendingCount;
		TStatIdData* Result = PendingStatIds;

		// Get the wide stat description.
		const int32 StatDescLen = FCString::Strlen( InDescription ) + 1;
		// We are leaking this. @see STAT_StatDescMemory
		WIDECHAR* StatDescWide = new WIDECHAR[StatDescLen];
		TCString<WIDECHAR>::Strcpy( StatDescWide, StatDescLen, StringCast<WIDECHAR>( InDescription ).Get() );
		Result->WideString = reinterpret_cast<uint64>(StatDescWide);

		// Get the ansi stat description.
		// We are leaking this. @see STAT_StatDescMemory
		ANSICHAR* StatDescAnsi = new ANSICHAR[StatDescLen];
		TCString<ANSICHAR>::Strcpy( StatDescAnsi, StatDescLen, StringCast<ANSICHAR>( InDescription ).Get() );
		Result->AnsiString = reinterpret_cast<uint64>(StatDescAnsi);

		MemoryCounter.Add( StatDescLen*(sizeof( ANSICHAR ) + sizeof( WIDECHAR )) );
		
		++PendingStatIds;

		if (Found->CurrentEnable)
		{
			EnableStat(Stat, Result);
		}
		else
		{
			Found->NamesInThisGroup.Add(Stat, Result);
		}
		return TStatId(Result);
	}

	void ListGroup(FName Group)
	{
		FGroupEnable* Found = HighPerformanceEnable.Find(Group);
		if (Found)
		{
			UE_LOG(LogStatGroupEnableManager, Display, TEXT("  %d  default %d %s"), !!Found->CurrentEnable, !!Found->DefaultEnable, *Group.ToString());
		}
	}

	void ListGroups(bool bDetailed = false)
	{
		for (auto It = HighPerformanceEnable.CreateConstIterator(); It; ++It)
		{
			UE_LOG(LogStatGroupEnableManager, Display, TEXT("  %d  default %d %s"), !!It.Value().CurrentEnable, !!It.Value().DefaultEnable, *(It.Key().ToString()));
			if (bDetailed)
			{
				for (auto ItInner = It.Value().NamesInThisGroup.CreateConstIterator(); ItInner; ++ItInner)
				{
					UE_LOG(LogStatGroupEnableManager, Display, TEXT("      %d %s"), !ItInner.Value()->IsNone(), *ItInner.Key().ToString());
				}
			}
		}
	}

	FName CheckGroup(TCHAR const *& Cmd, bool Enable)
	{
		FString MaybeGroup;
		FParse::Token(Cmd, MaybeGroup, false);
		MaybeGroup = FString(TEXT("STATGROUP_")) + MaybeGroup;
		FName MaybeGroupFName(*MaybeGroup);

		FGroupEnable* Found = HighPerformanceEnable.Find(MaybeGroupFName);
		if (!Found)
		{
			EnableForNewGroup.Add(MaybeGroupFName, Enable);
			ListGroups();
			UE_LOG(LogStatGroupEnableManager, Display, TEXT("Group Not Found %s"), *MaybeGroupFName.ToString());
			return NAME_None;
		}
		SetHighPerformanceEnableForGroup(MaybeGroupFName, Enable);
		ListGroup(MaybeGroupFName);
		return MaybeGroupFName;
	}

	void StatGroupEnableManagerCommand(FString const& InCmd)
	{
		FScopeLock ScopeLock(&SynchronizationObject);
		const TCHAR* Cmd = *InCmd;
		if( FParse::Command(&Cmd,TEXT("list")) )
		{
			ListGroups();
		}
		else if( FParse::Command(&Cmd,TEXT("listall")) )
		{
			ListGroups(true);
		}
		else if ( FParse::Command(&Cmd,TEXT("enable")) )
		{
			CheckGroup(Cmd, true);
		}
		else if ( FParse::Command(&Cmd,TEXT("disable")) )
		{
			CheckGroup(Cmd, false);
		}
		else if ( FParse::Command(&Cmd,TEXT("none")) )
		{
			EnableForNewGroups = false;
			UseEnableForNewGroups = true;
			SetHighPerformanceEnableForAllGroups(false);
			ListGroups();
		}
		else if ( FParse::Command(&Cmd,TEXT("all")) )
		{
			EnableForNewGroups = true;
			UseEnableForNewGroups = true;
			SetHighPerformanceEnableForAllGroups(true);
			ListGroups();
		}
		else if ( FParse::Command(&Cmd,TEXT("default")) )
		{
			UseEnableForNewGroups = false;
			EnableForNewGroup.Empty();
			ResetHighPerformanceEnableForAllGroups();
			ListGroups();
		}
	}
};

IStatGroupEnableManager& IStatGroupEnableManager::Get()
{
	static IStatGroupEnableManager* Singleton = NULL;
	if (!Singleton)
	{
		verify(!FPlatformAtomics::InterlockedCompareExchangePointer((void**)&Singleton, (void*)new FStatGroupEnableManager, NULL));
	}
	return *Singleton;
}

/*-----------------------------------------------------------------------------
	FStatNameAndInfo
-----------------------------------------------------------------------------*/

FName FStatNameAndInfo::ToLongName(FName InStatName, char const* InGroup, char const* InCategory, TCHAR const* InDescription)
{
	FString LongName;
	if (InGroup)
	{
		LongName += TEXT("//");
		LongName += InGroup;
		LongName += TEXT("//");
	}
	LongName += InStatName.ToString();
	if (InDescription)
	{
		LongName += TEXT("///");
		LongName += FStatsUtils::ToEscapedFString(InDescription);
		LongName += TEXT("///");
	}
	if (InCategory)
	{
		LongName += TEXT("////");
		LongName += InCategory;
		LongName += TEXT("////");
	}
	return FName(*LongName);
}

FName FStatNameAndInfo::GetShortNameFrom(FName InLongName)
{
	FString Input(InLongName.ToString());

	if (Input.StartsWith(TEXT("//")))
	{
		Input = Input.RightChop(2);
		const int32 IndexEnd = Input.Find(TEXT("//"), ESearchCase::CaseSensitive);
		if (IndexEnd == INDEX_NONE)
		{
			checkStats(0);
			return InLongName;
		}
		Input = Input.RightChop(IndexEnd + 2);
	}
	const int32 IndexEnd = Input.Find(TEXT("///"), ESearchCase::CaseSensitive);
	if (IndexEnd != INDEX_NONE)
	{
		Input = Input.Left(IndexEnd);
	}
	return FName(*Input);
}

FName FStatNameAndInfo::GetGroupNameFrom(FName InLongName)
{
	FString Input(InLongName.ToString());

	if (Input.StartsWith(TEXT("//")))
	{
		Input = Input.RightChop(2);
		const int32 IndexEnd = Input.Find(TEXT("//"), ESearchCase::CaseSensitive);
		if (IndexEnd != INDEX_NONE)
		{
			return FName(*Input.Left(IndexEnd));
		}
		checkStats(0);
	}
	return NAME_None;
}

FString FStatNameAndInfo::GetDescriptionFrom(FName InLongName)
{
	FString Input(InLongName.ToString());

	const int32 IndexStart = Input.Find(TEXT("///"), ESearchCase::CaseSensitive);
	if (IndexStart != INDEX_NONE)
	{
		Input = Input.RightChop(IndexStart + 3);
		const int32 IndexEnd = Input.Find(TEXT("///"), ESearchCase::CaseSensitive);
		if (IndexEnd != INDEX_NONE)
		{
			return FStatsUtils::FromEscapedFString(*Input.Left(IndexEnd));
		}
	}
	return FString();
}

FName FStatNameAndInfo::GetGroupCategoryFrom(FName InLongName)
{
	FString Input(InLongName.ToString());

	const int32 IndexStart = Input.Find(TEXT("///////"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	if (IndexStart != INDEX_NONE)
	{
		Input = Input.RightChop(IndexStart + 7);
		const int32 IndexEnd = Input.Find(TEXT("////"), ESearchCase::CaseSensitive);
		if (IndexEnd != INDEX_NONE)
		{
			return FName(*Input.Left(IndexEnd));
		}
		checkStats(0);
	}
	return NAME_None;
}

/*-----------------------------------------------------------------------------
	FStatsThread
-----------------------------------------------------------------------------*/

static TAutoConsoleVariable<int32> CVarDumpStatPackets(	TEXT("DumpStatPackets"),0,	TEXT("If true, dump stat packets."));

/** The rendering thread runnable object. */
class FStatsThread : public FRunnable, FSingleThreadRunnable
{
	/** Array of stat packets, queued data to be processed on this thread. */
	FStatPacketArray IncomingData; 

	/** Stats state. */
	FStatsThreadState& State;

	/** Whether we are ready to process the packets, sets by game or render packets. */
	bool bReadyToProcess;
public:

	/** Default constructor. */
	FStatsThread()
		: State(FStatsThreadState::GetLocalState())
		, bReadyToProcess(false)
	{
		check(IsInGameThread());
	}

	/**
	 * Returns a pointer to the single threaded interface when multithreading is disabled.
	 */
	virtual FSingleThreadRunnable* GetSingleThreadInterface() override
	{
		return this;
	}

	/** Attaches to the task graph stats thread, all processing will be handled by the task graph. */
	virtual uint32 Run()
	{
		FTaskGraphInterface::Get().AttachToThread(ENamedThreads::StatsThread);
		FTaskGraphInterface::Get().ProcessThreadUntilRequestReturn(ENamedThreads::StatsThread);
		return 0;
	}

	/** Tick function. */
	virtual void Tick() override
	{
		static double LastTime = -1.0;
		bool bShouldProcess = false;

		if( FThreadStats::bIsRawStatsActive )
		{
			// For raw stats we process every 24MB of packet data to minimize the stats messages memory usage.
			//const bool bShouldProcessRawStats = IncomingData.Packets.Num() > 10;
			const int32 MaxIncomingMessages = 24*1024*1024/sizeof(FStatMessage);
			const int32 MaxIncomingPackets = 16;

			int32 IncomingDataMessages = 0;
			for( int32 Index = 0; Index < IncomingData.Packets.Num(); ++Index )
			{
				IncomingDataMessages += IncomingData.Packets[Index]->StatMessages.Num();
			}

			bShouldProcess = IncomingDataMessages > MaxIncomingMessages || IncomingData.Packets.Num() > MaxIncomingPackets;
		}
		else
		{
			// For regular stats we won't process more than every 5ms.
			bShouldProcess = bReadyToProcess && FPlatformTime::Seconds() - LastTime > 0.005f; 
		}

		if( bShouldProcess )
		{
			SCOPE_CYCLE_COUNTER(STAT_StatsNewTick);

			IStatGroupEnableManager::Get().UpdateMemoryUsage();
			State.UpdateStatMessagesMemoryUsage();
		
			bReadyToProcess = false;
			FStatPacketArray NowData; 
			Exchange(NowData.Packets, IncomingData.Packets);
			INC_DWORD_STAT_BY(STAT_StatFramePacketsRecv, NowData.Packets.Num());
			{
				SCOPE_CYCLE_COUNTER(STAT_StatsNewParseMeta);
				TArray<FStatMessage> MetaMessages;
				{
					FScopeLock Lock(&FStartupMessages::Get().CriticalSection);
					Exchange(FStartupMessages::Get().DelayedMessages, MetaMessages);
				}
				if (MetaMessages.Num())
				{
					State.ProcessMetaDataOnly(MetaMessages);
				}
			}
			{		
				SCOPE_CYCLE_COUNTER(STAT_ScanForAdvance);
				State.ScanForAdvance(NowData);
			}
			
			if( FThreadStats::bIsRawStatsActive )
			{
				// Process raw stats.
				State.ProcessRawStats(NowData);
				State.ResetRegularStats();
			}
			else
			{
				// Process regular stats.
				SCOPE_CYCLE_COUNTER(STAT_StatsNewAddToHistory);
				State.ResetRawStats();
				State.AddToHistoryAndEmpty(NowData);
			}
			check(!NowData.Packets.Num());
			LastTime = FPlatformTime::Seconds();
		}
	}

	/** Accesses singleton. */
	static FStatsThread& Get()
	{
		static FStatsThread Singleton;
		return Singleton;
	}

	/** Received a stat packet from other thread and add to the processing queue. */
	void StatMessage(FStatPacket* Packet)
	{
		if (CVarDumpStatPackets.GetValueOnAnyThread())
		{
			UE_LOG(LogStats, Log, TEXT("Packet from %x with %d messages"), Packet->ThreadId, Packet->StatMessages.Num());
		}

		bReadyToProcess = Packet->ThreadType != EThreadType::Other;
		IncomingData.Packets.Add(Packet);
		State.NumStatMessages.Add(Packet->StatMessages.Num());

		Tick();
	}

	/** Start a stats runnable thread. */
	void Start()
	{
		FRunnableThread* Thread = FRunnableThread::Create(this, TEXT("StatsThread"), 512 * 1024, TPri_BelowNormal, FPlatformAffinity::GetStatsThreadMask());
		check(Thread != NULL);
	}
};

/*-----------------------------------------------------------------------------
	FStatMessagesTask
-----------------------------------------------------------------------------*/

// not using a delegate here to allow higher performance since we may end up sending a lot of small message arrays to the thread.
class FStatMessagesTask
{
	FStatPacket* Packet;
public:
	FStatMessagesTask(FStatPacket* InPacket)
		: Packet(InPacket)
	{
	}
	FORCEINLINE TStatId GetStatId() const
	{
		return TStatId(); // we don't want to record this or it spams the stat system; we cover this time when we tick the stats system
	}
	static ENamedThreads::Type GetDesiredThread()
	{
		return FPlatformProcess::SupportsMultithreading() ? ENamedThreads::StatsThread : ENamedThreads::GameThread;
	}
	static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		FStatsThread::Get().StatMessage(Packet);
		Packet = NULL;
	}
};

/*-----------------------------------------------------------------------------
	FThreadStatsPool
-----------------------------------------------------------------------------*/

FThreadStatsPool::FThreadStatsPool()
{
	for( int32 Index = 0; Index < NUM_ELEMENTS_IN_POOL; ++Index )
	{
		Pool.Push( new FThreadStats(EConstructor::FOR_POOL) );
	}
}

FThreadStats* FThreadStatsPool::GetFromPool()
{
	FPlatformMisc::MemoryBarrier();
	FThreadStats* Result = new(Pool.Pop()) FThreadStats();
	check(Result && "Increase NUM_ELEMENTS_IN_POOL");
	return Result;
}

void FThreadStatsPool::ReturnToPool( FThreadStats* Instance )
{
	check(Instance);
	Instance->~FThreadStats();
	Pool.Push(Instance);
}

/*-----------------------------------------------------------------------------
	FThreadStats
-----------------------------------------------------------------------------*/

uint32 FThreadStats::TlsSlot = 0;
FThreadSafeCounter FThreadStats::MasterEnableCounter;
FThreadSafeCounter FThreadStats::MasterEnableUpdateNumber;
FThreadSafeCounter FThreadStats::MasterDisableChangeTagLock;
bool FThreadStats::bMasterEnable = false;
bool FThreadStats::bMasterDisableForever = false;
bool FThreadStats::bIsRawStatsActive = false;

FThreadStats::FThreadStats():
	CurrentGameFrame(FStats::GameThreadStatsFrame),
	ScopeCount(0), 
	bWaitForExplicitFlush(0),
	MemoryMessageScope(0),
	bReentranceGuard(false),
	bSawExplicitFlush(false)
{
	Packet.SetThreadProperties();

	check(TlsSlot);
	FPlatformTLS::SetTlsValue(TlsSlot, this);
}

FThreadStats::FThreadStats( EConstructor ):
	CurrentGameFrame(-1),
	ScopeCount(0), 
	bWaitForExplicitFlush(0),
	MemoryMessageScope(0),
	bReentranceGuard(false),
	bSawExplicitFlush(false)
{}

void FThreadStats::CheckEnable()
{
	bool bOldMasterEnable(bMasterEnable);
	bool bNewMasterEnable( WillEverCollectData() && !IsRunningCommandlet() && IsThreadingReady() && (MasterEnableCounter.GetValue()) );
	if (bMasterEnable != bNewMasterEnable)
	{
		MasterDisableChangeTagLockAdd();
		bMasterEnable = bNewMasterEnable;
		MasterDisableChangeTagLockSubtract();
	}
}

void FThreadStats::Flush(bool bHasBrokenCallstacks /*= false*/, bool bForceFlush /*= false*/)
{
	if (bMasterDisableForever)
	{
		Packet.StatMessages.Empty();
		return;
	}

	if( bIsRawStatsActive )
	{
		FlushRawStats(bHasBrokenCallstacks, bForceFlush);
	}
	else
	{
		FlushRegularStats(bHasBrokenCallstacks, bForceFlush);
	}
}

void FThreadStats::FlushRegularStats( bool bHasBrokenCallstacks, bool bForceFlush )
{
	enum
	{
		PRESIZE_MAX_NUM_ENTRIES = 10,
		PRESIZE_MAX_SIZE = 256*1024,
	};


	// Sends all collected messages when:
	// The current game frame has changed.
	// This a force flush when we shutting down the thread stats.
	// This is an explicit flush from the game thread or the render thread.
	const bool bFrameHasChanged = DetectAndUpdateCurrentGameFrame();
	const bool bSendStatPacket = bFrameHasChanged || bForceFlush || bSawExplicitFlush;
	if( !bSendStatPacket )
	{
		return;
	}

	if (!ScopeCount && Packet.StatMessages.Num())
	{
		if( Packet.StatMessagesPresize.Num() >= PRESIZE_MAX_NUM_ENTRIES )
		{
			Packet.StatMessagesPresize.RemoveAt(0);
		}
		if (Packet.StatMessages.Num() < PRESIZE_MAX_SIZE)
		{
			Packet.StatMessagesPresize.Add(Packet.StatMessages.Num());
		}
		else
		{
			UE_LOG( LogStats, Verbose, TEXT( "StatMessage Packet has more than %i messages.  Ignoring for the presize history." ), (int32)PRESIZE_MAX_SIZE );
		}
		FStatPacket* ToSend = new FStatPacket(Packet);
		Exchange(ToSend->StatMessages, Packet.StatMessages);
		ToSend->bBrokenCallstacks = bHasBrokenCallstacks;

		check(!Packet.StatMessages.Num());
		if( Packet.StatMessagesPresize.Num() > 0 )
		{
			int32 MaxPresize = Packet.StatMessagesPresize[0];
			for (int32 Index = 0; Index < Packet.StatMessagesPresize.Num(); ++Index)
			{
				if (MaxPresize < Packet.StatMessagesPresize[Index])
				{
					MaxPresize = Packet.StatMessagesPresize[Index];
				}
			}
			Packet.StatMessages.Empty(MaxPresize);
		}

		TGraphTask<FStatMessagesTask>::CreateTask().ConstructAndDispatchWhenReady(ToSend);
		UpdateExplicitFlush();
	}
}

void FThreadStats::FlushRawStats( bool bHasBrokenCallstacks /*= false*/, bool bForceFlush /*= false*/ )
{
	if( bReentranceGuard )
	{
		return;
	}
	bReentranceGuard = true;
	
	enum
	{
		/** Maximum number of messages in the stat packet. */
		MAX_RAW_MESSAGES_IN_PACKET = 1024*1024 / sizeof(FStatMessage),
	};

	// Sends all collected messages when:
	// Number of messages is greater than MAX_RAW_MESSAGES_IN_PACKET.
	// The current game frame has changed.
	// This a force flush when we shutting down the thread stats.
	// This is an explicit flush from the game thread or the render thread.
	const bool bFrameHasChanged = DetectAndUpdateCurrentGameFrame();
	const int32 NumMessages = Packet.StatMessages.Num();
	if( NumMessages > MAX_RAW_MESSAGES_IN_PACKET || bFrameHasChanged || bForceFlush || bSawExplicitFlush )
	{
		SCOPE_CYCLE_COUNTER(STAT_FlushRawStats);

		FStatPacket* ToSend = new FStatPacket(Packet);
		Exchange(ToSend->StatMessages, Packet.StatMessages);
		ToSend->bBrokenCallstacks = bHasBrokenCallstacks;

		check(!Packet.StatMessages.Num());

		TGraphTask<FStatMessagesTask>::CreateTask().ConstructAndDispatchWhenReady(ToSend);
		UpdateExplicitFlush();

		const float NumMessagesAsMB = NumMessages*sizeof(FStatMessage) / 1024.0f / 1024.0f;
		if( NumMessages > 524288 )
		{
			UE_LOG( LogStats, Warning, TEXT( "FlushRawStats NumMessages: %i (%.2f MB), Thread: %u" ), NumMessages, NumMessagesAsMB, Packet.ThreadId );
		}

		UE_LOG( LogStats, Verbose, TEXT( "FlushRawStats NumMessages: %i (%.2f MB), Thread: %u" ), NumMessages, NumMessagesAsMB, Packet.ThreadId );
	}
	
	bReentranceGuard = false;
}

void FThreadStats::CheckForCollectingStartupStats()
{
	FString CmdLine(FCommandLine::Get());
	FString StatCmds(TEXT("-StatCmds="));
	while (1)
	{
		FString Cmds;
		if (!FParse::Value(*CmdLine, *StatCmds, Cmds, false))
		{
			break;
		}
		TArray<FString> CmdsArray;
		Cmds.ParseIntoArray(CmdsArray, TEXT( "," ), true);
		for (int32 Index = 0; Index < CmdsArray.Num(); Index++)
		{
			FString StatCmd = FString("stat ") + CmdsArray[Index].Trim();
			UE_LOG(LogStatGroupEnableManager, Log, TEXT("Sending Stat Command '%s'"), *StatCmd);
			DirectStatsCommand(*StatCmd);
		}
		int32 Index = CmdLine.Find(*StatCmds);
		ensure(Index >= 0);
		if (Index == INDEX_NONE)
		{
			break;
		}
		CmdLine = CmdLine.Mid(Index + StatCmds.Len());
	}
	
	if (FParse::Param(FCommandLine::Get(), TEXT("LoadTimeStats")))
	{
		DirectStatsCommand(TEXT("stat group enable LinkerLoad"));
		DirectStatsCommand(TEXT("stat group enable AsyncLoad"));
		DirectStatsCommand(TEXT("stat dumpsum -start -ms=250 -num=240"));
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("LoadTimeFile")))
	{
		DirectStatsCommand(TEXT("stat group enable LinkerLoad"));
		DirectStatsCommand(TEXT("stat group enable AsyncLoad"));
		DirectStatsCommand(TEXT("stat startfile"));
	}

	// Now we can safely enable malloc profiler.
	// @TODO yrx 2014-12-01 Investigate if we can enable it earlier.
	const bool bEnableMallocProfiler = FParse::Param( FCommandLine::Get(), TEXT("MemoryProfiler") );
	if( bEnableMallocProfiler )
	{
		// Enable all available groups and enable malloc profiler.
		IStatGroupEnableManager::Get().StatGroupEnableManagerCommand( TEXT("all") );
		FStatsMallocProfilerProxy::Get()->SetState( true );
		DirectStatsCommand(TEXT("stat startfileraw"), true);
	}
}

void FThreadStats::ExplicitFlush(bool DiscardCallstack)
{
	FThreadStats* ThreadStats = GetThreadStats();
	//check(ThreadStats->Packet.ThreadType != EThreadType::Other);
	if (ThreadStats->bWaitForExplicitFlush)
	{
		ThreadStats->ScopeCount--; // the main thread pre-incremented this to prevent stats from being sent. we send them at the next available opportunity
		ThreadStats->bWaitForExplicitFlush = 0;
	}
	bool bHasBrokenCallstacks = false;
	if (DiscardCallstack && ThreadStats->ScopeCount)
	{
		ThreadStats->ScopeCount = 0;
		bHasBrokenCallstacks = true;
	}
	ThreadStats->bSawExplicitFlush = true;
	ThreadStats->Flush(bHasBrokenCallstacks);
}

void FThreadStats::StartThread()
{
	FThreadStats::FrameDataIsIncomplete(); // make this non-zero
	check(IsInGameThread());
	check(!IsThreadingReady());
	FStatsThreadState::GetLocalState(); // start up the state
	FStatsThread::Get();
	FStatsThread::Get().Start();
	if (!TlsSlot)
	{
		TlsSlot = FPlatformTLS::AllocTlsSlot();
	}
	check(IsThreadingReady());
	CheckEnable();

	// Preallocate a bunch of FThreadStats to avoid dynamic memory allocation.
	FThreadStatsPool::Get();

	if( FThreadStats::WillEverCollectData() )
	{
		FThreadStats::ExplicitFlush(); // flush the stats and set update the scope so we don't flush again until a frame update, this helps prevent fragmentation
	}
	FStartupMessages::Get().AddThreadMetadata( NAME_GameThread, FPlatformTLS::GetCurrentThreadId() );

	CheckForCollectingStartupStats();
}

static FGraphEventRef LastFramesEvents[MAX_STAT_LAG];
static int32 CurrentEventIndex = 0;

void FThreadStats::StopThread()
{
	// Nothing to stop if it was never started
	if (IsThreadingReady())
	{	
		// If we are writing stats data, stop it now.
		DirectStatsCommand(TEXT("stat stopfile"), true);

		FThreadStats::MasterDisableForever();

		WaitForStats();
		for (int32 Index = 0; Index < MAX_STAT_LAG; Index++)
		{
			LastFramesEvents[Index] = NULL;
		}
		FGraphEventRef QuitTask = TGraphTask<FReturnGraphTask>::CreateTask(NULL, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(FPlatformProcess::SupportsMultithreading() ? ENamedThreads::StatsThread : ENamedThreads::GameThread);
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(QuitTask, ENamedThreads::GameThread_Local);
	}
}

void FThreadStats::WaitForStats()
{
	check(IsInGameThread());
	if (IsThreadingReady() && !bMasterDisableForever)
	{
		{
			SCOPE_CYCLE_COUNTER(STAT_WaitForStats);
			if (LastFramesEvents[(CurrentEventIndex + MAX_STAT_LAG - 1) % MAX_STAT_LAG].GetReference())
			{
				FTaskGraphInterface::Get().WaitUntilTaskCompletes(LastFramesEvents[(CurrentEventIndex + MAX_STAT_LAG - 1) % MAX_STAT_LAG], ENamedThreads::GameThread_Local);
			}
		}

		DECLARE_CYCLE_STAT(TEXT("FNullGraphTask.StatWaitFence"),
			STAT_FNullGraphTask_StatWaitFence,
			STATGROUP_TaskGraphTasks);

		LastFramesEvents[(CurrentEventIndex + MAX_STAT_LAG - 1) % MAX_STAT_LAG] = TGraphTask<FNullGraphTask>::CreateTask(NULL, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(GET_STATID(STAT_FNullGraphTask_StatWaitFence), FPlatformProcess::SupportsMultithreading() ? ENamedThreads::StatsThread : ENamedThreads::GameThread);
		CurrentEventIndex++;
	}
}

#endif
