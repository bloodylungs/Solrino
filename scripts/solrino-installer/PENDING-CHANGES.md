# Changes since v1.0.46

Not released yet. Collect everything here, then use it for the release notes.

## App

- **Photos, videos and files in messages:** the + button next to the message box sends anything from your PC up to 25 MB. Photos show in the chat; videos and other files get Play/Open and Save buttons. Files are encrypted on your PC before upload, so Solrino's server can't open them, and they're deleted after 14 days. Needs the badge server deploy.
- **7TV emotes in messages:** your 7TV emotes, the other person's and 7TV's global emotes show right in messages, animated. The smiley button opens a searchable emote picker.
- **Sent and Seen:** your newest message shows "Sent", then "Seen" once they've opened the chat.
- **Real profile pictures in messages:** chats show each person's Twitch profile picture.
- Fixed the messages window drawing old messages and "No chat open" on top of the chat, which made messages look doubled.
- New: Chat automations can change your Twitch name color on every message you send (Rainbow or Random)
- New: Chat automations can wear a random one of your Twitch badges on every message
- New: Chat automation rules can also run on messages caught by AutoMod or blocked terms
- New: The quick game bar shows the games you use most in each chat, and right-clicking it turns it off

## Mac (not ready to announce)

- Mac app is named Solrino with its own icon and app ID, so it doesn't share settings with Moltorino.
- On Mac, Solrino doesn't try to run the Windows installer for updates; it tells people to get new versions from the website.
- `.github/workflows/solrino-mac.yml` builds a test `Solrino.dmg` on GitHub's Mac machines, only when started by hand. Not run yet: it needs the code on GitHub first.
- Windows-only parts (Now Playing, the installer) don't work on Mac yet.

## Website

- **Mac downloads, hidden until the Mac release:** set `MAC_READY = true` in `site/nav.js` and change "Free for Windows" in `site/_build.py` to turn them on. separate downloads for Windows and Mac. Mac visitors get "Download for Mac" with Mac install steps; everyone sees a "Get the Mac/Windows version" link under the main button; the line under it says "Free for Windows and Mac". These are in `site/nav.js` and `site/_build.py` locally, so any website deploy before the Mac release would put them live. Deploy the badge server's `?platform=mac` download first, and attach `Solrino.dmg` to the GitHub release.
