# TULL Dibella says

Private CP008 development source snapshot for Skyrim SE. The currently installed partial QA release is 0.1.11.8. This snapshot is not a game release.

The native plugin targets SKSE 2.0.20 and CommonLibSSE-NG, with an ESPFE loaded through BEES and an optional PrismaUI frontend. Building requires MSVC, the project's pinned CommonLibSSE-NG headers and libraries, and their linked dependencies. The Python record generators require a local Skyrim master or the matching prior development ESP as their input, depending on the generator. Binary assets and external dependencies are not distributed here.

Speech tier records and Native selection are implemented. Direct Destruction damage has a tested pure calculation, with the engine hook and POT coexistence pending. See the CP008 notes for the remaining game QA gates.
