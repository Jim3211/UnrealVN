// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "EnginePrivate.h"
#include "GameFramework/DamageType.h"

UDamageType::UDamageType(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DamageImpulse = 800.0f;
	DestructibleImpulse = 800.0f;
	DestructibleDamageSpreadScale = 1.0f;
	bScaleMomentumByMass = true;
	DamageFalloff = 1.0f;
}
