# Contributing

Bug reports, ideas and focused pull requests are welcome.

Before changing connection semantics, read `DEVELOPMENT.md` and `TESTING.md`.
The key invariants are intentional: vanilla constructibility is authoritative,
attachment placement follows vanilla ordering, and the search must never tunnel
through a nearer physical endpoint.

For behavior changes:

1. describe the failing/desired endpoint topology;
2. add or update the relevant regression case in `TESTING.md`;
3. test both transport directions where applicable;
4. include `FactoryGame.log` when debugging construction/lifecycle behavior.

Do not commit generated build output (`Binaries/`, `Intermediate/`, `Saved/`).

By contributing to this repository, you agree that your contribution is licensed
under the repository's GNU General Public License v3.0.
