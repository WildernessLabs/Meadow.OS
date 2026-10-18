# Release Planning

Status: PROPOSAL for review. Nothing in this document has been implemented.

This document describes how a Meadow 3.0 release is assembled from the Meadow.OS and Meadow-ESP32
repositories, published on GitHub, and downloaded by the Meadow CLI (`meadow firmware download`).

## Goals

- A release is a deliberate act (a tag), not a side effect of a build.
- A release pulls together every binary the CLI needs, from both repositories.
- The release is reproducible: it records the exact source commits it was built from.
- Distribution is GitHub only. No S3 bucket and no Meadow.Cloud dependency.
- Legacy Meadow releases remain available, and legacy publishing continues unchanged for the short
  and medium term.

## Current state

### What the CLI expects today

The CLI `meadow-3.0` branch (commit 746e293a) downloads firmware from a public S3 bucket,
`https://s3-us-west-2.amazonaws.com/downloads.wildernesslabs.co/Meadow_Beta/`:

    Meadow_Beta/latest.json        pointer to the newest release
    Meadow_Beta/<version>.json     metadata for one release (version, minCLIVersion,
                                   downloadUrl, networkDownloadUrl)
    Meadow_Beta/<os zip>           OS package
    Meadow_Beta/<network zip>      ESP32 package

Both zips are extracted into one flat folder, `<FirmwareStore>/<version>/`. The CLI recognises files
by exact name (`F7FirmwareFiles` in `F7FirmwarePackageCollection.cs`):

| File | Produced by | Zip today |
|------|-------------|-----------|
| `Meadow.OS.bin` | Meadow.OS `build.sh` | OS |
| `Meadow.OS.Update.bin` | Meadow.OS `build.sh` | OS |
| `Meadow.OS.Runtime.bin` | Meadow.OS `build.sh` | OS |
| `meadow_assemblies/` | Meadow.OS `build-managed-assemblies.sh` | OS |
| `bootloader.bin` | Meadow-ESP32 | network |
| `partition-table.bin` | Meadow-ESP32 | network |
| `MeadowComms.bin` | Meadow-ESP32 | network |

The package collection lists a folder only when its name passes `System.Version.TryParse`, so a
semver suffix such as `3.0.0-beta5` is silently ignored.

### What the repositories produce today

- Meadow.OS: `.github/workflows/build-system.yml` builds the OS (`Meadow.OS.bin`,
  `Meadow.OS.Update.bin`, `Meadow.OS.Runtime.bin`, `build-info.json`), debug symbols, and the
  managed assemblies, as separate workflow artifacts. Workflow artifacts expire.
- Meadow-ESP32: `azure-pipelines.yml` builds `MeadowComms.bin`, `bootloader.bin` and
  `partition-table.bin` (each with a `.md5`) and publishes them as the Azure artifact `network`.
  Azure artifacts expire.
- Meadow.OS `esp32/` is a tracked directory that is not fully wired in. The ESP32 firmware that
  ships is built from the Meadow-ESP32 repository.

## Decisions

1. **GitHub Releases is the only distribution point.** The CLI reads the GitHub releases API.
2. **A new public Releases repository holds the releases.** Name to be decided
   (for example `WildernessLabs/Meadow.Releases`). It must be public so the CLI can download
   anonymously.
3. **The Releases repository is where the release decision is made.** A release is a commit plus a
   tag there. A file in that repository (`release.toml`) pins the source commits to build from.
4. **Meadow-ESP32 stays a separate repository.** It keeps its history, its issues, and its legacy
   releases. GitHub issues cannot be moved with their numbers, links and PR threads intact, and one
   ESP32 lineage serves both the legacy and 3.0 OS (the wire format stays NuttX 7 errno/levels,
   converted in `espcp_mapping`). The ESP32 code is not moved into Meadow.OS.
5. **Versions are numeric: `3.0.0.<build>`.** The build number is the existing count of commits
   since `version.txt` last changed. It is monotonic, parses with `Version.TryParse`, and needs no
   manual bump. "Beta" is expressed by the GitHub prerelease flag, and optionally in the release
   title (for example "3.0 beta 6 (3.0.0.412)"), not in the tag.
6. **Legacy publishes as is.** The existing Azure and S3 pipelines and the legacy CLI (`develop`
   branch) are untouched. Old releases are additionally imported into the Releases repository so
   the 3.0 CLI can download them.

## Release format

One GitHub Release per version, tag `v<version>` (for example `v3.0.0.412`). Assets:

| Asset | Contents |
|-------|----------|
| `Meadow.OS.zip` | `Meadow.OS.bin`, `Meadow.OS.Update.bin`, `Meadow.OS.Runtime.bin`, `meadow_assemblies/` |
| `Meadow.Network.zip` | `bootloader.bin`, `partition-table.bin`, `MeadowComms.bin` |
| `manifest.json` | metadata, file hashes, source pins (below) |
| `Meadow.Debug.zip` | ELF and map files (`nuttx.elf`, `nuttx_user.elf`, `System.map`, `User.map`, `Meadow.BL.elf`, ESP32 `.elf`/`.map`) |
| `sources-*.tar.gz` | `git archive` of each source repository at its pinned commit (optional) |

