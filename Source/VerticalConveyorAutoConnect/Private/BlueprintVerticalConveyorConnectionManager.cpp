#include "BlueprintVerticalConveyorConnectionManager.h"

#include "VerticalConveyorAutoConnect.h"
#include "Buildables/FGBuildableConveyorAttachment.h"
#include "Buildables/FGBuildableConveyorLift.h"
#include "Buildables/FGBuildablePassthrough.h"
#include "Components/SceneComponent.h"
#include "FGBuildableSubsystem.h"
#include "FGConstructDisqualifier.h"
#include "FGFactoryConnectionComponent.h"
#include "FGRecipe.h"
#include "Hologram/FGBlueprintHologram.h"
#include "Hologram/FGConveyorAttachmentHologram.h"
#include "Hologram/FGConveyorLiftHologram.h"
#include "Misc/SecureHash.h"

FBlueprintVerticalConveyorConnectionManager::
	FBlueprintVerticalConveyorConnectionManager(
		AFGBlueprintHologram* blueprintHologram)
	: FGBlueprintOpenConnectionManagerBase(blueprintHologram)
{
}

bool FBlueprintVerticalConveyorConnectionManager::IsConveyorFloorHole(
	const AFGBuildablePassthrough* hole)
{
	if (!IsValid(hole))
	{
		return false;
	}

	const TSubclassOf<UFGConnectionComponent> connectionClass =
		hole->GetConnectionClass();
	return connectionClass &&
		connectionClass->IsChildOf(UFGFactoryConnectionComponent::StaticClass());
}

bool FBlueprintVerticalConveyorConnectionManager::TryGetAttachmentLiftPorts(
	AFGBuildable* buildable,
	FAttachmentLiftPorts& outPorts)
{
	outPorts = {};

	// Capability contract:
	// - must participate in vanilla conveyor-attachment state/lifecycle semantics;
	// - must expose BOTH canonical lift-port identities used by
	//   AFGConveyorAttachmentHologram;
	// - both canonical components must actually be vertical factory connections.
	//
	// This intentionally avoids an allow-list of splitter/merger class families.
	// Vanilla subclasses and modded variants that preserve the standard attachment
	// lift contract become compatible automatically, while arbitrary factory
	// buildables and non-standard conveyor attachments remain excluded.
	if (!IsValid(Cast<AFGBuildableConveyorAttachment>(buildable)))
	{
		return false;
	}

	outPorts.Bottom = FindFactoryConnection(
		buildable,
		AFGConveyorAttachmentHologram::mLiftConnection_Bottom);
	outPorts.Top = FindFactoryConnection(
		buildable,
		AFGConveyorAttachmentHologram::mLiftConnection_Top);

	if (!IsValid(outPorts.Bottom) ||
		!IsValid(outPorts.Top) ||
		outPorts.Bottom == outPorts.Top ||
		!outPorts.Bottom->IsConnectionVertical() ||
		!outPorts.Top->IsConnectionVertical())
	{
		outPorts = {};
		return false;
	}

	return true;
}

bool FBlueprintVerticalConveyorConnectionManager::IsSupportedAttachment(
	const AFGBuildable* buildable)
{
	FAttachmentLiftPorts ports;
	return IsValid(buildable) &&
		TryGetAttachmentLiftPorts(
			const_cast<AFGBuildable*>(buildable),
			ports);
}

bool FBlueprintVerticalConveyorConnectionManager::IsSupportedBuildable(
	const AFGBuildable* buildable)
{
	return IsConveyorFloorHole(Cast<AFGBuildablePassthrough>(buildable)) ||
		IsSupportedAttachment(buildable);
}

EBlueprintVerticalEndpointSide
FBlueprintVerticalConveyorConnectionManager::OppositeSide(
	EBlueprintVerticalEndpointSide side)
{
	return side == EBlueprintVerticalEndpointSide::Top
		? EBlueprintVerticalEndpointSide::Bottom
		: EBlueprintVerticalEndpointSide::Top;
}

EFactoryConnectionDirection
FBlueprintVerticalConveyorConnectionManager::OppositeDirection(
	EFactoryConnectionDirection direction)
{
	switch (direction)
	{
	case EFactoryConnectionDirection::FCD_INPUT:
		return EFactoryConnectionDirection::FCD_OUTPUT;
	case EFactoryConnectionDirection::FCD_OUTPUT:
		return EFactoryConnectionDirection::FCD_INPUT;
	default:
		return EFactoryConnectionDirection::FCD_ANY;
	}
}

UFGFactoryConnectionComponent*
FBlueprintVerticalConveyorConnectionManager::GetFloorHoleSnappedConnection(
	const AFGBuildablePassthrough* hole,
	EBlueprintVerticalEndpointSide side)
{
	if (!IsValid(hole))
	{
		return nullptr;
	}

	AFGBuildablePassthrough* mutableHole =
		const_cast<AFGBuildablePassthrough*>(hole);
	return side == EBlueprintVerticalEndpointSide::Top
		? mutableHole->GetTopSnappedConnection<UFGFactoryConnectionComponent>()
		: mutableHole->GetBottomSnappedConnection<UFGFactoryConnectionComponent>();
}

void FBlueprintVerticalConveyorConnectionManager::SetFloorHoleSnappedConnection(
	AFGBuildablePassthrough* hole,
	EBlueprintVerticalEndpointSide side,
	UFGFactoryConnectionComponent* connection)
{
	if (!IsValid(hole))
	{
		return;
	}

	if (side == EBlueprintVerticalEndpointSide::Top)
	{
		hole->SetTopSnappedConnection(connection);
	}
	else
	{
		hole->SetBottomSnappedConnection(connection);
	}
}

UFGFactoryConnectionComponent*
FBlueprintVerticalConveyorConnectionManager::FindFactoryConnection(
	AFGBuildable* buildable,
	FName connectionName)
{
	if (!IsValid(buildable) || connectionName.IsNone())
	{
		return nullptr;
	}

	TInlineComponentArray<UFGFactoryConnectionComponent*> connections;
	buildable->GetComponents(connections);
	for (UFGFactoryConnectionComponent* connection : connections)
	{
		if (IsValid(connection) && connection->GetFName() == connectionName)
		{
			return connection;
		}
	}
	return nullptr;
}

void FBlueprintVerticalConveyorConnectionManager::GatherEndpoints(
	AFGBuildable* buildable,
	bool useBlueprintPreviewTransform,
	bool requireOpen,
	TArray<FEndpointRef>& outEndpoints) const
{
	if (!IsValid(buildable))
	{
		return;
	}

	if (AFGBuildablePassthrough* hole = Cast<AFGBuildablePassthrough>(buildable))
	{
		if (!IsConveyorFloorHole(hole))
		{
			return;
		}

		for (const EBlueprintVerticalEndpointSide side :
			{EBlueprintVerticalEndpointSide::Bottom,
			 EBlueprintVerticalEndpointSide::Top})
		{
			if (requireOpen &&
				IsValid(GetFloorHoleSnappedConnection(hole, side)))
			{
				continue;
			}

			FEndpointRef& endpoint = outEndpoints.AddDefaulted_GetRef();
			endpoint.Buildable = buildable;
			endpoint.Kind = EBlueprintVerticalEndpointKind::FloorHoleSide;
			endpoint.Side = side;
			endpoint.UsesBlueprintPreviewTransform = useBlueprintPreviewTransform;
		}
		return;
	}

	FAttachmentLiftPorts attachmentPorts;
	if (!TryGetAttachmentLiftPorts(buildable, attachmentPorts))
	{
		return;
	}

	// The capability probe above owns the definition of a vanilla-compatible
	// vertical conveyor attachment. Enumerate exactly those validated components
	// here so support detection and endpoint discovery cannot drift apart.
	//
	// Both ports are gathered because blueprint initialization does not yet know
	// the eventual opposite endpoint. Candidate matching later selects the port
	// vanilla actually uses for that span (TopConnection when the floor hole is
	// above the attachment, BottomConnection when it is below).
	struct FLiftPortSpec
	{
		EBlueprintVerticalEndpointSide Side;
		FName Name;
		UFGFactoryConnectionComponent* Connection;
	};
	const FLiftPortSpec liftPorts[] =
	{
		{EBlueprintVerticalEndpointSide::Bottom,
		 AFGConveyorAttachmentHologram::mLiftConnection_Bottom,
		 attachmentPorts.Bottom},
		{EBlueprintVerticalEndpointSide::Top,
		 AFGConveyorAttachmentHologram::mLiftConnection_Top,
		 attachmentPorts.Top}
	};
	for (const FLiftPortSpec& port : liftPorts)
	{
		UFGFactoryConnectionComponent* connection = port.Connection;
		if (requireOpen && connection->IsConnected())
		{
			continue;
		}

		FEndpointRef& endpoint = outEndpoints.AddDefaulted_GetRef();
		endpoint.Buildable = buildable;
		endpoint.Kind = EBlueprintVerticalEndpointKind::AttachmentPort;
		endpoint.Side = port.Side;
		endpoint.ConnectionName = port.Name;
		endpoint.UsesBlueprintPreviewTransform = useBlueprintPreviewTransform;
	}
}

void FBlueprintVerticalConveyorConnectionManager::GatherDiscoveryCandidates(
	TArray<AFGBuildable*>& outBuildables) const
{
	outBuildables.Reset();

	TSet<AFGBuildable*> uniqueBuildables;
	auto addCandidate = [&outBuildables, &uniqueBuildables](
		AFGBuildable* buildable)
	{
		if (IsSupportedBuildable(buildable) &&
			!uniqueBuildables.Contains(buildable))
		{
			uniqueBuildables.Add(buildable);
			outBuildables.Add(buildable);
		}
	};

	// Preserve vanilla's ordinary overlap feed. The complete-column query below
	// supplements it; it does not replace the parent hologram lifecycle.
	for (const TWeakObjectPtr<AFGBuildable>& weakBuildable : NearbyBuildables)
	{
		addCandidate(weakBuildable.Get());
	}

	AFGBuildableSubsystem* buildableSubsystem =
		AFGBuildableSubsystem::Get(GetHologram());
	if (!IsValid(buildableSubsystem))
	{
		return;
	}

	FBox sourceBounds(ForceInit);
	bool hasSource = false;
	for (const FConnectionState& state : ConnectionStates)
	{
		const FEndpointRef endpoint = GetBlueprintEndpoint(state, false);
		if (!IsValid(endpoint.Buildable))
		{
			continue;
		}
		sourceBounds += GetEndpointWorldTransform(endpoint).GetLocation();
		hasSource = true;
	}
	if (!hasSource)
	{
		return;
	}

	// The parent clearance detector is only vanilla's actor-discovery broadphase;
	// it is not a semantic connection-distance rule. Query the world-height prism
	// through all source endpoint columns, then let exact XY matching, no-tunneling
	// rules, and the real Conveyor Lift hologram decide which candidates are valid.
	// WORLD_MAX is Unreal's world-coordinate bound, not a mod-defined lift range.
	FBox queryBounds = sourceBounds;
	queryBounds.Min.Z = -WORLD_MAX;
	queryBounds.Max.Z = WORLD_MAX;
	TArray<AFGBuildable*> spatialCandidates;
	buildableSubsystem->GetCollidingBuildablesInBoundingBox(
		spatialCandidates,
		queryBounds);
	for (AFGBuildable* buildable : spatialCandidates)
	{
		addCandidate(buildable);
	}
}

