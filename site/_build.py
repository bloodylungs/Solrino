"""Writes the site's pages from one shared nav and footer.

    cd site && python _build.py

Files starting with _ aren't uploaded (see .assetsignore).
"""
import re
from html import escape

NAV = open("_nav.html", encoding="utf-8").read()
FOOT = open("_foot.html", encoding="utf-8").read()
TERMS = open("_terms.html", encoding="utf-8").read()
PRIVACY = open("_privacy.html", encoding="utf-8").read()

# What each badge is for, most important first. This is the same order as the
# Solrino tab in the app, then the Other Badges tab from A to Z.
BADGES = [
    ("developer", "Developer", "Helps build Solrino."),
    ("founder", "Founder", "Founded Solrino."),
    ("staff", "Staff", "Part of the Solrino team."),
    ("moderator", "Moderator", "Helps moderate the Solrino community."),
    ("contributor", "Contributor", "Has contributed to Solrino."),
    ("top_bug_hunter", "Top Bug Hunter", "Has reported the most bugs in Solrino."),
    ("alpha_tester", "Alpha Tester", "Tries the earliest, roughest Solrino builds."),
    ("beta_tester", "Beta Tester", "Tries Solrino updates before everyone else."),
    ("number_one_donor", "#1 Donor", "The person who has donated the most to Solrino."),
    ("donor_of_the_year", "Donor of the Year", "This year's top Solrino donor."),
    ("donor_of_the_month", "Donor of the Month", "This month's top Solrino donor."),
    ("first_donor", "First Donor", "The very first person to donate to Solrino."),
    ("top_donor", "Top Donor", "One of Solrino's top donors."),
    ("top_donor_alt", "Top Donor Alt", "One of Solrino's top donors, in a different style."),
    ("supporter_plus_plus_plus_alt", "Supporter+++ Alt", "Solrino's highest supporter tier, in a different style."),
    ("supporter_plus_plus_plus", "Supporter+++", "Solrino's highest supporter tier."),
    ("obsidian", "Early Supporter", "Supported Solrino in its early days."),
    ("veteran", "Veteran", "One of Solrino's longest-running users."),
    ("one_year_on_solrino", "One Year on Solrino", "Has been on Solrino for a full year."),
    ("supporter_plus_plus_alt", "Supporter++ Alt", "A Supporter++ on Solrino, in a different style."),
    ("supporter_plus_plus", "Supporter++", "One of Solrino's bigger supporters."),
    ("supporter_plus_alt", "Supporter+ Alt", "A Supporter+ on Solrino, in a different style."),
    ("supporter_plus", "Supporter+", "Gives extra support to Solrino."),
    ("supporter", "Supporter", "Supports Solrino."),
    ("black_hole", "Black Hole", "A very rare badge from Solrino's owner."),
    ("bug_hunter", "Bug Hunter", "Finds and reports bugs in Solrino."),
    ("clockwork", "Clockwork", "A rare badge from Solrino's owner."),
    ("friend_of_solrino", "Friend of Solrino", "A friend of Solrino."),
    ("halo", "Halo", "A rare badge from Solrino's owner."),
    ("kaleidoscope", "Kaleidoscope", "A rare badge from Solrino's owner."),
    ("katana", "Katana", "A rare badge from Solrino's owner."),
    ("lava_lamp", "Lava Lamp", "A rare badge from Solrino's owner."),
    ("matrix_rain", "Matrix Rain", "A rare badge from Solrino's owner."),
    ("nebula", "Nebula", "A rare badge from Solrino's owner."),
    ("oil_slick", "Oil Slick", "A rare badge from Solrino's owner."),
    ("plasma_ball", "Plasma Ball", "A rare badge from Solrino's owner."),
    ("special", "Special", "Someone who is special to Solrino."),
    ("spooky", "Spooky 2026", "Free for everyone for Halloween 2026."),
    ("valentines_2027", "Valentine's 2027", "Given out for Valentine's 2027."),
    ("winter_2026", "Winter 2026", "Free for everyone from December 1st, 2026."),
]
DOWNLOAD = '<button type="button" class="{cls}" data-download title="Solrino">{label}</button>'

