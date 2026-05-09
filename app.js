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

  // ===== DATABASE REF =====
  const trashRef = ref(db, "trashbin");

  // ===== REALTIME LISTENER =====
  onValue(trashRef, (snapshot) => {
    const data = snapshot.val();

    if (!data) {
      connectionStatus.textContent = "No data found";
      return;
    }

    connectionStatus.textContent = "Connected to Firebase ✔";

    // Lid counter
    lidCount.textContent = data.lidOpenCount ?? 0;

    // Bin level
    const level = data.binLevel ?? 0;
    binLevelText.textContent = level + "%";
    binGauge.style.width = level + "%";

    // Color logic
    if (level < 40) {
      binGauge.style.background = "#22c55e";
    } else if (level < 75) {
      binGauge.style.background = "#eab308";
    } else {
      binGauge.style.background = "#ef4444";
    }

    // Status
    lidStatus.textContent = data.lidStatus ?? "closed";
    overrideStatus.textContent = data.override ?? "none";
  });

  // ===== OPEN LID =====
  window.openLid = function () {
    overrideStatus.textContent = "open";

    set(ref(db, "trashbin/override"), "open")
      .then(() => console.log("Open command sent"))
      .catch((err) => {
        console.error(err);
        overrideStatus.textContent = "error";
      });
  };

  // ===== CLOSE LID =====
  window.closeLid = function () {
    overrideStatus.textContent = "close";

    set(ref(db, "trashbin/override"), "close")
      .then(() => console.log("Close command sent"))
      .catch((err) => {
        console.error(err);
        overrideStatus.textContent = "error";
      });
  };

});