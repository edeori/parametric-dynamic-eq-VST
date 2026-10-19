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