# Groups for the filter on the badges page
GROUPS = [("team", "Team"), ("supporter", "Supporters"), ("donor", "Donors"), ("rare", "Rare"), ("event", "Events")]
TEAM = {"developer", "founder", "staff", "moderator", "contributor", "top_bug_hunter", "alpha_tester", "beta_tester", "bug_hunter"}
SUPPORTER = {"obsidian", "veteran", "one_year_on_solrino", "friend_of_solrino"}
EVENT = {"spooky", "valentines_2027", "winter_2026"}


def badge_group(badge_id):
    if badge_id in TEAM:
        return "team"
    if "donor" in badge_id:
        return "donor"
    if badge_id.startswith("supporter") or badge_id in SUPPORTER:
        return "supporter"
    if badge_id in EVENT:
        return "event"
    return "rare"


def slug(text):
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")


def page(slug, title, description, body, scripts=("site",), current=None):
    nav = NAV
    if current:
        nav = nav.replace(f'data-tab="{current}">', f'data-tab="{current}" aria-current="true">')
    tags = "\n".join(f'<script src="/{name}.js" defer></script>' for name in ("nav",) + tuple(scripts))
    head_title = title if slug == "index" else f"{title} | Solrino"
    screen_pings = "" if 'id="screen-pings"' in body else '<div class="pings pings-screen" id="screen-pings" aria-live="polite"></div>\n'
    open(f"{slug}.html", "w", encoding="utf-8", newline="\n").write(f'''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{head_title}</title>
<meta name="description" content="{description}">
<meta name="theme-color" content="#09060a">
<meta property="og:title" content="{head_title}">
<meta property="og:description" content="{description}">
<meta property="og:image" content="/assets/icon.png">
<link rel="icon" href="/assets/icon.png">
<link rel="preload" href="/assets/fonts/geist.woff2" as="font" type="font/woff2" crossorigin>
<link rel="stylesheet" href="/style.css">
<script type="speculationrules">{{"prefetch": [{{"where": {{"href_matches": "/*"}}, "eagerness": "moderate"}}]}}</script>
<script>
document.documentElement.classList.add("js");
// The logo intro plays the first time someone opens the site, then never again in that browser
try {{
  if (!localStorage.getItem("solrino-intro") && !matchMedia("(prefers-reduced-motion: reduce)").matches) {{
    localStorage.setItem("solrino-intro", "1");
    document.documentElement.classList.add("intro-on");
  }}
}} catch {{}}
</script>
</head>
<body>
<div class="intro" aria-hidden="true"><img src="/assets/icon.png" alt="" width="256" height="256" decoding="sync" fetchpriority="high"></div>
<a class="skip" href="#main">Skip to content</a>
<div class="backdrop" aria-hidden="true"></div>

{nav}

<main id="main">
{body}
</main>

{FOOT}
{screen_pings}{tags}
</body>
</html>
''')


def page_head(title, lede):
    return f'''<header class="page-hero">
  <h1>{title}</h1>
  <p class="lede">{lede}</p>
</header>'''


def badge_img(badge_id, size=72):
    return f'<img src="/assets/badges/small/{badge_id}.webp" alt="" width="{size}" height="{size}" loading="lazy">'


def marquee(items, reverse=False):
    cards = "".join(f'<span class="mq-card" title="Solrino {b[1]}">{badge_img(b[0], 44)}<span>{b[1]}</span></span>' for b in items)
    return f'<div class="mq{" mq-rev" if reverse else ""}" aria-hidden="true"><div class="mq-track">{cards}{cards}</div></div>'