bool FBlueprintVerticalConveyorConnectionManager::IsEndpointOpen(
	const FEndpointRef& endpoint) const
{
	if (!IsValid(endpoint.Buildable))
	{
		return false;
	}

	if (endpoint.Kind == EBlueprintVerticalEndpointKind::FloorHoleSide)
	{
		const AFGBuildablePassthrough* hole =
			Cast<AFGBuildablePassthrough>(endpoint.Buildable);
		return IsConveyorFloorHole(hole) &&
			!IsValid(GetFloorHoleSnappedConnection(hole, endpoint.Side));
	}

	UFGFactoryConnectionComponent* connection =
		FindFactoryConnection(endpoint.Buildable, endpoint.ConnectionName);
	return IsValid(connection) &&
		connection->IsConnectionVertical() &&
		!connection->IsConnected();
}

bool FBlueprintVerticalConveyorConnectionManager::HasInterveningBlueprintEndpoint(
	const FEndpointRef& sourceEndpoint,
	const FEndpointRef& targetEndpoint,
	FEndpointRef* outBlockingEndpoint) const
{
	const FVector sourceLocation =
		GetEndpointWorldTransform(sourceEndpoint).GetLocation();
	const FVector targetLocation =
		GetEndpointWorldTransform(targetEndpoint).GetLocation();

	const float minZ = FMath::Min(sourceLocation.Z, targetLocation.Z);
	const float maxZ = FMath::Max(sourceLocation.Z, targetLocation.Z);
	if (!(minZ < maxZ))
	{
		return false;
	}

	for (const FEndpointRef& blocker : BlueprintPhysicalEndpoints)
	{
		if (!IsValid(blocker.Buildable) ||
			blocker.Buildable == sourceEndpoint.Buildable)
		{
			// The source buildable is the boundary origin, not an intervening
			// obstacle. This also excludes sibling endpoints on the same floor hole
			// or attachment. Identity must be explicit here: Blueprint preview
			// transforms may move between reads, so coordinate equality is not a
			// reliable way to prevent a source from blocking itself.
			continue;
		}

		const FVector blockerLocation =
			GetEndpointWorldTransform(blocker).GetLocation();

		// Same-column means semantic equality in X/Y. Use Unreal's numerical
		// equality primitive; do not introduce a mod-defined spatial tolerance.
		if (!FMath::IsNearlyZero(blockerLocation.X - sourceLocation.X) ||
			!FMath::IsNearlyZero(blockerLocation.Y - sourceLocation.Y))
		{
			continue;
		}

		// Only a STRICTLY intervening endpoint blocks. The source's opposite side
		// and sibling attachment port occupy the same Z plane and therefore do not
		// self-block.
		if (blockerLocation.Z > minZ && blockerLocation.Z < maxZ)
		{
			if (outBlockingEndpoint)
			{
				*outBlockingEndpoint = blocker;
			}
			return true;
		}
	}

	return false;
}

FBlueprintVerticalConveyorConnectionManager::FEndpointRef
FBlueprintVerticalConveyorConnectionManager::GetBlueprintEndpoint(
	const FConnectionState& state,
	bool useConstructedBlueprintBuildable) const
{
	FEndpointRef endpoint;
	endpoint.Buildable = useConstructedBlueprintBuildable
		? state.ConstructedBlueprintBuildable
		: state.BlueprintBuildable;
	endpoint.Kind = state.BlueprintKind;
	endpoint.Side = state.BlueprintSide;
	endpoint.ConnectionName = state.BlueprintConnectionName;
	endpoint.UsesBlueprintPreviewTransform = !useConstructedBlueprintBuildable;
	return endpoint;
}

FBlueprintVerticalConveyorConnectionManager::FEndpointRef
FBlueprintVerticalConveyorConnectionManager::GetTargetEndpoint(
	const FConnectionState& state) const
{
	FEndpointRef endpoint;
	endpoint.Buildable = state.TargetBuildable;
	endpoint.Kind = state.TargetKind;
	endpoint.Side = state.TargetSide;
	endpoint.ConnectionName = state.TargetConnectionName;
	endpoint.UsesBlueprintPreviewTransform = false;
	return endpoint;
}

FBlueprintVerticalConveyorConnectionManager::FEndpointKey
FBlueprintVerticalConveyorConnectionManager::MakeEndpointKey(
	const FEndpointRef& endpoint) const
{
	// A splitter/merger lift port has a stable component identity, but its
	// effective top/bottom facing is determined by which side the other endpoint
	// lies on. Do not let that derived facing side create two claim identities for
	// the same physical attachment connection.
	const EBlueprintVerticalEndpointSide keySide =
		endpoint.Kind == EBlueprintVerticalEndpointKind::AttachmentPort
			? EBlueprintVerticalEndpointSide::Top
			: endpoint.Side;
	return {
		endpoint.Buildable,
		endpoint.Kind,
		keySide,
		endpoint.ConnectionName};
}

FString FBlueprintVerticalConveyorConnectionManager::DescribeEndpoint(
	const FEndpointRef& endpoint) const
{
	const TCHAR* kind = endpoint.Kind ==
		EBlueprintVerticalEndpointKind::FloorHoleSide
		? TEXT("floor-hole")
		: TEXT("attachment-port");
	const TCHAR* side = endpoint.Side == EBlueprintVerticalEndpointSide::Top
		? TEXT("top")
		: TEXT("bottom");
	return FString::Printf(
		TEXT("%s[%s,%s,%s]"),
		*GetNameSafe(endpoint.Buildable),
		kind,
		side,
		*endpoint.ConnectionName.ToString());
}

void FBlueprintVerticalConveyorConnectionManager::RegisterNearbyActor(AActor* actor)
{
	AFGBuildable* buildable = Cast<AFGBuildable>(actor);
	if (!IsSupportedBuildable(buildable))
	{
		return;
	}

	const bool alreadyRegistered = NearbyBuildables.ContainsByPredicate(
		[buildable](const TWeakObjectPtr<AFGBuildable>& candidate)
		{
			return candidate.Get() == buildable;
		});
	if (!alreadyRegistered)
	{
		NearbyBuildables.Add(buildable);
	}
}

void FBlueprintVerticalConveyorConnectionManager::UnregisterNearbyActor(AActor* actor)
{
	NearbyBuildables.RemoveAllSwap(
		[actor](const TWeakObjectPtr<AFGBuildable>& candidate)
		{
			return candidate.Get() == actor;
		});
}

void FBlueprintVerticalConveyorConnectionManager::Initialize(
	TArray<AFGBuildable*> buildables)
{
	// Child holograms are registered by name on the parent hologram. Destroy() is
	// deferred and does not immediately remove that registration, so destroying a
	// child and spawning another child with the same name in the same placement
	// session can trip AFGHologram::AddChild's duplicate-name assertion. Keep old
	// bridge holograms disabled instead; SpawnBridgeHologram reuses the child that
	// belongs to the selected state/recipe.
	for (FConnectionState& state : ConnectionStates)
	{
		DisableBridge(state);
	}
	ConnectionStates.Reset();
	BlueprintPhysicalEndpoints.Reset();
	LoggedSourceTunnelStates.Reset();

	for (int32 buildableIndex = 0; buildableIndex < buildables.Num(); ++buildableIndex)
	{
		AFGBuildable* buildable = buildables[buildableIndex];

		TArray<FEndpointRef> physicalEndpoints;
		GatherEndpoints(buildable, true, false, physicalEndpoints);
		BlueprintPhysicalEndpoints.Append(physicalEndpoints);

		TArray<FEndpointRef> endpoints;
		GatherEndpoints(buildable, true, true, endpoints);

		for (const FEndpointRef& endpoint : endpoints)
		{
			FConnectionState& state = ConnectionStates.AddDefaulted_GetRef();
			state.BlueprintBuildable = buildable;
			state.BlueprintBuildableIndex = buildableIndex;
			state.BlueprintKind = endpoint.Kind;
			state.BlueprintSide = endpoint.Side;
			state.BlueprintConnectionName = endpoint.ConnectionName;
			state.LiftRecipe = GetEndpointLiftRecipe(endpoint);
			if (state.LiftRecipe)
			{
				SpawnBridgeHologram(state, ConnectionStates.Num() - 1);
			}
		}
	}

	UE_LOG(
		LogVerticalConveyorAutoConnect,
		Verbose,
		TEXT("VerticalConveyorAutoConnect: initialized %d open vertical endpoints"),
		ConnectionStates.Num());
}

bool FBlueprintVerticalConveyorConnectionManager::
	TryGetBlueprintBuildableWorldTransform(
		const AFGBuildable* buildable,
		FTransform& outTransform) const
{
	AFGBlueprintHologram* hologram = GetHologram();
	if (IsValid(hologram) && IsValid(buildable))
	{
		const TObjectPtr<USceneComponent>* visualRoot =
			hologram->mBuildableToNewRoot.Find(
				const_cast<AFGBuildable*>(buildable));
		if (visualRoot && IsValid(visualRoot->Get()))
		{
			outTransform = visualRoot->Get()->GetComponentTransform();
			return true;
		}
	}

	outTransform = FTransform::Identity;
	return false;
}

FTransform FBlueprintVerticalConveyorConnectionManager::GetBuildableWorldTransform(
	const FEndpointRef& endpoint) const
{
	if (!IsValid(endpoint.Buildable))
	{
		return FTransform::Identity;
	}

	if (endpoint.UsesBlueprintPreviewTransform)
	{
		FTransform visualTransform;
		if (TryGetBlueprintBuildableWorldTransform(
			endpoint.Buildable,
			visualTransform))
		{
			return visualTransform;
		}
	}

	return endpoint.Buildable->GetActorTransform();
}

FTransform FBlueprintVerticalConveyorConnectionManager::GetEndpointWorldTransform(
	const FEndpointRef& endpoint) const
{
	const FTransform buildableWorld = GetBuildableWorldTransform(endpoint);
	if (endpoint.Kind == EBlueprintVerticalEndpointKind::FloorHoleSide)
	{
		return buildableWorld;
	}

	UFGFactoryConnectionComponent* connection =
		FindFactoryConnection(endpoint.Buildable, endpoint.ConnectionName);
	if (!IsValid(connection) || !IsValid(endpoint.Buildable))
	{
		return buildableWorld;
	}

	const FTransform relativeTransform =
		connection->GetComponentTransform().GetRelativeTransform(
			endpoint.Buildable->GetActorTransform());
	return relativeTransform * buildableWorld;
}

FVector FBlueprintVerticalConveyorConnectionManager::
	GetEndpointOutwardNormalWorld(const FEndpointRef& endpoint) const
{
	if (endpoint.Kind == EBlueprintVerticalEndpointKind::FloorHoleSide)
	{
		return endpoint.Side == EBlueprintVerticalEndpointSide::Top
			? FVector::UpVector
			: FVector::DownVector;
	}

	return GetEndpointWorldTransform(endpoint)
		.GetRotation()
		.GetForwardVector()
		.GetSafeNormal();
}

UFGFactoryConnectionComponent*
FBlueprintVerticalConveyorConnectionManager::GetDirectEndpointConnection(
	const FEndpointRef& endpoint) const
{
	return endpoint.Kind == EBlueprintVerticalEndpointKind::AttachmentPort
		? FindFactoryConnection(endpoint.Buildable, endpoint.ConnectionName)
		: nullptr;
}

