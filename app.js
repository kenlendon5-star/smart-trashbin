// ===== FIREBASE IMPORT (MODULAR) =====
import { initializeApp } from "https://www.gstatic.com/firebasejs/10.7.1/firebase-app.js";
import { getDatabase, ref, onValue, set } from "https://www.gstatic.com/firebasejs/10.7.1/firebase-database.js";

// ===== CONFIG =====
const firebaseConfig = {
  apiKey: "AIzaSyA-lRaDMrUtaONmTth7YZXOCj0qWhqLKwQ",
  authDomain: "trashbin-esp32.firebaseapp.com",
  databaseURL: "https://trashbin-esp32-default-rtdb.asia-southeast1.firebasedatabase.app",
  projectId: "trashbin-esp32",
  storageBucket: "trashbin-esp32.firebasestorage.app",
  messagingSenderId: "779021486618",
  appId: "1:779021486618:web:800ca0ce05d5c8bd24dfa3"
};

// Initialize Firebase (modular)
const app = initializeApp(firebaseConfig);
const db = getDatabase(app);

const lidCount = document.getElementById("lidCount");
const binLevelText = document.getElementById("binLevelText");
const binGauge = document.getElementById("binGauge");
const lidStatus = document.getElementById("lidStatus");
const overrideStatus = document.getElementById("overrideStatus");
const connectionStatus = document.getElementById("connectionStatus");

const trashRef = db.ref("trashbin");

// Real-time listener
trashRef.on("value", (snapshot) => {
  const data = snapshot.val();

  if (!data) {
    connectionStatus.innerText = "Connected, but no data found.";
    return;
  }

  connectionStatus.innerText = "Firebase Connected Successfully";

  lidCount.innerText = data.lidOpenCount || 0;

  const level = data.binLevel || 0;
  binLevelText.innerText = level + "%";

  lidStatus.innerText = data.lidStatus || "closed";
  overrideStatus.innerText = data.override || "none";

  binGauge.style.width = level + "%";

  if (level < 40) {
    binGauge.style.background = "#22c55e";
  } else if (level < 75) {
    binGauge.style.background = "#eab308";
  } else {
    binGauge.style.background = "#ef4444";
  }
});

// Open lid command
function openLid() {
  db.ref("trashbin/override")
    .set("open")
    .then(() => {
      overrideStatus.innerText = "open";
    })
    .catch((error) => {
      console.error(error);
      alert("Failed to send open command");
    });
}

// Close lid command
function closeLid() {
  db.ref("trashbin/override")
    .set("close")
    .then(() => {
      overrideStatus.innerText = "close";
    })
    .catch((error) => {
      console.error(error);
      alert("Failed to send close command");
    });
}

// Runtime tests
console.assert(typeof openLid === "function", "openLid function missing");
console.assert(typeof closeLid === "function", "closeLid function missing");
console.assert(binGauge !== null, "Gauge element missing");
console.assert(lidCount !== null, "Lid count element missing");