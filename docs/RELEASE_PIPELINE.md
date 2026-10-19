# ParaDyn EQ release pipeline

Build installers runs in edeori/parametric-dynamic-eq-VST through the pinned
shared edeori/vst-release-pipeline workflow. It builds the Match EQ feature,
DSP/Match integration tests and stable machine fingerprint tests.

Both adapters explicitly set PARADYNEQ_ENABLE_LICENSING=ON. Before packaging,
ci/verify-licensed.py verifies the CMake cache and licensing UI, identifier
failure handling and product configuration markers in the actual product
binaries. This detects accidentally packaging a development build; it is not
an end-to-end activation-server test. Licensing product ID: `paradyn-eq`.
Trial: 15 days. Machine identity: macOS IOPlatformUUID / Windows MachineGuid,
SHA-256, independent of computer name and OS version. Existing stable hashes
are preserved; no license or trial state is reset.

Mac: universal arm64/x86_64 VST3, AU and standalone, Developer ID signing and
Apple notarization. Windows: x64 VST3 and standalone, Inno Setup, required
Defender scan. Windows installers are not Authenticode signed.

Publish installers is a separate manually started workflow. Choose Latest
successful build (default), or Specific build ID for a particular run.
`verify_only=true` validates provenance, both installers and the matching
clean Defender report without uploading anything. `verify_only=false`
uploads both installers; starting it is the publication decision.

Stable destinations:
- https://download.mothproduction.hu/downloads/plugins/paradyn-eq/mac/ParaDynEQ.pkg
- https://download.mothproduction.hu/downloads/plugins/paradyn-eq/win/ParaDynEQ.exe

The Shop entry starts hidden and unavailable. Publication does not automatically
change visibility, availability or price. Build alone never uploads to MinIO.

## Verified 2026-10-19

- Build [39302735284](https://github.com/edeori/parametric-dynamic-eq-VST/actions/runs/39302735284), source `212cf282f171bcee0472d7dfe4d50558804f1d0b`: macOS and Windows successful. All four tests passed on both runners. Licensing markers verified in every product binary.
- Apple notarization accepted; stapling verified. Downloaded Mac PKG was additionally expanded and its AU, VST3 and standalone licensing markers checked.
- Windows Defender clean, exit code 0. Installer SHA-256: `a6b57f919146adf8dbc8c96504da809de4b44920bca7ad46b78a1d53ef9e9b49`.
- Publish verification [39302757282](https://github.com/edeori/parametric-dynamic-eq-VST/actions/runs/39302757282): selected build 39302735284; provenance and file/hash checks passed; upload step skipped. No live installers published.
- Local universal build and all four tests also passed. A real license-key activation against the server was not performed.

## Automatic versions

A push to main or a new manual branch build increments the patch version in
CMakeLists.txt and commits it back before building. Both platforms use that
reserved commit. Pull the bot commit before your next local push. A rerun of
an existing run keeps its version; failed builds consume a version. Explicit
vX.Y.Z tags must match CMake and preserve the tagged version. Publish never
changes versions and remains a separate manual action.

The release-metadata artifact records both the triggering commit and the actual
versioned build commit. Publish validates and reports the actual build commit.

Automatic-version integration verified on 2026-10-19 using ParaDyn EQ:
[build 39305139718](https://github.com/edeori/parametric-dynamic-eq-VST/actions/runs/39305139718)
reserved 0.1.2 in commit `35235e3ade46bc4d1050a6296d3bcc385c1eda0a`.
Both platforms passed, including licensing checks, Apple notarization and Defender.
Downloaded Windows ProductVersion, Mac package version and all three Mac bundle
versions were checked as 0.1.2.
[Publish verification 39305375515](https://github.com/edeori/parametric-dynamic-eq-VST/actions/runs/39305375515)
validated the reserved commit/version and both installers; live upload was skipped.
