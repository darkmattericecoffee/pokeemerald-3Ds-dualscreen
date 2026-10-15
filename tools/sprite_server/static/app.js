// Emerald 3DS plane sharing: confirmations and the swaying plane stack. No libraries.
document.addEventListener("submit", function (ev) {
  var msg = ev.target.getAttribute("data-confirm");
  if (msg && !window.confirm(msg)) ev.preventDefault();
});

document.addEventListener("DOMContentLoaded", function () {
  document.querySelectorAll(".sway img[data-d]").forEach(function (img) {
    img.style.setProperty("--d", img.getAttribute("data-d"));
  });
});