UFGFactoryConnectionComponent*
FBlueprintVerticalConveyorConnectionManager::GetTransportConnectionAcrossEndpoint(
	const FEndpointRef& endpoint) const
{
	if (endpoint.Kind == EBlueprintVerticalEndpointKind::AttachmentPort)
	{
		return GetDirectEndpointConnection(endpoint);
	}

	const AFGBuildablePassthrough* hole =
		Cast<AFGBuildablePassthrough>(endpoint.Buildable);
	return GetFloorHoleSnappedConnection(hole, OppositeSide(endpoint.Side));
}

UFGFactoryConnectionComponent*
FBlueprintVerticalConveyorConnectionManager::
	GetBlueprintRepresentationConnection(const FEndpointRef& endpoint) const
{
	UFGFactoryConnectionComponent* physicalConnection =
		GetTransportConnectionAcrossEndpoint(endpoint);
	AFGBlueprintHologram* hologram = GetHologram();
	if (!IsValid(physicalConnection) || !IsValid(hologram))
	{
		return physicalConnection;
	}

	// Vanilla represents blueprint-world connections with duplicated components
	// attached to the placed hologram. Its state-change delegate receives those
	// preview components, not necessarily the original BlueprintWorld component.
	// Reuse the duplicate already created by AFGBlueprintHologram so the standard
	// automatic-link icon can replace the correct direction indicator.
	if (hologram->mConnectionRepresentationMeshes.Contains(physicalConnection))
	{
		return physicalConnection;
	}
	UFGFactoryConnectionComponent* fallbackDuplicate = nullptr;
	for (const auto& pair : hologram->mDuplicateConnectionToOriginalMap)
	{
		if (pair.Value.Get() == physicalConnection)
		{
			if (UFGFactoryConnectionComponent* duplicate =
				Cast<UFGFactoryConnectionComponent>(pair.Key.Get()))
			{
				if (hologram->mConnectionRepresentationMeshes.Contains(duplicate))
				{
					return duplicate;
				}
				fallbackDuplicate = duplicate;
			}
		}
	}
	return IsValid(fallbackDuplicate)
		? fallbackDuplicate
		: physicalConnection;
}

AFGBuildableConveyorLift*
FBlueprintVerticalConveyorConnectionManager::GetAdjacentLift(
	const FEndpointRef& endpoint) const
{
	if (!IsValid(endpoint.Buildable))
	{
		return nullptr;
	}

	if (endpoint.Kind == EBlueprintVerticalEndpointKind::FloorHoleSide)
	{
		UFGFactoryConnectionComponent* connection =
			GetTransportConnectionAcrossEndpoint(endpoint);
		return IsValid(connection)
			? Cast<AFGBuildableConveyorLift>(connection->GetOwner())
			: nullptr;
	}

	UFGFactoryConnectionComponent* openConnection =
		GetDirectEndpointConnection(endpoint);
	TInlineComponentArray<UFGFactoryConnectionComponent*> connections;
	endpoint.Buildable->GetComponents(connections);
	for (UFGFactoryConnectionComponent* connection : connections)
	{
		if (!IsValid(connection) ||
			connection == openConnection ||
			!connection->IsConnectionVertical())
		{
			continue;
		}

		UFGFactoryConnectionComponent* connected = connection->GetConnection();
		if (AFGBuildableConveyorLift* lift = IsValid(connected)
			? Cast<AFGBuildableConveyorLift>(connected->GetOwner())
			: nullptr)
		{
			return lift;
		}
	}
	return nullptr;
}

TSubclassOf<UFGRecipe>
FBlueprintVerticalConveyorConnectionManager::GetEndpointLiftRecipe(
	const FEndpointRef& endpoint) const
{
	if (AFGBuildableConveyorLift* lift = GetAdjacentLift(endpoint))
	{
		return lift->GetBuiltWithRecipe();
	}
	return nullptr;
}

EFactoryConnectionDirection
FBlueprintVerticalConveyorConnectionManager::GetRequiredBridgeDirection(
	const FEndpointRef& endpoint) const
{
	UFGFactoryConnectionComponent* outside =
		GetTransportConnectionAcrossEndpoint(endpoint);
	if (!IsValid(outside))
	{
		return EFactoryConnectionDirection::FCD_ANY;
	}

	// Return a normalized contribution to the direction of the PHYSICAL LOWER
	// bridge endpoint, not the direction of this endpoint itself.
	//
	// This deliberately preserves the old floor-hole-only ResolveBaseDirection
	// semantics:
	//   lower/open-top endpoint    -> Opposite(outside)
	//   upper/open-bottom endpoint -> outside
	//
	// The same side rule also applies to a direct lift-attachment port:
	// an output top port requires an input lower bridge endpoint, while an input
	// bottom port likewise implies an input lower bridge endpoint.
	return endpoint.Side == EBlueprintVerticalEndpointSide::Top
		? OppositeDirection(outside->GetDirection())
		: outside->GetDirection();
}


bool FBlueprintVerticalConveyorConnectionManager::
	RestorePersistedAttachmentDirectionBeforeConstruct(
		const FEndpointRef& attachmentEndpoint,
		EFactoryConnectionDirection expectedDirection) const
{
	if (attachmentEndpoint.Kind !=
			EBlueprintVerticalEndpointKind::AttachmentPort ||
		(expectedDirection != EFactoryConnectionDirection::FCD_INPUT &&
		 expectedDirection != EFactoryConnectionDirection::FCD_OUTPUT))
	{
		return false;
	}

	AFGBuildableConveyorAttachment* attachment =
		Cast<AFGBuildableConveyorAttachment>(attachmentEndpoint.Buildable);
	UFGFactoryConnectionComponent* selectedConnection =
		GetDirectEndpointConnection(attachmentEndpoint);
	if (!IsValid(attachment) || !IsValid(selectedConnection))
	{
		return false;
	}

	const EFactoryConnectionDirection runtimeDirection =
		selectedConnection->GetDirection();

	// Conveyor attachments persist directions so BeginPlay can restore dynamic
	// input/output roles after spawning/loading. Before BeginPlay the component
	// can be ANY *or a concrete default/stale value*, so the persisted state is
	// authoritative for a freshly constructed blueprint attachment.
	TInlineComponentArray<UFGFactoryConnectionComponent*> orderedConnections;
	attachment->GetComponents(orderedConnections);
	UFGFactoryConnectionComponent::SortComponentList(orderedConnections);

	const int32 connectionIndex =
		orderedConnections.IndexOfByKey(selectedConnection);
	if (orderedConnections.Num() != attachment->mSavedDirections.Num() ||
		connectionIndex == INDEX_NONE ||
		!attachment->mSavedDirections.IsValidIndex(connectionIndex))
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: cannot restore persisted attachment direction actor=%s port=%s index=%d componentCount=%d savedCount=%d"),
			*GetNameSafe(attachment),
			*GetNameSafe(selectedConnection),
			connectionIndex,
			orderedConnections.Num(),
			attachment->mSavedDirections.Num());
		return false;
	}

	const EFactoryConnectionDirection savedDirection =
		attachment->mSavedDirections[connectionIndex];
	if (savedDirection != expectedDirection)
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: persisted attachment direction disagrees with locked transport actor=%s port=%s index=%d saved=%d expected=%d"),
			*GetNameSafe(attachment),
			*GetNameSafe(selectedConnection),
			connectionIndex,
			static_cast<int32>(savedDirection),
			static_cast<int32>(expectedDirection));
		return false;
	}

	if (runtimeDirection != savedDirection)
	{
		selectedConnection->SetDirection(savedDirection);
	}

	return true;
}

bool FBlueprintVerticalConveyorConnectionManager::
	PrepareAttachmentEndpointDirection(
		const FEndpointRef& attachmentEndpoint,
		const FEndpointRef& blueprintEndpoint,
		bool isTransportInput,
		bool finalValidation,
		EFactoryConnectionDirection& outDirection) const
{
	if (attachmentEndpoint.Kind !=
		EBlueprintVerticalEndpointKind::AttachmentPort)
	{
		return false;
	}

	// The attachment is outside the generated lift. It must therefore output
	// into the lift at the transport-input end and accept lift output at the
	// transport-output end.
	outDirection = isTransportInput
		? EFactoryConnectionDirection::FCD_OUTPUT
		: EFactoryConnectionDirection::FCD_INPUT;

	UFGFactoryConnectionComponent* connection =
		GetDirectEndpointConnection(attachmentEndpoint);
	if (!IsValid(connection))
	{
		return false;
	}

	// Final bridge construction precedes BeginPlay on blueprint-owned
	// attachments. Restore either end of an attachment-to-attachment bridge,
	// not merely placement slot 0.
	const bool originatedFromBlueprint =
		attachmentEndpoint.Buildable == blueprintEndpoint.Buildable;
	if (finalValidation && originatedFromBlueprint &&
		!RestorePersistedAttachmentDirectionBeforeConstruct(
			attachmentEndpoint,
			outDirection))
	{
		return false;
	}

	const EFactoryConnectionDirection runtimeDirection =
		connection->GetDirection();
	return (runtimeDirection != EFactoryConnectionDirection::FCD_INPUT &&
			runtimeDirection != EFactoryConnectionDirection::FCD_OUTPUT) ||
		runtimeDirection == outDirection;
}

bool FBlueprintVerticalConveyorConnectionManager::
	CanConnectLiftToPlacementEnd(
		AFGConveyorLiftHologram* bridge,
		const FEndpointRef& placementEnd) const
{
	if (!IsValid(bridge))
	{
		return false;
	}

	if (placementEnd.Kind ==
		EBlueprintVerticalEndpointKind::FloorHoleSide)
	{
		return IsConveyorFloorHole(
			Cast<AFGBuildablePassthrough>(placementEnd.Buildable));
	}

	UFGFactoryConnectionComponent* liftConnection =
		bridge->mConnectionComponents[1].Get();
	UFGFactoryConnectionComponent* attachmentConnection =
		GetDirectEndpointConnection(placementEnd);
	if (!IsValid(liftConnection) || !IsValid(attachmentConnection))
	{
		return false;
	}

	// This call is the runtime capability contract. Vanilla currently rejects a
	// lift whose second placement endpoint is a vertical attachment port. A mod
	// such as VerticalLogisticsQoL can extend this exact hologram method. Calling
	// the real method keeps compatibility implicit and fails closed when no such
	// capability is present.
	const bool canConnect = bridge->CanConnectToConnection(
		liftConnection,
		attachmentConnection);
	UE_LOG(
		LogVerticalConveyorAutoConnect,
		VeryVerbose,
		TEXT("VerticalConveyorAutoConnect: placement-end capability bridge=%s from=%s to=%s accepted=%d"),
		*GetNameSafe(bridge),
		*GetNameSafe(liftConnection),
		*DescribeEndpoint(placementEnd),
		canConnect ? 1 : 0);
	return canConnect;
}

bool FBlueprintVerticalConveyorConnectionManager::ResolveLiftPlacementEndpoints(
	const FEndpointRef& transportInput,
	const FEndpointRef& transportOutput,
	FEndpointRef& outPlacementStart,
	FEndpointRef& outPlacementEnd) const
{
	// Blueprint ownership is deliberately irrelevant here. Manual vanilla
	// placement establishes that an attachment<->floor-hole lift must START at
	// the splitter/merger side, even when that attachment is the world-side target
	// or the transport output. For two attachments, start at the transport-input
	// attachment and let the live lift hologram decide whether it can connect its
	// second endpoint. Floor-hole<->floor-hole keeps the known-working
	// transport-input-first convention.
	if (transportInput.Kind == EBlueprintVerticalEndpointKind::AttachmentPort)
	{
		outPlacementStart = transportInput;
		outPlacementEnd = transportOutput;
		return true;
	}
	if (transportOutput.Kind == EBlueprintVerticalEndpointKind::AttachmentPort)
	{
		outPlacementStart = transportOutput;
		outPlacementEnd = transportInput;
		return true;
	}

	outPlacementStart = transportInput;
	outPlacementEnd = transportOutput;
	return true;
}

