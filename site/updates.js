(() => {
  // Reads the real release list, so a new version shows up here by itself. The
  // list already on the page stays if GitHub can't be reached.
  const list = document.getElementById("releases");
  const when = new Intl.DateTimeFormat(undefined, { year: "numeric", month: "long", day: "numeric" });

  fetch("https://api.github.com/repos/bloodylungs/Solrino/releases?per_page=50")
    .then((reply) => (reply.ok ? reply.json() : null))
    .then((releases) => {
      const shown = (releases ?? []).filter((release) => !release.draft && !release.prerelease);
      if (!shown.length) return;
      list.replaceChildren(...shown.map((release, index) => {
        const item = document.createElement("li");
        item.className = "release";

        const side = document.createElement("div");
        side.className = "release-side";
        const version = document.createElement("b");
        version.textContent = release.tag_name.replace(/^v/, "");
        side.append(version);
        if (index === 0) {
          const latest = document.createElement("span");
          latest.className = "release-latest";
          latest.textContent = "Latest";
          side.append(latest);
        }
        const date = document.createElement("time");
        date.dateTime = release.published_at;
        date.textContent = when.format(new Date(release.published_at));
        side.append(date);

        const body = document.createElement("div");
        body.className = "release-body";
        // Shown as plain text, one paragraph per line
        (release.body ?? "").split(/\r?\n/).map((line) => line.trim()).filter(Boolean).forEach((line) => {
          const paragraph = document.createElement("p");
          paragraph.textContent = line.replace(/^[-*]\s+/, "");
          body.append(paragraph);
        });

        item.append(side, body);
        return item;
      }));
    })
    .catch(() => {});
})();
