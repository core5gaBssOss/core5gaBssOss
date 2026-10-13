## ADR-0484: UPF CC-POI real packet capture: wiring the eBPF ring buffer into on_packet (ADR-0463/0464 stage 4)

**Date:** 2026-10-13. **Status:** Built; kernel-side half not live-verified on a privileged host --
`cc_capable` stays false in any real deployment (ADR-0464's own caveat, unchanged).

**Context.**

ADR-0463/0464 built the UPF's CC-POI (`nfs/upf/src/li_poi.cpp`) and its `on_packet()` tap seam,
but disclosed that nothing in the real datapath called it: "the UPF's eBPF/XDP program is uplink
decapsulation only and cannot attach in CI -- so until the capture stage exists a deployed UPF
intercepts nothing. Tests feed it." This is that capture stage.

The UPF's existing eBPF/XDP uplink datapath (ADR-0043) already pushes every decapsulated T-PDU for
a registered TEID into a BPF ring buffer (`tpdu_ringbuf`), which `nfs/upf/src/datapath.cpp`'s
polling thread reads and writes to a TUN device. No new kernel-side capture mechanism was needed --
every packet the CC-POI would ever need to see is already flowing through that ring buffer. Two
things were missing: (1) the ring buffer record carried no TEID, so userspace had no way to
attribute a record to a session, and (2) nothing called `on_packet()` from that read loop.

**Decision:** wire every decapsulated uplink T-PDU from the existing eBPF ring buffer into the
already-built CC-POI's `on_packet()`, closing the "nothing calls it" half of ADR-0463/0464's
disclosed capture gap, without any new kernel-side capture mechanism.

1. `nfs/upf/bpf/gtpu_decap.bpf.c`: added a `teid` field to `struct tpdu_record` (alongside the
   existing `length`/`data`), populated from the TEID the program already extracts and matches
   against `teid_map`. The only kernel-side change.
2. `nfs/upf/src/datapath.hpp`/`.cpp`: added `Datapath::PacketHandler`, a second optional callback
   to `Datapath::create()`, invoked for every decapsulated T-PDU (not just intercepted ones --
   filtering by TEID is `UpfLiPoi::on_packet()`'s own job via its `session_by_teid_` lookup, a
   cheap no-op otherwise). The ring buffer record mirror struct, the GTP-U header reconstruction,
   and the TUN-write-plus-dispatch logic were pulled out of `datapath.cpp`'s anonymous namespace
   into a new `nfs/upf/src/datapath_internal.hpp` / `upf::detail` namespace specifically so they
   are unit-testable without a real BPF ring buffer, CAP_NET_ADMIN, or CAP_BPF.
3. The ring buffer only ever carried the stripped T-PDU (post-GTP-U-header), but `on_packet()`'s
   contract (and X3 Payload Format 12, "GTP-U message") wants the full GTP-U packet. Rather than
   widen the ring buffer record to carry the original header bytes, `detail::reconstruct_gtpu_header()`
   rebuilds the minimal 8-octet header (version/PT/msg_type/length/teid) in userspace from the
   TEID and length already in the record. This is bit-exact for every record this datapath can
   produce: the XDP program only ever forwards G-PDU (msg_type 255) packets with E/S/PN all 0 to
   the ring buffer in the first place (`gtpu_decap.bpf.c`'s own file header), so those header
   fields are never actually variable for a record that exists at all.
4. `nfs/upf/src/main.cpp`: reordered so `li_poi` is constructed before `Datapath::create()` (it
   previously came after), then wired a lambda capturing `li_poi.get()` as the new packet handler,
   calling `on_packet(teid, PacketDirection::Uplink, gtpu_packet)`.
5. Downlink is unchanged and out of scope -- this project has no downlink datapath at all (ADR-0043's
   own disclosed scope), so the CC-POI's `Downlink` direction remains fed only by tests, same as
   before this ADR.

**Verification:** a new unit test (`tests/unit/test_upf_datapath_internal.cpp`) feeds synthetic
`TpduRecord` values straight into `detail::process_tpdu_record()` -- a plain TUN-fd-less run
(tun_fd=-1, so the `write()` fails and is logged, matching the existing disclosed non-fatal
`write()`-failure path) -- and asserts the `PacketHandler` is invoked with the TEID and the exact
reconstructed GTP-U bytes (header + T-PDU), including the record-too-short and oversized-length
guard paths. This proves the userspace wiring and the header reconstruction byte-for-bit, entirely
in-process, no privileges needed.

**Disclosed gap, carried forward:** the kernel-side half (the BPF program actually attaching to a
live `upf-n3` interface and a real GTP-U packet reaching it) is not exercised by this ADR's own
verification -- that still needs the privileged lab host CAP_NET_ADMIN/CAP_BPF setup ADR-0043
already requires, which this session did not have. `cc_capable` therefore stays `false` for any
real deployment per ADR-0464's own caveat; this ADR only removes the "nothing calls on_packet()"
half of that gap, not the "never run on a privileged host" half.

**Rejected alternatives.**

- *Widen the ring buffer record to carry the original 8-octet GTP-U header bytes verbatim, instead
  of reconstructing them in userspace.* Avoids the "is this reconstruction actually bit-exact"
  argument entirely, but costs 8 more bytes per record (a `bpf_ringbuf_reserve` size the BPF
  verifier needs as a compile-time constant, see `tpdu_record`'s own comment) for information the
  XDP program already decided is fixed the moment it decided to forward the record at all.
  Reconstructing in userspace keeps the kernel-side diff to one field and is provably correct given
  the XDP program's own already-enforced invariants (E/S/PN=0, msg_type=G-PDU) -- not pursued.
- *Have the CC-POI attach its own, second XDP/tc hook independent of the existing decap path.*
  Would be the more "textbook" tap architecture (capture independent of the decap path's own
  fate), but this project has exactly one user-plane ingress point in its lab topology (the
  `upf-n3` veth) and the existing decap path already sees every packet that would matter -- a
  second attach point is a second thing that can fail to attach in CI/without privileges, for no
  additional packets seen. Revisit only if a real N3/gNB-facing interface (not this project's
  current veth stand-in) needs a capture point the decap path doesn't already cover.
- *Flip `cc_capable` to true now that `on_packet()` has a real caller.* Rejected: a real caller
  existing is not the same as the kernel-side capture having been run and observed correct on a
  privileged host, which is what ADR-0464 actually conditioned the flag on. Flipping it without
  that evidence would violate this project's own "never report a metric you have not measured"
  rule.