bool FBlueprintVerticalConveyorConnectionManager::ResolveLowerEndpointDirection(
	const FEndpointRef& first,
	const FEndpointRef& second,
	EFactoryConnectionDirection& outLowerEndpointDirection,
	FEndpointRef& outLower,
	FEndpointRef& outUpper) const
{
	const float firstZ = GetEndpointWorldTransform(first).GetLocation().Z;
	const float secondZ = GetEndpointWorldTransform(second).GetLocation().Z;
	if (firstZ <= secondZ)
	{
		outLower = first;
		outUpper = second;
	}
	else
	{
		outLower = second;
		outUpper = first;
	}

	// GetRequiredBridgeDirection() already normalizes BOTH physical endpoints to
	// the direction required at the lower bridge end. Compatible contributions
	// therefore have to be EQUAL. Treating the upper contribution as an actual
	// upper-end direction here was the generalized direction regression.
	const EFactoryConnectionDirection lowerRequired =
		GetRequiredBridgeDirection(outLower);
	const EFactoryConnectionDirection upperRequired =
		GetRequiredBridgeDirection(outUpper);

	const bool lowerConcrete =
		lowerRequired == EFactoryConnectionDirection::FCD_INPUT ||
		lowerRequired == EFactoryConnectionDirection::FCD_OUTPUT;
	const bool upperConcrete =
		upperRequired == EFactoryConnectionDirection::FCD_INPUT ||
		upperRequired == EFactoryConnectionDirection::FCD_OUTPUT;

	if (lowerConcrete && upperConcrete &&
		upperRequired != lowerRequired)
	{
		return false;
	}

	if (lowerConcrete)
	{
		outLowerEndpointDirection = lowerRequired;
		return true;
	}
	if (upperConcrete)
	{
		outLowerEndpointDirection = upperRequired;
		return true;
	}

	return false;
}

FVector FBlueprintVerticalConveyorConnectionManager::
	GetAdjacentLiftVisualDirectionWorld(const FEndpointRef& endpoint) const
{
	AFGBuildableConveyorLift* lift = GetAdjacentLift(endpoint);
	if (!IsValid(lift))
	{
		return FVector::ZeroVector;
	}

	FTransform liftTransform;
	if (endpoint.UsesBlueprintPreviewTransform)
	{
		if (!TryGetBlueprintBuildableWorldTransform(lift, liftTransform))
		{
			return FVector::ZeroVector;
		}
	}
	else
	{
		liftTransform = lift->GetActorTransform();
	}

	FVector direction = liftTransform.GetRotation().GetForwardVector();
	direction.Z = 0.0f;
	return direction.GetSafeNormal();
}

FVector FBlueprintVerticalConveyorConnectionManager::ResolveBridgeVisualDirection(
	const FEndpointRef& blueprintEndpoint,
	const FEndpointRef& targetEndpoint) const
{
	const FVector blueprintDirection =
		GetAdjacentLiftVisualDirectionWorld(blueprintEndpoint);
	const FVector targetDirection =
		GetAdjacentLiftVisualDirectionWorld(targetEndpoint);

	FVector direction = !blueprintDirection.IsNearlyZero()
		? blueprintDirection
		: targetDirection;

	if (direction.IsNearlyZero() &&
		blueprintEndpoint.Kind == EBlueprintVerticalEndpointKind::FloorHoleSide)
	{
		const AFGBuildablePassthrough* hole =
			Cast<AFGBuildablePassthrough>(blueprintEndpoint.Buildable);
		FVector validDirection = FVector::ZeroVector;
		if (IsValid(hole) &&
			hole->GetValidAttachDirection(validDirection) &&
			!validDirection.IsNearlyZero())
		{
			const FVector localDirection =
				hole->GetActorTransform().InverseTransformVectorNoScale(
					validDirection);
			direction = GetBuildableWorldTransform(blueprintEndpoint)
				.TransformVectorNoScale(localDirection);
		}
	}

	if (direction.IsNearlyZero())
	{
		direction = GetBuildableWorldTransform(blueprintEndpoint)
			.GetRotation()
			.GetForwardVector();
	}
	direction.Z = 0.0f;
	if (direction.IsNearlyZero())
	{
		direction = FVector::ForwardVector;
	}

	return direction.GetSafeNormal();
}

TSubclassOf<UFGRecipe>
FBlueprintVerticalConveyorConnectionManager::ResolveLiftRecipe(
	const FEndpointRef& blueprintEndpoint,
	const FEndpointRef& targetEndpoint) const
{
	if (TSubclassOf<UFGRecipe> recipe =
		GetEndpointLiftRecipe(blueprintEndpoint))
	{
		return recipe;
	}
	return GetEndpointLiftRecipe(targetEndpoint);
}

void FBlueprintVerticalConveyorConnectionManager::SpawnBridgeHologram(
	FConnectionState& state,
	int32 stateIndex)
{
	AFGBlueprintHologram* parent = GetHologram();
	if (!IsValid(parent) || !state.LiftRecipe)
	{
		return;
	}

	// AFGHologram keeps a stable name lookup for child holograms. Destroying a
	// child does not synchronously release that name, which is especially easy to
	// hit while rotating a blueprint and switching between candidate lift tiers.
	// Give every state/recipe pair a deterministic child name and cache/reuse that
	// hologram for the lifetime of the parent blueprint hologram.
	if (IsValid(state.BridgeHologram))
	{
		state.BridgeHologram->SetDisabled(true);
	}
	state.BridgeHologram = nullptr;

	UClass* liftRecipe = state.LiftRecipe.Get();
	const FString recipeName = GetNameSafe(liftRecipe);
	const FString recipePath = IsValid(liftRecipe)
		? liftRecipe->GetPathName()
		: FString();

	// Use the complete recipe object path as cache identity. The readable short
	// name is retained only for diagnostics; the deterministic MD5 prevents two
	// recipes from different packages with the same object name from aliasing.
	const FString recipeIdentity = FMD5::HashAnsiString(*recipePath);
	const FName childName(*FString::Printf(
		TEXT("VerticalConveyorAutoBridge_%d_%s_%s"),
		stateIndex,
		*recipeName,
		*recipeIdentity));

	AFGHologram* child = parent->FindChildHologramByName(childName);
	if (child == nullptr)
	{
		// True cache miss: no child is registered under this deterministic name.
		AActor* hologramOwner = IsValid(parent->GetOwner())
			? parent->GetOwner()
			: parent;

		child = AFGHologram::SpawnChildHologramFromRecipe(
			parent,
			childName,
			state.LiftRecipe,
			hologramOwner,
			parent->GetActorLocation());
	}
	else if (!IsValid(child))
	{
		// AFGHologram can retain a child name while the actor is already pending
		// destruction. Treat that as a poisoned cache entry and fail closed for
		// this placement session. Respawning with the same registered name would
		// hit AFGHologram::AddChild's duplicate-name assertion again.
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: stale registered bridge child %s for recipe=%s path=%s state=%d; refusing same-name respawn"),
			*childName.ToString(),
			*recipeName,
			*recipePath,
			stateIndex);
		return;
	}

	state.BridgeHologram = Cast<AFGConveyorLiftHologram>(child);
	if (!IsValid(state.BridgeHologram))
	{
		// Leave an unexpectedly typed child disabled and registered. Destroying it
		// here would recreate the same stale-name problem on a later retry.
		if (IsValid(child))
		{
			child->SetDisabled(true);
		}
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: recipe %s child %s is not a conveyor-lift hologram"),
			*GetNameSafe(state.LiftRecipe.Get()),
			*childName.ToString());
		return;
	}

	state.BridgeHologram->SetShouldSpawnChildHolograms(false);
	state.BridgeHologram->SetBuildModeOverride(
		parent->GetBridgeHologramBuildModeOverride());
	state.BridgeHologram->SetDisabled(true);

}

bool FBlueprintVerticalConveyorConnectionManager::EnsureBridgeRecipe(
	FConnectionState& state,
	int32 stateIndex,
	TSubclassOf<UFGRecipe> recipe)
{
	if (!recipe)
	{
		return false;
	}

	if (state.LiftRecipe != recipe || !IsValid(state.BridgeHologram))
	{
		state.LiftRecipe = recipe;
		SpawnBridgeHologram(state, stateIndex);
	}
	return IsValid(state.BridgeHologram);
}

