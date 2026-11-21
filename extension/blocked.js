// Shows what blocked the page and when it opens, then goes back to the
// page by itself once the service lets it through.

const params = new URLSearchParams(location.search);
const original = params.get("url") || "";
let until = params.get("until") || "";
let blockName = params.get("block") || "";

function host(url) {
  try { return new URL(url).host.replace(/^www\./, ""); } catch (e) { return url; }
}

function remaining() {
  if (!until) return "";
  const seconds = Math.max(0, Math.round((new Date(until) - new Date()) / 1000));
  const h = Math.floor(seconds / 3600), m = Math.floor((seconds % 3600) / 60), s = seconds % 60;
  return h > 0 ? `${h}h ${String(m).padStart(2, "0")}m` : `${m}m ${String(s).padStart(2, "0")}s`;
}

function render() {
  document.getElementById("site").textContent = host(original);
  document.getElementById("reason").textContent = blockName
    ? `Blocked by ${blockName}` + (until ? ` until ${new Date(until).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" })}` : "")
    : "Blocked";
  document.getElementById("countdown").textContent = remaining();
  document.title = `Blocked · ${host(original)}`;
}

async function check() {
  try {
    const response = await fetch("http://127.0.0.1:47321/check", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ url: original }),
    });
    const answer = await response.json();
    const colors = answer.colors || {};
    const root = document.documentElement.style;
    if (colors.background) root.setProperty("--bg", colors.background);
    if (colors.foreground) root.setProperty("--fg", colors.foreground);
    if (colors.dark_foreground) root.setProperty("--dim", colors.dark_foreground);
    if (colors.accent) root.setProperty("--accent", colors.accent);
    if (!answer.block && /^https?:/.test(original)) {
      location.replace(original);
      return;
    }
    blockName = answer.block;
    until = answer.until || "";
  } catch (e) {
    // Without the service there is nothing to wait for.
  }
  render();
}

render();
check();
setInterval(render, 1000);
setInterval(check, 5000);
