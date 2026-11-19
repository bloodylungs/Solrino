# Releasing a Solrino update

1. Raise `SOLRINO_VERSION` in `src/common/SolrinoVersion.hpp`, for example to `1.0.1`.
2. Write the release notes to a file, one `- New ...`, `- Fixed ...` or `- Removed ...` line per change. Build the app, then run `build-installer.ps1 -NotesFile notes.md -Notes "One-line summary"`. The update card shows each change in New / Fixed sections, using the first sentence as the title and the next as the detail. `-Notes` is what older copies show instead.
3. If any paints changed, refresh the website's paint try-on: `build\bin\Solrino.exe --export-solrino-paints site\assets\paints.json` (in PowerShell, run it through `Start-Process -Wait` with the full path to `paints.json`, since it returns at once otherwise).
4. For the Mac version, run the **Solrino for Mac** workflow on GitHub (Actions tab > Solrino for Mac > Run workflow) and download its `Solrino.dmg`.
5. On the GitHub project's Releases page, make a new release with the tag `v1.0.1` (the same version with a `v` in front).
6. Attach the files with exactly these file names, and give the two setups labels so people can tell them apart on the release page:

   | File | Label shown on GitHub |
   | --- | --- |
   | `build\Solrino-Setup.exe` | Windows setup |
   | `Solrino.dmg` | Mac setup |
   | `build\solrino-update.json` | (no label, the app reads it) |

   With the GitHub command line, everything after `#` is the label:

   ```
   gh release upload v1.0.1 "build/Solrino-Setup.exe#Windows setup" "Solrino.dmg#Mac setup" build/solrino-update.json
   ```

   Use this command rather than dragging the files onto the release page, since that way the labels are set for you.

7. Publish it.

Keep the file names exactly as above: the website's download buttons and the app's updater look for `Solrino-Setup.exe` and `Solrino.dmg` by name. The labels only change what GitHub shows.

Installed copies find it within 6 hours, or when someone starts Solrino or clicks **Check for updates**.

The release must be the newest one on the project, and not marked as a pre-release, or copies won't see it.
