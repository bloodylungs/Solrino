#pragma once

class QString;

namespace chatterino {

/// Copies settings and tabs from an older profile into a brand new one.
///
/// Only runs when newSettingsDir has no settings.json yet, and never changes
/// the old profile. Everything comes across, including logins, so features that
/// need an account keep working. The theme is left out, so Solrino's own
/// default applies.
///
/// Returns true if anything was imported.
bool importOldProfileOnce(const QString &oldSettingsDir,
                          const QString &newSettingsDir);

/// Setup asks whether to import from another fork, and leaves the answer in
/// this file beside the profile: the fork's folder, or nothing for a fresh start.
///
/// Reads and removes the answer, and imports if one was given. A brand new
/// profile with no answer starts fresh too, so nothing comes over unasked.
void applyFirstRunImportAnswer(const QString &rootDir,
                               const QString &newSettingsDir,
                               bool newProfileMeansFresh);

/// For profiles made before logins were imported: brings over accounts if this
/// profile has none. Runs once, so removing an account later keeps it removed.
///
/// Returns true if anything was imported.
bool importOldAccountsOnce(const QString &oldSettingsDir,
                           const QString &newSettingsDir);

}  // namespace chatterino