FAQ = [
    ("Is Solrino free?", "Yes, it's free to download and use, with no ads."),
    ("What does it run on?", "Windows. There isn't a Mac or Linux version."),
    ("Why does Windows show a warning when I install it?", "The app isn't signed yet, so SmartScreen doesn't recognize it. Click <b>More info</b>, then <b>Run anyway</b>."),
    ("Will the on-screen pings work over my game?", "In borderless and windowed games, yes. Exclusive fullscreen can hide them. The cards are a normal always-on-top window that clicks pass through, and they don't touch the game itself. Every game's anti-cheat has its own rules, so check them if you're unsure."),
    ("Can I keep my Chatterino or Moltorino setup?", "Yes. The first time you run setup, it asks if you'd like to bring over your settings, chat tabs and accounts from another fork."),
    ("How do I get a badge?", "Solrino's owner gives out most badges, and event badges are free for everyone. Once you have one, type <code>/vanity</code> in chat and equip it."),
    ("Who can see my badge?", "Everyone using Solrino, usually within a couple of seconds of it changing. People on the Twitch website or other chat apps won't see it."),
]
faq = "\n".join(f'''    <details class="faq-item glass" id="{slug(q)}"><summary>{q}<i aria-hidden="true"></i></summary><p>{a}</p></details>''' for q, a in FAQ)

CHAT_APP = '''<div class="app" aria-label="Demo of Solrino's chat window. The chatters are made up.">
    <div class="app-title">
      <span class="logo-sweep"><img src="/assets/icon.png" alt="" width="22" height="22"></span>
      <span class="app-account">you</span>
    </div>
    <div class="app-tabs" role="tablist" aria-label="Demo channels">
      <button class="app-tab is-on" role="tab" aria-selected="true" data-channel="emberline">emberline</button>
      <button class="app-tab" role="tab" aria-selected="false" data-channel="quietroom">quietroom<i class="dot"></i></button>
    </div>
    <div class="app-chat">
      <ol class="chat" id="chat" aria-live="off"></ol>
      <div class="pings" id="chat-pings" aria-live="polite"></div>
    </div>
    <div class="app-games" aria-label="Quick game bar">
      <button class="chip" data-game="Fortnite">Fortnite</button>
      <button class="chip" data-game="Minecraft">Minecraft</button>
      <button class="chip" data-game="Just Chatting">Just Chatting</button>
    </div>
    <form class="app-input" id="chat-form">
      <label class="sr" for="chat-text">Send a message in the demo</label>
      <input id="chat-text" maxlength="120" autocomplete="off" placeholder="Say something and see what happens">
      <button class="send" aria-label="Send">&#8593;</button>
    </form>
  </div>'''

home = f'''<section class="hero">
  <div class="hero-copy">
    <h1>Twitch chat that reaches you <em>mid&#8209;game.</em></h1>
    <p class="hero-sub">When someone mentions you, a card appears on top of the game you're playing. Mod tools are next to the chat box.</p>
    <div class="hero-actions">
      {DOWNLOAD.format(cls="btn btn-big", label="Download for Windows")}
      <a class="btn btn-ghost btn-big" href="#chat-text">Try the chat</a>
    </div>
    <p class="platforms">Free for Windows<span data-latest></span>. Works with Twitch, Kick, YouTube and TikTok.</p>
  </div>
  <div class="desk" id="demo">
    {CHAT_APP}
  </div>
  <p class="demo-note">This is a live demo. Type a message and someone pings you back. The people are made up, and nothing you type leaves your browser.</p>
</section>

<section class="explore">
  <div class="sec-title sec-left reveal"><h2>What Solrino <em>adds.</em></h2></div>
  <div class="explore-grid">
    <a class="ex-card glass reveal" href="/pings">
      <div class="ex-visual ex-ping">
        <div class="ping-card ping-still"><span class="ping-pic">L</span><div class="ping-body"><p><b>@lunaris</b> pinged you</p><p class="ping-msg">you up for a game?</p></div><span class="ping-line"><i></i></span></div>
      </div>
      <h3>Pings</h3>
      <p>A card shows up over your game when someone says your name.</p>
      <span class="ex-go">See pings <i aria-hidden="true">&rarr;</i></span>
    </a>
    <a class="ex-card glass reveal" href="/modding">
      <div class="ex-visual ex-mod">
        <span class="chip chip-static">Valorant</span><span class="chip chip-static">Minecraft</span><span class="chip chip-static">Fortnite</span>
        <span class="ex-sword">&#9876;</span>
      </div>
      <h3>Modding</h3>
      <p>Mod tools are one click from the chat box.</p>
      <span class="ex-go">See modding <i aria-hidden="true">&rarr;</i></span>
    </a>
    <a class="ex-card glass reveal" href="/badges">
      <div class="ex-visual ex-badges">
        {badge_img("supporter_plus", 64)}{badge_img("obsidian", 84)}{badge_img("top_donor", 64)}
      </div>
      <h3>Badges</h3>
      <p>Animated stars next to your name that everyone on Solrino sees.</p>
      <span class="ex-go">See badges <i aria-hidden="true">&rarr;</i></span>
    </a>
  </div>
</section>

<section class="stand reveal">
  <div class="sec-title sec-left"><h2>Badges other Solrino users <em>see.</em></h2></div>
  {marquee(BADGES[:6])}
  {marquee(BADGES[5:] + BADGES[:1], True)}
</section>

<section class="faq reveal">
  <div class="faq-wrap">
    <div class="sec-title sec-left"><h2>Common <em>questions.</em></h2><p>Something else? <a href="https://github.com/bloodylungs/Solrino/issues">Ask on GitHub.</a></p></div>
    <div class="faq-list">
{faq}
    </div>
  </div>
</section>

<section class="cta glass reveal">
  <img src="/assets/icon.png" alt="" width="84" height="84">
  <h2>Download Solrino.</h2>
  <p>It's free and updates itself. Setup can bring over your settings and tabs from another fork.</p>
  <p class="small latest-line" data-latest-full hidden></p>
  {DOWNLOAD.format(cls="btn btn-big", label="Download for Windows")}
  <p class="small">Windows may show a SmartScreen warning because the app isn't signed yet. Click <b>More info</b>, then <b>Run anyway</b>.</p>
</section>'''
page("index", "Solrino: Twitch chat that reaches you mid-game",
     "Solrino is a free Twitch chat app for Windows. It shows pings on top of your game, puts mod tools next to the chat box, and shows your chat on your Discord profile.", home)