Two zips are kept so the extracted layout is identical to today and `F7FirmwarePackageCollection`
needs no change. A single combined zip is possible later; the manifest makes that a CLI-only
change.

### manifest.json

    {
      "version": "3.0.0.412",
      "generation": "3.0",
      "prerelease": true,
      "minCLIVersion": "3.0.0",
      "files": {
        "Meadow.OS.zip":      { "sha256": "..." },
        "Meadow.Network.zip": { "sha256": "..." }
      },
      "contents": { "MeadowComms.bin": { "sha256": "..." } },
      "sources": {
        "Meadow.OS":    { "repository": "...", "commit": "..." },
        "Meadow-ESP32": { "repository": "...", "commit": "..." },
        "runtime":      { "repository": "...", "commit": "...", "tree": "..." }
      }
    }

`generation` is `"3.0"` or `"legacy"`. It lets one repository hold both lines, and lets the CLI
refuse a mismatched release with a clear message.

## Process

### Deciding to release

1. Tag Meadow-ESP32 if its firmware changed (it publishes its artifacts as a GitHub Release on its
   own tag, because Azure artifacts expire).
2. Edit `release.toml` in the Releases repository: the Meadow.OS ref, the Meadow-ESP32 ref, and
   the runtime pin (the same repository, commit and tree as Meadow.OS `MONO_RUNTIME_COMMIT`).
3. Merge that change, then tag the Releases repository `v3.0.0.<build>`. The tag is the release
   decision.

### The release workflow (runs on a `v*` tag)

1. Check out Meadow.OS at the pinned ref. Compute the version and verify it matches the tag.
2. Obtain the OS binaries and assemblies, and the ESP32 binaries.
3. Stage the zips with the flat layout above, and write `manifest.json` with hashes and pins.
4. Create the GitHub Release (prerelease unless the tag is marked final) and attach the assets.

Open decision (see below): obtain by rebuilding from the pins, or by collecting already-built CI
artifacts. The packaging script in `tools/` is the same either way.

### What "latest" means

The CLI asks the GitHub API for the newest release of its own generation:
`/releases/latest` excludes prereleases by default, and the manifest `generation` field filters
out the other line. Imported legacy releases are created with `make_latest=false`.

## Legacy

- Legacy continues to publish to S3 through the existing pipelines. No change.
- The legacy CLI (`develop`) continues to read S3.
- **Import (one-off script).** For each legacy version: read `<version>.json` from the bucket,
  download the two zips, create a GitHub Release in the Releases repository with the zips attached,
  and add a generated `manifest.json` (`generation: "legacy"`, original `minCLIVersion`, hashes).
  Tags point at the repository's initial commit; the notes say "imported from S3".
- The 3.0 CLI can offer legacy releases behind an explicit generation switch.
- Enumerating the versions needs a list. `latest.json` names only the newest. Use the bucket
  listing or the legacy CLI's version list, or probe for `<version>.json` over the known range.

## CLI changes (`meadow-3.0` branch)

`F7FirmwareDownloadManager` is the single seam; `F7ReleaseMetadata` and the tests' stub HTTP
handler can be reused.

1. Replace `GetReleaseMetadata` with calls to the GitHub releases API (anonymous), selecting
   `latest` or `tags/v<version>` for the CLI's generation.
2. Download `manifest.json`, then the zips, and verify the sha256 values.
3. Extract to `<FirmwareStore>/<version>/` as today.
4. Version display and parsing: numeric versions need no change. If `-betaN` tags are ever wanted,
   the CLI must parse semver (naive string sorting puts `beta10` before `beta9`, and
   `Version.TryParse` rejects the suffix).
5. Keep the legacy `develop` CLI unchanged.

## Meadow.OS esp32/ directory

Independent of the release work and not on its critical path. Options:

1. Leave it. It stays stale and confusing.
2. Replace it with a Meadow-ESP32 submodule at the same path. History and issues are preserved
   because the repository is not touched. Costs: ESP32 changes become two steps (commit, then bump
   the pointer), the CI submodule step (currently `if: false`) must be re-enabled with HTTPS URLs,
   and the Phase branch flow needs a decision on which ESP32 branch the pointer tracks.
3. Delete it and update `build-esp32.sh` and `CLAUDE.md`, which refer to it.

Decide separately from the release work.

## Implementation order

1. Create the Releases repository (public) and define `manifest.json`.
2. Meadow-ESP32: publish its artifacts as a GitHub Release on its own tag.
3. Write the packaging script (`tools/`) that stages the zips and the manifest from a set of
   binaries. Test it locally.
4. Write `release.toml` and the release workflow.
5. Change the 3.0 CLI to read GitHub, with hash verification. Test against a real
   `meadow firmware download <version>`.
6. Write and run the legacy import script.
7. Decide the `esp32/` directory.

## Open questions

- Releases repository name.
- Rebuild from pins, or collect already-built artifacts? Collecting is faster to start (the OS
  build needs Docker and a Mono runtime build) but needs durable artifacts from both repositories.
  Rebuilding is the better end state.
- Two zips (as proposed) or a single zip or individual assets.
- Where does the list of legacy versions come from for the import?
- Source tarballs: include `git archive` of each repository (cheap), or only the commit pins in
  `manifest.json`?
- Who may tag a release, and does the release workflow need a manual approval gate?