bool FBlueprintVerticalConveyorConnectionManager::ConfigureBridgeHologram(
	FConnectionState& state,
	bool useConstructedBlueprintBuildable,
	bool finalValidation)
{
	if (!IsValid(state.BridgeHologram) || !IsValid(state.TargetBuildable))
	{
		return false;
	}

	const FEndpointRef blueprintEndpoint =
		GetBlueprintEndpoint(state, useConstructedBlueprintBuildable);
	const FEndpointRef targetEndpoint = GetTargetEndpoint(state);
	if (!IsValid(blueprintEndpoint.Buildable) ||
		!IsValid(targetEndpoint.Buildable))
	{
		return false;
	}

	FEndpointRef lowerEndpoint;
	FEndpointRef upperEndpoint;
	EFactoryConnectionDirection lowerEndpointDirection =
		state.ResolvedLowerEndpointDirection;

	// Final construction runs before the freshly spawned blueprint actors have
	// completed BeginPlay. Their transport-side connection can therefore be
	// transiently FCD_ANY. The connection was already validated and locked during
	// preview, so remap geometry here but retain that transport intent.
	const bool hasLockedTransportDirection =
		(finalValidation || state.HasSnappedTarget) &&
		(lowerEndpointDirection == EFactoryConnectionDirection::FCD_INPUT ||
		 lowerEndpointDirection == EFactoryConnectionDirection::FCD_OUTPUT);
	if (hasLockedTransportDirection)
	{
		const float blueprintZ =
			GetEndpointWorldTransform(blueprintEndpoint).GetLocation().Z;
		const float targetZ =
			GetEndpointWorldTransform(targetEndpoint).GetLocation().Z;
		if (blueprintZ <= targetZ)
		{
			lowerEndpoint = blueprintEndpoint;
			upperEndpoint = targetEndpoint;
		}
		else
		{
			lowerEndpoint = targetEndpoint;
			upperEndpoint = blueprintEndpoint;
		}
	}
	else
	{
		if (!ResolveLowerEndpointDirection(
			blueprintEndpoint,
			targetEndpoint,
			lowerEndpointDirection,
			lowerEndpoint,
			upperEndpoint))
		{
			return false;
		}
		state.ResolvedLowerEndpointDirection = lowerEndpointDirection;
	}

	const bool flowsUpwards =
		lowerEndpointDirection == EFactoryConnectionDirection::FCD_INPUT;
	const FEndpointRef& inputEndpoint =
		flowsUpwards ? lowerEndpoint : upperEndpoint;
	const FEndpointRef& outputEndpoint =
		flowsUpwards ? upperEndpoint : lowerEndpoint;

	FEndpointRef placementStart;
	FEndpointRef placementEnd;
	if (!ResolveLiftPlacementEndpoints(
		inputEndpoint,
		outputEndpoint,
		placementStart,
		placementEnd))
	{
		return false;
	}

	const FVector inputLocation =
		GetEndpointWorldTransform(inputEndpoint).GetLocation();
	const FVector outputLocation =
		GetEndpointWorldTransform(outputEndpoint).GetLocation();
	const FVector placementStartLocation =
		GetEndpointWorldTransform(placementStart).GetLocation();
	const FVector placementEndLocation =
		GetEndpointWorldTransform(placementEnd).GetLocation();
	const FVector visualDirection = ResolveBridgeVisualDirection(
		blueprintEndpoint,
		targetEndpoint);

	FRotator baseRotation = visualDirection.Rotation();
	baseRotation.Pitch = 0.0f;
	baseRotation.Roll = 0.0f;
	const FTransform placementStartTransform(
		baseRotation,
		placementStartLocation,
		FVector::OneVector);
	const FTransform placementEndTransform(
		baseRotation,
		placementEndLocation,
		FVector::OneVector);

	AFGConveyorLiftHologram* bridge = state.BridgeHologram;
	bridge->SetActorTransform(placementStartTransform);
	bridge->mTopTransform =
		placementEndTransform.GetRelativeTransform(placementStartTransform);

	const bool placementStartsAtAttachment =
		placementStart.Kind == EBlueprintVerticalEndpointKind::AttachmentPort;
	const bool placementEndsAtAttachment =
		placementEnd.Kind == EBlueprintVerticalEndpointKind::AttachmentPort;
	EFactoryConnectionDirection forcedAttachmentDirection =
		EFactoryConnectionDirection::FCD_ANY;
	bridge->mSnappedPassthroughs.SetNum(2);

	if (placementStartsAtAttachment)
	{
		// Measured vanilla state: an attachment is placement slot 0. Slot 1 is
		// either a Floor Hole or, when the live hologram exposes that capability,
		// another attachment. Flow is expressed by arrow/reversal, not by swapping
		// placement slots.
		UFGFactoryConnectionComponent* startAttachmentConnection =
			GetDirectEndpointConnection(placementStart);
		if (!IsValid(startAttachmentConnection))
		{
			return false;
		}

		const bool startIsTransportInput =
			MakeEndpointKey(placementStart) == MakeEndpointKey(inputEndpoint);
		if (!PrepareAttachmentEndpointDirection(
				placementStart,
				blueprintEndpoint,
				startIsTransportInput,
				finalValidation,
				forcedAttachmentDirection))
		{
			return false;
		}

		AFGBuildablePassthrough* floorHole = nullptr;
		UFGFactoryConnectionComponent* endAttachmentConnection = nullptr;
		if (placementEndsAtAttachment)
		{
			EFactoryConnectionDirection endAttachmentDirection =
				EFactoryConnectionDirection::FCD_ANY;
			const bool endIsTransportInput =
				MakeEndpointKey(placementEnd) == MakeEndpointKey(inputEndpoint);
			if (!PrepareAttachmentEndpointDirection(
					placementEnd,
					blueprintEndpoint,
					endIsTransportInput,
					finalValidation,
					endAttachmentDirection))
			{
				return false;
			}
			endAttachmentConnection =
				GetDirectEndpointConnection(placementEnd);
			if (!IsValid(endAttachmentConnection) ||
				OppositeDirection(endAttachmentDirection) !=
					forcedAttachmentDirection)
			{
				return false;
			}
		}
		else
		{
			floorHole =
				Cast<AFGBuildablePassthrough>(placementEnd.Buildable);
			if (!IsConveyorFloorHole(floorHole))
			{
				return false;
			}
		}

		bridge->mSnappedPassthroughs[0] = nullptr;
		bridge->mSnappedPassthroughs[1] = floorHole;
		bridge->mSnappedConnectionComponents[0] = startAttachmentConnection;
		// For a Floor Hole, manual vanilla placement stores the continuation
		// across the passthrough in slot 1 when one exists. A bare Floor Hole leaves
		// it null. An attachment endpoint stores its direct vertical connection.
		bridge->mSnappedConnectionComponents[1] = placementEndsAtAttachment
			? endAttachmentConnection
			: GetTransportConnectionAcrossEndpoint(placementEnd);
		bridge->mForcedNormalDirection =
			GetEndpointOutwardNormalWorld(placementStart);
		bridge->mArrowDirection = forcedAttachmentDirection;
		bridge->mIsReversed =
			forcedAttachmentDirection == EFactoryConnectionDirection::FCD_INPUT;
		bridge->mFirstStepYaw = 0.0f;
	}
	else
	{
		// Proven floor-hole-only state remains transport-input first.
		bridge->mSnappedPassthroughs[0] =
			Cast<AFGBuildablePassthrough>(inputEndpoint.Buildable);
		bridge->mSnappedPassthroughs[1] =
			Cast<AFGBuildablePassthrough>(outputEndpoint.Buildable);
		bridge->mSnappedConnectionComponents[0] = nullptr;
		bridge->mSnappedConnectionComponents[1] = nullptr;
		bridge->mForcedNormalDirection = visualDirection;
		bridge->mArrowDirection = EFactoryConnectionDirection::FCD_INPUT;
		bridge->mIsReversed = false;
		bridge->mFirstStepYaw = baseRotation.Yaw;
	}
	bridge->mActivePointIdx = 1;

	bridge->Tags.AddUnique(
		VerticalConveyorAutoConnect::
			PreviewInvalidFloorSuppressionTag());

	bridge->SetDisabled(false);
	bridge->ResetConstructDisqualifiers();
	bridge->UpdateConnectionDirections();
	bridge->OnRep_TopTransform();
	bridge->OnRep_SnappedPassthroughs();

	if (placementStartsAtAttachment)
	{
		// UpdateConnectionDirections() and the transform/passthrough rep callbacks are
		// designed around a normally snapped interactive placement. With a synthetic
		// bridge and a bare attachment port (FCD_ANY), they can erase the transport
		// state we established and leave mArrowDirection as FCD_ANY. Re-apply the
		// FULL state measured from manual vanilla placement AFTER those state-mutating
		// calls, including the hologram's own two connection-component directions.
		// This reconstructs hologram state without mutating the real attachment port.
		UFGFactoryConnectionComponent* ownConnection0 =
			bridge->mConnectionComponents[0].Get();
		UFGFactoryConnectionComponent* ownConnection1 =
			bridge->mConnectionComponents[1].Get();
		const EFactoryConnectionDirection ownDirection0 =
			OppositeDirection(forcedAttachmentDirection);
		if (!IsValid(ownConnection0) ||
			!IsValid(ownConnection1) ||
			ownDirection0 == EFactoryConnectionDirection::FCD_ANY)
		{
			return false;
		}

		ownConnection0->SetDirection(ownDirection0);
		ownConnection1->SetDirection(forcedAttachmentDirection);
		bridge->mArrowDirection = forcedAttachmentDirection;
		bridge->mIsReversed =
			forcedAttachmentDirection == EFactoryConnectionDirection::FCD_INPUT;
	}

	if (!CanConnectLiftToPlacementEnd(bridge, placementEnd))
	{
		return false;
	}

	bridge->OnRep_ArrowDirection();
	bridge->UpdateClearance();
	if (!finalValidation)
	{
		bridge->CheckBlueprintCommingling();
	}
	bridge->CheckValidPlacement();

	TArray<TSubclassOf<UFGConstructDisqualifier>> disqualifiers;
	bridge->GetConstructDisqualifiers(disqualifiers);
	FString disqualifierNames;
	for (const TSubclassOf<UFGConstructDisqualifier>& disqualifier : disqualifiers)
	{
		if (!disqualifierNames.IsEmpty())
		{
			disqualifierNames += TEXT(",");
		}
		disqualifierNames += GetNameSafe(disqualifier.Get());
	}
	if (disqualifierNames.IsEmpty())
	{
		disqualifierNames = TEXT("<none>");
	}

	const bool canConstruct = bridge->CanConstruct();
	const bool placementStartsAtTransportInput =
		MakeEndpointKey(placementStart) == MakeEndpointKey(inputEndpoint);

	if (finalValidation)
	{
		bridge->Tags.Remove(
			VerticalConveyorAutoConnect::
				PreviewInvalidFloorSuppressionTag());
	}

	if (finalValidation)
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Verbose,
			TEXT("VerticalConveyorAutoConnect: final-config %s flow=%s transportInput=%s loc=%s transportOutput=%s loc=%s placementStart=%s placementEnd=%s placementStartIsTransportInput=%d startsAtAttachment=%d endsAtAttachment=%d reversed=%d arrow=%d own0=%d own1=%d snap0=%s snap1=%s pass0=%s pass1=%s spanZ=%.1f canConstruct=%d disqualifiers=[%s]"),
			*GetNameSafe(bridge),
			flowsUpwards ? TEXT("up") : TEXT("down"),
			*DescribeEndpoint(inputEndpoint),
			*inputLocation.ToString(),
			*DescribeEndpoint(outputEndpoint),
			*outputLocation.ToString(),
			*DescribeEndpoint(placementStart),
			*DescribeEndpoint(placementEnd),
			placementStartsAtTransportInput ? 1 : 0,
			placementStartsAtAttachment ? 1 : 0,
			placementEndsAtAttachment ? 1 : 0,
			bridge->mIsReversed ? 1 : 0,
			static_cast<int32>(bridge->mArrowDirection),
			IsValid(bridge->mConnectionComponents[0].Get())
			? static_cast<int32>(bridge->mConnectionComponents[0]->GetDirection())
			: -1,
			IsValid(bridge->mConnectionComponents[1].Get())
			? static_cast<int32>(bridge->mConnectionComponents[1]->GetDirection())
			: -1,
			*GetNameSafe(bridge->mSnappedConnectionComponents[0].Get()),
			*GetNameSafe(bridge->mSnappedConnectionComponents[1].Get()),
			bridge->mSnappedPassthroughs.Num() > 0
			? *GetNameSafe(bridge->mSnappedPassthroughs[0].Get())
			: TEXT("<none>"),
			bridge->mSnappedPassthroughs.Num() > 1
			? *GetNameSafe(bridge->mSnappedPassthroughs[1].Get())
			: TEXT("<none>"),
			bridge->mTopTransform.GetTranslation().Z,
			canConstruct ? 1 : 0,
			*disqualifierNames);
	}

	// Do not reinterpret vanilla disqualifiers. Soft-clearance warnings are
	// intentionally constructible in vanilla; CanConstruct() is the authority.
	return canConstruct;
}

void FBlueprintVerticalConveyorConnectionManager::DisableBridge(
	FConnectionState& state)
{
	if (IsValid(state.BridgeHologram))
	{
		state.BridgeHologram->SetDisabled(true);
	}
}

void FBlueprintVerticalConveyorConnectionManager::ClearTarget(
	FConnectionState& state)
{
	state.TargetBuildable = nullptr;
	state.TargetConnectionName = NAME_None;
	state.TargetKind = EBlueprintVerticalEndpointKind::FloorHoleSide;
	state.TargetSide = EBlueprintVerticalEndpointSide::Bottom;
	state.ResolvedLowerEndpointDirection =
		EFactoryConnectionDirection::FCD_ANY;
	state.CanDirectlyConnect = false;
	state.IsValid = false;
}