pings = page_head("Stop missing your <em>name</em> in chat.",
                  "When someone says your name, or a word you chose, a card slides in at the top of your chat and on your screen. The screen card goes in the bottom right corner unless you move it.") + '''
<section class="shell">
  <div class="ping-demo glass reveal">
    <div class="ping-demo-slot" id="ping-slot" aria-live="polite"></div>
    <button class="btn btn-big" id="ping-me">Send me a ping</button>
  </div>

  <div class="sec-title reveal"><h2>What's on a <em>ping card.</em></h2></div>
  <div class="anatomy reveal">
    <div class="anatomy-card glass">
      <div class="ping-card ping-large">
        <span class="ping-pic">E<u>1</u></span>
        <div class="ping-body">
          <p><b>@lunaris<u>2</u></b> pinged you <em>#emberline<u>3</u></em></p>
          <p class="ping-msg">you up for a game after this?</p>
        </div>
        <span class="ping-line"><i></i><u>4</u></span>
      </div>
    </div>
    <ol class="anatomy-list">
      <li><b>1</b><div><h3>Whose chat it was</h3><p>The channel's profile picture, so you know where to look.</p></div></li>
      <li><b>2</b><div><h3>Who pinged you</h3><p>Their name, plus what they said right underneath.</p></div></li>
      <li><b>3</b><div><h3>Which channel</h3><p>A tag that's easy to spot with lots of tabs open.</p></div></li>
      <li><b>4</b><div><h3>How long it stays</h3><p>A red line drains away, then the card fades out by itself.</p></div></li>
    </ol>
  </div>

  <div class="sec-title reveal"><h2>How the cards <em>behave.</em></h2></div>
  <div class="bento">
    <article class="card glass bento-wide reveal">
      <div class="bento-visual"><div class="ping-card ping-still ping-count"><span class="ping-pic">E</span><div class="ping-body"><p><b>@lunaris</b> pinged you <span class="count" id="ping-count">&times;2</span></p><p class="ping-msg">hello??</p></div><span class="ping-line"><i></i></span></div></div>
      <h3>No pile-ups</h3><p>Repeat pings from the same person stack into one card with a count.</p>
    </article>
    <article class="card glass reveal">
      <div class="bento-visual"><span class="click-through"><svg viewBox="0 0 24 24" width="30" height="30" aria-hidden="true"><path d="M5 3l14 9-6 1.6L10 20z" fill="#f7ece7"/></svg></span></div>
      <h3>Clicks pass through</h3><p>They can't interrupt your game or your typing.</p>
    </article>
    <article class="card glass reveal">
      <div class="bento-visual bento-words"><span class="word-hit">scaryxdxd</span><span>your name</span><span class="word-hit">any word</span></div>
      <h3>Your name or your words</h3><p>Your username and any custom highlights both trigger a card.</p>
    </article>
    <article class="card glass bento-full reveal">
      <div class="bento-visual bento-stream"><span class="live-dot">Streaming</span><span class="fake-switch"></span></div>
      <h3>Private while you stream</h3><p>Turn on one setting to hide the cards whenever streamer mode is on.</p>
    </article>
  </div>
  <p class="note center reveal">Exclusive fullscreen games can hide the screen card. Borderless or windowed mode shows it.</p>
</section>'''
page("pings", "Pings", "Pings show in your chat and on top of your game, then fade away.", pings, current="pings")

