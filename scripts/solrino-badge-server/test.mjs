// Tests the badge server with no network or Cloudflare account:
//   node scripts/solrino-badge-server/test.mjs
import assert from "node:assert/strict";

const OWNER = "111";
const FRIEND = "222";
const TOKENS = { ownertoken: OWNER, friendtoken: FRIEND };
const seenByTwitch = [];

// Stands in for Twitch: only these tokens are real
globalThis.fetch = async (url, init) => {
  assert.equal(url, "https://id.twitch.tv/oauth2/validate");
  const token = init.headers.authorization.replace("OAuth ", "");
  seenByTwitch.push(token);
  const id = TOKENS[token];
  return id
    ? new Response(JSON.stringify({ user_id: id, login: "x" }), { status: 200 })
    : new Response("{}", { status: 401 });
};

// Stands in for Cloudflare's edge cache
const edge = new Map();
globalThis.caches = {
  default: {
    match: async (req) => edge.get(req.url)?.clone(),
    put: async (req, res) => void edge.set(req.url, res),
    delete: async (req) => edge.delete(req.url),
  },
};

const { default: worker, BadgeHub } = await import("./worker.js");

const store = new Map();
const notices = [];
const openApp = { send: (text) => notices.push(JSON.parse(text)) };
const goneApp = { send: () => { throw new Error("closed"); } };
const state = {
  storage: {
    get: async (key) => (store.has(key) ? structuredClone(store.get(key)) : undefined),
    put: async (key, value) => void store.set(key, structuredClone(value)),
  },
  getWebSockets: () => [goneApp, openApp],
};
const env = {
  OWNER_USER_ID: OWNER,
  HUB: { idFromName: () => "badges", get: () => hub },
};
const hub = new BadgeHub(state, env);
const ctx = { waitUntil: (p) => p };

async function call(method, path, { token, body } = {}) {
  const headers = {};
  if (token) headers.authorization = `OAuth ${token}`;
  const res = await worker.fetch(
    new Request(`https://x.test${path}`, {
      method,
      headers,
      body: body === undefined ? undefined : JSON.stringify(body),
    }),
    env,
    ctx,
  );
  return { status: res.status, body: await res.json() };
}

const equip = (badge, revision, userId = FRIEND) => ({
  userId,
  revision,
  layoutSchemaVersion: 1,
  order: ["m", "c"],
  hidden: ["fa"],
  selectedBadge: badge,
  badgeSelectionExplicit: true,
});
// What someone was given, without the badges everybody has
const given = (badges) => badges.filter((badge) => !["spooky", "winter_2026"].includes(badge));
const tests = [];
const test = (name, fn) => tests.push([name, fn]);

test("badges from the older store are carried over", async () => {
  env.BADGES = { get: async () => ({ revision: 7, users: { 555: { badges: ["founder"], revision: 0 } } }) };
  const me = await call("GET", "/v2/badges/me?userId=555");
  assert.deepEqual(given(me.body.assignedBadges), ["founder"]);
  delete env.BADGES;
});

test("the list starts valid and empty", async () => {
  const { status, body } = await call("GET", "/v2/badges");
  assert.equal(status, 200);
  assert.equal(body.schemaVersion, 2);
  assert.ok(body.generation.length > 0);
  assert.ok(body.bundleVersion >= 0);
  assert.equal(body.badges.length, 41);
  for (const badge of body.badges) {
    for (const size of ["1x", "2x", "3x", "4x"]) {
      assert.ok(badge.images[size].endsWith(`/assets/badges/sizes/${badge.id}/${size}.webp`));
    }
  }
  assert.deepEqual(body.users, {});
});

test("nobody can give badges without logging in", async () => {
  const r = await call("POST", "/v2/badges/give", { body: { userId: FRIEND, badge: "founder" } });
  assert.equal(r.status, 401);
});

test("a made-up token is refused", async () => {
  const r = await call("POST", "/v2/badges/give", { token: "faketoken", body: { userId: FRIEND, badge: "founder" } });
  assert.equal(r.status, 401);
});

