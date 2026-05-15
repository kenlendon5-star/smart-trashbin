# TODO

- [ ] Add manual override gate in `esp32_setup.ino` so lid override open does not immediately close due to sensor logic.
- [ ] Implement manual override behavior:
  - While manual override is active, skip the automatic “no personDetected -> close lid” logic.
  - When manual override is explicitly closed (override command "close"), resume automatic control.
- [ ] (Optional) Add a timeout for manual override (30s) if needed; otherwise keep open until explicit close.
- [x] Add manual override gate in `esp32_setup.ino` so lid override open does not immediately close due to sensor logic.
- [x] Ensure manual override remains open until explicit close or timeout (30s).
- [ ] Rebuild/verify compile (if applicable) and re-test in real hardware.


