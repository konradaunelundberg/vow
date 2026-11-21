// Reports open tabs to the Vow service on 127.0.0.1 and sends any tab it
// says is blocked to the blocked page. The service decides; this only asks.

const SERVICE = "http://127.0.0.1:47321";
const BLOCKED_PAGE = chrome.runtime.getURL("blocked.html");

let reporting = false;

async function report() {
  if (reporting) return;
  reporting = true;
  try {
    const win = await chrome.windows.getLastFocused().catch(() => null);
    const tabs = await chrome.tabs.query({});
    const payload = {
      focused: Boolean(win && win.focused),
      tabs: tabs
        .filter((tab) => tab.url && /^https?:/.test(tab.url))
        .map((tab) => ({
          id: tab.id,
          url: tab.url,
          active: Boolean(tab.active && win && tab.windowId === win.id),
        })),
    };
    const response = await fetch(SERVICE + "/report", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });
    const answer = await response.json();
    for (const blocked of answer.blocked || []) {
      const tab = tabs.find((t) => t.id === blocked.id);
      if (!tab) continue;
      const params = new URLSearchParams({ url: tab.url, block: blocked.block, until: blocked.until || "" });
      chrome.tabs.update(blocked.id, { url: BLOCKED_PAGE + "?" + params.toString() }).catch(() => {});
    }
  } catch (error) {
    // The service is not running; nothing to enforce from here.
  } finally {
    reporting = false;
  }
}

chrome.tabs.onUpdated.addListener((tabId, change) => { if (change.url || change.status === "loading") report(); });
chrome.tabs.onActivated.addListener(report);
chrome.windows.onFocusChanged.addListener(report);
chrome.runtime.onStartup.addListener(report);
chrome.runtime.onInstalled.addListener(report);

// Usage is counted from these reports, so they keep coming while Chromium
// runs. The alarm wakes the worker again if Chromium put it to sleep.
chrome.alarms.create("vow", { periodInMinutes: 0.5 });
chrome.alarms.onAlarm.addListener(report);
setInterval(report, 3000);
report();
