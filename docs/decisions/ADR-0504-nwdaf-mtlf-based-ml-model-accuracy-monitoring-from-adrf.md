## ADR-0504: NWDAF MTLF-based ML model accuracy monitoring from ADRF-stored inference data (TS 23.288 6.2E.2)

**Date:** 2026-10-22. **Status:** Accepted -- approved by the user on 2026-10-22.
Procedure list, D1a-D8a and Q1-Q6 as recorded under "User decisions" below; increment 1 (ADRF)
in progress, nothing else built yet. It
answers GitHub issue #12 and is the follow-up ADR-0370 disclosed ("The MTLF-based variant (6.2E.2
...) is not built"). Approval was asked for the procedure list, the recommended options (D1-D8)
and the answers to the open questions at the end.

**Context -- what is true today (each point checked in the tree on 2026-10-22).**

Sources read: TS 23.288 V19.7.0 (`specs/3gpp/23288-j70.docx`) clauses 5C.1, 6.2A.2, 6.2B.2,
6.2B.3, 6.2E.1, 6.2E.2 (figure 6.2E.2-1), 6.2E.3, 7.5.2, 7.5.4, 10.2.2, 10.2.5, 10.2.6; TS 29.520
V19.7.0 (`specs/3gpp/TS_29.520_j70.txt`) 4.5.2.2.2, 4.5.2.2.3, 4.5.2.4.2, tables 5.4.6.2.3-1,
5.4.6.2.14-1, 5.4.6.2.17-1, 5.4.8-1; TS 29.575 V19.7.0 (`specs/3gpp/TS_29.575_j70.txt`) 4.2.2.2.2,
4.2.2.3.2, 4.2.2.5.2, 4.2.2.6.2, 4.2.2.8.2; the vendored REL-19 YAML in
`specs/5G_APIs-REL-19/` (commit `bca84b60a37773133bcae97e5c6c0d10a93b47b6`, as cited by the
NWDAF rows of `docs/TRACEABILITY.md`). The YAML line numbers below are from that tree.

1. **The AnLF-assisted variant (6.2E.3) is built** (ADR-0370): the AnLF registers its model use
   at the MTLF (`Nnwdaf_MLModelMonitor_Register`), the MTLF subscribes at the AnLF, the AnLF
   judges each prediction against the next observed load (`nfs/nwdaf/src/accuracy_monitor.cpp`)
   and notifies; the MTLF marks the model degraded (`store_.put_degraded`) and re-trains
   (`nfs/nwdaf/src/mtlf.cpp`, `run`, the `due_by_accuracy` branch). Proven by
   `NwdafMtlf.AccuracyBelowThresholdIsReportedAndDrivesRetraining`. ADR-0499's token-scope
   enforcement broke the MTLF's NRF read of the AnLF profile; commit `2fff932` fixed it by
   NFDiscovery. Nothing in this ADR changes that path.
2. **The MTLF already uses the ADRF for training data, under its own data set.**
   `Mtlf::ensure_storage_subscription` (mtlf.cpp, comment "TS 23.288 6.2E.2 step 8 / 6.2B.3") asks
   the ADRF (`POST /request-storage-sub`) to collect the AnLF's NRF NF-status data under
   `mtlf.data_set_id` (`config/nwdaf.json`: `nwdaf-mtlf-nrf-status`); `Mtlf::training_dataset`
   reads it back with `GET /data-store-records?data-set-id=...` and uses
   `dataNotif.nrfEventNotifs[].nfProfile.load`. That is the 6.2E.2 step 8 bullet "If the NWDAF
   containing AnLF does not include a DataSetTag with ADRF ID at step 2, the NWDAF containing MTLF
   may request ADRF to subscribe for the collection ..." in substance, used for training, not
   for accuracy.