test("a real user who isn't the owner can't give badges", async () => {
  const r = await call("POST", "/v2/badges/give", { token: "friendtoken", body: { userId: FRIEND, badge: "founder" } });
  assert.equal(r.status, 403);
  assert.equal(r.body.error.code, "not_owner");
});

test("a non-owner can't give themselves a badge", async () => {
  const r = await call("POST", "/v2/badges/give", { token: "friendtoken", body: { userId: FRIEND, badge: "obsidian" } });
  assert.equal(r.status, 403);
  const me = await call("GET", `/v2/badges/me?userId=${FRIEND}`);
  assert.deepEqual(given(me.body.assignedBadges), []);
});

test("a non-owner can't take someone's badge", async () => {
  const r = await call("POST", "/v2/badges/take", { token: "friendtoken", body: { userId: OWNER, badge: "founder" } });
  assert.equal(r.status, 403);
});

test("claiming to be the owner in the request doesn't work", async () => {
  const r = await call("POST", "/v2/badges/give", { token: "friendtoken", body: { userId: FRIEND, badge: "obsidian", ownerId: OWNER, owner: true, callerId: OWNER } });
  assert.equal(r.status, 403);
});

test("saving a layout can't add badges", async () => {
  const body = { ...equip("", 0), badges: ["obsidian", "founder"], assignedBadges: ["obsidian"] };
  const r = await call("PUT", "/v2/badges/me", { token: "friendtoken", body });
  assert.equal(r.status, 200);
  assert.deepEqual(given(r.body.assignedBadges), []);
  store.get("state").users[FRIEND].revision = 0;
});

test("a badge that doesn't exist can't be given", async () => {
  const r = await call("POST", "/v2/badges/give", { token: "ownertoken", body: { userId: FRIEND, badge: "nope" } });
  assert.equal(r.status, 400);
});

test("you can't equip a badge you weren't given", async () => {
  const r = await call("PUT", "/v2/badges/me", { token: "friendtoken", body: equip("founder", 0) });
  assert.equal(r.status, 422);
  assert.equal(r.body.error.code, "badge_not_owned");
});

test("the owner gives a badge, and the newest list has it", async () => {
  await call("GET", "/v2/badges"); // Fills the cache
  const r = await call("POST", "/v2/badges/give", { token: "ownertoken", body: { userId: FRIEND, badge: "founder" } });
  assert.equal(r.status, 200);
  assert.deepEqual(given(r.body.assignedBadges), ["founder"]);
  const list = await call("GET", "/v2/badges?refresh=1");
  assert.deepEqual(list.body.users[FRIEND].badges, ["founder"]);
});

test("every open app is told about a change, even if one has closed", async () => {
  notices.length = 0;
  await call("POST", "/v2/badges/give", { token: "ownertoken", body: { userId: OWNER, badge: "supporter" } });
  assert.equal(notices.length, 1);
  assert.equal(notices[0].type, "badge_v2_changed");
  const list = await call("GET", "/v2/badges?refresh=3");
  assert.equal(notices[0].generation, list.body.generation);
  assert.equal(notices[0].version, list.body.bundleVersion);
});

test("a failed attempt tells nobody", async () => {
  notices.length = 0;
  await call("POST", "/v2/badges/give", { token: "friendtoken", body: { userId: FRIEND, badge: "founder" } });
  assert.equal(notices.length, 0);
});

test("the newest list skips the cache after a change", async () => {
  await call("GET", "/v2/badges");
  store.get("state").users["333"] = { badges: ["founder"], revision: 0 };
  const cached = await call("GET", "/v2/badges");
  assert.equal(cached.body.users["333"], undefined);
  const fresh = await call("GET", "/v2/badges?version=9&generation=x");
  assert.deepEqual(fresh.body.users["333"].badges, ["founder"]);
  delete store.get("state").users["333"];
  edge.clear();
});

