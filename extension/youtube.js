// Hides Shorts from YouTube's pages (home, subscriptions, search, the
// sidebar, channel tabs, recommendations) while the service blocks Shorts.
// Opening a Short is stopped by background.js; this only keeps them out of
// sight. The background worker keeps "hideShorts" in storage up to date; the
// rules are in youtube.css and apply while <html> has the class set here.

function apply(hide) {
  document.documentElement.classList.toggle("vow-hide-shorts", Boolean(hide));
}

// Hide right away and show again only if the service says Shorts are allowed,
// so a feed never flashes Shorts while storage is read.
apply(true);
chrome.storage.local.get("hideShorts").then((items) => apply(items.hideShorts !== false)).catch(() => {});
chrome.storage.onChanged.addListener((changes, area) => {
  if (area === "local" && changes.hideShorts) apply(changes.hideShorts.newValue !== false);
});

// Filter chips ("Shorts" on home and search) carry no attribute to select by,
// only their text, so they are tagged for youtube.css. At most once a frame,
// since YouTube changes the page constantly.
let queued = false;
function tagChips() {
  queued = false;
  for (const chip of document.querySelectorAll("yt-chip-cloud-chip-renderer:not([data-vow-shorts]), ytm-chip-cloud-chip-renderer:not([data-vow-shorts])")) {
    if (chip.textContent.trim() === "Shorts") chip.setAttribute("data-vow-shorts", "");
  }
}
new MutationObserver(() => {
  if (!queued) { queued = true; requestAnimationFrame(tagChips); }
}).observe(document.documentElement, { childList: true, subtree: true });