bool FBlueprintVerticalConveyorConnectionManager::IsAttachmentPortForSpan(
	const FEndpointRef& attachmentEndpoint,
	const FEndpointRef& otherEndpoint) const
{
	if (attachmentEndpoint.Kind !=
		EBlueprintVerticalEndpointKind::AttachmentPort)
	{
		return true;
	}

	const float attachmentZ =
		GetEndpointWorldTransform(attachmentEndpoint).GetLocation().Z;
	const float otherZ =
		GetEndpointWorldTransform(otherEndpoint).GetLocation().Z;
	if (FMath::IsNearlyEqual(attachmentZ, otherZ))
	{
		return true;
	}

	// Measured vanilla behavior:
	//   attachment below floor hole  -> snap0 = TopConnection
	//   attachment above floor hole  -> snap0 = BottomConnection
	//
	// AFGConveyorAttachmentHologram's named lift-connection constants resolve to
	// those component identities in the opposite-looking order used by the
	// buildable. Use the vanilla/API identities rather than literal component
	// strings so this remains tied to the game's own attachment definition.
	const FName requiredConnectionName =
		attachmentZ < otherZ
			? AFGConveyorAttachmentHologram::mLiftConnection_Bottom
			: AFGConveyorAttachmentHologram::mLiftConnection_Top;

	return attachmentEndpoint.ConnectionName == requiredConnectionName;
}

void FBlueprintVerticalConveyorConnectionManager::NormalizeAttachmentFacingSides(
	FEndpointRef& first,
	FEndpointRef& second) const
{
	const float firstZ = GetEndpointWorldTransform(first).GetLocation().Z;
	const float secondZ = GetEndpointWorldTransform(second).GetLocation().Z;
	if (FMath::IsNearlyEqual(firstZ, secondZ))
	{
		return;
	}

	// Floor-hole top/bottom is an intrinsic side of the passthrough. A vertical
	// attachment connection is different: the component identity is intrinsic,
	// while the side it faces for THIS lift span follows the other endpoint's Z.
	// This is why the same open splitter/merger port can legitimately accept a
	// lift arriving from above or below in vanilla.
	if (first.Kind == EBlueprintVerticalEndpointKind::AttachmentPort)
	{
		first.Side = firstZ < secondZ
			? EBlueprintVerticalEndpointSide::Top
			: EBlueprintVerticalEndpointSide::Bottom;
	}
	if (second.Kind == EBlueprintVerticalEndpointKind::AttachmentPort)
	{
		second.Side = secondZ < firstZ
			? EBlueprintVerticalEndpointSide::Top
			: EBlueprintVerticalEndpointSide::Bottom;
	}
}

bool FBlueprintVerticalConveyorConnectionManager::IsGeometricallyCompatible(
	const FEndpointRef& blueprintEndpoint,
	const FEndpointRef& targetEndpoint,
	float& outVerticalDistance,
	bool& outCanDirectlyConnect) const
{
	const FVector blueprintLocation =
		GetEndpointWorldTransform(blueprintEndpoint).GetLocation();
	const FVector targetLocation =
		GetEndpointWorldTransform(targetEndpoint).GetLocation();
	const FVector delta = targetLocation - blueprintLocation;

	// These endpoints are required to occupy the same physical XY column.
	// Use Unreal's own numerical-equality semantics only; do not widen this into
	// a mod-defined snapping radius.
	if (!FMath::IsNearlyZero(delta.X) ||
		!FMath::IsNearlyZero(delta.Y))
	{
		return false;
	}

	outVerticalDistance = FMath::Abs(delta.Z);
	outCanDirectlyConnect = delta.IsNearlyZero();
	if (outCanDirectlyConnect)
	{
		const FVector firstNormal =
			GetEndpointOutwardNormalWorld(blueprintEndpoint).GetSafeNormal();
		const FVector secondNormal =
			GetEndpointOutwardNormalWorld(targetEndpoint).GetSafeNormal();

		// Direct connection requires genuinely opposing endpoint normals. Again,
		// use FVector's engine-defined equality tolerance rather than inventing
		// an angular allowance.
		return firstNormal.Equals(-secondNormal);
	}

	// Do not pre-limit vertical span here. The generated vanilla conveyor-lift
	// hologram owns the validity decision for whatever span Satisfactory allows.
	if (delta.Z > 0.0f)
	{
		return blueprintEndpoint.Side == EBlueprintVerticalEndpointSide::Top &&
			targetEndpoint.Side == EBlueprintVerticalEndpointSide::Bottom;
	}
	return blueprintEndpoint.Side == EBlueprintVerticalEndpointSide::Bottom &&
		targetEndpoint.Side == EBlueprintVerticalEndpointSide::Top;
}

void FBlueprintVerticalConveyorConnectionManager::FindBestTarget(
	FConnectionState& state,
	int32 stateIndex,
	const TSet<FEndpointKey>& claimedTargetEndpoints,
	const TArray<AFGBuildable*>& discoveryBuildables)
{
	if (state.HasSnappedTarget && IsValid(state.TargetBuildable))
	{
		return;
	}
	if (state.HasSnappedTarget)
	{
		// Match vanilla's state lifecycle: a destroyed/unloaded locked target
		// releases the first-click lock instead of silently carrying it to a new
		// candidate.
		state.HasSnappedTarget = false;
	}

	ClearTarget(state);
	const FEndpointRef rawBlueprintEndpoint = GetBlueprintEndpoint(state, false);

	if (!IsEndpointOpen(rawBlueprintEndpoint))
	{
		DisableBridge(state);
		return;
	}

	struct FPhysicalCandidate
	{
		FEndpointRef BlueprintEndpoint;
		FEndpointRef Endpoint;
		float VerticalDistance = 0.0f;
		bool CanDirectlyConnect = false;
	};

	// Discover physical endpoints first, including occupied ones. A vertical
	// splitter/merger or floor-hole endpoint physically terminates the search
	// column: if the nearest endpoint cannot be used, do not tunnel through it
	// to a farther endpoint at the same X/Y coordinates.
	TArray<FPhysicalCandidate> physicalCandidates;
	for (AFGBuildable* buildable : discoveryBuildables)
	{
		if (!IsValid(buildable))
		{
			continue;
		}

		TArray<FEndpointRef> endpoints;
		GatherEndpoints(buildable, false, false, endpoints);
		for (const FEndpointRef& rawTargetEndpoint : endpoints)
		{
			// Attachment port identity is not interchangeable. Manual vanilla
			// placement selects TopConnection for an upward span from the
			// attachment and BottomConnection for a downward span. Filtering the
			// wrong identity here also prevents it from claiming the floor-hole
			// target before the correct sibling endpoint is evaluated.
			if (!IsAttachmentPortForSpan(
					rawBlueprintEndpoint,
					rawTargetEndpoint) ||
				!IsAttachmentPortForSpan(
					rawTargetEndpoint,
					rawBlueprintEndpoint))
			{
				continue;
			}

			FEndpointRef effectiveBlueprintEndpoint = rawBlueprintEndpoint;
			FEndpointRef effectiveTargetEndpoint = rawTargetEndpoint;
			NormalizeAttachmentFacingSides(
				effectiveBlueprintEndpoint,
				effectiveTargetEndpoint);

			float verticalDistance = 0.0f;
			bool canDirectlyConnect = false;
			if (!IsGeometricallyCompatible(
				effectiveBlueprintEndpoint,
				effectiveTargetEndpoint,
				verticalDistance,
				canDirectlyConnect))
			{
				continue;
			}
			FEndpointRef blockingBlueprintEndpoint;
			if (!canDirectlyConnect &&
				HasInterveningBlueprintEndpoint(
					effectiveBlueprintEndpoint,
					effectiveTargetEndpoint,
					&blockingBlueprintEndpoint))
			{
				if (!LoggedSourceTunnelStates.Contains(stateIndex))
				{
					LoggedSourceTunnelStates.Add(stateIndex);
					UE_LOG(
						LogVerticalConveyorAutoConnect,
						Verbose,
						TEXT("VerticalConveyorAutoConnect: source-side no-tunnel state=%d source=%s blocker=%s target=%s"),
						stateIndex,
						*DescribeEndpoint(effectiveBlueprintEndpoint),
						*DescribeEndpoint(blockingBlueprintEndpoint),
						*DescribeEndpoint(effectiveTargetEndpoint));
				}
				continue;
			}

			FPhysicalCandidate& candidate =
				physicalCandidates.AddDefaulted_GetRef();
			candidate.BlueprintEndpoint = effectiveBlueprintEndpoint;
			candidate.Endpoint = effectiveTargetEndpoint;
			candidate.VerticalDistance = verticalDistance;
			candidate.CanDirectlyConnect = canDirectlyConnect;
		}
	}

	physicalCandidates.Sort(
		[](const FPhysicalCandidate& first, const FPhysicalCandidate& second)
		{
			if (!FMath::IsNearlyEqual(
				first.VerticalDistance,
				second.VerticalDistance))
			{
				return first.VerticalDistance < second.VerticalDistance;
			}
			const FString firstName = IsValid(first.Endpoint.Buildable)
				? first.Endpoint.Buildable->GetPathName()
				: FString();
			const FString secondName = IsValid(second.Endpoint.Buildable)
				? second.Endpoint.Buildable->GetPathName()
				: FString();
			if (firstName != secondName)
			{
				return firstName.Compare(secondName, ESearchCase::CaseSensitive) < 0;
			}
			if (first.Endpoint.Kind != second.Endpoint.Kind)
			{
				return static_cast<uint8>(first.Endpoint.Kind) <
					static_cast<uint8>(second.Endpoint.Kind);
			}
			if (first.Endpoint.Side != second.Endpoint.Side)
			{
				return static_cast<uint8>(first.Endpoint.Side) <
					static_cast<uint8>(second.Endpoint.Side);
			}
			return first.Endpoint.ConnectionName.ToString().Compare(
				second.Endpoint.ConnectionName.ToString(),
				ESearchCase::CaseSensitive) < 0;
		});

	if (physicalCandidates.IsEmpty())
	{
		DisableBridge(state);
		return;
	}

	const float nearestDistance = physicalCandidates[0].VerticalDistance;
	for (const FPhysicalCandidate& physical : physicalCandidates)
	{
		if (!FMath::IsNearlyEqual(
			physical.VerticalDistance,
			nearestDistance))
		{
			break;
		}

		const FEndpointRef& blueprintEndpoint = physical.BlueprintEndpoint;
		const FEndpointRef& targetEndpoint = physical.Endpoint;
		if (!IsEndpointOpen(targetEndpoint))
		{
			continue;
		}
		if (claimedTargetEndpoints.Contains(MakeEndpointKey(targetEndpoint)))
		{
			continue;
		}

		const TSubclassOf<UFGRecipe> recipe =
			ResolveLiftRecipe(blueprintEndpoint, targetEndpoint);
		if (!recipe)
		{
			continue;
		}

		EFactoryConnectionDirection resolvedLowerDirection =
			EFactoryConnectionDirection::FCD_ANY;
		if (physical.CanDirectlyConnect)
		{
			UFGFactoryConnectionComponent* first =
				GetTransportConnectionAcrossEndpoint(blueprintEndpoint);
			UFGFactoryConnectionComponent* second =
				GetTransportConnectionAcrossEndpoint(targetEndpoint);
			if (!IsValid(first) || !IsValid(second) ||
				(first->GetConnection() != second &&
					(first->IsConnected() || second->IsConnected() ||
						(!first->CanConnectTo(second) &&
						 !second->CanConnectTo(first)))))
			{
				continue;
			}
		}
		else
		{
			FEndpointRef lower;
			FEndpointRef upper;
			if (!ResolveLowerEndpointDirection(
				blueprintEndpoint,
				targetEndpoint,
				resolvedLowerDirection,
				lower,
				upper))
			{
				continue;
			}
		}

		// Persist the geometry-derived attachment facing side as part of the locked
		// candidate state. Floor-hole sides remain unchanged/intrinsic.
		state.BlueprintSide = blueprintEndpoint.Side;
		state.TargetBuildable = targetEndpoint.Buildable;
		state.TargetKind = targetEndpoint.Kind;
		state.TargetSide = targetEndpoint.Side;
		state.TargetConnectionName = targetEndpoint.ConnectionName;
		state.ResolvedLowerEndpointDirection = resolvedLowerDirection;
		state.CanDirectlyConnect = physical.CanDirectlyConnect;

		if (physical.CanDirectlyConnect)
		{
			state.LiftRecipe = recipe;
			state.IsValid = true;
			DisableBridge(state);
			return;
		}

		if (EnsureBridgeRecipe(state, stateIndex, recipe) &&
			ConfigureBridgeHologram(state, false, false))
		{
			state.IsValid = true;
			return;
		}
	}

	// The nearest physical endpoint(s) existed but none were usable. Intentionally
	// stop here instead of considering farther targets in the same vertical column.
	ClearTarget(state);
	DisableBridge(state);
}