test("two saves at once both count", async () => {
  await Promise.all([
    call("POST", "/v2/badges/give", { token: "ownertoken", body: { userId: "444", badge: "founder" } }),
  ]);
  const me = await call("GET", "/v2/badges/me?userId=444");
  assert.deepEqual(given(me.body.assignedBadges), ["founder"]);
});

test("giving twice doesn't duplicate it", async () => {
  await call("POST", "/v2/badges/give", { token: "ownertoken", body: { userId: FRIEND, badge: "founder" } });
  const me = await call("GET", `/v2/badges/me?userId=${FRIEND}`);
  assert.deepEqual(given(me.body.assignedBadges), ["founder"]);
});

test("the app's profile check passes", async () => {
  const me = await call("GET", `/v2/badges/me?userId=${FRIEND}`);
  assert.equal(me.body.userId, FRIEND);
  assert.equal(me.body.profile.layoutSchemaVersion, 1);
  assert.equal(me.body.profile.revision, 0);
});

test("you can equip a badge you were given, and others see it", async () => {
  const r = await call("PUT", "/v2/badges/me", { token: "friendtoken", body: equip("founder", 0) });
  assert.equal(r.status, 200);
  assert.equal(r.body.profile.selectedBadge, "founder");
  assert.equal(r.body.profile.badgeSelectionExplicit, true);
  assert.deepEqual(r.body.profile.order, ["m", "c"]);
  assert.equal(r.body.profile.revision, 1);
  const list = await call("GET", "/v2/badges?refresh=2");
  assert.equal(list.body.users[FRIEND].activeBadge, "founder");
  assert.deepEqual(list.body.users[FRIEND].hidden, ["fa"]);
});

test("you can't change someone else's badges", async () => {
  const r = await call("PUT", "/v2/badges/me", { token: "ownertoken", body: equip("", 1, FRIEND) });
  assert.equal(r.status, 403);
  assert.equal(r.body.error.code, "wrong_user");
});

test("an out-of-date save is refused with the current state", async () => {
  const r = await call("PUT", "/v2/badges/me", { token: "friendtoken", body: equip("founder", 0) });
  assert.equal(r.status, 409);
  assert.equal(r.body.error.currentProfile.revision, 1);
});

test("taking an equipped badge also unequips it", async () => {
  const r = await call("POST", "/v2/badges/take", { token: "ownertoken", body: { userId: FRIEND, badge: "founder" } });
  assert.equal(r.status, 200);
  const me = await call("GET", `/v2/badges/me?userId=${FRIEND}`);
  assert.deepEqual(given(me.body.assignedBadges), []);
  assert.equal(me.body.profile.selectedBadge, "");
});

test("bad input is refused", async () => {
  const tooMany = { ...equip("", 1), order: Array(65).fill("x") };
  assert.equal((await call("PUT", "/v2/badges/me", { token: "friendtoken", body: tooMany })).status, 400);
  assert.equal((await call("GET", "/v2/badges/me?userId=abc")).status, 400);
  assert.equal((await call("GET", "/nothing")).status, 404);
});

const realNow = Date.now;
const atDate = (text) => {
  Date.now = () => Date.parse(text);
};

test("before December 1st, everyone has Spooky but not Winter", async () => {
  atDate("2026-11-30T12:00:00Z");
  const { body } = await call("GET", "/v2/badges/me?userId=999");
  assert.ok(body.assignedBadges.includes("spooky"));
  assert.ok(!body.assignedBadges.includes("winter_2026"));
  Date.now = realNow;
});

test("from December 1st, everyone has Winter too", async () => {
  atDate("2026-12-01T05:00:00Z");
  const { body } = await call("GET", "/v2/badges/me?userId=999");
  assert.ok(body.assignedBadges.includes("winter_2026"));
  Date.now = realNow;
});

