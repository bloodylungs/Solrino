// Solrino's badge server. One Durable Object holds who has which badge, so saves
// can't overwrite each other, and tells every open Solrino the moment it changes.
//
//   GET  /v2/badges              Everyone's badges, read by every Solrino.
//   GET  /v2/badges/me?userId=   One person's badges and layout.
//   PUT  /v2/badges/me           Saves your own layout and equipped badge.
//   PUT  /v2/paints/me           Wears one of Solrino's username paints.
//   POST /v2/badges/give         The owner gives someone a badge.
//   POST /v2/badges/take         The owner takes a badge away.
//   GET  /v2/badges/socket       A live connection announcing each change.
//   POST /v2/announcements       The owner announces something to everyone.
//
// Saving and giving prove who you are with your Twitch login token. It's only
// passed to Twitch to check it, and is never stored or logged.
//
// A badge with everyoneFrom belongs to everybody from that moment. It isn't put
// on anyone's name until they choose to wear it. A badge with onlyOne can only
// be held by one person: giving it takes it from whoever had it.

const LIST_CACHE_SECONDS = 60;
const MAX_BODY_BYTES = 16 * 1024;
const MAX_KEYS = 64;
const MAX_KEY_LENGTH = 64;
const ANNOUNCEMENT_MAX = 300;
const STATE_KEY = "state";
const HUB_NAME = "badges";
const USER_ID = /^[0-9]{1,20}$/;
const IMAGE_BASE = "https://solrino.solrino-badge-server.workers.dev/assets/badges/sizes";

const DEFAULT_ORDER = ["ta", "ts", "tv", "tp", "c", "ff", "fa", "bt", "bl", "jc", "bh", "m", "7", "hc", "hs"];

const BADGES = [
  ["developer", "Solrino Developer", "Helps build Solrino.", false],
  ["founder", "Solrino Founder", "Founded Solrino.", true],
  ["staff", "Solrino Staff", "Part of the Solrino team.", true],
  ["moderator", "Solrino Moderator", "Helps moderate the Solrino community.", true],
  ["contributor", "Solrino Contributor", "Has contributed to Solrino.", true],
  ["top_bug_hunter", "Solrino Top Bug Hunter", "Has reported the most bugs in Solrino.", true, { onlyOne: true }],
  ["alpha_tester", "Solrino Alpha Tester", "Tries the earliest, roughest Solrino builds.", true],
  ["beta_tester", "Solrino Beta Tester", "Tries Solrino updates before everyone else.", true],
  ["number_one_donor", "Solrino #1 Donor", "The person who has donated the most to Solrino.", true, { onlyOne: true }],
  ["donor_of_the_year", "Solrino Donor of the Year", "This year's top Solrino donor.", true, { onlyOne: true }],
  ["donor_of_the_month", "Solrino Donor of the Month", "This month's top Solrino donor.", true, { onlyOne: true }],
  ["first_donor", "Solrino First Donor", "The very first person to donate to Solrino.", true, { onlyOne: true }],
  ["top_donor", "Solrino Top Donor", "One of Solrino's top donors.", true],
  ["top_donor_alt", "Solrino Top Donor Alt", "One of Solrino's top donors, in a different style.", true],
  ["supporter_plus_plus_plus_alt", "Solrino Supporter+++ Alt", "Solrino's highest supporter tier, in a different style.", true],
  ["supporter_plus_plus_plus", "Solrino Supporter+++", "Solrino's highest supporter tier.", true],
  ["obsidian", "Solrino Early Supporter", "Supported Solrino in its early days.", true],
  ["veteran", "Solrino Veteran", "One of Solrino's longest-running users.", true],
  ["one_year_on_solrino", "Solrino One Year on Solrino", "Has been on Solrino for a full year.", true],
  ["supporter_plus_plus_alt", "Solrino Supporter++ Alt", "A Supporter++ on Solrino, in a different style.", true],
  ["supporter_plus_plus", "Solrino Supporter++", "One of Solrino's bigger supporters.", true],
  ["supporter_plus_alt", "Solrino Supporter+ Alt", "A Supporter+ on Solrino, in a different style.", true],
  ["supporter_plus", "Solrino Supporter+", "Gives extra support to Solrino.", true],
  ["supporter", "Solrino Supporter", "Supports Solrino.", true],
  ["black_hole", "Solrino Black Hole", "A very rare badge from Solrino's owner.", true],
  ["bug_hunter", "Solrino Bug Hunter", "Finds and reports bugs in Solrino.", true],
  ["clockwork", "Solrino Clockwork", "A rare badge from Solrino's owner.", true],
  ["demon", "Solrino Demon", "A rare badge from Solrino's owner.", true],
  ["friend_of_solrino", "Friend of Solrino", "A friend of Solrino.", true],
  ["halo", "Solrino Halo", "A rare badge from Solrino's owner.", true],
  ["kaleidoscope", "Solrino Kaleidoscope", "A rare badge from Solrino's owner.", true],
  ["katana", "Solrino Katana", "A rare badge from Solrino's owner.", true],
  ["lava_lamp", "Solrino Lava Lamp", "A rare badge from Solrino's owner.", true],
  ["matrix_rain", "Solrino Matrix Rain", "A rare badge from Solrino's owner.", true],
  ["nebula", "Solrino Nebula", "A rare badge from Solrino's owner.", true],
  ["oil_slick", "Solrino Oil Slick", "A rare badge from Solrino's owner.", true],
  ["plasma_ball", "Solrino Plasma Ball", "A rare badge from Solrino's owner.", true],
  ["special", "Solrino Special", "Someone who is special to Solrino.", true],
  ["spooky", "Solrino Spooky 2026", "Free for everyone for Halloween 2026.", true, { everyoneFrom: "2026-11-01T00:00:00Z" }],
  ["valentines_2027", "Solrino Valentine's 2027", "Given out for Valentine's 2027.", true],
  // Midnight on December 1st, US Eastern time
  ["winter_2026", "Solrino Winter 2026", "Free for everyone from December 1st, 2026.", true, { everyoneFrom: "2026-12-01T05:00:00Z" }],
].map(([id, tooltip, description, listed, extra], index) => ({
  ...extra,
  id,
  tooltip,
  description,
  listed,
  priority: (index + 1) * 10,
  assetVersion: 1,
  // Solrino bundles the artwork, and other apps showing these badges use these
  images: Object.fromEntries(
    ["1x", "2x", "3x", "4x"].map((size) => [size, `${IMAGE_BASE}/${id}/${size}.webp`]),
  ),
}));
const BADGE_IDS = new Set(BADGES.map((badge) => badge.id));

