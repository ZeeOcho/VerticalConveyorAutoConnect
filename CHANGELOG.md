# Changelog

All notable changes to this project will be documented here.

## [Unreleased]

### Fixed

- Generated Conveyor Lift finalization now preflights both endpoints before the
  lift is constructed and revalidates both sides before either factory
  connection is committed.
- A rejected or incomplete finalization can no longer leave a returned bridge
  with only one reciprocal factory connection or stale Floor Hole bookkeeping.
- Blueprint-owned Floor Hole continuations are explicitly remapped by buildable
  index and physical connection slot and checked against the constructed Floor
  Hole's passthrough reference before linking.

### Diagnostics

- Added a read-only next-tick audit of reciprocal links, Floor Hole
  backreferences, conveyor bucket membership, and saved/tick-group chain actor
  ownership for every generated bridge.

## [1.0.2] - 2026-08-15

### Fixed

- Attachment ↔ attachment capability checks now evaluate blueprint-owned
  vertical attachment ports at their resolved preview-world transforms when
  vanilla has not created a duplicated preview connection. Compatible placement
  extensions therefore receive the same endpoint geometry used for final
  construction.

## [1.0.1] - 2026-08-15

### Changed

- Attachment ↔ attachment candidates now use the real Conveyor Lift hologram's
  runtime connection capability instead of being rejected categorically.
- Compatible placement extensions such as `VerticalLogisticsQoL` can therefore
  enable attachment ↔ attachment blueprint bridges without a hard dependency;
  vanilla-only behavior remains fail-closed.
- Pre-`BeginPlay` direction restoration now handles a blueprint-owned attachment
  in either lift placement slot.
- Initial target discovery now searches complete vertical endpoint columns,
  eliminating blueprint-bound-dependent reach while leaving length validity to
  the real vanilla Conveyor Lift hologram.
- Vertical connection states now drive the vanilla blueprint automatic-link
  representation and reset it when a candidate is lost.

## [1.0.0] - 2026-08-09

Initial public release.

### Added

- Automatic Conveyor Lift bridges across blueprint boundaries.
- Floor Hole ↔ Floor Hole support.
- Floor Hole ↔ vanilla-compatible conveyor-attachment support.
- Capability-based support for normal, Smart/Programmable Splitters, normal
  Mergers, Priority Mergers, and compatible future/modded variants.
- Blueprint-side-first lift-tier selection.
- Upward and downward flow support.
- Symmetric no-endpoint-tunneling behavior.
- Vanilla Conveyor Lift constructibility/soft-clearance parity.
- Pre-`BeginPlay` restoration of blueprint-owned attachment connection roles.
- Construct-message serialization for deterministic bridge intent.
- Detailed regression fixture and diagnostic logging.

### Known limitations

- Multiplayer and dedicated servers have not been tested and are not advertised
  as supported in this release.