void FBlueprintVerticalConveyorConnectionManager::
	BroadcastConnectionStateChange(
		const FEndpointRef& blueprintEndpoint,
		const FEndpointRef& previousTargetEndpoint,
		const FEndpointRef& targetEndpoint,
		bool isValid)
{
	TArray<UFGConnectionComponent*> blueprintConnections;
	if (UFGFactoryConnectionComponent* connection =
		GetBlueprintRepresentationConnection(blueprintEndpoint))
	{
		blueprintConnections.Add(connection);
	}
	if (blueprintConnections.IsEmpty())
	{
		// A bare Floor Hole has no factory connection component (and therefore no
		// ordinary direction indicator) for the parent hologram to replace.
		return;
	}

	UFGFactoryConnectionComponent* previousTargetConnection =
		GetTransportConnectionAcrossEndpoint(previousTargetEndpoint);
	UFGFactoryConnectionComponent* targetConnection =
		GetTransportConnectionAcrossEndpoint(targetEndpoint);

	// This is the same delegate contract used by vanilla's manager. The parent
	// hologram owns the automatic-link representation and swaps the ordinary
	// connection-direction visualization when a state becomes valid.
	mOnConnectionStateChanged.Broadcast(
		blueprintConnections,
		previousTargetConnection,
		targetConnection,
		isValid);
}

void FBlueprintVerticalConveyorConnectionManager::UpdateAutomaticConnections(
	const FHitResult& /*hitResult*/,
	bool& outPlaySnapEffects)
{
	NearbyBuildables.RemoveAllSwap(
		[](const TWeakObjectPtr<AFGBuildable>& buildable)
		{
			return !buildable.IsValid();
		});

	TArray<AFGBuildable*> discoveryBuildables;
	GatherDiscoveryCandidates(discoveryBuildables);

	TSet<FEndpointKey> claimedTargetEndpoints;
	for (int32 stateIndex = 0; stateIndex < ConnectionStates.Num(); ++stateIndex)
	{
		FConnectionState& state = ConnectionStates[stateIndex];
		AFGBuildable* previousTarget = state.TargetBuildable;
		const FName previousConnectionName = state.TargetConnectionName;
		const EBlueprintVerticalEndpointKind previousTargetKind =
			state.TargetKind;
		const EBlueprintVerticalEndpointSide previousTargetSide =
			state.TargetSide;
		const bool previousValid = state.IsValid;
		const FEndpointRef previousTargetEndpoint = GetTargetEndpoint(state);

		FindBestTarget(
			state,
			stateIndex,
			claimedTargetEndpoints,
			discoveryBuildables);
		if (state.IsValid && IsValid(state.TargetBuildable))
		{
			claimedTargetEndpoints.Add(MakeEndpointKey(GetTargetEndpoint(state)));
		}

		if (previousTarget != state.TargetBuildable ||
			previousConnectionName != state.TargetConnectionName ||
			previousTargetKind != state.TargetKind ||
			previousTargetSide != state.TargetSide ||
			previousValid != state.IsValid)
		{
			BroadcastConnectionStateChange(
				GetBlueprintEndpoint(state, false),
				previousTargetEndpoint,
				GetTargetEndpoint(state),
				state.IsValid);
			outPlaySnapEffects = outPlaySnapEffects || state.IsValid;
			UE_LOG(
				LogVerticalConveyorAutoConnect,
				Verbose,
				TEXT("VerticalConveyorAutoConnect: state=%d source=%s loc=%s target=%s loc=%s valid=%d direct=%d flow=%s recipe=%s"),
				stateIndex,
				*DescribeEndpoint(GetBlueprintEndpoint(state, false)),
				*GetEndpointWorldTransform(GetBlueprintEndpoint(state, false)).GetLocation().ToString(),
				*DescribeEndpoint(GetTargetEndpoint(state)),
				*GetEndpointWorldTransform(GetTargetEndpoint(state)).GetLocation().ToString(),
				state.IsValid ? 1 : 0,
				state.CanDirectlyConnect ? 1 : 0,
				state.ResolvedLowerEndpointDirection == EFactoryConnectionDirection::FCD_INPUT
					? TEXT("up")
					: state.ResolvedLowerEndpointDirection == EFactoryConnectionDirection::FCD_OUTPUT
						? TEXT("down")
						: TEXT("any"),
				*GetNameSafe(state.LiftRecipe.Get()));
		}
	}
}

bool FBlueprintVerticalConveyorConnectionManager::AttemptConnectionStateSnap()
{
	bool snappedAny = false;
	for (FConnectionState& state : ConnectionStates)
	{
		if (state.IsValid && IsValid(state.TargetBuildable) &&
			!state.HasSnappedTarget)
		{
			state.HasSnappedTarget = true;
			snappedAny = true;
			UE_LOG(
				LogVerticalConveyorAutoConnect,
				Verbose,
				TEXT("VerticalConveyorAutoConnect: locked %s -> %s; next click confirms the blueprint"),
				*DescribeEndpoint(GetBlueprintEndpoint(state, false)),
				*DescribeEndpoint(GetTargetEndpoint(state)));


		}
	}
	return snappedAny;
}

bool FBlueprintVerticalConveyorConnectionManager::CanSnapConnectionStates() const
{
	for (const FConnectionState& state : ConnectionStates)
	{
		if (state.IsValid && IsValid(state.TargetBuildable) &&
			!state.HasSnappedTarget)
		{
			return true;
		}
	}
	return false;
}

void FBlueprintVerticalConveyorConnectionManager::ResetAutomaticConnections()
{
	for (FConnectionState& state : ConnectionStates)
	{
		const FEndpointRef blueprintEndpoint =
			GetBlueprintEndpoint(state, false);
		const FEndpointRef previousTargetEndpoint = GetTargetEndpoint(state);
		const bool shouldBroadcast =
			state.IsValid || IsValid(state.TargetBuildable);
		ClearTarget(state);
		state.ConstructedBlueprintBuildable = nullptr;
		state.HasSnappedTarget = false;
		DisableBridge(state);
		if (shouldBroadcast)
		{
			BroadcastConnectionStateChange(
				blueprintEndpoint,
				previousTargetEndpoint,
				GetTargetEndpoint(state),
				false);
		}
	}
}

void FBlueprintVerticalConveyorConnectionManager::
	HandleBuildableConnectionRemapping(
		AFGBuildable* buildable,
		int32 blueprintBuildableIndex)
{
	for (int32 stateIndex = 0; stateIndex < ConnectionStates.Num(); ++stateIndex)
	{
		FConnectionState& state = ConnectionStates[stateIndex];
		if (state.BlueprintBuildableIndex == blueprintBuildableIndex &&
			IsSupportedBuildable(buildable))
		{
			state.ConstructedBlueprintBuildable = buildable;
			UE_LOG(
				LogVerticalConveyorAutoConnect,
				Verbose,
				TEXT("VerticalConveyorAutoConnect: remapped state=%d index=%d source=%s constructed=%s"),
				stateIndex,
				blueprintBuildableIndex,
				*GetNameSafe(state.BlueprintBuildable),
				*GetNameSafe(state.ConstructedBlueprintBuildable));
		}
	}
}

void FBlueprintVerticalConveyorConnectionManager::SerializeConstructMessage(
	FArchive& archive,
	FNetConstructionID /*id*/)
{
	for (FConnectionState& state : ConnectionStates)
	{
		archive << state;
	}
}

void FBlueprintVerticalConveyorConnectionManager::
	PostConstructMessageDeserialization()
{
	for (int32 stateIndex = 0; stateIndex < ConnectionStates.Num(); ++stateIndex)
	{
		FConnectionState& state = ConnectionStates[stateIndex];
		if (!IsValid(state.TargetBuildable))
		{
			state.IsValid = false;
			DisableBridge(state);
			continue;
		}

		const FEndpointRef blueprintEndpoint = GetBlueprintEndpoint(state, false);
		const FEndpointRef targetEndpoint = GetTargetEndpoint(state);
		const TSubclassOf<UFGRecipe> recipe =
			ResolveLiftRecipe(blueprintEndpoint, targetEndpoint);
		if (!recipe)
		{
			state.IsValid = false;
			DisableBridge(state);
			continue;
		}

		if (state.CanDirectlyConnect)
		{
			state.LiftRecipe = recipe;
			state.IsValid = true;
			DisableBridge(state);
		}
		else
		{
			state.IsValid =
				EnsureBridgeRecipe(state, stateIndex, recipe) &&
				ConfigureBridgeHologram(state, false, false);
			if (!state.IsValid)
			{
				DisableBridge(state);
			}
		}
	}
}

void FBlueprintVerticalConveyorConnectionManager::ConnectDirectly(
	FConnectionState& state)
{
	const FEndpointRef blueprintEndpoint = GetBlueprintEndpoint(state, true);
	const FEndpointRef targetEndpoint = GetTargetEndpoint(state);
	UFGFactoryConnectionComponent* blueprintConnection =
		GetTransportConnectionAcrossEndpoint(blueprintEndpoint);
	UFGFactoryConnectionComponent* targetConnection =
		GetTransportConnectionAcrossEndpoint(targetEndpoint);
	if (!IsValid(blueprintConnection) || !IsValid(targetConnection))
	{
		return;
	}

	if (blueprintConnection->GetConnection() != targetConnection)
	{
		if (blueprintConnection->IsConnected() || targetConnection->IsConnected())
		{
			return;
		}
		if (blueprintConnection->CanConnectTo(targetConnection))
		{
			blueprintConnection->SetConnection(targetConnection);
		}
		else if (targetConnection->CanConnectTo(blueprintConnection))
		{
			targetConnection->SetConnection(blueprintConnection);
		}
		else
		{
			return;
		}
	}

	if (blueprintEndpoint.Kind ==
		EBlueprintVerticalEndpointKind::FloorHoleSide)
	{
		SetFloorHoleSnappedConnection(
			Cast<AFGBuildablePassthrough>(blueprintEndpoint.Buildable),
			blueprintEndpoint.Side,
			targetConnection);
	}
	if (targetEndpoint.Kind == EBlueprintVerticalEndpointKind::FloorHoleSide)
	{
		SetFloorHoleSnappedConnection(
			Cast<AFGBuildablePassthrough>(targetEndpoint.Buildable),
			targetEndpoint.Side,
			blueprintConnection);
	}
}

