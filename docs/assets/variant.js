(function () {
  var meta = document.querySelector('meta[name="awtrix-variant"]');
  var variant = meta && meta.content;
  if (!variant || variant === "root") return;
  try { localStorage.setItem("awtrix-docs-variant", variant); } catch (e) {}
})();
