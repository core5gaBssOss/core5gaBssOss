## ADR-0561: Feast feature store shared by the NWDAF and CHF training sidecars, training only

**Date:** 2026-11-03. **Status:** Accepted.

**Origin.** From the reference-diagram review (data layer: Feast). Implemented after ADR-0556
to ADR-0558. See ADR-0556 "Origin".

**Context.** The NWDAF MTLF and the CHF AI pipeline train in Python sidecars (MLflow), and
inference runs in-process in C++ with ONNX Runtime (`docs/project-context/ai-pipelines.md`).
Each sidecar builds its own training features from Doris and Kafka data, so the same feature
can be computed two ways.

**Decision:**
1. **Feast** (Apache-2.0) holds feature definitions and the offline (historical) store used to
   build training sets for both sidecars, with point-in-time-correct joins.
2. **Training only.** Inference does not call Feast. The C++ inference path computes its inputs
   in-process, as today; no Python or network call is added to an NF request path.
3. To limit training/serving skew, each feature's definition is written once in config and both
   the Feast definition and the C++ input computation are checked against it by a test.
4. The offline store reads the data already in the stack (Doris or PostgreSQL); no new database.
5. Charging rule unchanged (guardrail 7): a CHF model trained on these features informs; the
   deterministic engine decides.

**Rejected alternatives.**
- Feast online store serving NF inference: puts a network hop and a Python-managed store on the
  NF request path, against the in-process inference rule.
- No feature store: features stay duplicated per sidecar.
- A commercial feature store: not OSI-approved.

**Disclosed.**
- Design only. Feast's offline-store support for Doris has not been verified; if it is missing,
  PostgreSQL is used, or the store is deferred.
- The skew check in item 3 is a plan; how a config-defined feature maps to C++ code is not
  designed yet and will be shown for approval before code.
