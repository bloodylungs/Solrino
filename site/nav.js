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

  // A light that follows the mouse across glass
  if (matchMedia("(hover: hover)").matches && !matchMedia("(prefers-reduced-motion: reduce)").matches) {
    let pending = null;
    addEventListener("pointermove", (event) => {
      pending = event;
      if (pending.handled) return;
      requestAnimationFrame(() => {
        const last = pending;
        pending = null;
        const card = last?.target?.closest?.(".glass");
        if (!card) return;
        const box = card.getBoundingClientRect();
        const x = last.clientX - box.left;
        const y = last.clientY - box.top;
        card.style.setProperty("--mx", `${x}px`);
        card.style.setProperty("--my", `${y}px`);
      });
    }, { passive: true });
  }

  // Download buttons aren't links, so the browser doesn't show the address on hover
  document.querySelectorAll("[data-download]").forEach((button) => {
    button.addEventListener("click", () => {
      location.href = "https://github.com/bloodylungs/Solrino/releases/latest/download/Solrino-Setup.exe";
    });
  });
})();
