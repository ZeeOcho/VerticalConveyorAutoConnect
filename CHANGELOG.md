# Changelog

All notable changes to this project will be documented here.

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
