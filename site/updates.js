(() => {
  // Reads the real release list, so a new version shows up here by itself. The
  // list already on the page stays if GitHub can't be reached.
  const list = document.getElementById("releases");
  const when = new Intl.DateTimeFormat(undefined, { year: "numeric", month: "long", day: "numeric" });

  // A label for each change, from the word it starts with. Changes that start
  // with something else get no label.
  const KINDS = [
    [/^(new|added|add)\b/i, "New", "new"],
    [/^(fixed|fix)\b/i, "Fixed", "fixed"],
    [/^(removed|remove)\b/i, "Removed", "removed"],
  ];

  function el(tag, className, text) {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined) node.textContent = text;
    return node;
  }

  // One change: its first sentence is the headline, the rest is the detail
  function change(line) {
    const item = el("li", "change");
    const kind = KINDS.find(([pattern]) => pattern.test(line));
    if (kind) item.append(el("span", `change-tag tag-${kind[2]}`, kind[1]));
    const split = line.search(/[.!?](\s|$)/);
    const head = split === -1 ? line : line.slice(0, split + 1);
    const rest = split === -1 ? "" : line.slice(split + 1).trim();
    const text = el("div", "change-text");
    text.append(el("b", "", head));
    if (rest) text.append(el("p", "", rest));
    item.append(text);
    return item;
  }

  function release(data, isLatest) {
    const item = el("li", `release${isLatest ? " is-latest" : ""}`);
    item.id = data.tag_name;

    const lines = (data.body ?? "").split(/\r?\n/).map((line) => line.trim().replace(/^[-*]\s+/, "")).filter(Boolean);

    const head = el("header", "release-head");
    const title = el("h2", "release-version");
    const link = el("a", "", `Solrino ${data.tag_name.replace(/^v/, "")}`);
    link.href = `#${data.tag_name}`;
    title.append(link);
    head.append(title);
    if (isLatest) head.append(el("span", "release-latest", "Latest"));
    const date = el("time", "release-date", when.format(new Date(data.published_at)));
    date.dateTime = data.published_at;
    head.append(date);
    head.append(el("span", "release-count", `${lines.length} ${lines.length === 1 ? "change" : "changes"}`));
    item.append(head);

    const share = el("button", "copy-link");
    share.type = "button";
    share.setAttribute("aria-label", `Copy a link to Solrino ${data.tag_name.replace(/^v/, "")}`);
    share.title = "Copy link";
    share.innerHTML = '<svg viewBox="0 0 24 24" width="15" height="15" aria-hidden="true"><path d="M10 14a4 4 0 0 0 5.66 0l3-3a4 4 0 0 0-5.66-5.66l-1 1M14 10a4 4 0 0 0-5.66 0l-3 3a4 4 0 0 0 5.66 5.66l1-1" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"/></svg>';
    share.addEventListener("click", () => {
      history.replaceState(null, "", `#${data.tag_name}`);
      solrino.copy(location.href, "Link copied");
    });
    head.append(share);

    const changes = el("ul", "changes");
    changes.append(...lines.map(change));
    item.append(changes);
    return item;
  }

  // ---- Search and filter the changes ----
  const tools = document.getElementById("update-tools");
  const search = document.getElementById("update-search");
  const filters = [...tools.querySelectorAll(".filter")];
  const count = document.getElementById("update-count");
  const empty = document.getElementById("update-empty");
  let kind = "all";

  function apply() {
    const words = search.value.trim().toLowerCase();
    let shown = 0;
    let total = 0;
    list.querySelectorAll(".release").forEach((version) => {
      let inVersion = 0;
      version.querySelectorAll(".change").forEach((item) => {
        total += 1;
        const tag = item.querySelector(".change-tag");
        const fits = (kind === "all" || tag?.classList.contains(`tag-${kind}`)) && (!words || item.textContent.toLowerCase().includes(words));
        item.hidden = !fits;
        if (fits) inVersion += 1;
      });
      version.hidden = inVersion === 0;
      shown += inVersion;
    });
    const filtering = kind !== "all" || words;
    count.textContent = filtering ? `${shown} of ${total} changes` : `${total} changes`;
    empty.hidden = shown > 0;
  }
  function pick(next) {
    kind = next;
    filters.forEach((button) => button.setAttribute("aria-pressed", String(button.dataset.filter === next)));
    apply();
  }
  filters.forEach((button) => button.addEventListener("click", () => pick(button.dataset.filter)));
  search.addEventListener("input", apply);
  search.addEventListener("keydown", (event) => {
    if (event.key === "Escape" && search.value) { search.value = ""; apply(); }
  });
  document.getElementById("update-reset").addEventListener("click", () => {
    search.value = "";
    pick("all");
    search.focus();
  });
  document.addEventListener("keydown", (event) => {
    if (event.key === "/" && !tools.hidden && !event.target.closest("input, textarea, dialog")) {
      event.preventDefault();
      search.focus();
    }
  });

  fetch("https://api.github.com/repos/bloodylungs/Solrino/releases?per_page=50")
    .then((reply) => (reply.ok ? reply.json() : null))
    .then((releases) => {
      const shown = (releases ?? []).filter((item) => !item.draft && !item.prerelease);
      if (!shown.length) return;
      list.replaceChildren(...shown.map((item, index) => release(item, index === 0)));
      tools.hidden = false;
      apply();
      // Opening the page with a version in the address goes straight to it
      if (location.hash) document.getElementById(decodeURIComponent(location.hash.slice(1)))?.scrollIntoView();
    })
    .catch(() => {});
})();