// Username paints anyone can wear. The app draws them itself.
const PAINT_IDS = new Set([
  "sunset", "deep_ocean", "rose_gold", "chrome", "toxic", "candy_cane", "galaxy",
  "cotton_candy", "blood_moon", "rainbow_flow", "aurora", "ember", "gold_shimmer",
  "neon_pulse", "frostbite", "witching_hour", "peach_fuzz", "lavender_haze",
  "matcha_latte", "pink_lemonade", "tropical", "mermaid", "dragonfruit", "sakura_petal",
  "midnight_city", "vaporwave", "bubblegum_split", "sunrise_split", "holographic",
  "copper", "emerald", "sapphire", "amethyst", "ruby", "pearl", "barbershop", "bumblebee",
  "watermelon", "arctic", "lava", "forest_spirit", "cherry_cola", "glitter", "starlight",
  "disco_ball", "ocean_wave", "lava_lamp", "toxic_waste", "glitch", "siren", "heartbeat",
  "mainframe", "prism", "liquid_chrome", "holo_flow", "candy_swirl", "barber_pole",
  "caution_tape", "sunset_drift", "thunderstorm", "plasma", "phantom", "bubblegum_pop",
  "sea_glass", "fireflies", "snowfall", "rgb_gamer", "rose_shimmer", "diamond", "inferno",
  "galaxy_swirl", "string_lights", "fireworks", "confetti", "cyber_scan", "breathing",
  "heat_wave", "magma_rise", "rainfall", "tide_pool", "oil_slick", "tie_dye", "neon_sign",
  "traffic_light", "campfire", "will_o_wisp", "spotlight", "equalizer", "hypnotize",
  "zebra_run", "peppermint_twist", "spectrum_stripes", "day_and_night", "mood_ring",
  "chameleon", "tv_static", "arcade", "soda_fizz", "static_shock", "comet",
  "bioluminescence", "sakura_breeze", "supernova", "ripple", "acid_trip", "champagne",
  "nebula_drift", "candlelight", "bleeding", "freeze", "charging", "kaleidoscope",
  "fire_and_ice", "candy_floss_spin", "pulsar", "witch_cauldron", "vinyl", "portal", "hex",
  "lunar_lantern", "anodized", "peacock", "prismatic", "royale", "reflection", "catseye",
  "celestial", "eclipse", "ultraviolet", "sunflower", "magma_pool", "abyss", "luminary",
  "nightbloom", "psychic", "tinsel", "honey_drip", "slushie", "jellybean", "frutiger_aero",
  "mana_potion", "sixties", "wavy", "whisper", "blizzard", "nightclub", "clownfish",
  "camouflage", "film_reel", "copper_patina", "spellcaster", "retrovision", "crystalline",
  "thermal", "biohazard", "outer_space", "warp_speed", "maple_syrup", "warm_nights",
  "dragon_scale", "fairy_glow", "puddle", "eruption", "moonlight", "twilight",
  "glow_stick", "coral_reef",
]);

