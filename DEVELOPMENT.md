# Development notes

## Pre-amble (human-written):

This document was generated using ChatGPT. The basic ideas / principles of this mod are:
- Extend the auto-connect feature to vertical conveyor lifts until it gets implemented by CSS itself.
- Be consistent with vanilla behavior:
  - we can connect: floor-hole ↔ floor-hole, and floor-hole ↔ [splitter / merger]
  - vanilla does not allow building a lift beween to open vertical splitter / merger ports
  - we may soft-clip the auto-connection
- Inherit lift Mk. and orientation from already attached lift(s), where blueprint-side wins.
- At least one side must already have an attached belt.

Everything below this line is AI generated.
vvv

This document records the current architecture and invariants. It is not a
development diary.

## Core principle: reproduce vanilla semantics

The mod should create the state vanilla Conveyor Lift placement would create,
rather than inventing an independent conveyor-connection system.

A generated `AFGConveyorLiftHologram` is authoritative for placement validity.
After configuration, the normal vanilla update/validation path runs and the
bridge is eligible only when `CanConstruct()` is true.

Do not add arbitrary mod-defined distance, height, clearance, or angular
thresholds. Numerical tolerances are limited to engine equality semantics or
values supplied by vanilla/API data.

The one intentional validation-context correction is suppression of transient
`UFGCDInvalidFloor` on the tagged synthetic blueprint child. This is specific to
the synthetic child context; soft-clearance disqualifiers are not suppressed.

## Endpoint model

Supported endpoint kinds:

- Conveyor Floor Hole side;
- vanilla-compatible conveyor-attachment lift port.

A conveyor attachment is compatible when:

1. it derives from `AFGBuildableConveyorAttachment`;
2. it exposes both canonical
   `AFGConveyorAttachmentHologram::mLiftConnection_Bottom` and
   `mLiftConnection_Top` components;
3. those components are distinct vertical `UFGFactoryConnectionComponent`s.

`TryGetAttachmentLiftPorts()` is the single capability probe. Endpoint discovery
uses the exact validated components returned by that probe.

Supported topology:

- Floor Hole ↔ Floor Hole;
- one compatible attachment ↔ one Floor Hole.

Attachment ↔ attachment is intentionally not synthesized because vanilla manual
Conveyor Lift placement cannot complete that topology.

## Four independent concepts

Keep these separate:

1. **Ownership** — the endpoint originated in the blueprint or already existed in
   the world.
2. **Representation** — use BlueprintWorld preview transforms or the remapped
   constructed world actor.
3. **Transport direction** — input vs output.
4. **Vanilla placement order** — first vs second hologram placement endpoint.

`FEndpointRef::UsesBlueprintPreviewTransform` describes representation only.

For attachment ↔ Floor Hole, vanilla placement starts at the attachment
regardless of ownership or transport role.

## Attachment placement state

Measured vanilla behavior:

- placement slot 0 = attachment vertical connection;
- placement slot 1 = Floor Hole;
- `mSnappedPassthroughs = [nullptr, floorHole]`;
- `mSnappedConnectionComponents[0] = attachmentPort`;
- slot 1 contains the connection across the Floor Hole when present;
- attachment as transport input:
  attachment port `FCD_OUTPUT`, `mArrowDirection=FCD_OUTPUT`,
  `mIsReversed=false`;
- attachment as transport output:
  attachment port `FCD_INPUT`, `mArrowDirection=FCD_INPUT`,
  `mIsReversed=true`;
- Floor Hole above attachment -> `TopConnection`;
- Floor Hole below attachment -> `BottomConnection`.

Port identity and transport direction are independent.

## Locked transport intent

Direction is resolved while the preview state is valid and stored as normalized
physical-lower-endpoint intent.

Final construction must not rediscover that already-locked intent from newly
spawned blueprint actors: they can still be pre-`BeginPlay`.

## Pre-BeginPlay attachment lifecycle

Blueprint bridge construction can happen before newly spawned blueprint
attachments receive `BeginPlay()`.

`AFGBuildableConveyorAttachment` persists dynamic connection roles in
`mSavedDirections`. Before `BeginPlay`, a selected port can expose `FCD_ANY` or
a concrete default/stale direction.

For a blueprint-owned mixed bridge, restore the selected port from
`mSavedDirections` before constructing the bridge child. Resolve the persisted
index with `UFGFactoryConnectionComponent::SortComponentList`.

The persisted direction must be concrete and exactly match the transport role
already locked during preview. Otherwise fail closed. Existing world attachments
are not modified.

## Floor-hole passthrough state

A geometrically aligned lift mouth is not necessarily snapped through a Conveyor
Floor Hole.

When finalizing a bridge, capture any continuation across the Floor Hole before
mutating snapped bookkeeping. `SetTop/BottomSnappedConnection` changes
passthrough state, so resolving the continuation afterwards can lose the
pre-existing transport relationship.

Do not manufacture a continuation for a stale fixture where vanilla does not
report one.

## Tier policy

Recipe precedence is:

1. blueprint-side adjacent Conveyor Lift recipe;
2. target-side adjacent Conveyor Lift recipe;
3. no recipe -> reject.

Tier choice is independent of transport direction and attachment placement order.

## No endpoint tunneling

A physical endpoint terminates a vertical path even when it is not usable for an
automatic bridge.

Target side:
- gather physical candidates including occupied endpoints;
- identify the nearest physical endpoint plane;
- validate only candidates on that plane;
- never examine farther targets if the nearest plane cannot produce a bridge.

Blueprint/source side:
- a farther source cannot form a bridge through another physical blueprint
  endpoint in the same XY column;
- the source buildable itself is explicitly excluded from blocker detection.

This rule is independent of endpoint openness. An unsupported or occupied
intervening endpoint still blocks tunneling.

## Bridge hologram lifetime

Child holograms are cached per connection state and lift recipe for the lifetime
of the parent blueprint hologram.

Do not destroy and respawn a child with the same stable name while the parent is
alive: Unreal's deferred destruction can leave the old child registered and
trigger duplicate-child assertions.

## Final connection linking

Blueprint actors may not have run `BeginPlay()` when bridge construction occurs.
Finalization therefore performs the required reciprocal factory connection
linking explicitly.

For generated lifts, do not assume `GetConnection0()` is always transport input
and `GetConnection1()` output. Attachment-first placement can reverse physical
slot order. Resolve input/output from the constructed component directions and
verify that `GetConveyorLiftFlowDirection()` agrees with the locked transport
intent before linking.

## Networking

`FConnectionState::operator<<` serializes the snapped state required for
construction:

- geometry-derived blueprint side;
- target buildable/kind/side/connection identity;
- lift recipe;
- normalized lower-endpoint direction;
- snap/direct-connect state.

`PostConstructMessageDeserialization()` reconstructs the preview child from that
state rather than searching for a new target.

Before claiming multiplayer support, regression-test:

- host/client preview agreement;
- client-originated blueprint placement;
- save/reload;
- Windows dedicated server;
- Linux dedicated server if that release target is enabled.

## Building from source

Use the Satisfactory Modding Starter Project matching the game/SML version.
Clone this repository into the Starter Project's `Mods/` directory so the folder
containing `VerticalConveyorAutoConnect.uplugin` is named exactly:

`VerticalConveyorAutoConnect`

Regenerate project files/build as needed, then use Alpakit for development or
release packaging. The repository intentionally contains only mod/plugin source
and test fixtures, not generated binaries.
