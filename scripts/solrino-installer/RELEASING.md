# Releasing a Solrino update

1. Raise `SOLRINO_VERSION` in `src/common/SolrinoVersion.hpp`, for example to `1.0.1`.
2. Write the release notes to a file, one `- New ...`, `- Fixed ...` or `- Removed ...` line per change. Build the app, then run `build-installer.ps1 -NotesFile notes.md -Notes "One-line summary"`. The update card shows each change in New / Fixed sections, using the first sentence as the title and the next as the detail. `-Notes` is what older copies show instead.
3. On the GitHub project's Releases page, make a new release with the tag `v1.0.1` (the same version with a `v` in front).
4. Attach `build\Solrino-Setup.exe` and `build\solrino-update.json` with exactly those names, then publish it.

Installed copies find it within 6 hours, or when someone starts Solrino or clicks **Check for updates**.

The release must be the newest one on the project, and not marked as a pre-release, or copies won't see it.