modding = page_head("Mod without <em>leaving</em> the chat box.",
                    "Game changes, predictions, mod presets and mod history are next to the chat box.") + '''
<section class="shell">
  <div class="split reveal">
    <div>
      <h2>Change the game in one click.</h2>
      <p>Your most used games sit by the chat box as small buttons, and the ones you use most move to the front. Click one and it sends <code>!game fortnite</code>.</p>
      <p class="note">A bot in the channel has to support the command. If a channel uses a different command, set it in the game popup, and Solrino saves it for that channel. The bar's position, size and alignment are in settings.</p>
    </div>
    <div class="panel glass demo-games"><span class="chip chip-big" title="Sends !game valorant">Valorant</span><span class="chip chip-big" title="Sends !game minecraft">Minecraft</span><span class="chip chip-big" title="Sends !game fortnite">Fortnite</span><span class="chip chip-big" title="Sends !game just chatting">Just Chatting</span></div>
  </div>
  <div class="split split-flip reveal">
    <div>
      <h2>Show Discord what you're up to.</h2>
      <p>Your Discord profile shows the chat you're in, and whether you're modding or chatting. Press the sword button here to switch between the two.</p>
      <p class="note">You can turn this off, or hide the chat name, in settings.</p>
    </div>
    <div class="panel glass discord-demo">
      <button class="sword" id="sword" aria-pressed="false" aria-label="Moderation mode">&#9876;</button>
      <div class="discord">
        <span class="discord-playing">Watching</span>
        <div class="discord-row">
          <img src="/assets/icon.png" alt="" width="52" height="52">
          <div><b>Solrino</b><span>In #emberline's chat</span><span id="discord-state">Chatting</span><span class="discord-time" id="discord-time">00:21</span></div>
        </div>
      </div>
    </div>
  </div>
  <div class="sec-title reveal"><h2>Other mod <em>tools.</em></h2></div>
  <div class="cards three">
    <article class="card glass reveal"><h3>Polls and predictions</h3><p>Start and end them from the chat box. Your most used predictions for each channel are one click away.</p></article>
    <article class="card glass reveal"><h3>Mod presets</h3><p>Save the timeouts and bans you use most, one click from any message.</p></article>
    <article class="card glass reveal"><h3>Mod history with undo</h3><p>See your recent mod actions and undo a mistake.</p></article>
    <article class="card glass reveal"><h3>Focus view</h3><p>Cut chat down to what you need while it's moving fast.</p></article>
    <article class="card glass reveal"><h3>Saved messages</h3><p>Keep messages you'll want to come back to, plus quick phrases you send often.</p></article>
    <article class="card glass reveal"><h3>Command palette</h3><p>Press <kbd>Ctrl</kbd> <kbd>Shift</kbd> <kbd>P</kbd> to open any tool by name.</p></article>
  </div>
</section>'''
page("modding", "Modding", "Change the game in one click, show Discord you're modding, and keep your tools by the chat box.", modding, current="modding")

