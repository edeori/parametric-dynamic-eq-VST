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

## Live versioned release — 2026-10-19

Version **0.1.2**, built source `35235e3ade46bc4d1050a6296d3bcc385c1eda0a`.
[Build](https://github.com/edeori/parametric-dynamic-eq-VST/actions/runs/39305139718) and [publication](https://github.com/edeori/parametric-dynamic-eq-VST/actions/runs/39309411818) succeeded for macOS and Windows.
The published Mac package version and Windows ProductVersion were verified; the
live EXE SHA-256 matches the clean Defender report. Shop version updated after
publication. See the [shared release record](https://github.com/edeori/vst-release-pipeline/blob/main/docs/releases/2026-10-19-version-rollout.md) for all products and hashes.

## Verified release candidate — 2026-10-20

Version **0.1.4**, built source `398334b917f596d63f290f8b75322e73ba88c9e2`,
triggered by `7353eff35901094c7797efc4161e18edd443827d`.
Includes captured spectrum overlays, editable correction display and automatic
dynamic Match EQ with bounded reduction and temporal capture analysis.

- [Build 39498311429](https://github.com/edeori/parametric-dynamic-eq-VST/actions/runs/39498311429): macOS and Windows successful; all four tests passed on each platform.
- Apple notarization accepted; package signature and stapling checks passed.
- Defender clean, exit code 0; Windows installer SHA-256 `8ad83ac1ca73c314fa3656e0045cf41c7afa46238dde740f627bb135ad2dedf3`.
- [Publish verification 39499716231](https://github.com/edeori/parametric-dynamic-eq-VST/actions/runs/39499716231): verified the exact build, source/version, installers and Defender hash; publication step skipped.

No live installers changed in this verification. Live Shop version remains 0.1.2.
To publish this candidate, run Publish installers with Specific build ID
`39498311429` and `verify_only=false` after the live publication decision.

## Live release — 2026-10-20

Version **0.1.4** published after explicit approval using build `39498311429`.
[Publication 39500723364](https://github.com/edeori/parametric-dynamic-eq-VST/actions/runs/39500723364)
succeeded. Both public downloads were compared byte-for-byte with the verified
build artifacts and match. Mac PKG SHA-256:
`0c8028ae42fa201002baa0bdab96b054b3e17324bf657abe2cdf31e8b4a3437b`;
Windows SHA-256 matches the Defender report above.

The live Shop product version was changed from 0.1.2 to 0.1.4 and verified in
the public Shop HTML. Other product fields were preserved. Server backup:
`/srv/mothproduction-web/.deploy-backups/paradyn-eq-0.1.4-20261020T213022Z/shop.md`.
