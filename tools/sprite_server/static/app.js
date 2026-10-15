// Emerald 3DS plane sharing: confirmations, the swaying plane stack, and live pages. No libraries.
document.addEventListener("submit", function (ev) {
  var msg = ev.target.getAttribute("data-confirm");
  if (msg && !window.confirm(msg)) ev.preventDefault();
});

function setUpSway(root) {
  root.querySelectorAll(".sway img[data-d]").forEach(function (img) {
    img.style.setProperty("--d", img.getAttribute("data-d"));
  });
}

// The progress grid and a Pokémon's page follow the server by themselves: every few seconds
// (while the tab is visible) they ask /api/rev, which moves on with every upload, vote, pin
// and delete, and on a change fetch the page again and swap its <main> in place.
function startLive() {
  var path = window.location.pathname;
  if (path !== "/" && path.indexOf("/mon/") !== 0) return;
  var rev = null, busy = false, dot = document.createElement("span");
  dot.className = "live";
  dot.title = "This page updates by itself";
  dot.textContent = "live";
  var header = document.querySelector("header");
  if (header) header.appendChild(dot);

  function refresh() {
    return fetch(window.location.href, {credentials: "same-origin"}).then(function (r) {
      if (!r.ok) throw new Error("HTTP " + r.status);
      return r.text();
    }).then(function (html) {
      var doc = new DOMParser().parseFromString(html, "text/html");
      var fresh = doc.querySelector("main"), main = document.querySelector("main");
      if (!fresh || !main) return;
      main.innerHTML = fresh.innerHTML;
      setUpSway(main);
      dot.classList.add("pulse");
      setTimeout(function () { dot.classList.remove("pulse"); }, 900);
    });
  }

  function poll() {
    if (busy || document.visibilityState !== "visible") return;
    busy = true;
    fetch("/api/rev", {credentials: "same-origin"}).then(function (r) {
      if (!r.ok) throw new Error("HTTP " + r.status);
      return r.json();
    }).then(function (j) {
      var changed = rev !== null && j.rev !== rev;
      rev = j.rev;
      dot.classList.remove("off");
      return changed ? refresh() : null;
    }).catch(function () {
      dot.classList.add("off");
    }).then(function () {
      busy = false;
    });
  }

  poll();
  setInterval(poll, 4000);
  document.addEventListener("visibilitychange", poll);
}

document.addEventListener("DOMContentLoaded", function () {
  setUpSway(document);
  startLive();
});