int32 FBlueprintVerticalConveyorConnectionManager::ConnectBridgeEndpoint(
	AFGBuildableConveyorLift* lift,
	const TCHAR* endpointName,
	UFGFactoryConnectionComponent* bridgeConnection,
	UFGFactoryConnectionComponent* outsideConnection,
	const FEndpointRef& endpoint) const
{
	if (!IsValid(bridgeConnection))
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: %s endpoint %s has no bridge connection"),
			*GetNameSafe(lift),
			endpointName);
		return 0;
	}

	if (!IsValid(outsideConnection))
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Verbose,
			TEXT("VerticalConveyorAutoConnect: %s endpoint %s terminates at bare %s"),
			*GetNameSafe(lift),
			endpointName,
			*DescribeEndpoint(endpoint));
		return -1;
	}

	if (bridgeConnection->GetConnection() == outsideConnection &&
		outsideConnection->GetConnection() == bridgeConnection)
	{
		return 1;
	}

	if ((bridgeConnection->IsConnected() &&
			bridgeConnection->GetConnection() != outsideConnection) ||
		(outsideConnection->IsConnected() &&
			outsideConnection->GetConnection() != bridgeConnection))
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: cannot link %s endpoint %s; bridge alreadyTo=%s outside=%s alreadyTo=%s"),
			*GetNameSafe(lift),
			endpointName,
			*GetNameSafe(bridgeConnection->GetConnection()),
			*GetNameSafe(outsideConnection),
			*GetNameSafe(outsideConnection->GetConnection()));
		return 0;
	}

	if (outsideConnection->GetDirection() ==
		EFactoryConnectionDirection::FCD_ANY)
	{
		const EFactoryConnectionDirection required =
			OppositeDirection(bridgeConnection->GetDirection());
		if (required == EFactoryConnectionDirection::FCD_ANY)
		{
			return 0;
		}
		outsideConnection->SetDirection(required);
	}

	if (bridgeConnection->CanConnectTo(outsideConnection))
	{
		bridgeConnection->SetConnection(outsideConnection);
	}
	else if (outsideConnection->CanConnectTo(bridgeConnection))
	{
		outsideConnection->SetConnection(bridgeConnection);
	}
	else
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: cannot link %s endpoint %s bridge=%s(%d) outside=%s(%d)"),
			*GetNameSafe(lift),
			endpointName,
			*GetNameSafe(bridgeConnection),
			static_cast<int32>(bridgeConnection->GetDirection()),
			*GetNameSafe(outsideConnection),
			static_cast<int32>(outsideConnection->GetDirection()));
		return 0;
	}

	return bridgeConnection->GetConnection() == outsideConnection &&
		outsideConnection->GetConnection() == bridgeConnection
		? 1
		: 0;
}

void FBlueprintVerticalConveyorConnectionManager::FinalizeConstructedBridge(
	FConnectionState& state,
	AFGBuildableConveyorLift* lift) const
{
	const FEndpointRef blueprintEndpoint = GetBlueprintEndpoint(state, true);
	const FEndpointRef targetEndpoint = GetTargetEndpoint(state);
	if (!IsValid(lift))
	{
		return;
	}

	const EFactoryConnectionDirection lowerDirection =
		state.ResolvedLowerEndpointDirection;
	if (lowerDirection != EFactoryConnectionDirection::FCD_INPUT &&
		lowerDirection != EFactoryConnectionDirection::FCD_OUTPUT)
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: refusing to finalize %s; snapped state has no resolved transport direction"),
			*GetNameSafe(lift));
		return;
	}

	FEndpointRef lowerEndpoint;
	FEndpointRef upperEndpoint;
	if (GetEndpointWorldTransform(blueprintEndpoint).GetLocation().Z <=
		GetEndpointWorldTransform(targetEndpoint).GetLocation().Z)
	{
		lowerEndpoint = blueprintEndpoint;
		upperEndpoint = targetEndpoint;
	}
	else
	{
		lowerEndpoint = targetEndpoint;
		upperEndpoint = blueprintEndpoint;
	}

	const bool expectedFlowsUpwards =
		lowerDirection == EFactoryConnectionDirection::FCD_INPUT;
	const bool actorFlowsUpwards =
		lift->GetConveyorLiftFlowDirection() ==
		EFGBuildableConveyorLiftDirection::LD_Upwards;
	UFGFactoryConnectionComponent* connection0 = lift->GetConnection0();
	UFGFactoryConnectionComponent* connection1 = lift->GetConnection1();
	if (!IsValid(connection0) || !IsValid(connection1))
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: refusing to finalize %s; constructed lift is missing connection components"),
			*GetNameSafe(lift));
		return;
	}

	// Attachment-first vanilla placement can reverse the hologram's physical slot
	// order: manual state has own0=OUTPUT, own1=INPUT when slot 0 is the transport
	// output. Therefore GetConnection0()/GetConnection1() are physical slots, not a
	// universally valid input/output ordering. Resolve transport identity from the
	// constructed component directions instead of assuming indices.
	UFGFactoryConnectionComponent* inputConnection = nullptr;
	UFGFactoryConnectionComponent* outputConnection = nullptr;
	if (connection0->GetDirection() == EFactoryConnectionDirection::FCD_INPUT &&
		connection1->GetDirection() == EFactoryConnectionDirection::FCD_OUTPUT)
	{
		inputConnection = connection0;
		outputConnection = connection1;
	}
	else if (connection0->GetDirection() == EFactoryConnectionDirection::FCD_OUTPUT &&
		connection1->GetDirection() == EFactoryConnectionDirection::FCD_INPUT)
	{
		inputConnection = connection1;
		outputConnection = connection0;
	}
	else
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: refusing to finalize %s; connection directions are c0=%d c1=%d"),
			*GetNameSafe(lift),
			static_cast<int32>(connection0->GetDirection()),
			static_cast<int32>(connection1->GetDirection()));
		return;
	}

	if (actorFlowsUpwards != expectedFlowsUpwards)
	{
		UE_LOG(
			LogVerticalConveyorAutoConnect,
			Warning,
			TEXT("VerticalConveyorAutoConnect: refusing to finalize %s expectedFlow=%s actorFlow=%s c0=%d c1=%d"),
			*GetNameSafe(lift),
			expectedFlowsUpwards ? TEXT("up") : TEXT("down"),
			actorFlowsUpwards ? TEXT("up") : TEXT("down"),
			static_cast<int32>(connection0->GetDirection()),
			static_cast<int32>(connection1->GetDirection()));
		return;
	}

	const FEndpointRef& inputEndpoint =
		expectedFlowsUpwards ? lowerEndpoint : upperEndpoint;
	const FEndpointRef& outputEndpoint =
		expectedFlowsUpwards ? upperEndpoint : lowerEndpoint;

	// Capture the transport connection ACROSS each endpoint before changing any
	// floor-hole snapped bookkeeping. SetTop/BottomSnappedConnection mutates the
	// passthrough state; resolving the opposite side afterwards can therefore turn
	// a real continuing lift into an apparent bare endpoint (the D1/T3 symptom).
	UFGFactoryConnectionComponent* inputOutsideConnection =
		GetTransportConnectionAcrossEndpoint(inputEndpoint);
	UFGFactoryConnectionComponent* outputOutsideConnection =
		GetTransportConnectionAcrossEndpoint(outputEndpoint);

	UE_LOG(
		LogVerticalConveyorAutoConnect,
		Verbose,
		TEXT("VerticalConveyorAutoConnect: final-endpoints %s inputOutside=%s outputOutside=%s"),
		*GetNameSafe(lift),
		*GetNameSafe(inputOutsideConnection),
		*GetNameSafe(outputOutsideConnection));

	if (inputEndpoint.Kind == EBlueprintVerticalEndpointKind::FloorHoleSide)
	{
		SetFloorHoleSnappedConnection(
			Cast<AFGBuildablePassthrough>(inputEndpoint.Buildable),
			inputEndpoint.Side,
			inputConnection);
	}
	if (outputEndpoint.Kind == EBlueprintVerticalEndpointKind::FloorHoleSide)
	{
		SetFloorHoleSnappedConnection(
			Cast<AFGBuildablePassthrough>(outputEndpoint.Buildable),
			outputEndpoint.Side,
			outputConnection);
	}

	const int32 inputLink = ConnectBridgeEndpoint(
		lift,
		TEXT("input"),
		inputConnection,
		inputOutsideConnection,
		inputEndpoint);
	const int32 outputLink = ConnectBridgeEndpoint(
		lift,
		TEXT("output"),
		outputConnection,
		outputOutsideConnection,
		outputEndpoint);
	lift->OnRep_SnappedPassthroughs();

	UE_LOG(
		LogVerticalConveyorAutoConnect,
		Verbose,
		TEXT("VerticalConveyorAutoConnect: finalized %s flow=%s input=%s output=%s inputLink=%d outputLink=%d"),
		*GetNameSafe(lift),
		expectedFlowsUpwards ? TEXT("up") : TEXT("down"),
		*DescribeEndpoint(inputEndpoint),
		*DescribeEndpoint(outputEndpoint),
		inputLink,
		outputLink);
}

void FBlueprintVerticalConveyorConnectionManager::Construct(
	TArray<AFGBuildable*>& outConstructedBridgeBuildables,
	FNetConstructionID netConstructionID)
{
	UE_LOG(
		LogVerticalConveyorAutoConnect,
		Verbose,
		TEXT("VerticalConveyorAutoConnect: construct begin states=%d"),
		ConnectionStates.Num());

	for (int32 stateIndex = 0; stateIndex < ConnectionStates.Num(); ++stateIndex)
	{
		FConnectionState& state = ConnectionStates[stateIndex];
		if (!state.IsValid ||
			!IsValid(state.TargetBuildable) ||
			!IsValid(state.ConstructedBlueprintBuildable))
		{
			continue;
		}

		if (state.CanDirectlyConnect)
		{
			ConnectDirectly(state);
			continue;
		}


		if (!ConfigureBridgeHologram(state, true, true) ||
			!IsValid(state.BridgeHologram) ||
			state.BridgeHologram->IsDisabled())
		{
			UE_LOG(
				LogVerticalConveyorAutoConnect,
				Warning,
				TEXT("VerticalConveyorAutoConnect: final bridge preparation failed for %s -> %s"),
				*DescribeEndpoint(GetBlueprintEndpoint(state, true)),
				*DescribeEndpoint(GetTargetEndpoint(state)));
			continue;
		}


		TArray<AActor*> constructedChildren;
		AFGBuildableConveyorLift* lift = Cast<AFGBuildableConveyorLift>(
			state.BridgeHologram->Construct(
				constructedChildren,
				netConstructionID));
		if (!IsValid(lift))
		{
			UE_LOG(
				LogVerticalConveyorAutoConnect,
				Warning,
				TEXT("VerticalConveyorAutoConnect: bridge construction failed for %s -> %s"),
				*DescribeEndpoint(GetBlueprintEndpoint(state, true)),
				*DescribeEndpoint(GetTargetEndpoint(state)));
			continue;
		}

		FinalizeConstructedBridge(state, lift);
		outConstructedBridgeBuildables.Add(lift);
		for (AActor* child : constructedChildren)
		{
			if (AFGBuildable* childBuildable = Cast<AFGBuildable>(child))
			{
				outConstructedBridgeBuildables.Add(childBuildable);
			}
		}
	}
}
