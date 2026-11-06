(() => {
  const reduced = matchMedia("(prefers-reduced-motion: reduce)").matches;
  const pick = (list) => list[Math.floor(Math.random() * list.length)];
  const chat = document.getElementById("chat");
  const chatPings = document.getElementById("chat-pings");
  const screenPings = document.getElementById("screen-pings");

  // ---- Made-up chat ----
  const PEOPLE = [
    ["lunaris", "#ff7a6b", "founder"], ["tinkerbolt", "#7fd4ff", "supporter_plus"],
    ["mossy_owl", "#9be88a", null], ["sableheart", "#d9a6ff", "top_donor"],
    ["pixelplum", "#ffb454", "supporter"], ["quietfox", "#ff9ed2", null],
    ["rookie_rae", "#8ab6ff", null], ["ovenmitt", "#ffd166", "developer"],
  ];
  const LINES = {
    emberline: ["that last round was so close", "clip it clip it", "the chat is moving fast today", "wait what game is this", "GG", "one more?", "how did that even work", "who's winning right now", "stream looks clean today", "someone ping the mods"],
    quietroom: ["good evening everyone", "this music is really nice", "just got here, what did I miss", "taking it slow tonight", "tea break", "love this song", "how's everyone doing"],
  };
  let channel = "emberline";

  function clock() {
    return new Date().toLocaleTimeString([], { hour: "numeric", minute: "2-digit" }).replace(/\s?[AP]M/i, "");
  }

  function say(person, text, { ping = false, system = false } = {}) {
    const line = document.createElement("li");
    const stamp = document.createElement("time");
    stamp.textContent = clock();
    line.append(stamp);
    if (ping) line.className = "is-ping";
    if (system) {
      const span = document.createElement("span");
      span.className = "sys";
      span.textContent = text;
      line.append(span);
    } else {
      const [name, color, badge] = person;
      if (badge) {
        const image = new Image(18, 18);
        image.src = `/assets/badges/small/${badge}.webp`;
        image.alt = "";
        line.append(image);
      }
      const who = document.createElement("span");
      who.className = "who";
      who.style.color = color;
      who.textContent = `${name}: `;
      line.append(who, text);
    }
    chat.append(line);
    while (chat.children.length > 26) chat.firstChild.remove();
  }

  // ---- Ping cards: the same behaviour as the app ----
  const DRAIN_MS = 6000;

  function makeCard(sender, message) {
    const card = document.createElement("div");
    card.className = "ping-card";
    card.style.setProperty("--drain", `${DRAIN_MS}ms`);
    const pic = document.createElement("span");
    pic.className = "ping-pic";
    pic.textContent = channel[0].toUpperCase();
    const body = document.createElement("div");
    body.className = "ping-body";
    const top = document.createElement("p");
    const name = document.createElement("b");
    name.textContent = `@${sender}`;
    const tag = document.createElement("em");
    tag.textContent = `#${channel}`;
    top.append(name, " pinged you ", tag);
    const text = document.createElement("p");
    text.className = "ping-msg";
    text.textContent = message;
    body.append(top, text);
    const lineTrack = document.createElement("span");
    lineTrack.className = "ping-line";
    lineTrack.append(document.createElement("i"));
    card.append(pic, body, lineTrack);
    setTimeout(() => {
      card.classList.add("leaving");
      setTimeout(() => card.remove(), 430);
    }, DRAIN_MS);
    return card;
  }

  // Small screens only have room for one card in each spot
  function trim() {
    const small = matchMedia("(max-width: 720px)").matches;
    while (chatPings && chatPings.children.length > (small ? 1 : 2)) chatPings.lastChild.remove();
    while (screenPings.children.length > (small ? 1 : 3)) screenPings.firstChild.remove();
  }

  function ping(sender, message) {
    say([sender, "#ff7a6b", null], message, { ping: true });
    chatPings.prepend(makeCard(sender, message));
    screenPings.append(makeCard(sender, message));
    trim();
  }

  if (chat) {
  // ---- Chat loop, only while the demo is on screen ----
  let timer = null;
  function tick() {
    const person = pick(PEOPLE);
    say(person, pick(LINES[channel]));
    timer = setTimeout(tick, 1100 + Math.random() * 1700);
  }
  say(null, "connected", { system: true });
  for (let i = 0; i < 5; i += 1) say(pick(PEOPLE), pick(LINES.emberline));
  if (!reduced) {
    new IntersectionObserver(([entry]) => {
      clearTimeout(timer);
      timer = entry.isIntersecting ? setTimeout(tick, 900) : null;
    }).observe(chat.parentElement);
  }

  // ---- A first ping shows what the app is about ----
  setTimeout(() => {
    if (document.visibilityState === "visible" && scrollY < innerHeight / 2) {
      ping("lunaris", "@you are you seeing this?");
    }
  }, 3200);

  // ---- Things to try ----
  const YOU = ["you", "#ffffff", "supporter_plus_plus"];
  document.getElementById("chat-form").addEventListener("submit", (event) => {
    event.preventDefault();
    const input = document.getElementById("chat-text");
    const text = input.value.trim();
    if (!text) return;
    say(YOU, text);
    input.value = "";
    const short = text.length > 26 ? `${text.slice(0, 26)}...` : text;
    setTimeout(() => ping(pick(PEOPLE)[0], `@you "${short}" made me laugh`), 900);
  });

  document.querySelectorAll("button.chip").forEach((chip) => {
    chip.addEventListener("click", () => {
      if (chip.disabled) return;
      const game = chip.dataset.game;
      say(YOU, `!game ${game.toLowerCase()}`);
      chip.classList.add("is-sent");
      chip.disabled = true;
      setTimeout(() => say(["gamebot", "#b8a4ff", null], `Category changed to ${game}`), 800);
      setTimeout(() => {
        chip.classList.remove("is-sent");
        chip.disabled = false;
      }, 900);
    });
  });

  document.querySelectorAll(".app-tab").forEach((tab) => {
    tab.addEventListener("click", () => {
      if (tab.classList.contains("is-on")) return;
      document.querySelectorAll(".app-tab").forEach((other) => {
        const on = other === tab;
        other.classList.toggle("is-on", on);
        other.setAttribute("aria-selected", String(on));
      });
      tab.querySelector(".dot")?.remove();
      channel = tab.dataset.channel;
      chat.replaceChildren();
      say(null, "connected", { system: true });
      for (let i = 0; i < 5; i += 1) say(pick(PEOPLE), pick(LINES[channel]));
    });
  });

  }

  // ---- Pings page: a card that comes and goes ----
  const pingSlot = document.getElementById("ping-slot");
  if (pingSlot) {
    const NOTES = ["you up for a game after this?", "@you check this clip", "@you you still playing?", "@you wait did you see that", "@you gg"];
    let which = 0;

    function showPing() {
      const sender = ["lunaris", "tinkerbolt", "sableheart"][which % 3];
      pingSlot.replaceChildren(makeCard(sender, NOTES[which % NOTES.length]));
      which += 1;
    }

    document.getElementById("ping-me").addEventListener("click", showPing);
    showPing();

    if (!reduced) {
      let loop = null;
      new IntersectionObserver(([entry]) => {
        clearInterval(loop);
        loop = entry.isIntersecting ? setInterval(showPing, 7000) : null;
      }, { threshold: 0.35 }).observe(pingSlot);

      // The repeat counter ticks up on its own
      const count = document.getElementById("ping-count");
      let repeats = 2;
      setInterval(() => {
        repeats = repeats >= 6 ? 2 : repeats + 1;
        count.textContent = `\u00d7${repeats}`;
        count.classList.remove("bump");
        void count.offsetWidth;
        count.classList.add("bump");
      }, 2200);
    }
  }

  if (document.getElementById("sword")) {
  // ---- Sword button and Discord card ----
  const sword = document.getElementById("sword");
  const discordState = document.getElementById("discord-state");
  sword.addEventListener("click", () => {
    const on = sword.getAttribute("aria-pressed") !== "true";
    sword.setAttribute("aria-pressed", String(on));
    discordState.textContent = on ? "Modding" : "Chatting";
  });
  let seconds = 21;
  const discordTime = document.getElementById("discord-time");
  if (!reduced) {
    setInterval(() => {
      seconds += 1;
      discordTime.textContent = `${String(Math.floor(seconds / 60)).padStart(2, "0")}:${String(seconds % 60).padStart(2, "0")}`;
    }, 1000);
  }

  }

})();