test("Winter can't be worn before its date, and can after", async () => {
  const save = (revision) => ({
    userId: FRIEND,
    revision,
    selectedBadge: "winter_2026",
    badgeSelectionExplicit: true,
    order: [],
    hidden: [],
  });
  const before = await call("GET", `/v2/badges/me?userId=${FRIEND}`);
  atDate("2026-11-30T12:00:00Z");
  const early = await call("PUT", "/v2/badges/me", { token: "friendtoken", body: save(before.body.profile.revision) });
  assert.equal(early.status, 422);
  atDate("2026-12-02T12:00:00Z");
  const late = await call("PUT", "/v2/badges/me", { token: "friendtoken", body: save(before.body.profile.revision) });
  assert.equal(late.status, 200);
  const list = await call("GET", "/v2/badges?refresh=1");
  assert.ok(list.body.users[FRIEND].badges.includes("winter_2026"), "others can see it once worn");
  Date.now = realNow;
});

test("badges for everyone aren't put on anyone who didn't choose them", async () => {
  await call("POST", "/v2/badges/give", { token: "ownertoken", body: { userId: "333", badge: "special" } });
  const list = await call("GET", "/v2/badges?refresh=1");
  assert.deepEqual(list.body.users["333"].badges, ["special"]);
});

test("the list says when badges for everyone come out", async () => {
  const list = await call("GET", "/v2/badges?refresh=1");
  const winter = list.body.badges.find((badge) => badge.id === "winter_2026");
  assert.equal(winter.everyoneFrom, "2026-12-01T05:00:00Z");
  assert.equal(list.body.badges.find((badge) => badge.id === "special").everyoneFrom, undefined);
});

test("only one person can hold the #1 Donor badge", async () => {
  await call("POST", "/v2/badges/give", { token: "ownertoken", body: { userId: "444", badge: "number_one_donor" } });
  const profile = await call("GET", "/v2/badges/me?userId=444");
  assert.ok(profile.body.assignedBadges.includes("number_one_donor"));
  await call("POST", "/v2/badges/give", { token: "ownertoken", body: { userId: "555", badge: "number_one_donor" } });
  const earlier = await call("GET", "/v2/badges/me?userId=444");
  const current = await call("GET", "/v2/badges/me?userId=555");
  assert.ok(!earlier.body.assignedBadges.includes("number_one_donor"));
  assert.ok(current.body.assignedBadges.includes("number_one_donor"));
});

test("only one person can hold the Donor of the Month badge", async () => {
  await call("POST", "/v2/badges/give", { token: "ownertoken", body: { userId: "444", badge: "donor_of_the_month" } });
  await call("POST", "/v2/badges/give", { token: "ownertoken", body: { userId: "555", badge: "donor_of_the_month" } });
  const earlier = await call("GET", "/v2/badges/me?userId=444");
  const current = await call("GET", "/v2/badges/me?userId=555");
  assert.ok(!earlier.body.assignedBadges.includes("donor_of_the_month"));
  assert.ok(current.body.assignedBadges.includes("donor_of_the_month"));
});

test("the badges only one person holds are the right ones", async () => {
  const list = await call("GET", "/v2/badges?refresh=1");
  const only = list.body.badges.filter((badge) => badge.onlyOne).map((badge) => badge.id).sort();
  assert.deepEqual(only, ["donor_of_the_month", "donor_of_the_year", "first_donor", "number_one_donor", "top_bug_hunter"]);
});

test("login tokens are never saved", async () => {
  const saved = JSON.stringify([...store.values()]);
  for (const token of Object.keys(TOKENS)) assert.ok(!saved.includes(token));
  assert.ok(seenByTwitch.includes("friendtoken"));
});

let failed = 0;
for (const [name, fn] of tests) {
  try {
    await fn();
    console.log(`ok   ${name}`);
  } catch (error) {
    failed += 1;
    console.log(`FAIL ${name}\n     ${error.message}`);
  }
}
console.log(`${tests.length - failed} of ${tests.length} passed`);
process.exit(failed ? 1 : 0);
