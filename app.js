// ===== FIREBASE IMPORTS =====
import { initializeApp } from "https://www.gstatic.com/firebasejs/10.7.1/firebase-app.js";
import {
  getDatabase,
  ref,
  onValue,
  set
} from "https://www.gstatic.com/firebasejs/10.7.1/firebase-database.js";

// ===== FIREBASE CONFIG =====
const firebaseConfig = {
  apiKey: "AIzaSyA-lRaDMrUtaONmTth7YZXOCj0qWhqLKwQ",
  authDomain: "trashbin-esp32.firebaseapp.com",
  databaseURL:
    "https://trashbin-esp32-default-rtdb.asia-southeast1.firebasedatabase.app",
  projectId: "trashbin-esp32",
  storageBucket: "trashbin-esp32.firebasestorage.app",
  messagingSenderId: "779021486618",
  appId: "1:779021486618:web:800ca0ce05d5c8bd24dfa3"
};

// ===== INIT FIREBASE =====
const app = initializeApp(firebaseConfig);
const db = getDatabase(app);

// ===== WAIT FOR DOM =====
window.addEventListener("DOMContentLoaded", () => {

  // ===== ELEMENTS =====
  const lidCount = document.getElementById("lidCount");
  const binLevelText = document.getElementById("binLevelText");
  const binGauge = document.getElementById("binGauge");
  const lidStatus = document.getElementById("lidStatus");
  const overrideStatus = document.getElementById("overrideStatus");
  const connectionStatus = document.getElementById("connectionStatus");
  const binFullBell = document.getElementById("binFullBell");


  // Guard against missing markup (prevents the listener from crashing).
  const requiredEls = {
    lidCount,
    binLevelText,
    binGauge,
    lidStatus,
    overrideStatus,
    connectionStatus
  };

  // Bell is optional to keep the app resilient if markup changes.
  const hasBell = !!binFullBell;

  for (const [k, el] of Object.entries(requiredEls)) {
    if (!el) {
      console.error(`Missing required element: ${k}`);
      return;
    }
  }


  // ===== REALTIME LISTENERS (field-level to reduce UI churn) =====
  const trashRootRef = ref(db, "trashbin");

  let lastLevel = null;
  let lastLevelColorBand = null;
  let lastLidCount = null;
  let lastLidStatus = null;
  let lastOverride = null;

  const setLevelUI = (raw) => {
    const level = Math.max(0, Math.min(100, Number(raw) || 0));
    if (lastLevel === level) return;
    lastLevel = level;

    binLevelText.textContent = level + "%";
    binGauge.style.width = level + "%";

    const colorBand = level < 40 ? "low" : level < 75 ? "mid" : "high";
    if (lastLevelColorBand !== colorBand) {
      lastLevelColorBand = colorBand;
      binGauge.style.background =
        colorBand === "low"
          ? "#22c55e"
          : colorBand === "mid"
            ? "#eab308"
            : "#ef4444";
    }
  };

  const BIN_FULL_THRESHOLD = 90;
  let lastBinFull = null;

  const setBinFullUI = (isFull) => {
    if (!hasBell) return;
    const next = !!isFull;
    if (lastBinFull === next) return;
    lastBinFull = next;

    binFullBell.dataset.full = String(next);
    binFullBell.setAttribute("aria-pressed", next ? "true" : "false");

    if (next) {
      binFullBell.classList.remove("is-animating");
      // Trigger animation on state change.
      // eslint-disable-next-line no-unused-expressions
      binFullBell.offsetHeight;
      binFullBell.classList.add("is-animating");
      window.setTimeout(() => binFullBell.classList.remove("is-animating"), 1100);
    }
  };


  // Connection + existence
  onValue(trashRootRef, (snapshot) => {
    const data = snapshot.val();
    if (!data) {
      connectionStatus.textContent = "No data found";
      return;
    }
    connectionStatus.textContent = "Connected to Firebase ✔";
  });

  onValue(ref(db, "trashbin/lidOpenCount"), (snap) => {
    const v = snap.val();
    const next = v ?? 0;
    if (lastLidCount === next) return;
    lastLidCount = next;
    lidCount.textContent = next;
  });

  onValue(ref(db, "trashbin/binLevel"), (snap) => {
    const level = snap.val() ?? 0;
    setLevelUI(level);
    setBinFullUI(Number(level) >= BIN_FULL_THRESHOLD);
  });


  onValue(ref(db, "trashbin/lidStatus"), (snap) => {
    const next = snap.val() ?? "closed";
    if (lastLidStatus === next) return;
    lastLidStatus = next;
    lidStatus.textContent = next;
  });

  onValue(ref(db, "trashbin/override"), (snap) => {
    const next = snap.val() ?? "none";
    if (lastOverride === next) return;
    lastOverride = next;
    overrideStatus.textContent = next;
  });


  const openLid = () => {
    overrideStatus.textContent = "open";

    set(ref(db, "trashbin/override"), "open")
      .then(() => console.log("Open command sent"))
      .catch((err) => {
        console.error(err);
        overrideStatus.textContent = "error";
      });
  };

  const closeLid = () => {
    overrideStatus.textContent = "close";

    set(ref(db, "trashbin/override"), "close")
      .then(() => console.log("Close command sent"))
      .catch((err) => {
        console.error(err);
        overrideStatus.textContent = "error";
      });
  };

  // Wire buttons via addEventListener (avoid fragile inline onclick globals).
  const openBtn = document.getElementById("openLidBtn");
  const closeBtn = document.getElementById("closeLidBtn");
  if (openBtn) openBtn.addEventListener("click", openLid);
  if (closeBtn) closeBtn.addEventListener("click", closeLid);

  // Backward compatibility: allow inline onclick handlers if present.
  window.openLid = openLid;
  window.closeLid = closeLid;

});

