#pragma once

/// Solrino's own version. Raise it for every release: the updater only installs
/// versions newer than this, and build-installer.ps1 reads it from here.
#define SOLRINO_VERSION "1.0.41"

/// The GitHub project holding the releases, as "owner/name". While it's empty
/// the updater stays off.
#define SOLRINO_UPDATE_REPOSITORY "bloodylungs/Solrino"

/// The Application ID from discord.com/developers. While it's empty the Discord
/// activity stays off.
#define SOLRINO_DISCORD_APP_ID "1567681887588065400"

/// Solrino's own badge server: see scripts/solrino-badge-server.
#define SOLRINO_BADGE_SERVER "https://solrino-badges.solrino-badge-server.workers.dev"

/// Who can give badges. The server checks this too, so changing it here alone
/// wouldn't let anyone else give them.
#define SOLRINO_OWNER_USER_ID "798782097"

/// The logo Discord shows beside the activity.
#define SOLRINO_DISCORD_LOGO_URL \
    "https://raw.githubusercontent.com/bloodylungs/Solrino/main/assets/logo.png"
