# Changes since v1.0.45

Not released yet. Collect everything here, then use it for the release notes.

## App

- **Two Solrinos at once:** opening Solrino while it's already on screen now opens a second copy instead of just bringing the first one forward. If the first one is hidden in the tray, it still comes back instead. Automatic actions (raid shoutouts, auto-redeem, opening the browser when someone goes live) only run in the first copy, so they never happen twice. Both copies share the same settings, so whichever closes last saves its layout.

- **Solrino messages (end-to-end encrypted DMs):** a new Messages button in the title bar, or type `/dm @name` (`/messages` opens the inbox). Only the two people talking can read them: each PC makes its own keys, every message is sealed for each of the other person's devices and signed, and Solrino's server only passes on sealed envelopes without knowing who sent them, deleting them once delivered (or after 14 days). First messages from someone new go to Requests. Block, delete chat, and a Security code to compare with the other person. If someone's keys change, Solrino warns and won't send until you've checked. History stays on your PC, locked to your Windows user. Needs the badge server deploy.
- **Message button on user cards:** right under Follow and Show Twitch, it opens an encrypted Solrino chat with that person. The Messages window has avatars, chat previews, search, real chat bubbles and a rounded send box.
- **Gift a badge:** type `/gift @name` to buy a for-sale badge (supporter or top donor) for someone else, like a gifted sub. They get a "<you> gifted you a badge" popup with your note. Checkout isn't open yet, so the last step says "Checkout coming soon". The website's Purchase page has a matching "Gift it" button on every badge.
- **Early Supporter badge redesign:** a glossy black star with a white S, now in a silver frame on black marble, with thin silver veins slowly drifting through it.
- **Stopwatch on timeout buttons can be turned off:** Settings > General has a "Stopwatch on timeout buttons" box. Unticking it brings back the plain "5 over m" timeout buttons.
- Fixed the reaction button sometimes covering the reply button. It now moves to the left of it.
- Fixed user cards sometimes hiding the newest message. The card stays on screen when it grows, and opens scrolled to the newest message.
- Fixed messages sometimes sending from your other account after switching accounts, when Chat spoofer was on. It now only uses a saved login for the account you're on, and otherwise sends normally from that account.
- **Mod button size:** Settings > General has a "Mod button size" dropdown (Normal, Large, Larger) for the ban, timeout, delete and pin buttons next to messages.
- **Turn off user card banners:** a "Banner on user cards" box in Settings > General. Unticking it removes the banner picture, so user cards are much shorter.
- **Keep tab groups closed:** a "Keep tab groups closed when switching tabs" box in Settings > General. Switching to a tab inside a closed group no longer opens the group.
- **Uses less CPU:** animated emotes, badges and paints now run at 30 frames a second instead of 50, and stop completely while every Solrino window is minimized or hidden in the tray (like while gaming), then pick up again when you come back.
- **Replace with bot:** the 7TV Emotes tab in Vanity has a new button that sends `#add <emote link> -f`, so a bot like PotatBot swaps out an emote that already has the same name.
- **Selfbot rules come with you:** importing from another fork during setup now also brings over your Chat automations (selfbot rules) and their counters. Setup's import screen says so. (Installer change: needs the installer rebuilt at release.)
- Fixed it looking like a Solrino paint took off your 7TV paint. Your 7TV paint was never removed (other apps still show it), but user cards only listed the Solrino one. They now show both: "Solrino Paint" and "7TV Paint".
- **Early Supporter (Classic):** anyone with Early Supporter can choose between the new framed marble look and the original black glass star. Pick "Early Supporter (Classic)" in /vanity. Needs the badge server deploy.

## Mac (not ready to announce)

- Mac app is named Solrino with its own icon and app ID, so it doesn't share settings with Moltorino.
- On Mac, Solrino doesn't try to run the Windows installer for updates; it tells people to get new versions from the website.
- `.github/workflows/solrino-mac.yml` builds a test `Solrino.dmg` on GitHub's Mac machines, only when started by hand. Not run yet: it needs the code on GitHub first.
- Windows-only parts (Now Playing, the installer) don't work on Mac yet.

## Website

- **Mac downloads, hidden until the Mac release:** set `MAC_READY = true` in `site/nav.js` and change "Free for Windows" in `site/_build.py` to turn them on. separate downloads for Windows and Mac. Mac visitors get "Download for Mac" with Mac install steps; everyone sees a "Get the Mac/Windows version" link under the main button; the line under it says "Free for Windows and Mac". These are in `site/nav.js` and `site/_build.py` locally, so any website deploy before the Mac release would put them live. Deploy the badge server's `?platform=mac` download first, and attach `Solrino.dmg` to the GitHub release.