3. **`inferDataForModel` is neither sent nor read.** The AnLF's provision subscription
   (`MlConsumer::subscribe`, `nfs/nwdaf/src/ml_consumer.cpp`) sends `mLEvent`, `mLEventFilter`,
   `mlEvRepCon{modelMetric: ACCURACY, mlAccuracyThreshold}`, `notifUri`, `notifCorreId`,
   `eventReq{immRep}`; it never issues the PUT modification. The MTLF's PUT handler
   (`mtlf.cpp`, `/subscriptions/{subscriptionId}`) validates and replaces the stored request but
   does nothing with `inferDataForModel`. `docs/TRACEABILITY.md` (ADR-0369 table) lists
   `inferDataForModel` as **not built**.
4. **No `suppFeats` negotiation on Nnwdaf_MLModelProvision.** TS 29.520 4.5.2.2.2 makes
   `mlEvRepCon` and `inferDataForModel` conditional on feature `ModelProvisionExt` (table 5.4.8-1,
   feature 4). Neither side negotiates it today, and `mlEvRepCon` is already sent without it -- a
   pre-existing nonconformance this work would fix.
5. **The AnLF stores nothing in the ADRF.** It holds an `nadrf-datamanagement` token client
   (`nfs/nwdaf/src/main.cpp`, `oauth_adrf_dm`), which only the MTLF role uses. Predictions are
   logged to Valkey only (`AccuracyMonitor::record_prediction`).
6. **The ADRF can hold a mixed data set and deliver it, but loses per-record timestamps when it
   merges.** `store_record` (`nfs/adrf/src/main.cpp`) stores one row per record with its
   `dataSetTag`; a shared collection stores one copy per subscriber tag. RetrievalNotify sends one
   `NadrfDataRetrievalNotification` per (subscription, kind) -- analytics and data separately --
   built by `merge_records`, which concatenates every data record's buckets into one `dataNotif`
   and keeps **only the oldest record's `timeStamp`**. A `GET` by `data-set-id` returns a single
   `NadrfDataStoreRecord` and, for a data set mixing analytics and data, answers with the kind of
   the oldest record only (ADR-0367 discloses this).
7. **NRF notifications carry no time of their own.** `NotificationData`
   (`TS29510_Nnrf_NFManagement.yaml:3605`) has none; `NFProfile.loadTimeStamp`
   (`TS29510_Nnrf_NFManagement.yaml:1560`) exists in the YAML but no code in `nfs/nrf/src` or
   `nfs/nwdaf/src` sets or reads it. The AnLF timestamps observations by receipt time.
8. **The ADRF's address comes from configuration.** `mtlf.adrf_base_url` / `adrf_base_url`; the
   ADRF's NRF profile carries no `ipEndPoints` (ADRF header comment "our NF profiles carry no
   ipEndPoints"). `Mtlf::adrf_instance_id` learns the ADRF's nfInstanceId by NFDiscovery.

**What the spec asks (TS 23.288 6.2E.2, figure 6.2E.2-1).** The AnLF "may include a DataSetTag
(see clause 6.2B.2) and ADRF ID or only the ADRF ID, which is used to store and fetch the inference
data (including input data, prediction and the ground truth data at the time which the prediction
refers to)" (step 2); after receiving the model it sends the tuple (unique ML model identifier,
DataSetTag and ADRF ID, or ADRF ID only) by Subscribe modification (step 2, second paragraph); the
MTLF "determines the relationship between the ML Model and the DataSetTag" (step 3); the MTLF
retrieves from the indicated ADRF "by invoking Nadrf_DataManagementRetrievalRequest or
Nadrf_DataManagement_Retrieval_Subscribe" (step 8); computes accuracy per 5C.1 (step 10); sends an
accuracy report by Nnwdaf_MLModelProvision_Notify "e.g. when the reporting threshold is met" (step
11); decides to re-train/re-provision (step 12) and notifies the new model (step 13). Steps 4-7 of
the same figure are the AnLF-assisted feed (6.2E.3) -- the spec runs both variants side by side.

### Procedure list (approved 2026-10-22)

"Exists" means already in the tree; "new" means this work; "change" means an existing path is
modified. Every operation and attribute name is grepped from the YAML file and line cited.

