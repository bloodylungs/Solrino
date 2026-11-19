# Releasing a Solrino update

1. Raise `SOLRINO_VERSION` in `src/common/SolrinoVersion.hpp`, for example to `1.0.1`.
2. Build the app, then run `build-installer.ps1`. Add `-Notes "What's new"` to show a line in the update popup.
3. On the GitHub project's Releases page, make a new release with the tag `v1.0.1` (the same version with a `v` in front).
4. Attach `build\Solrino-Setup.exe` and `build\solrino-update.json` with exactly those names, then publish it.

Installed copies find it within 6 hours, or when someone starts Solrino or clicks **Check for updates**.

The release must be the newest one on the project, and not marked as a pre-release, or copies won't see it.
