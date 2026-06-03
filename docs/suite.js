// ECU Studio Suite — bilingual toggle (EN/FR).
// Elements carry data-en / data-fr text. We swap textContent on switch and
// remember the choice. No build step, no dependencies.
(function () {
  "use strict";

  var STORAGE_KEY = "ess-lang";
  var SUPPORTED = ["en", "fr"];
  var body = document.body;

  function pickInitial() {
    var saved = null;
    try { saved = localStorage.getItem(STORAGE_KEY); } catch (e) {}
    if (saved && SUPPORTED.indexOf(saved) !== -1) return saved;
    var nav = (navigator.language || "en").slice(0, 2).toLowerCase();
    return nav === "fr" ? "fr" : "en";
  }

  function applyLang(lang) {
    if (SUPPORTED.indexOf(lang) === -1) lang = "en";
    body.setAttribute("data-lang", lang);
    document.documentElement.setAttribute("lang", lang);

    // Swap text for any element that declares both translations.
    var nodes = document.querySelectorAll("[data-en][data-fr]");
    for (var i = 0; i < nodes.length; i++) {
      var el = nodes[i];
      var txt = el.getAttribute("data-" + lang);
      if (txt !== null) el.textContent = txt;
    }

    // Update toggle button state.
    var btns = document.querySelectorAll(".lang-btn");
    for (var j = 0; j < btns.length; j++) {
      var on = btns[j].getAttribute("data-setlang") === lang;
      btns[j].classList.toggle("active", on);
      btns[j].setAttribute("aria-pressed", on ? "true" : "false");
    }

    try { localStorage.setItem(STORAGE_KEY, lang); } catch (e) {}
  }

  function init() {
    var toggles = document.querySelectorAll(".lang-btn");
    for (var i = 0; i < toggles.length; i++) {
      toggles[i].addEventListener("click", function () {
        applyLang(this.getAttribute("data-setlang"));
      });
    }
    applyLang(pickInitial());
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