function json(body, status = 200, headers = {}) {
  return new Response(JSON.stringify(body), {
    status,
    headers: { "content-type": "application/json; charset=utf-8", ...headers },
  });
}

function fail(status, code, message, extra = {}) {
  return json({ error: { code, message, ...extra } }, status);
}

/** Badges that belong to everybody right now. */
function everyoneBadges() {
  const now = Date.now();
  return BADGES.filter((badge) => badge.everyoneFrom && Date.parse(badge.everyoneFrom) <= now).map(
    (badge) => badge.id,
  );
}

/** Everything someone may wear: what they were given, then what everybody has. */
function ownedBadges(user) {
  return [...new Set([...(user?.badges ?? []), ...everyoneBadges()])];
}

function userEntry(state, userId) {
  state.users[userId] ??= { badges: [], revision: 0 };
  return state.users[userId];
}

function profileOf(user) {
  return {
    layoutSchemaVersion: 1,
    revision: user?.revision ?? 0,
    order: user?.order ?? [],
    hidden: user?.hidden ?? [],
    selectedBadge: user?.activeBadge ?? "",
    badgeSelectionExplicit: user?.explicit ?? false,
  };
}

function listBody(state) {
  const users = {};
  const forEveryone = everyoneBadges();
  for (const [id, user] of Object.entries(state.users)) {
    // Apps show the first badge of anyone who hasn't chosen, so badges everybody
    // has are only listed for those who chose to wear one
    const worn =
      forEveryone.includes(user.activeBadge) && !user.badges.includes(user.activeBadge);
    const entry = { badges: worn ? [...user.badges, user.activeBadge] : user.badges };
    if (user.order?.length) entry.order = user.order;
    if (user.hidden?.length) entry.hidden = user.hidden;
    if (user.explicit) entry.activeBadge = user.activeBadge ?? "";
    // Chat names are logins, so paints are listed with one
    if (user.paint && user.login) {
      entry.paint = user.paint;
      entry.login = user.login;
    }
    users[id] = entry;
  }
  return {
    schemaVersion: 2,
    generation: `solrino-${state.revision}`,
    bundleVersion: state.revision,
    layout: { schemaVersion: 1, defaultOrder: DEFAULT_ORDER },
    badges: BADGES,
    // The owner's latest announcement, shown once in every app
    ...(state.announcement ? { announcement: state.announcement } : {}),
    users,
  };
}

/** Asks Twitch who a login token belongs to. Returns null if it isn't valid. */
async function verifiedUserId(request) {
  return (await verifiedUser(request))?.userId ?? null;
}

/** Like verifiedUserId, but also gives the Twitch login. */
async function verifiedUser(request) {
  const header = request.headers.get("authorization") ?? "";
  const match = header.match(/^OAuth ([A-Za-z0-9]{1,256})$/);
  if (!match) return null;
  const reply = await fetch("https://id.twitch.tv/oauth2/validate", {
    headers: { authorization: `OAuth ${match[1]}` },
  });
  if (!reply.ok) return null;
  const body = await reply.json();
  if (typeof body.user_id !== "string" || !USER_ID.test(body.user_id)) return null;
  const login = typeof body.login === "string" ? body.login.toLowerCase() : "";
  return { userId: body.user_id, login };
}

