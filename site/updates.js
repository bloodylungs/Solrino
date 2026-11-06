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

    const changes = el("ul", "changes");
    changes.append(...lines.map(change));
    item.append(changes);
    return item;
  }

  fetch("https://api.github.com/repos/bloodylungs/Solrino/releases?per_page=50")
    .then((reply) => (reply.ok ? reply.json() : null))
    .then((releases) => {
      const shown = (releases ?? []).filter((item) => !item.draft && !item.prerelease);
      if (!shown.length) return;
      list.replaceChildren(...shown.map((item, index) => release(item, index === 0)));
      // Opening the page with a version in the address goes straight to it
      if (location.hash) document.getElementById(decodeURIComponent(location.hash.slice(1)))?.scrollIntoView();
    })
    .catch(() => {});
})();
