# Changelog

All notable changes to this project will be documented in this file.

## [0.1.0-beta] - 2026-06-02
- Project marked as Beta for wider staging tests.
- Added `TRACTION_VERSION` macro in `traction_config.h`.
- Minor documentation updates in `README.md`.
- Code reviewed: no breaking issues found; recommended further testing under load and review of edge cases (shared memory race conditions and bucket reset logic).

---

## v0.4.2-beta

- Added latency-aware scoring system.
- Health evaluation now combines upstream error rate and response latency.
- Early degradation detection before HTTP 5xx generation.
- Improved protection for overloaded but still responsive services.
- Enhanced recovery decision quality under partial saturation.
- Validated under 92k+ req/sec emergency protection scenario.
- Maintained near-zero overhead during local rejection path.
