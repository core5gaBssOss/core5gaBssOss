## ADR-0556: Kafka event contracts in a schema registry (Apicurio), schemas derived from the 3GPP and TMF sources

**Date:** 2026-11-03. **Status:** Proposed.

**Origin.** One of seven items taken from a review of the reference diagram
`Digital_arch-tecture_Tmforum_AI_integration.jpeg` (data layer: Kafka, Avro). User-directed on
2026-11-03: write an ADR for each item worth adding, and implement in this order: this ADR first,
then ADR-0557 (OPA), then ADR-0558 (TMF640/TMF664), then ADR-0559 to ADR-0562. Diagram items that
break project rules (Kong/Istio on the SBI, runtime LLM agent frameworks, non-OSI model weights,
gRPC SBI, ScyllaDB/MQTT/Airflow, RDF ontology layer) were rejected and are not recorded as ADRs.

**Context.** Two Kafka topics carry project events today: `chf.cdr` (the CHF's CDR stream,
ADR-0355, consumed by Doris Routine Load) and `mfaf.notifications` (MFAF, ADR-0365). Both carry
JSON. Nothing records what a valid message on each topic looks like, so a field rename in the
producer reaches the consumers (Doris, the BSS layer, training sidecars) only as a failed load or a
silently empty column.

**Decision:**
1. Every project Kafka topic gets a registered, versioned schema in **Apicurio Registry**
   (Apache-2.0, self-hosted, one Compose service and one Helm chart).
2. The payload format stays **JSON**, and the registered schema is **JSON Schema**. No wire
   format change for producers or for Doris Routine Load.
3. Schemas are **derived, never hand-written** (CLAUDE.md source-of-truth rule; same principle as
   ADR-0421 for the GUI). A script generates each topic's schema from the source its C++ type comes
   from: the 3GPP REL-19 YAML for 3GPP-shaped payloads, the fetched `tmforum-apis` swagger for
   TMF-shaped events. Project-owned envelope fields, if any, are listed and marked as such.
4. The compatibility rule is `BACKWARD` per topic, so a consumer on the new schema still reads old
   messages. A change that fails it needs a new topic version and an ADR.
5. CI checks each derived schema against the registry rules (a registry container in the CI job),
   and a contract test validates sample producer output against the schema.
6. Producers do not call the registry at runtime in the first increment; the registry is the
   reviewed contract and CI gate. Runtime schema-ID headers are deferred for future implementation.

**Rejected alternatives.**
- Confluent Schema Registry: Confluent Community Licence, not OSI-approved.
- Switching the payload to Avro now (as the diagram shows). It changes the wire format for every
  consumer, and Doris Routine Load's Avro support has not been verified in this project. Kept as a
  later option: Apicurio stores Avro schemas too, so the registry choice does not block it.
- Karapace (Apache-2.0): viable, but it is Confluent-API-only and Python; Apicurio also stores
  OpenAPI and JSON Schema artefacts, which fits the project's generated-from-spec model.
- No registry, schemas in the repo only: gives review but no compatibility check against what
  was published before.

**Disclosed.**
- Design only. No code, Compose service, chart or CI job exists yet.
- Which 3GPP schema the `chf.cdr` payload derives from is to be confirmed from the CHF's
  `CdrWriter` source in the implementation PR, not assumed here.
- Registry throughput and latency: not measured; not on any request path in increment 1.
