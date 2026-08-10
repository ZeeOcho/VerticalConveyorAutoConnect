#pragma once

#include "CoreMinimal.h"
#include "Hologram/FGBlueprintOpenConnectionManager.h"

class AFGBuildable;
class AFGBuildableConveyorLift;
class AFGBuildablePassthrough;
class AFGConveyorLiftHologram;
class UFGFactoryConnectionComponent;
class UFGRecipe;

enum class EBlueprintVerticalEndpointKind : uint8
{
	FloorHoleSide,
	AttachmentPort
};

enum class EBlueprintVerticalEndpointSide : uint8
{
	Bottom,
	Top
};

/**
 * Adds automatic conveyor-lift bridges between compatible vertical conveyor
 * endpoints exposed by a blueprint and nearby world buildables.
 *
 * Supported endpoint kinds:
 * - Conveyor Floor Hole top/bottom sides.
 * - Canonical vertical lift ports on capability-compatible conveyor attachments.
 */
class FBlueprintVerticalConveyorConnectionManager final
	: public FGBlueprintOpenConnectionManagerBase
{
public:
	explicit FBlueprintVerticalConveyorConnectionManager(
		AFGBlueprintHologram* blueprintHologram);
	virtual ~FBlueprintVerticalConveyorConnectionManager() override = default;

	virtual void RegisterNearbyActor(AActor* actor) override;
	virtual void UnregisterNearbyActor(AActor* actor) override;
	virtual void UpdateAutomaticConnections(
		const FHitResult& hitResult,
		bool& outPlaySnapEffects) override;
	virtual void HandleBuildableConnectionRemapping(
		AFGBuildable* buildable,
		int32 blueprintBuildableIndex) override;
	virtual void Initialize(TArray<AFGBuildable*> buildables) override;
	virtual bool AttemptConnectionStateSnap() override;
	virtual bool CanSnapConnectionStates() const override;
	virtual void Construct(
		TArray<AFGBuildable*>& outConstructedBridgeBuildables,
		FNetConstructionID netConstructionID) override;
	virtual void SerializeConstructMessage(
		FArchive& archive,
		FNetConstructionID id) override;
	virtual void PostConstructMessageDeserialization() override;
	virtual void ResetAutomaticConnections() override;

private:
	struct FEndpointRef
	{
		AFGBuildable* Buildable = nullptr;
		EBlueprintVerticalEndpointKind Kind =
			EBlueprintVerticalEndpointKind::FloorHoleSide;
		EBlueprintVerticalEndpointSide Side =
			EBlueprintVerticalEndpointSide::Top;
		FName ConnectionName = NAME_None;
		bool UsesBlueprintPreviewTransform = false;
	};

	struct FConnectionState
	{
		AFGBuildable* BlueprintBuildable = nullptr;
		AFGBuildable* ConstructedBlueprintBuildable = nullptr;
		TObjectPtr<AFGBuildable> TargetBuildable = nullptr;
		TObjectPtr<AFGConveyorLiftHologram> BridgeHologram = nullptr;
		TSubclassOf<UFGRecipe> LiftRecipe;
		FName BlueprintConnectionName = NAME_None;
		FName TargetConnectionName = NAME_None;
		int32 BlueprintBuildableIndex = INDEX_NONE;
		EBlueprintVerticalEndpointKind BlueprintKind =
			EBlueprintVerticalEndpointKind::FloorHoleSide;
		EBlueprintVerticalEndpointKind TargetKind =
			EBlueprintVerticalEndpointKind::FloorHoleSide;
		EBlueprintVerticalEndpointSide BlueprintSide =
			EBlueprintVerticalEndpointSide::Top;
		EBlueprintVerticalEndpointSide TargetSide =
			EBlueprintVerticalEndpointSide::Bottom;
		// Transport intent belongs to the snapped preview state. Constructed
		// blueprint actors may still expose FCD_ANY before BeginPlay, so final
		// construction must not rediscover an already-locked direction.
		EFactoryConnectionDirection ResolvedLowerEndpointDirection =
			EFactoryConnectionDirection::FCD_ANY;
		bool HasSnappedTarget = false;
		bool CanDirectlyConnect = false;
		bool IsValid = false;

		friend FArchive& operator<<(FArchive& archive, FConnectionState& state)
		{
			// BlueprintSide is normally authored by the source endpoint, but for an
			// attachment port it is geometry-derived and therefore part of the snapped
			// state that must survive client/server construct-message transfer.
			uint8 blueprintSide = static_cast<uint8>(state.BlueprintSide);
			archive << blueprintSide;
			if (archive.IsLoading())
			{
				state.BlueprintSide =
					static_cast<EBlueprintVerticalEndpointSide>(blueprintSide);
			}

			archive << state.TargetBuildable;
			archive << state.BridgeHologram;
			archive << state.LiftRecipe;
			archive << state.TargetConnectionName;

			uint8 targetKind = static_cast<uint8>(state.TargetKind);
			uint8 targetSide = static_cast<uint8>(state.TargetSide);
			archive << targetKind;
			archive << targetSide;
			if (archive.IsLoading())
			{
				state.TargetKind =
					static_cast<EBlueprintVerticalEndpointKind>(targetKind);
				state.TargetSide =
					static_cast<EBlueprintVerticalEndpointSide>(targetSide);
			}

			uint8 resolvedLowerDirection =
				static_cast<uint8>(state.ResolvedLowerEndpointDirection);
			archive << resolvedLowerDirection;
			if (archive.IsLoading())
			{
				state.ResolvedLowerEndpointDirection =
					static_cast<EFactoryConnectionDirection>(resolvedLowerDirection);
			}

			archive << state.HasSnappedTarget;
			archive << state.CanDirectlyConnect;
			return archive;
		}
	};

	struct FEndpointKey
	{
		AFGBuildable* Buildable = nullptr;
		EBlueprintVerticalEndpointKind Kind =
			EBlueprintVerticalEndpointKind::FloorHoleSide;
		EBlueprintVerticalEndpointSide Side =
			EBlueprintVerticalEndpointSide::Top;
		FName ConnectionName = NAME_None;

		bool operator==(const FEndpointKey& other) const
		{
			return Buildable == other.Buildable &&
				Kind == other.Kind &&
				Side == other.Side &&
				ConnectionName == other.ConnectionName;
		}
	};

	friend uint32 GetTypeHash(const FEndpointKey& key)
	{
		uint32 hash = HashCombine(
			GetTypeHash(key.Buildable),
			GetTypeHash(static_cast<uint8>(key.Kind)));
		hash = HashCombine(hash, GetTypeHash(static_cast<uint8>(key.Side)));
		return HashCombine(hash, GetTypeHash(key.ConnectionName));
	}

	struct FAttachmentLiftPorts
	{
		UFGFactoryConnectionComponent* Bottom = nullptr;
		UFGFactoryConnectionComponent* Top = nullptr;
	};

	TArray<FConnectionState> ConnectionStates;
	// All managed physical vertical endpoints in the blueprint, including
	// occupied ones. These are used only as source-side tunnel blockers.
	TArray<FEndpointRef> BlueprintPhysicalEndpoints;
	TSet<int32> LoggedSourceTunnelStates;
	TArray<TWeakObjectPtr<AFGBuildable>> NearbyBuildables;

	static bool IsConveyorFloorHole(const AFGBuildablePassthrough* hole);
	static bool TryGetAttachmentLiftPorts(
		AFGBuildable* buildable,
		FAttachmentLiftPorts& outPorts);
	static bool IsSupportedAttachment(const AFGBuildable* buildable);
	static bool IsSupportedBuildable(const AFGBuildable* buildable);
	static bool IsSupportedBridgeTopology(
		const FEndpointRef& first,
		const FEndpointRef& second);
	static EBlueprintVerticalEndpointSide OppositeSide(
		EBlueprintVerticalEndpointSide side);
	static EFactoryConnectionDirection OppositeDirection(
		EFactoryConnectionDirection direction);
	static UFGFactoryConnectionComponent* GetFloorHoleSnappedConnection(
		const AFGBuildablePassthrough* hole,
		EBlueprintVerticalEndpointSide side);
	static void SetFloorHoleSnappedConnection(
		AFGBuildablePassthrough* hole,
		EBlueprintVerticalEndpointSide side,
		UFGFactoryConnectionComponent* connection);
	static UFGFactoryConnectionComponent* FindFactoryConnection(
		AFGBuildable* buildable,
		FName connectionName);

	void GatherEndpoints(
		AFGBuildable* buildable,
		bool useBlueprintPreviewTransform,
		bool requireOpen,
		TArray<FEndpointRef>& outEndpoints) const;
	bool IsEndpointOpen(const FEndpointRef& endpoint) const;
	bool HasInterveningBlueprintEndpoint(
		const FEndpointRef& sourceEndpoint,
		const FEndpointRef& targetEndpoint,
		FEndpointRef* outBlockingEndpoint = nullptr) const;
	FEndpointRef GetBlueprintEndpoint(
		const FConnectionState& state,
		bool useConstructedBlueprintBuildable) const;
	FEndpointRef GetTargetEndpoint(const FConnectionState& state) const;
	FEndpointKey MakeEndpointKey(const FEndpointRef& endpoint) const;
	FString DescribeEndpoint(const FEndpointRef& endpoint) const;

	bool TryGetBlueprintBuildableWorldTransform(
		const AFGBuildable* buildable,
		FTransform& outTransform) const;
	FTransform GetBuildableWorldTransform(const FEndpointRef& endpoint) const;
	FTransform GetEndpointWorldTransform(const FEndpointRef& endpoint) const;
	FVector GetEndpointOutwardNormalWorld(const FEndpointRef& endpoint) const;
	UFGFactoryConnectionComponent* GetDirectEndpointConnection(
		const FEndpointRef& endpoint) const;
	UFGFactoryConnectionComponent* GetTransportConnectionAcrossEndpoint(
		const FEndpointRef& endpoint) const;
	AFGBuildableConveyorLift* GetAdjacentLift(
		const FEndpointRef& endpoint) const;
	TSubclassOf<UFGRecipe> GetEndpointLiftRecipe(
		const FEndpointRef& endpoint) const;
	EFactoryConnectionDirection GetRequiredBridgeDirection(
		const FEndpointRef& endpoint) const;

	bool RestorePersistedAttachmentDirectionBeforeConstruct(
		const FEndpointRef& attachmentEndpoint,
		EFactoryConnectionDirection expectedDirection) const;

	bool ResolveVanillaPlacementEndpoints(
		const FEndpointRef& transportInput,
		const FEndpointRef& transportOutput,
		FEndpointRef& outPlacementStart,
		FEndpointRef& outPlacementEnd) const;
	bool ResolveLowerEndpointDirection(
		const FEndpointRef& first,
		const FEndpointRef& second,
		EFactoryConnectionDirection& outLowerEndpointDirection,
		FEndpointRef& outLower,
		FEndpointRef& outUpper) const;
	FVector GetAdjacentLiftVisualDirectionWorld(
		const FEndpointRef& endpoint) const;
	FVector ResolveBridgeVisualDirection(
		const FEndpointRef& blueprintEndpoint,
		const FEndpointRef& targetEndpoint) const;
	TSubclassOf<UFGRecipe> ResolveLiftRecipe(
		const FEndpointRef& blueprintEndpoint,
		const FEndpointRef& targetEndpoint) const;

	void SpawnBridgeHologram(FConnectionState& state, int32 stateIndex);
	bool EnsureBridgeRecipe(
		FConnectionState& state,
		int32 stateIndex,
		TSubclassOf<UFGRecipe> recipe);
	bool ConfigureBridgeHologram(
		FConnectionState& state,
		bool useConstructedBlueprintBuildable,
		bool finalValidation);
	void DisableBridge(FConnectionState& state);
	void ClearTarget(FConnectionState& state);
	void FindBestTarget(
		FConnectionState& state,
		int32 stateIndex,
		const TSet<FEndpointKey>& claimedTargetEndpoints);
	bool IsAttachmentPortForSpan(
		const FEndpointRef& attachmentEndpoint,
		const FEndpointRef& otherEndpoint) const;
	void NormalizeAttachmentFacingSides(
		FEndpointRef& first,
		FEndpointRef& second) const;
	bool IsGeometricallyCompatible(
		const FEndpointRef& blueprintEndpoint,
		const FEndpointRef& targetEndpoint,
		float& outVerticalDistance,
		bool& outCanDirectlyConnect) const;

	void ConnectDirectly(FConnectionState& state);
	void FinalizeConstructedBridge(
		FConnectionState& state,
		AFGBuildableConveyorLift* lift) const;
	int32 ConnectBridgeEndpoint(
		AFGBuildableConveyorLift* lift,
		const TCHAR* endpointName,
		UFGFactoryConnectionComponent* bridgeConnection,
		UFGFactoryConnectionComponent* outsideConnection,
		const FEndpointRef& endpoint) const;
};
