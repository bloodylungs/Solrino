(() => {
  // ---- Search and filter the badge grid ----
  const cards = [...document.querySelectorAll(".badge-card")];
  const search = document.getElementById("badge-search");
  const filters = [...document.querySelectorAll(".filters .filter")];
  const count = document.getElementById("badge-count");
  const empty = document.getElementById("badge-empty");
  const GROUP_NAMES = { team: "Team", supporter: "Supporter", donor: "Donor", rare: "Rare", event: "Event" };
  let group = "all";

  function apply() {
    const words = search.value.trim().toLowerCase();
    let shown = 0;
    cards.forEach((card) => {
      const fits = (group === "all" || card.dataset.group === group) && (!words || card.textContent.toLowerCase().includes(words));
      card.hidden = !fits;
      if (fits) {
        shown += 1;
        // Cards brought back by a filter shouldn't wait to scroll into view
        if (group !== "all" || words) card.classList.add("is-in");
      }
    });
    count.textContent = shown === cards.length ? `${cards.length} badges` : `${shown} of ${cards.length} badges`;
    empty.hidden = shown > 0;
  }
  function pick(next) {
    group = next;
    filters.forEach((button) => button.setAttribute("aria-pressed", String(button.dataset.filter === next)));
    apply();
  }
  filters.forEach((button) => button.addEventListener("click", () => pick(button.dataset.filter)));
  search.addEventListener("input", apply);
  search.addEventListener("keydown", (event) => {
    if (event.key === "Escape" && search.value) { event.stopPropagation(); search.value = ""; apply(); }
  });
  document.getElementById("badge-reset").addEventListener("click", () => {
    search.value = "";
    pick("all");
    search.focus();
  });
  // Press / anywhere to start searching
  document.addEventListener("keydown", (event) => {
    if (event.key === "/" && !event.target.closest("input, textarea, dialog")) {
      event.preventDefault();
      search.focus();
    }
  });
  apply();

  // ---- A big view of one badge, with arrows to flip through the rest ----
  const viewer = document.createElement("dialog");
  viewer.className = "viewer";
  viewer.setAttribute("aria-labelledby", "viewer-name");
  viewer.innerHTML = `
    <button type="button" class="sheet-x" aria-label="Close" data-close>&times;</button>
    <div class="viewer-stage">
      <button type="button" class="viewer-arrow" data-step="-1" aria-label="Previous badge">&#8249;</button>
      <img alt="" width="288" height="288">
      <button type="button" class="viewer-arrow" data-step="1" aria-label="Next badge">&#8250;</button>
    </div>
    <span class="viewer-group"></span>
    <h2 id="viewer-name"></h2>
    <p class="viewer-text"></p>
    <div class="viewer-end">
      <span class="viewer-place"></span>
      <button type="button" class="link-btn" data-share>Copy link to this badge</button>
    </div>`;
  document.body.append(viewer);
  const image = viewer.querySelector("img");
  let current = null;

  function visible() { return cards.filter((card) => !card.hidden); }
  function show(card) {
    current = card;
    const list = visible();
    image.src = `/assets/badges/sizes/${card.dataset.badge}/4x.webp`;
    viewer.querySelector("#viewer-name").textContent = card.querySelector("b").textContent;
    viewer.querySelector(".viewer-text").textContent = card.querySelector("span").textContent;
    viewer.querySelector(".viewer-group").textContent = GROUP_NAMES[card.dataset.group];
    viewer.querySelector(".viewer-place").textContent = `${list.indexOf(card) + 1} of ${list.length}`;
    viewer.querySelectorAll(".viewer-arrow").forEach((arrow) => { arrow.hidden = list.length < 2; });
    history.replaceState(null, "", `#${card.id}`);
    // Load the neighbours so flipping is instant
    [-1, 1].forEach((step) => {
      const next = list[(list.indexOf(card) + step + list.length) % list.length];
      new Image().src = `/assets/badges/sizes/${next.dataset.badge}/4x.webp`;
    });
  }
  function step(by) {
    const list = visible();
    show(list[(list.indexOf(current) + by + list.length) % list.length]);
  }
  function open(card) {
    show(card);
    if (!viewer.open) viewer.showModal();
  }

  cards.forEach((card) => card.querySelector(".badge-open").addEventListener("click", () => open(card)));
  viewer.addEventListener("click", (event) => {
    if (event.target === viewer || event.target.closest("[data-close]")) viewer.close();
    const arrow = event.target.closest("[data-step]");
    if (arrow) step(Number(arrow.dataset.step));
    if (event.target.closest("[data-share]")) solrino.copy(location.href, "Link copied");
  });
  viewer.addEventListener("keydown", (event) => {
    if (event.key === "ArrowLeft") step(-1);
    if (event.key === "ArrowRight") step(1);
  });
  viewer.addEventListener("close", () => {
    history.replaceState(null, "", location.pathname);
    current?.querySelector(".badge-open").focus({ preventScroll: true });
  });

  // Opening the page with a badge in the address shows that badge
  const linked = !location.hash ? null : cards.find((card) => card.id === decodeURIComponent(location.hash.slice(1)));
  if (linked) {
    linked.scrollIntoView({ block: "center" });
    open(linked);
  }
})();
