# Changes since v1.0.47

Not released yet. Collect everything here, then use it for the release notes.

## App
- New: Now Playing works with Spotify in your browser (open.spotify.com in Chrome, Edge, Firefox, Brave, Opera or Vivaldi), so the Spotify app doesn't need to be open. On by default: "Spotify in a browser".
- New: Reply to messages. Hover a message and click the reply arrow (or right-click, Reply); the reply shows which message it answers. Esc cancels.
- New: Hover a 7TV emote in messages to see its name, like in chat.
- Changed: The send button in messages is a proper paper-plane icon.
- Fixed: Chat automation rules with "Also messages caught by AutoMod or blocked terms" now actually run on held messages (they were skipped before).
- Fixed: YouTube chats said "YouTube chat is not configured in this build". Solrino now finds the live stream and reads its chat straight from YouTube, with no relay server.

## Mac (not ready to announce)

- Mac app is named Solrino with its own icon and app ID, so it doesn't share settings with Moltorino.
- On Mac, Solrino doesn't try to run the Windows installer for updates; it tells people to get new versions from the website.
- `.github/workflows/solrino-mac.yml` builds a test `Solrino.dmg` on GitHub's Mac machines, only when started by hand. Not run yet: it needs the code on GitHub first.
- Windows-only parts (Now Playing, the installer) don't work on Mac yet.

## Website

- **Mac downloads, hidden until the Mac release:** set `MAC_READY = true` in `site/nav.js` and change "Free for Windows" in `site/_build.py` to turn them on. separate downloads for Windows and Mac. Mac visitors get "Download for Mac" with Mac install steps; everyone sees a "Get the Mac/Windows version" link under the main button; the line under it says "Free for Windows and Mac". These are in `site/nav.js` and `site/_build.py` locally, so any website deploy before the Mac release would put them live. Deploy the badge server's `?platform=mac` download first, and attach `Solrino.dmg` to the GitHub release.