async function readBody(request) {
  const text = await request.text();
  if (text.length > MAX_BODY_BYTES) return null;
  try {
    const body = JSON.parse(text);
    return body && typeof body === "object" && !Array.isArray(body) ? body : null;
  } catch {
    return null;
  }
}

function cleanKeys(value) {
  if (value === undefined) return [];
  if (!Array.isArray(value) || value.length > MAX_KEYS) return null;
  const keys = [];
  for (const key of value) {
    if (typeof key !== "string" || key.length > MAX_KEY_LENGTH) return null;
    keys.push(key);
  }
  return keys;
}

function listCacheKey(request) {
  return new Request(new URL("/v2/badges", request.url).toString());
}

async function getList(request, env, ctx) {
  const url = new URL(request.url);
  const hub = env.HUB.get(env.HUB.idFromName(HUB_NAME));
  // The app adds these after hearing about a change, and needs the newest list
  if (url.searchParams.has("generation") || url.searchParams.has("refresh")) {
    return hub.fetch(request);
  }
  const key = listCacheKey(request);
  const cached = await caches.default.match(key);
  if (cached) return cached;
  const response = await hub.fetch(request);
  const shared = new Response(response.body, response);
  shared.headers.set("cache-control", `public, max-age=${LIST_CACHE_SECONDS}`);
  // Browser extensions showing these badges read the list from other sites
  shared.headers.set("access-control-allow-origin", "*");
  ctx.waitUntil(caches.default.put(key, shared.clone()));
  return shared;
}

export class BadgeHub {
  constructor(state, env) {
    this.state = state;
    this.env = env;
    // Answers keep-alive pings without waking this up
    if (typeof WebSocketRequestResponsePair !== "undefined") {
      state.setWebSocketAutoResponse(new WebSocketRequestResponsePair("ping", "pong"));
    }
  }

  async readState() {
    const stored = await this.state.storage.get(STATE_KEY);
    if (stored) return stored;
    // Badges given before this server held them itself are carried over once
    const earlier = await this.env.BADGES?.get(STATE_KEY, "json");
    return earlier ?? { revision: 1, users: {} };
  }

  async writeState(state) {
    state.revision += 1;
    await this.state.storage.put(STATE_KEY, state);
    const notice = JSON.stringify({
      type: "badge_v2_changed",
      generation: `solrino-${state.revision}`,
      version: state.revision,
    });
    for (const socket of this.state.getWebSockets()) {
      try {
        socket.send(notice);
      } catch {
        // That app has gone away
      }
    }
  }

  openSocket(request) {
    if (request.headers.get("upgrade")?.toLowerCase() !== "websocket") {
      return fail(426, "needs_websocket", "This address needs a WebSocket connection.");
    }
    const pair = new WebSocketPair();
    this.state.acceptWebSocket(pair[1]);
    return new Response(null, { status: 101, webSocket: pair[0] });
  }

  async getMe(url) {
    const userId = url.searchParams.get("userId") ?? "";
    if (!USER_ID.test(userId)) return fail(400, "bad_user", "That isn't a valid user ID.");
    const user = (await this.readState()).users[userId];
    return json({ userId, profile: profileOf(user), assignedBadges: ownedBadges(user) });
  }

  async putMe(request) {
    const caller = await verifiedUserId(request);
    if (!caller) return fail(401, "not_verified", "Twitch didn't confirm this login.");

    const body = await readBody(request);
    if (!body) return fail(400, "bad_request", "That request couldn't be read.");
    if (body.userId !== caller) return fail(403, "wrong_user", "You can only change your own badges.");

    const order = cleanKeys(body.order);
    const hidden = cleanKeys(body.hidden);
    const selected = typeof body.selectedBadge === "string" ? body.selectedBadge.trim().toLowerCase() : "";
    if (!order || !hidden) return fail(400, "bad_request", "That layout isn't valid.");

    const state = await this.readState();
    const user = userEntry(state, caller);
    if (Number.isInteger(body.revision) && body.revision !== user.revision) {
      return fail(409, "changed_elsewhere", "These badges were changed somewhere else.", {
        currentProfile: profileOf(user),
      });
    }
    // Nobody can equip a badge they weren't given
    if (selected && !ownedBadges(user).includes(selected)) {
      return fail(422, "badge_not_owned", "You haven't been given that badge.");
    }

    user.order = order;
    user.hidden = hidden;
    user.activeBadge = selected;
    user.explicit = body.badgeSelectionExplicit === true;
    user.revision += 1;
    await this.writeState(state);
    return json({ userId: caller, profile: profileOf(user), assignedBadges: ownedBadges(user) });
  }