| # | TS 23.288 step | NF (role) | Operation / schema (YAML file:line) | State | Test (from the call flow) |
|---|---|---|---|---|---|
| P1 | 6.2E.2 step 1 | consumer -> AnLF | Nnwdaf_AnalyticsSubscription / AnalyticsInfo for NF_LOAD with a future period (prediction) | exists | existing NF_LOAD prediction tests (unchanged) |
| P2 | 6.2E.2 step 2 (first paragraph), 6.2A.2 | AnLF -> MTLF | Nnwdaf_MLModelProvision_Subscribe: `POST /subscriptions` `CreateNWDAFMLModelProvisionSubcription` (`TS29520_Nnwdaf_MLModelProvision.yaml:28-31`), `NwdafMLModelProvSubsc` (:233) with `suppFeats` (:254) carrying `ModelProvisionExt` (TS 29.520 table 5.4.8-1, feature 4); `MLEventSubscription.mlEvRepCon` (:356) -> `MLRepEventCondition{mlAccuracyThreshold, modelMetric}` (:479-490) | change (POST exists; feature negotiation new) | `NwdafMtlfInferenceData.ProvisionSubscriptionNegotiatesModelProvisionExt`: 201 body `suppFeats` has bit 4; without it `inferDataForModel` on a later PUT is ignored and logged |
| P3 | 6.2B.2 steps 1-4 (storage of the inference data the step 2 tag names) | AnLF -> ADRF | Nadrf_DataManagement_StorageRequest: `POST /data-store-records` `CreateADRFDataStoreRecord` (`TS29575_Nadrf_DataManagement.yaml:26-29`), `NadrfDataStoreRecord` (:613, oneOf `anaSub`+`anaNotifications` / `dataSub`+`dataNotif` :616-622) with `dataSetTag` (:648 -> `DataSetTag{dataSetId, dataSetDesc}` :965) and `storeHandl` (`StorageHandlingInfo.lifetime` :924). Prediction = `anaNotifications[]` `NnwdafEventsSubscriptionNotification` (`TS29520_Nnwdaf_EventsSubscription.yaml:741`, `subscriptionId` required :771-772) -> `EventNotification{event, start, expiry, timeStampGen, nfLoadLevelInfos}` (:780-802) -> `NfLoadLevelInformation{nfType, nfInstanceId, nfLoadLevelAverage, confidence}` (:1458); `anaSub` = `NnwdafEventsSubscription` (:414, `eventSubscriptions` required) | new | `NwdafMtlfInferenceData.AnlfStoresEachPredictionUnderTheModelsDataSet`: a future-period NF_LOAD request -> one analytics record in the ADRF whose `dataSetTag.dataSetId` is the model's tag, `nfLoadLevelAverage` equal to the answer's |
| P4 | 6.2B.3 steps 2a-b (input and ground-truth data under the same tag) | AnLF -> ADRF | Nadrf_DataManagement_StorageSubscriptionRequest: `POST /request-storage-sub` `CreateADRFStorageSubscription` (`TS29575_Nadrf_DataManagement.yaml:390-393`), `NadrfDataStoreSubscription{dataSub{nrfDataSub}, targetNfId = this AnLF, dataSetTag}` (:656); removal `POST /request-storage-sub-removal` (:492-495) with `NadrfDataStoreSubscriptionRef{dataSetId}` (:765) | new (the same request the MTLF already sends, by the AnLF and with the model's tag) | same test: data records with `nrfEventNotifs` appear under the model's tag; on model replacement the subscription is removed after the grace period |
| P5 | 6.2E.2 step 2 (second paragraph) | AnLF -> MTLF | Nnwdaf_MLModelProvision_Subscribe modification: `PUT /subscriptions/{subscriptionId}` `UpdateNWDAFMLModelProvisionSubcription` (`TS29520_Nnwdaf_MLModelProvision.yaml:123-126`), each `MLEventSubscription.inferDataForModel` (:370-371) = `InferenceDataForModelTrain{adrfId \| adrfSetId, dataSetTag, modelId}` (:563-579; `modelId` "may be present only in a subscription modification request", TS 29.520 table 5.4.6.2.17-1) | new | `NwdafMtlfInferenceData.AnlfSendsTheModelDataSetTupleByModification`: after the model notification the MTLF's stored subscription carries `inferDataForModel{adrfId, dataSetTag, modelId = modelUniqueId}`; after a re-provision, the new `modelId` and tag |
| P6 | 6.2E.2 step 3 (second paragraph) | MTLF | PUT handler (TS 29.520 4.5.2.2.3, 200 with `NwdafMLModelProvSubsc`) records (subscription, event, modelId) -> (ADRF, dataSetTag) | change | same test, plus: a `modelId` the MTLF never provisioned for that event is accepted, logged and not monitored (see Q4) |
| P7 | 6.2E.2 steps 8 / 9 (ADRF bullet) | MTLF -> ADRF | Nadrf_DataManagement_RetrievalSubscribe: `POST /data-retrieval-subscriptions` `CreateADRFDataRetrievalSubscription` (`TS29575_Nadrf_DataManagement.yaml:241-244`), `NadrfDataRetrievalSubscription{dataSetId, notificationURI, notifCorrId, timePeriod}` (:693-716); RetrievalNotify callback `adrfDataRetrievalNotification` (:300-303), `NadrfDataRetrievalNotification{notifCorrId, timeStamp, anaNotifications \| dataNotif \| fetchInstruct}` (:728-762); RetrievalUnsubscribe `DELETE /data-retrieval-subscriptions/{subscriptionId}` (:341-344) when the model is replaced. ADRF located by NFDiscovery `target-nf-instance-id` / `target-nf-set-id` (`TS29510_Nnrf_NFDiscovery.yaml:108`, `:581`) | new | `NwdafMtlfInferenceData.MtlfRetrievesTheTaggedDataSet`: the MTLF's inbound route receives both an analytics and a data notification for the tag |
| P8 | (supports P7) 4.2.2.8.2 | ADRF | RetrievalNotify: data records delivered so that each observation keeps its own `DataNotification.timeStamp` (:920) -- see D4 | change (ADRF) | `AdrfRetrievalNotifyKeepsPerRecordTimestamps` (ADRF integration): two data records with distinct `timeStamp`s arrive with both timestamps |
| P9 | 6.2E.2 step 10, 5C.1 | MTLF | Join prediction <-> ground truth (D5), accuracy = correct / total (5C.1), discard rules (step 10, NOTE 3) | new | `MtlfInferenceAccuracy.*` unit tests on the join and the rule (pure function, no network): exact-tolerance edge, no-truth-yet, truth after timeout, unknown instance, fewer than `min_inferences` -> no value |
| P10 | 6.2E.2 step 11, 6.2A.2 "ML Model monitoring reporting mode" | MTLF -> AnLF | Nnwdaf_MLModelProvision_Notify: callback `myNotification` (`TS29520_Nnwdaf_MLModelProvision.yaml:79-82`), `NwdafMLModelProvNotif` (:380) -> `MLEventNotif{event, notifCorreId, mLFileAddr \| mLModelAdrf, modelUniqueId, modelUpdateInd: false, addModelInfo}` (:397-448) -> `AdditionalMLModelInformation{modelUniqueId, mLFileAddr \| mLModelAdrf, modelMetric: ACCURACY, accMLModel, mlDegradInd}` (:492-535); trigger `mlEvRepCon.mlAccuracyThreshold` crossing or `eventReq` (:258) `notifMethod`/`repPeriod` (`TS29523_Npcf_EventExposure.yaml:345-357`) | new (Notify exists for models; accuracy-report content new) | `NwdafMtlfInferenceData.DegradedModelIsReportedThenReprovisioned` step 1: the AnLF (or the test as a second provision subscriber) receives `modelUpdateInd=false`, same `modelUniqueId`, `accMLModel` below threshold, `mlDegradInd=true` |
| P11 | 6.2E.2 step 12 | MTLF | degradation -> re-train (existing `train`, cooldown), degraded marker records its source | change | same test, step 2 |
| P12 | 6.2E.2 step 13 | MTLF -> AnLF | Nnwdaf_MLModelProvision_Notify with the re-trained model, `modelUpdateInd: true` (:439), new `modelUniqueId` (:426) | exists | same test, step 3: re-provision with a new `modelUniqueId`; the AnLF then sends P5 for the new model |
| P13 | 6.2E.2 steps 4-7 | AnLF <-> MTLF | Nnwdaf_MLModelMonitor (ADR-0370) | exists | unchanged; disabled in the P10-P12 test so it cannot be the trigger (D7) |

The acceptance test the issue asks for is `NwdafMtlfInferenceData.DegradedModelIsReportedThenReprovisioned`
(new file `tests/integration/test_nwdaf_mtlf_inference_data.cpp`; real processes: NRF, AnLF,
MTLF, ADRF with Doris + PostgreSQL + Valkey; skips, never passes, without Doris or the sidecar
interpreter, as `test_nwdaf_mtlf.cpp` does). Its flow mirrors figure 6.2E.2-1: bootstrap model
provisioned (P2, P12) -> the AnLF sends the tuple (P5) -> predictions are requested (P1) and stored
(P3) -> ground truth 60 load points away from every prediction is injected through the NRF (P4) ->
the MTLF retrieves (P7), computes (P9), reports (P10) and re-provisions (P11, P12). With the
AnLF-assisted path switched off and the data-growth and interval triggers disabled, the
`modelUpdateInd=true` re-provision can only come from the 6.2E.2 path; the MTLF's degraded marker
source is also asserted. Failure behaviour gets two more tests:
`NwdafMtlfInferenceData.AdrfUnreachableYieldsNoAccuracy` (the tuple names an ADRF that is not
running: no report, no re-train, a warning and the `nwdaf_mtlf_inference_accuracy_total{result="adrf_unreachable"}`
counter) and `NwdafMtlfInferenceData.EmptyDataSetYieldsNoAccuracy` (a tag with no records, or fewer
than `min_inferences` judged pairs: no report, no re-train; `result="insufficient_data"`). No
accuracy value is ever produced from absent data.

**Decision:** Build 6.2E.2 with one DataSetTag per model version, predictions stored as analytics records and the NRF load stream as data records under that tag, retrieved by the MTLF through RetrievalSubscribe, judged by ADR-0370's rule, reported through Nnwdaf_MLModelProvision_Notify, and fed into the existing re-train path alongside the AnLF-assisted variant (options D1a-D8a below).

### Decision options (recommendation marked)

**D1 -- DataSetTag granularity.** (a) **Recommended:** one tag per (AnLF deployment, event, model
version): `dataSetId` = `{config prefix}-{event}-{modelUniqueId}`, `dataSetDesc` human-readable.
Stored analytics carry no model identifier (`NfLoadLevelInformation` and `EventNotification` have
none; `AnalyticsMetadataInfo` neither), so the tag is the only standard link from a prediction to
the model that made it -- which is what step 3's "relationship between the ML Model and the
DataSetTag" is for. Every AnLF replica derives the tag from the active model, so no coordination
is needed; the holder replica (the one owning the provision subscription) sends the PUT.
(b) One stable tag per AnLF and event, re-pointed by each PUT: rejected -- predictions of the old
and new model would share a data set with nothing to tell them apart.

**D2 -- What the AnLF stores as the prediction (P3).** (a) **Recommended:** one analytics record
per NF_LOAD answer that contains model predictions, holding only the `NfLoadLevelInformation`
entries the model produced (those carrying `confidence`; statistical entries are not inference
data), `start`/`expiry` = the predicted period, `timeStampGen` = when it was produced, `anaSub` =
the NF_LOAD subscription or request it answered. (b) One record per instance prediction: more
rows, same information. The `subscriptionId` required by `NnwdafEventsSubscriptionNotification`
has no natural value for an Nnwdaf_AnalyticsInfo request-response answer -- see Q2.

**D3 -- Input and ground-truth data (P4).** For NF_LOAD the input and the ground truth are the
same stream (the instance's NRF-reported load) at different times. (a) **Recommended:** the AnLF
asks the ADRF to collect its NRF data under the model's tag through a storage subscription (6.2B.3)
-- the exact request the MTLF already sends for training (context 2), and the ADRF already stores
a shared collection once per subscriber tag. (b) The AnLF stores each observation itself with
StorageRequest (6.2B.2): more AnLF code, one POST per observation. (c) The MTLF takes ground truth
from its own training data set (`nwdaf-mtlf-nrf-status`) instead of the tagged set: departs from
step 2's "inference data (including input data, prediction and the ground truth data ...)" stored
under the AnLF's tag, and couples monitoring to training retention; rejected.

**D4 -- Keeping the time of each observation (P8).** The join needs the time each load was
observed; today it is lost on retrieval (context 6, 7). (a) **Recommended:** the ADRF's
RetrievalNotify sends data records so that each `DataNotification` keeps its own `timeStamp`
(one notification per stored data record, or per distinct `timeStamp`). The YAML gives
`NadrfDataRetrievalNotification` one `dataNotif` with one `timeStamp` and nothing requires
merging, so this is conformant; it is a change to the ADRF and would be its own PR. (b) The NRF
sets `NFProfile.loadTimeStamp` when an NF reports load, so the time travels in the payload:
spec-faithful, but it changes the NRF and depends on every NF reporting through it; worth doing
later, and the MTLF would prefer `loadTimeStamp` when present. (c) `consTrigNotif` fetch
instructions, then one `GET ?fetch-correlation-ids=` per record: N round trips per evaluation;
rejected.

**D5 -- The ground-truth join and the "correct" rule (P9).** (a) **Recommended:** exactly
ADR-0370's rule, so the two variants are measured identically on the same model (the comparison
the issue names): for each stored prediction (instance, `nfLoadLevelAverage`, `timeStampGen`), the
ground truth is the first observation of the same `nfInstanceId` strictly after `timeStampGen`,
within `truth_timeout_seconds`; correct iff |prediction - load| <= the tolerance the model was
trained with (the MTLF holds it as the model's `accuracyTolerance`); accuracy = correct / total
over a sliding window, computed only with at least `min_inferences` judged pairs (5C.1 NOTE 3
leaves "correct" to the implementation). (b) Ground truth = the mean observed load over the
predicted period [`start`, `expiry`]: arguably closer to 5C.1's "ground truth data at the time
which the prediction refers to" for a period prediction, but then the two variants disagree by
construction; offered as Q3 rather than chosen.

**D6 -- How the MTLF fetches (P7).** (a) **Recommended:** RetrievalSubscribe by `dataSetId`
with a `timePeriod` running from the model's provisioning into the future, so stored and future
records arrive as notifications; the MTLF keeps the received records in Valkey (no in-process
state, ADR-0359) and evaluates on its existing loop tick; it unsubscribes when the model is
replaced (after the truth timeout, so late ground truth still counts). This is the only shape the
YAML allows for one data set holding both analytics and data. (b) RetrievalRequest
(`GET /data-store-records?data-set-id=`, `TS29575_Nadrf_DataManagement.yaml:125-166`): the 200
body is a single `NadrfDataStoreRecord`, whose oneOf cannot carry analytics and data together
(TS 29.575 4.2.2.5.2), so it would need two tags -- but `InferenceDataForModelTrain` carries one
`dataSetTag`; rejected.

**D7 -- Both variants active for the same model.** Figure 6.2E.2-1 itself runs them together
(steps 4-7 next to 8-10), and step 8 leaves the combination to "internal logic". (a)
**Recommended:** either path may declare degradation (OR); one degraded marker per (event,
`modelUniqueId`) that records which path(s) declared it; one re-train per
`retrain_cooldown_seconds`; a report from either path about a model already replaced is
discarded. Two new switches: `mtlf.inference_data_monitoring.enabled` (this path) and
`mtlf.anlf_assisted_monitoring.enabled` (whether the MTLF subscribes at AnLFs, default true,
today's behaviour) -- the acceptance test turns the latter off. (b) Require both to agree: a
silent path would block re-training; rejected. (c) A configured precedence (one path overrides
the other): adds a rule the spec does not suggest; rejected.

**D8 -- Locating the ADRF named by `adrfId` / `adrfSetId` (P7).** (a) **Recommended:**
NFDiscovery by `target-nf-instance-id` / `target-nf-set-id`, with the ADRF registering
`nfServices[nadrf-datamanagement].ipEndPoints` the way ADR-0370 made the NWDAF register its own
(TS 29.510 NFService); an ID that cannot be resolved yields no accuracy (logged, counted), never
a guess. (b) Keep `adrf_base_url` and refuse any `adrfId` other than the configured ADRF's:
works for the single-ADRF lab only; kept as the fallback when discovery returns no endpoint.

### Rejected alternatives (summary)

- A stable tag per AnLF re-pointed by each modification (D1b): predictions of different models
  become indistinguishable.
- Ground truth from the MTLF's training data set (D3c): not the inference data step 2 describes.
- RetrievalRequest by data set (D6b): the single-record response cannot carry a mixed data set.
- Fetch-instruction round trips per record (D4c): N requests per evaluation for no gain.
- Requiring both monitoring variants to agree, or a fixed precedence (D7b/c).
- A hand-written "inference record" DTO (input + prediction + truth in one object): no such
  schema exists in TS 29.575 or TS 29.520; inventing one is exactly what the project's source-of-truth rule forbids.
- MTLF-side inference for the "only input data and ground truth" case of step 10: the MTLF does
  not run ONNX in-process today (training is in the Python sidecar, inference in the AnLF); not in
  scope, disclosed below.

### Increments (each its own PR, each with its tests and traceability rows)

1. ADRF: per-record timestamps on RetrievalNotify (D4a) and `ipEndPoints` in its NRF profile (D8a).
2. MTLF: `ModelProvisionExt` negotiation, PUT handling of `inferDataForModel` (P2, P6), retrieval
   subscription and buffer (P7), the accuracy function with its unit tests (P9).
3. AnLF: storing predictions and the storage subscription per model tag (P3, P4), the PUT
   modification (P5), recognising an accuracy-only notification (Q5).
4. MTLF: accuracy report (P10), degradation source and the D7 rules (P11), the acceptance and
   failure-behaviour integration tests, README ML-pipeline table, `docs/TRACEABILITY.md`, ADR-0370's
   disclosure updated.

### Gaps / not specified (stated, not filled)

- **No stage-3 shape for an "inference data" record.** Stage 2 speaks of inference data
  "including input data, prediction and the ground truth data"; TS 29.575's
  `NadrfDataStoreRecord` stores analytics notifications or data notifications, nothing that ties
  an input, a prediction and a truth together. The design uses analytics records for predictions
  and data records for inputs/truth under one tag, and joins them by instance and time -- an
  implementation choice, not a 3GPP-defined structure.
- **No model identifier on a stored prediction.** Only the tag (D1) links a prediction to its
  model.
- **The "ADRF ID only" case.** Step 2 allows the tuple without a DataSetTag; nothing says how
  the MTLF then finds the right records in that ADRF. Step 8's third bullet points to 6.2B.3
  collection by the MTLF instead. Not designed here -- Q1.
- **The accuracy report has no dedicated attribute.** Step 11's report can only be expressed as
  an `MLEventNotif` restating the current model (its oneOf requires `mLFileAddr` or `mLModelAdrf`)
  with `addModelInfo[].accMLModel` -- the same attribute already used for the training-time
  accuracy (step 3) -- plus `mlDegradInd`. There is no attribute for the number of samples (5C.1
  "optionally the corresponding number of samples") or the evaluation window.
- **`mlDegradInd` wording differs.** The YAML description (`TS29520_Nnwdaf_MLModelProvision.yaml:508-512`)
  says "indicate support degration of an ML model"; TS 29.520 table 5.4.6.2.14-1 says "Set to
  'true' to indicate that the ML model is degraded". This design uses the table's meaning (the
  shape is the same boolean) and flags the YAML text as a likely 3GPP editorial error.
- **"Set of tuples" vs one attribute.** Stage 2 sends a set of (model, tag, ADRF) tuples;
  stage 3 has one `inferDataForModel` per `MLEventSubscription`. One event with several
  provisioned models (multiple-models feature) cannot carry several tuples in one subscription
  entry. NF_LOAD has one model at a time here, so this does not bite yet.
- **Reporting mode direction.** 6.2A.2 says the MTLF reports when the accuracy crosses the
  threshold "either becomes higher or lower"; the YAML's `MLRepEventCondition` carries only the
  threshold value. The design reports on both crossings (as ADR-0370's AnLF does with
  `accuMeetInd`).
- **Step 10's MTLF-side prediction** from input data when no prediction is stored: not built.
- **Data quality (step 10 NOTE 3, MDAS bullet of step 8)**: only structural discards (missing
  instance, missing load, wrong event); no source rating.
- **Other step 8 sources** (Nnf_EventExposure, Ndccf, Nudm_SDM, MDA): not used by this path.

### Open questions for the user

- **Q1.** "ADRF ID only" tuples (no DataSetTag): leave unsupported (accepted, logged, not
  monitored) in this work, or design the 6.2B.3 collection by the MTLF now?
- **Q2.** The value of `NnwdafEventsSubscriptionNotification.subscriptionId` (required) in a stored
  prediction that answered an Nnwdaf_AnalyticsInfo request rather than a subscription: a
  synthetic per-request identifier generated by the AnLF (and said so in the record's
  `dataSetDesc`), or store only predictions made for Nnwdaf_EventsSubscription subscriptions?
- **Q3.** Keep ADR-0370's "next observation" ground truth for both variants (recommended, D5a), or
  move both to "mean over the predicted period" (D5b)?
- **Q4.** A PUT whose `inferDataForModel.modelId` the MTLF never provisioned for that event: TS
  29.520 defines no error cause for it. Accept, log and not monitor (recommended), or reject with
  a generic 400 `INVALID_MSG_FORMAT`-style ProblemDetails?
- **Q5.** When the AnLF receives the MTLF's accuracy report (P10: same `modelUniqueId`,
  `modelUpdateInd=false`), today's handler would overwrite the model's `accuracyPct`, which the
  AnLF uses as the `confidence` of later predictions. Keep `confidence` = training accuracy and
  record the monitored value separately (recommended), or let the monitored accuracy become the
  confidence?
- **Q6.** Is the ADRF change (D4a, D8a) acceptable as increment 1 inside issue #12, or should it be
  a separate issue (it touches a second NF)?

**User decisions (2026-10-22).** The user approved the procedure list and every recommendation
("approve with recommendations"):
- **Q1:** "ADRF ID only" tuples stay unsupported in this work: accepted, logged, not monitored.
- **Q2:** only predictions made for Nnwdaf_EventsSubscription subscriptions are stored. A
  prediction that answered an Nnwdaf_AnalyticsInfo request is not stored, so no synthetic
  `subscriptionId` is generated.
- **Q3:** ADR-0370's "next observation" ground truth for both variants (D5a).
- **Q4:** a PUT naming a `modelId` the MTLF never provisioned for that event is accepted, logged
  and not monitored.
- **Q5:** `confidence` stays the training accuracy; the monitored accuracy is recorded separately.
- **Q6:** the ADRF change (D4a, D8a) is increment 1 inside issue #12.

**Disclosed.** This ADR is a design; no code, test or configuration exists for any "new" row
above. The numbers in the acceptance test (60 load points, thresholds) are the ones ADR-0370's
test uses and are illustrative until the test is written. Nothing here has been measured.