grid = "\n".join(f'''    <li class="badge-card glass reveal" id="{b[0].replace("_", "-")}" data-badge="{b[0]}" data-group="{badge_group(b[0])}"><button type="button" class="badge-open" aria-label="View the {b[1]} badge"></button><img src="/assets/badges/{b[0]}.webp" alt="" width="144" height="144" loading="lazy"><b>{b[1]}</b><span>{b[2]}</span></li>''' for b in BADGES)
group_chips = "".join(f'<button type="button" class="filter" data-filter="{key}" aria-pressed="false">{label}</button>' for key, label in GROUPS)
badges = page_head("Badges everyone on Solrino <em>can see.</em>",
                   "Forty-one animated stars, and what each one is for. Click one to see it up close.") + f'''
<section class="shell">
  <div class="toolbar" role="search">
    <label class="search"><span class="sr">Search badges</span>
      <svg viewBox="0 0 24 24" width="16" height="16" aria-hidden="true"><circle cx="11" cy="11" r="6.5" fill="none" stroke="currentColor" stroke-width="2"/><path d="m16 16 4 4" stroke="currentColor" stroke-width="2" stroke-linecap="round"/></svg>
      <input type="search" id="badge-search" placeholder="Search badges" autocomplete="off"><kbd aria-hidden="true">/</kbd>
    </label>
    <div class="filters" aria-label="Show a group of badges"><button type="button" class="filter" data-filter="all" aria-pressed="true">All</button>{group_chips}</div>
    <p class="toolbar-count" id="badge-count" aria-live="polite"></p>
  </div>
  <p class="empty" id="badge-empty" hidden>No badges match that. <button type="button" class="link-btn" id="badge-reset">Show all badges</button></p>
  <ul class="badge-grid" id="badge-grid">
{grid}
  </ul>
  <div class="sec-title reveal"><h2>How it <em>works.</em></h2></div>
  <ol class="steps">
    <li class="card glass reveal"><span class="step">1</span><h3>Get a badge</h3><p>Most badges come from Solrino's owner and can't be claimed or bought. Event badges are free for everyone.</p></li>
    <li class="card glass reveal"><span class="step">2</span><h3>Equip it</h3><p>Type <code>/vanity</code> in chat, pick your badge and save.</p></li>
    <li class="card glass reveal"><span class="step">3</span><h3>Everyone sees it</h3><p>It appears next to your name for everyone using Solrino, with no restart.</p></li>
  </ol>
  <p class="note center reveal">Badges only show inside Solrino. People on the Twitch website or other chat apps won't see them.</p>
</section>'''
page("badges", "Badges", "Forty-one animated star badges that everyone using Solrino can see.", badges, scripts=("badges",), current="badges")

updates = page_head("It keeps itself <em>up to date.</em>",
                    "When a new version is ready, a card asks whether to update now, update when you quit, or remind you later.") + f'''
<section class="shell narrow">
  <div class="toolbar" role="search" id="update-tools" hidden>
    <label class="search"><span class="sr">Search changes</span>
      <svg viewBox="0 0 24 24" width="16" height="16" aria-hidden="true"><circle cx="11" cy="11" r="6.5" fill="none" stroke="currentColor" stroke-width="2"/><path d="m16 16 4 4" stroke="currentColor" stroke-width="2" stroke-linecap="round"/></svg>
      <input type="search" id="update-search" placeholder="Search changes" autocomplete="off"><kbd aria-hidden="true">/</kbd>
    </label>
    <div class="filters" aria-label="Show a kind of change"><button type="button" class="filter" data-filter="all" aria-pressed="true">All</button><button type="button" class="filter" data-filter="new" aria-pressed="false">New</button><button type="button" class="filter" data-filter="fixed" aria-pressed="false">Fixed</button><button type="button" class="filter" data-filter="removed" aria-pressed="false">Removed</button></div>
    <p class="toolbar-count" id="update-count" aria-live="polite"></p>
  </div>
  <p class="empty" id="update-empty" hidden>No changes match that. <button type="button" class="link-btn" id="update-reset">Show everything</button></p>
  <ol class="releases" id="releases">
    <li class="release is-latest"><header class="release-head"><h2 class="release-version">Every version</h2></header><div class="release-body changes"><p>See the full list on <a href="https://github.com/bloodylungs/Solrino/releases">GitHub</a>.</p></div></li>
  </ol>
  <div class="center-action">{DOWNLOAD.format(cls="btn btn-big", label="Download for Windows")}</div>
</section>'''
page("updates", "Updates", "Everything new in Solrino, version by version.", updates, scripts=("updates",), current="updates")

