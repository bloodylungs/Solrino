#pragma once

namespace chatterino::solrino {

/// Badges everyone gets, like Spooky 2026 and Winter 2026, are announced with a
/// popup the first time someone is using the app once the badge is out. Each
/// badge's popup only appears once. The server says when each one comes out.
/// Badges the owner gives someone from the badge panel get their own popup on
/// that person's Solrino: "scary has given you a badge".
void startBadgeGifts();

}  // namespace chatterino::solrino
