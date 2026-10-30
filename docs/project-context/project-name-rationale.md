# Project goal -- full text, with the name rationale

Moved verbatim from CLAUDE.md "Project goal" (2026-10-29, `docs/optimization/MOVE_LOG.md`).
CLAUDE.md keeps the goal; this file keeps why the title says R19 / 5G-Advanced and not 6G.

A modular, standards-faithful 5G Core (5GC) **and Charging (CHF + Online
Charging, Gy/CAP)**
implementation in modern C++, targeting 3GPP **R19 (5G-Advanced)**. R19 is
what 3GPP itself brands 5G-Advanced; 6G has no stage-3 specification yet and
nothing here implements it, so it is deliberately absent from the title. When
Release 20 lands and 3GPP defines 6G, the intent is to carry this architecture
forward and revisit the name then — a statement of direction, not a capability
claim. Every Network Function's northbound API is **generated** from the
official 3GPP OpenAPI YAML (forge.3gpp.org/rep/all/5G_APIs, REL-19 branch) —
never hand-written — with a TM Forum SID-aligned charging/BSS domain, a
JSON-schema-driven operator GUI, and AI/ML pipelines wired into **both NWDAF
and the CHF**. Target: a production-grade, spec-traceable reference
implementation (raised from the original lab-grade scope — see ADR-0009 in
`docs/DECISIONS.md`).
