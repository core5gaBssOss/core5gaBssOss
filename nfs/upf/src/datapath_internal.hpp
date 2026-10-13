#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "datapath.hpp"

// ADR-0484 (LI stage 4): the part of datapath.cpp's ring-buffer handling that has real logic
// (TUN write + CC-POI dispatch, independent of any actual BPF ring buffer) lives here instead of
// datapath.cpp's own anonymous namespace, specifically so a unit test can call it directly without
// linking a real eBPF program or needing CAP_NET_ADMIN/CAP_BPF. Everything else in datapath.cpp
// (the ring_buffer__poll plumbing, veth/TUN/XDP setup) stays private to that file -- there's
// nothing to unit-test there that doesn't require a real kernel.
namespace upf::detail {

// Mirrors nfs/upf/bpf/gtpu_decap.bpf.c's struct tpdu_record exactly -- the fixed-size ring buffer
// record format the BPF verifier's constant-size requirement on bpf_ringbuf_reserve forced (see
// that struct's own comment). `length` is the real T-PDU size; `data`'s bytes beyond that are
// stale/unused ring buffer memory, never written to the TUN device. `teid` is the tunnel this
// record came from, used to feed the CC-POI.
struct TpduRecord {
    std::uint16_t length;
    std::uint32_t teid;
    unsigned char data[1500];
} __attribute__((packed));

// Reconstructs the minimal 8-octet GTP-U header (TS 29.281 Figure 5.1-1) this datapath stripped
// before buffering, so the CC-POI's on_packet() gets the real GTP-U wire bytes (X3 Payload Format
// 12 is "GTP-U message", i.e. header + T-PDU, not the bare T-PDU). Bit-exact for every record
// this datapath can produce: the XDP program only ever forwards G-PDU (msg_type 255) packets with
// E/S/PN all 0 to the ring buffer in the first place (gtpu_decap.bpf.c), so those fields are never
// actually variable here.
std::array<std::uint8_t, 8> reconstruct_gtpu_header(std::uint32_t teid, std::uint16_t tpdu_len);

// Pulled out of the ring-buffer callback trampoline (datapath.cpp's Impl::handle_tpdu_sample) so
// it's directly unit-testable: TUN write + CC-POI dispatch for one decapsulated T-PDU record.
void process_tpdu_record(const TpduRecord& rec,
                         std::size_t data_sz,
                         int tun_fd,
                         const upf::Datapath::PacketHandler& on_packet);

} // namespace upf::detail
