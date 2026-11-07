(() => {
  // ---- Nav: shows where you are, and shrinks to a pill as you scroll ----
  const links = document.querySelectorAll(".link");

  // The home tab is the current one on the home page
  if (location.pathname === "/" || location.pathname.endsWith("/index")) {
    document.querySelector('.link[data-tab="top"]').setAttribute("aria-current", "true");
  }

  const bar = document.getElementById("bar");
  const menuButton = document.getElementById("menu-btn");
  const phone = matchMedia("(max-width: 720px)");
  let scrolledDown = false;

  function updateBar() {
    // Two different points, so it can't flicker while sitting near one of them
    scrolledDown = scrollY > (scrolledDown ? 40 : 90);
    // Reach for the pill and the links come back
    const reached = !phone.matches && (bar.matches(":hover") || bar.contains(document.activeElement));
    bar.classList.toggle("is-small", scrolledDown && !reached);
  }
  function closeMenu() {
    bar.classList.remove("is-menu");
    menuButton.setAttribute("aria-expanded", "false");
  }
  addEventListener("scroll", () => {
    updateBar();
    if (phone.matches) closeMenu();
  }, { passive: true });
  bar.addEventListener("pointerenter", (event) => { if (event.pointerType === "mouse") updateBar(); });
  bar.addEventListener("pointerleave", () => setTimeout(updateBar));
  bar.addEventListener("focusin", updateBar);
  bar.addEventListener("focusout", () => setTimeout(updateBar));
  menuButton.addEventListener("click", () => {
    const open = bar.classList.toggle("is-menu");
    menuButton.setAttribute("aria-expanded", String(open));
  });
  links.forEach((link) => link.addEventListener("click", () => {
    closeMenu();
    link.blur();
  }));
  document.addEventListener("click", (event) => { if (!bar.contains(event.target)) closeMenu(); });
  updateBar();

  // Things rise into place the first time they're reached
  let revealWorks = false;
  const revealed = new IntersectionObserver((entries) => {
    revealWorks = true;
    entries.forEach((entry) => {
      if (entry.isIntersecting) {
        entry.target.classList.add("is-in");
        revealed.unobserve(entry.target);
      }
    });
  }, { rootMargin: "0px 0px -8% 0px" });
  document.querySelectorAll(".reveal").forEach((item) => {
    // Items side by side arrive one after another
    const siblings = [...item.parentElement.children].filter((other) => other.classList.contains("reveal"));
    item.style.setProperty("--i", String(Math.min(siblings.indexOf(item), 7)));
    revealed.observe(item);
  });
  // Nothing may stay hidden because of this: if it never starts, show everything
  setTimeout(() => {
    if (!revealWorks) document.querySelectorAll(".reveal").forEach((item) => item.classList.add("is-in"));
  }, 1500);

  // ---- Little messages at the bottom of the screen ----
  const toast = document.createElement("div");
  toast.className = "toast";
  toast.setAttribute("role", "status");
  document.body.append(toast);
  let toastTimer;
  function say(text) {
    toast.textContent = text;
    toast.classList.add("is-on");
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => toast.classList.remove("is-on"), 1800);
  }
  // The old way, for browsers that block the clipboard API
  function copyOld(text) {
    const box = document.createElement("textarea");
    box.value = text;
    box.setAttribute("readonly", "");
    box.style.cssText = "position:fixed;opacity:0;pointer-events:none";
    (document.querySelector("dialog[open]") ?? document.body).append(box);
    box.select();
    let worked = false;
    try { worked = document.execCommand("copy"); } catch {}
    box.remove();
    return worked;
  }
  async function copy(text, done) {
    try {
      await navigator.clipboard.writeText(text);
      say(done);
    } catch {
      say(copyOld(text) ? done : "Couldn't copy. Your browser blocked it.");
    }
  }
  window.solrino = { say, copy };

  // ---- A thin line along the top that fills as you read ----
  const progress = document.createElement("div");
  progress.className = "progress";
  progress.setAttribute("aria-hidden", "true");
  document.body.append(progress);

  // ---- Back to the top ----
  const up = document.createElement("button");
  up.type = "button";
  up.className = "to-top";
  up.setAttribute("aria-label", "Back to the top");
  up.innerHTML = '<svg viewBox="0 0 24 24" width="18" height="18" aria-hidden="true"><path d="M12 19V5m0 0-6 6m6-6 6 6" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"/></svg>';
  up.addEventListener("click", () => {
    scrollTo({ top: 0, behavior: matchMedia("(prefers-reduced-motion: reduce)").matches ? "auto" : "smooth" });
    document.querySelector(".bar-logo").focus({ preventScroll: true });
  });
  document.body.append(up);
  function updateUp() { document.documentElement.classList.toggle("has-top", scrollY > 900); }
  addEventListener("scroll", updateUp, { passive: true });
  updateUp();

  // ---- Commands like /vanity copy when clicked ----
  document.querySelectorAll("main code").forEach((code) => {
    if (!/^[/!]/.test(code.textContent)) return;
    code.classList.add("copyable");
    code.tabIndex = 0;
    code.setAttribute("role", "button");
    code.title = "Click to copy";
    code.setAttribute("aria-label", `Copy ${code.textContent}`);
    const run = () => copy(code.textContent, `Copied ${code.textContent}`);
    code.addEventListener("click", run);
    code.addEventListener("keydown", (event) => {
      if (event.key === "Enter" || event.key === " ") { event.preventDefault(); run(); }
    });
  });

  // ---- Cards glow a little where the mouse is ----
  if (matchMedia("(pointer: fine)").matches) {
    document.addEventListener("pointermove", (event) => {
      const card = event.target.closest?.(".glass");
      if (!card) return;
      const box = card.getBoundingClientRect();
      card.style.setProperty("--mx", `${event.clientX - box.left}px`);
      card.style.setProperty("--my", `${event.clientY - box.top}px`);
    }, { passive: true });
  }

  // ---- Questions can be linked to: opening one puts it in the address ----
  const faqs = document.querySelectorAll(".faq-item[id]");
  faqs.forEach((item) => item.addEventListener("toggle", () => {
    if (item.open) history.replaceState(null, "", `#${item.id}`);
    else if (location.hash === `#${item.id}`) history.replaceState(null, "", location.pathname);
  }));
  const asked = location.hash ? document.getElementById(decodeURIComponent(location.hash.slice(1))) : null;
  if (asked?.classList.contains("faq-item")) {
    asked.open = true;
    asked.classList.add("is-in");
    requestAnimationFrame(() => asked.scrollIntoView({ block: "center" }));
  }

  // ---- Esc closes the phone menu ----
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape" && bar.classList.contains("is-menu")) {
      closeMenu();
      menuButton.focus();
    }
  });

  // ---- The newest version, shown next to the download buttons ----
  const SETUP = "https://github.com/bloodylungs/Solrino/releases/latest/download/Solrino-Setup.exe";
  let latest = null;
  const megabytes = (bytes) => `${Math.round(bytes / 1048576)} MB`;
  const day = new Intl.DateTimeFormat(undefined, { month: "long", day: "numeric", year: "numeric" });
  function showLatest(info) {
    latest = info;
    document.querySelectorAll("[data-latest]").forEach((spot) => { spot.textContent = `, version ${info.version}`; });
    document.querySelectorAll("[data-latest-full]").forEach((spot) => {
      spot.textContent = `Version ${info.version}, out ${day.format(new Date(info.date))}${info.size ? `. ${megabytes(info.size)}` : ""}.`;
      spot.hidden = false;
    });
  }
  try {
    const kept = JSON.parse(sessionStorage.getItem("solrino-latest"));
    if (kept && Date.now() - kept.at < 30 * 60 * 1000) showLatest(kept);
  } catch {}
  if (!latest) {
    fetch("https://api.github.com/repos/bloodylungs/Solrino/releases/latest")
      .then((reply) => (reply.ok ? reply.json() : null))
      .then((data) => {
        if (!data?.tag_name) return;
        const setup = data.assets?.find((asset) => asset.name === "Solrino-Setup.exe");
        const info = { version: data.tag_name.replace(/^v/, ""), date: data.published_at, size: setup?.size ?? 0, at: Date.now() };
        showLatest(info);
        try { sessionStorage.setItem("solrino-latest", JSON.stringify(info)); } catch {}
      })
      .catch(() => {});
  }

  // ---- Download: starts the setup, then explains what to do next ----
  const onWindows = /Windows/i.test(navigator.userAgent) && !/Windows Phone/i.test(navigator.userAgent);
  const onPhone = /Android|iPhone|iPad|iPod|Mobile/i.test(navigator.userAgent);
  const helper = document.createElement("dialog");
  helper.className = "sheet";
  helper.setAttribute("aria-labelledby", "sheet-title");
  document.body.append(helper);

  function openHelper() {
    const version = latest ? `Version ${latest.version}${latest.size ? `, ${megabytes(latest.size)}` : ""}` : "";
    if (onWindows) {
      helper.innerHTML = `
        <button type="button" class="sheet-x" aria-label="Close" data-close>&times;</button>
        <img src="/assets/icon.png" alt="" width="56" height="56">
        <h2 id="sheet-title">Your download is starting.</h2>
        <ol class="sheet-steps">
          <li><b>1</b><p>Open <code>Solrino-Setup.exe</code> from your downloads.</p></li>
          <li><b>2</b><p>If Windows says it protected your PC, click <b>More info</b>, then <b>Run anyway</b>. The app isn't signed yet.</p></li>
          <li><b>3</b><p>Setup can bring over your settings, tabs and accounts from Chatterino or Moltorino.</p></li>
        </ol>
        <div class="sheet-end">
          <span>${version}</span>
          <button type="button" class="link-btn" data-again>Didn't start? Try again</button>
        </div>`;
    } else {
      helper.innerHTML = `
        <button type="button" class="sheet-x" aria-label="Close" data-close>&times;</button>
        <img src="/assets/icon.png" alt="" width="56" height="56">
        <h2 id="sheet-title">Solrino is for Windows.</h2>
        <p class="sheet-text">It looks like you're on ${onPhone ? "a phone" : "a Mac or Linux computer"}. Copy this page's link and open it on your Windows PC to get it there.</p>
        <div class="sheet-actions">
          <button type="button" class="btn" data-share>Copy the link</button>
          <button type="button" class="btn btn-ghost" data-again>Download anyway</button>
        </div>
        ${version ? `<p class="sheet-small">${version}</p>` : ""}`;
    }
    helper.showModal();
  }
  helper.addEventListener("click", (event) => {
    if (event.target === helper || event.target.closest("[data-close]")) helper.close();
    if (event.target.closest("[data-again]")) location.href = SETUP;
    if (event.target.closest("[data-share]")) {
      const url = location.origin + "/";
      // Phones get their own share sheet, so the link can go straight to a chat or email
      if (onPhone && navigator.share) navigator.share({ title: "Solrino", text: "Twitch chat for Windows", url }).catch(() => {});
      else copy(url, "Link copied. Open it on your PC.");
    }
  });

  // Download buttons aren't links, so the browser doesn't show the address on hover
  document.querySelectorAll("[data-download]").forEach((button) => {
    button.addEventListener("click", () => {
      if (onWindows) location.href = SETUP;
      openHelper();
    });
  });
})();