lost = f'''<section class="lost">
  <img src="/assets/badges/black_hole.webp" alt="" width="144" height="144">
  <p class="lost-code">404</p>
  <h1>This page fell into a <em>black hole.</em></h1>
  <p class="lede">The address might be mistyped, or the page moved. These still work:</p>
  <div class="hero-actions">
    <a class="btn btn-big" href="/">Go home</a>
    <a class="btn btn-ghost btn-big" href="/updates">See what's new</a>
  </div>
</section>'''
page("404", "Page not found", "This page doesn't exist.", lost, scripts=())

page("terms", "Terms of Service", "The terms for using Solrino, its website and its badge service.", TERMS, scripts=())
page("privacy", "Privacy Policy", "What data Solrino uses, where it goes, and how to control it.", PRIVACY, scripts=())

# ---- Licenses: the same list as the app's About page, plus what only the setup or this site includes


def read_text(path):
    return open(path, encoding="utf-8", errors="replace").read().strip()


def license_entry(name, url, text):
    return f'''    <details class="license">
      <summary>{escape(name)}</summary>
      <p><a href="{escape(url)}">{escape(url)}</a></p>
      <pre>{escape(text)}</pre>
    </details>'''


about = open("../src/widgets/settingspages/AboutPage.cpp", encoding="utf-8").read()
listed = [(name, url, "../resources" + path.replace(":", "", 1))
          for name, url, path in re.findall(r'addLicense\(form\.getElement\(\),\s*"([^"]+)",\s*"([^"]+)",\s*":(/[^"]+)"\)', about)]
if len(listed) < 25:
    raise SystemExit("Couldn't read the license list from AboutPage.cpp")
ofl_body = read_text("../resources/fonts/Gabarito/OFL.txt")
ofl_body = ofl_body[ofl_body.index("This Font Software is licensed"):]
software = [(name, url, read_text(path)) for name, url, path in listed] + [
    ("emoji-data", "https://github.com/iamcal/emoji-data", read_text("../resources/licenses/emoji-data-source.txt")),
    ("FFmpeg", "https://ffmpeg.org", read_text("../resources/licenses/ffmpeg-source.txt") + "\n\n" + read_text("../resources/licenses/ffmpeg-LGPL-2.1.txt")),
    ("QR Code generator", "https://github.com/nayuki/QR-Code-generator", read_text("_licenses/qrcodegen.txt")),
    ("quirc", "https://github.com/dlbeer/quirc", read_text("_licenses/quirc.txt")),
    ("WebView2", "https://developer.microsoft.com/microsoft-edge/webview2/", read_text("_licenses/WebView2.txt")),
    ("Geist and Geist Mono (website)", "https://github.com/vercel/geist-font",
     "Copyright 2024 The Geist Project Authors (https://github.com/vercel/geist-font)\n\n" + ofl_body),
]
software.sort(key=lambda item: item[0].lower())
entries = "\n".join(license_entry(*item) for item in software)
licenses = f'''<article class="page legal">
  <header class="page-head">
    <h1>Licenses</h1>
  </header>
  <p>Solrino is free and open source. Its code is on <a href="https://github.com/bloodylungs/Solrino">GitHub</a>, and it includes work from the projects listed here, each under its own license.</p>

  <h2>Solrino</h2>
  <p>Solrino is a fork of Moltorino7, which builds on Chatterino7 and <a href="https://github.com/Chatterino/chatterino2">Chatterino 2</a>. It uses their MIT License:</p>
  <pre class="license-text">{escape(read_text("../LICENSE"))}</pre>

  <h2>Open source software</h2>
  <p>Open any entry to read its license.</p>
  <div class="licenses">
{entries}
  </div>

  <h2>Emoji</h2>
  <p>Emoji artwork comes from <a href="https://github.com/twitter/twemoji">Twemoji</a>, Facebook, Apple and Google, and belongs to those companies. Each is shown under its own terms.</p>
</article>'''
page("licenses", "Licenses", "The MIT License and the open source licenses for everything Solrino includes.", licenses, scripts=())
print("pages written")
