// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Runtime/Online/OnlineSubsystemUtils/Classes/OnlineBeaconHost.h"
#include "TestBeaconHost.generated.h"

/**
 * A beacon host used for taking reservations for an existing game session
 */
UCLASS(transient, notplaceable, config=Engine)
class ONLINESUBSYSTEMUTILS_API ATestBeaconHost : public AOnlineBeaconHostObject
{
	GENERATED_UCLASS_BODY()

	// Begin AOnlineBeaconHost Interface 
	virtual AOnlineBeaconClient* SpawnBeaconActor(class UNetConnection* ClientConnection) override;
	virtual void ClientConnected(class AOnlineBeaconClient* NewClientActor, class UNetConnection* ClientConnection);
	// End AOnlineBeaconHost Interface 

	virtual bool Init();
};
