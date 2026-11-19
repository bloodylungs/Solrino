# Changes since v1.0.44

Not released yet. Collect everything here, then use it for the release notes.

## App

- **Song for OBS:** Solrino shares the song you're playing with the Solrino website's OBS overlay (on this PC only, at 127.0.0.1:47391, readable only by solrino's site). The overlay turns from "Chat by Solrino" into the song card, and Solrino hides its own song card while OBS shows it.

- **Raid shoutouts are instant:** the automatic raid shoutout now goes out as soon as the raid comes in, and sends Twitch's own `/shoutout` too, in case the channel has no bot for `!so`.

- **Tab group click style for all groups:** Settings > Solrino now has "Make all groups expand tabs" and "Make all groups open a tab list" buttons, so you can switch every existing folder back to the older tab-list style in one click.

- **7TV Emotes tab in Vanity:** browse trending, most popular and newly uploaded 7TV emotes, or search them. Pick one and click **Add to my channel** to put it in your 7TV channel emote set (needs your 7TV account connected in the 7TV tab), or **Add with bot** to send `#add` with the emote's 7TV link in chat for a bot like PotatBot, so even brand-new emotes are found.
- **Full usercard banners:** the banner at the top of a usercard now shows the whole image in its own shape instead of a cropped strip.

- **30 new paints:** Sunny Side Up, Crispy Bacon, Kiwi Cut, Triple Scoop, Butter Popcorn, Sour Worm, Choco Peanut, Coconut Split, Gas Giant, Ice Giant, Fallout, Cloud Nine, Storm Front, Tiger Stripe, Dalmatian, Koi Pond, Firefly Meadow, Lobster, Malachite, Bismuth, Brushed Steel, Verdigris, Argyle Knit, Patchwork, Beehive, Darkroom, Pressed Flowers, Lucky Clover, Birthday Cake and Glass Marble.

- **Prediction buttons:** mods and the streamer get a "Pay out <outcome>" button for each outcome and a "Refund" button right under an open prediction. Click twice to confirm, since paying out or refunding can't be undone.
- **End poll button:** mods and the streamer get an "End poll" button right under a running poll in the poll banner. One click ends it, the same as typing `/endpoll`.
- **Auto-open when live opens your browser:** when a channel you picked goes live, the stream also opens in your default browser (Chrome, Brave and so on). There's a checkbox for it in the go-live dialog, on by default.
- Fixed the Now Playing overlay sometimes showing no album art, or the last song's art, when Spotify changed songs.
- **New Early Supporter badge look:** still black, now glossy like polished volcanic glass, with moving reflections, a rainbow sheen sliding across, a bright silver edge and a glowing white S.
- **Top Donor badge:** a new background behind the star, with dark silk flowing in folds of magenta, pink and gold.
- **Developer badge:** a new background behind the star, with bright neon bands in lime, green, teal and yellow flowing like contour lines.
- **New Founder badge look:** a crimson metal star with a gold edge, a glowing S and sparkles on its points.

## Mac (not ready to announce)

- Mac app is named Solrino with its own icon and app ID, so it doesn't share settings with Moltorino.
- On Mac, Solrino doesn't try to run the Windows installer for updates; it tells people to get new versions from the website.
- `.github/workflows/solrino-mac.yml` builds a test `Solrino.dmg` on GitHub's Mac machines, only when started by hand. Not run yet: it needs the code on GitHub first.
- Windows-only parts (Now Playing, the installer) don't work on Mac yet.

## Website

- **Live since 2026-11-17:** page loading animation; hero chat window leans toward the mouse; drag-to-spin 3D badge viewer; "Same chat. Better." before/after slider; live badge rarity tags; "The people of Solrino" section (streamers, supporters, credits; fill in `site/assets/people.json`); a **Paints** page to try every paint on your own name; interactive settings tour on Features; a 3D laptop you can turn; "Your own Solrino page" teaser; a **Streamer kit** page with the logo, an OBS overlay (`/overlay`) and a blurb.


- **Mac downloads, hidden until the Mac release:** set `MAC_READY = true` in `site/nav.js` and change "Free for Windows" in `site/_build.py` to turn them on. separate downloads for Windows and Mac. Mac visitors get "Download for Mac" with Mac install steps; everyone sees a "Get the Mac/Windows version" link under the main button; the line under it says "Free for Windows and Mac". These are in `site/nav.js` and `site/_build.py` locally, so any website deploy before the Mac release would put them live. Deploy the badge server's `?platform=mac` download first, and attach `Solrino.dmg` to the GitHub release.
