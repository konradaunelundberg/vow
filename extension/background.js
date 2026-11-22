// Reports open tabs to the Vow service on 127.0.0.1 and does what it says:
// sends blocked tabs to the blocked page, and in a focus session keeps one
// tab on the session's site and closes the rest. The service decides; this
// only asks and follows.

const SERVICE = "http://127.0.0.1:47321";
const BLOCKED_PAGE = chrome.runtime.getURL("blocked.html");

let reporting = false;
let again = false;
let focus = null;
let hideShorts = null;

// youtube.js reads this to hide Shorts from feeds. Written only on change,
// since reports come every two seconds.
function setHideShorts(value) {
  if (value === hideShorts) return;
  hideShorts = value;
  chrome.storage.local.set({ hideShorts: value }).catch(() => {});
}

// The same rule the service uses: "reddit.com" covers the host and its
// subdomains, "youtube.com/shorts" also needs the path.
function matches(pattern, url) {
  let parsed;
  try { parsed = new URL(url); } catch (e) { return false; }
  if (parsed.protocol !== "http:" && parsed.protocol !== "https:") return false;
  let p = String(pattern).trim().toLowerCase().replace(/^[a-z]+:\/\//, "").replace(/^\*\./, "").replace(/^www\./, "");
  if (p === "*") return true;
  const slash = p.indexOf("/");
  const host = slash < 0 ? p : p.slice(0, slash);
  const path = slash < 0 ? "" : p.slice(slash);
  const h = parsed.host.toLowerCase();
  if (h !== host && !h.endsWith("." + host)) return false;
  return !path || path === "/" || (parsed.pathname + parsed.search).toLowerCase().startsWith(path);
}

function tabUrl(tab) { return tab.pendingUrl || tab.url || ""; }

async function enforceFocus(tabs) {
  const onSite = (tab) => focus.sites.some((p) => matches(p, tabUrl(tab)));
  const keeper = tabs.find((t) => t.active && onSite(t)) || tabs.find(onSite) || tabs.find((t) => t.active) || tabs[0];
  if (!keeper) {
    await chrome.windows.create({ url: focus.url, state: "maximized" }).catch(() => {});
    return;
  }
  // One tab in one window.
  const windows = await chrome.windows.getAll().catch(() => []);
  for (const w of windows) if (w.id !== keeper.windowId) chrome.windows.remove(w.id).catch(() => {});
  for (const t of tabs) if (t.id !== keeper.id) chrome.tabs.remove(t.id).catch(() => {});
  if (!onSite(keeper)) chrome.tabs.update(keeper.id, { url: focus.url, active: true }).catch(() => {});
}

async function report() {
  if (reporting) { again = true; return; }
  reporting = true;
  try {
    const win = await chrome.windows.getLastFocused().catch(() => null);
    const tabs = await chrome.tabs.query({});
    const payload = {
      focused: Boolean(win && win.focused),
      tabs: tabs
        .filter((tab) => /^https?:/.test(tabUrl(tab)))
        .map((tab) => ({
          id: tab.id,
          url: tabUrl(tab),
          active: Boolean(tab.active && win && tab.windowId === win.id),
        })),
    };
    const response = await fetch(SERVICE + "/report", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });
    const answer = await response.json();
    focus = answer.focus && answer.focus.url ? answer.focus : null;
    setHideShorts(Boolean(answer.hideShorts));

    if (focus) {
      await enforceFocus(tabs);
    } else {
      for (const blocked of answer.blocked || []) {
        const tab = tabs.find((t) => t.id === blocked.id);
        if (!tab) continue;
        const params = new URLSearchParams({ url: tabUrl(tab), block: blocked.block, until: blocked.until || "" });
        chrome.tabs.update(blocked.id, { url: BLOCKED_PAGE + "?" + params.toString() }).catch(() => {});
      }
    }
  } catch (error) {
    // The service is not running; nothing to enforce from here.
    if (error instanceof TypeError) setHideShorts(false);
  } finally {
    reporting = false;
    if (again) { again = false; report(); }
  }
}

chrome.tabs.onCreated.addListener(report);
chrome.tabs.onRemoved.addListener(report);
chrome.tabs.onUpdated.addListener((tabId, change) => { if (change.url || change.status === "loading") report(); });
chrome.tabs.onActivated.addListener(report);
chrome.windows.onCreated.addListener(report);
chrome.windows.onFocusChanged.addListener(report);
chrome.runtime.onStartup.addListener(report);
chrome.runtime.onInstalled.addListener(report);

// Usage is counted from these reports, so they keep coming while Chromium
// runs. The alarm wakes the worker again if Chromium put it to sleep.
chrome.alarms.create("vow", { periodInMinutes: 0.5 });
chrome.alarms.onAlarm.addListener(report);
setInterval(report, 2000);
report();