  async putPaint(request) {
    const caller = await verifiedUser(request);
    if (!caller) return fail(401, "not_verified", "Twitch didn't confirm this login.");

    const body = await readBody(request);
    if (!body) return fail(400, "bad_request", "That request couldn't be read.");
    if (body.userId !== caller.userId) return fail(403, "wrong_user", "You can only change your own paint.");
    const paint = typeof body.paint === "string" ? body.paint.trim().toLowerCase() : "";
    if (paint && !PAINT_IDS.has(paint)) return fail(400, "bad_paint", "That paint doesn't exist.");

    const state = await this.readState();
    const user = userEntry(state, caller.userId);
    user.paint = paint;
    user.login = caller.login;
    await this.writeState(state);
    return json({ userId: caller.userId, paint });
  }

  async announce(request) {
    const caller = await verifiedUserId(request);
    if (!caller) return fail(401, "not_verified", "Twitch didn't confirm this login.");
    if (caller !== this.env.OWNER_USER_ID) return fail(403, "not_owner", "Only Solrino's owner can do that.");

    const body = await readBody(request);
    if (!body || typeof body.message !== "string") {
      return fail(400, "bad_request", "That request couldn't be read.");
    }
    const message = body.message.trim();
    if (message.length > ANNOUNCEMENT_MAX) {
      return fail(400, "too_long", `Announcements can be up to ${ANNOUNCEMENT_MAX} characters.`);
    }

    const state = await this.readState();
    // An empty message takes the current announcement down
    state.announcement = message
      ? { id: `a${state.revision + 1}`, message, at: new Date().toISOString() }
      : null;
    await this.writeState(state);
    return json({ announcement: state.announcement });
  }

  async changeBadge(request, giving) {
    const caller = await verifiedUserId(request);
    if (!caller) return fail(401, "not_verified", "Twitch didn't confirm this login.");
    if (caller !== this.env.OWNER_USER_ID) return fail(403, "not_owner", "Only Solrino's owner can do that.");

    const body = await readBody(request);
    if (!body || typeof body.userId !== "string" || !USER_ID.test(body.userId)) {
      return fail(400, "bad_user", "That isn't a valid user ID.");
    }
    if (!BADGE_IDS.has(body.badge)) return fail(400, "bad_badge", "That badge doesn't exist.");

    const state = await this.readState();
    const user = userEntry(state, body.userId);
    if (giving) {
      if (BADGES.find((badge) => badge.id === body.badge).onlyOne) {
        for (const other of Object.values(state.users)) {
          if (other === user) continue;
          other.badges = other.badges.filter((badge) => badge !== body.badge);
          if (other.activeBadge === body.badge) {
            other.activeBadge = "";
            other.explicit = false;
          }
        }
      }
      if (!user.badges.includes(body.badge)) user.badges.push(body.badge);
    } else {
      user.badges = user.badges.filter((badge) => badge !== body.badge);
      if (user.activeBadge === body.badge) {
        user.activeBadge = "";
        user.explicit = false;
      }
    }
    await this.writeState(state);
    return json({ userId: body.userId, assignedBadges: user.badges });
  }

  async fetch(request) {
    const url = new URL(request.url);
    switch (`${request.method} ${url.pathname}`) {
      case "GET /v2/badges":
        return json(listBody(await this.readState()));
      case "GET /v2/badges/me":
        return this.getMe(url);
      case "GET /v2/badges/socket":
        return this.openSocket(request);
      case "PUT /v2/badges/me":
        return this.putMe(request);
      case "PUT /v2/paints/me":
        return this.putPaint(request);
      case "POST /v2/announcements":
        return this.announce(request);
      case "POST /v2/badges/give":
        return this.changeBadge(request, true);
      case "POST /v2/badges/take":
        return this.changeBadge(request, false);
      default:
        return fail(404, "not_found", "There's nothing here.");
    }
  }
}

export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    if (request.method === "GET" && url.pathname === "/v2/badges") {
      return getList(request, env, ctx);
    }
    return env.HUB.get(env.HUB.idFromName(HUB_NAME)).fetch(request);
  },
};
