#pragma once

// SMS-over-NAS codec: the three layers an SMSF sees inside an SMS payload (ADR-0486).
//
//   CP layer  (SM-CP, TS 24.011 clause 7.2 / 8.1, PD+TI per TS 24.007 clause 11.2.3.1)
//     CP-DATA carries an RPDU in its CP-User data element.
//   RP layer  (SM-RL, TS 24.011 clause 7.3 / 8.2)
//     RP-DATA / RP-ACK / RP-ERROR / RP-SMMA; RP-DATA (and optionally RP-ACK/RP-ERROR) carries a
//     TPDU.
//   TP layer  (SM-TL, TS 23.040 clause 9.2)
//     SMS-SUBMIT / SMS-COMMAND / SMS-DELIVER-REPORT (MS -> SC) and SMS-DELIVER /
//     SMS-SUBMIT-REPORT / SMS-STATUS-REPORT (SC -> MS). Decoded here only as far as an SMSF needs:
//     the TP-MTI, TP-MR and the address field (TP-DA). TP-UD is never decoded -- it is carried as
//     opaque octets inside the whole-TPDU byte string.
//
// Sources in specs/3gpp/: TS_24.011_j00.txt, TS_23.040_j00.txt, TS_24.007_j50.txt.
//
// Disclosed limits: the extended TI (TIO = 7, TS 24.007 figure 11.9) is rejected, not decoded;
// SMS-DELIVER / SMS-STATUS-REPORT TPDUs (the MT direction) are classified by TP-MTI but their
// TP-OA / TP-RA are not extracted yet -- the MT path (MAP MT-FORWARD-SHORT-MESSAGE) is a later
// increment.

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <tl/expected.hpp>
#include <vector>

namespace sms_core {

using Bytes = std::vector<std::uint8_t>;

// ---- CP layer (TS 24.011 clause 8.1) ----

// TS 24.007 table 11.2: "1 0 0 1  SMS messages".
inline constexpr std::uint8_t kProtocolDiscriminatorSms = 0x09;

// TS 24.011 table 8.1.
enum class CpMessageType : std::uint8_t {
    Data = 0x01,
    Ack = 0x04,
    Error = 0x10,
};

struct CpMessage {
    // TS 24.007 table 11.3: false = sent from the side that originates the TI.
    bool ti_flag = false;
    std::uint8_t ti_value = 0; // 0..6 (7 = extended TI, unsupported)
    CpMessageType type = CpMessageType::Data;
    Bytes rpdu;             // CP-DATA only: CP-User data contents (max 248 octets, clause 8.1.4.1)
    std::uint8_t cause = 0; // CP-ERROR only: CP-Cause value (table 8.2)
};

tl::expected<CpMessage, std::string> decode_cp(std::span<const std::uint8_t> in);
Bytes encode_cp(const CpMessage& msg);

// The reply direction of a received CP message: same TI value, flipped TI flag (TS 24.007
// table 11.3 -- the reply is "sent to the side that originates the TI").
CpMessage make_cp_ack(const CpMessage& received);
CpMessage make_cp_data_reply(const CpMessage& received, Bytes rpdu);

// ---- RP layer (TS 24.011 clause 8.2) ----

// TS 24.011 table 8.3 -- the MTI values are distinct per direction, so the value alone
// identifies both the message and the direction.
enum class RpMti : std::uint8_t {
    DataMsToNetwork = 0b000,
    DataNetworkToMs = 0b001,
    AckMsToNetwork = 0b010,
    AckNetworkToMs = 0b011,
    ErrorMsToNetwork = 0b100,
    ErrorNetworkToMs = 0b101,
    SmmaMsToNetwork = 0b110,
};

struct RpMessage {
    RpMti mti = RpMti::DataMsToNetwork;
    std::uint8_t message_reference = 0;
    // RP-DATA only: address element CONTENTS (octet 3 onwards of figures 8.5/8.6, i.e. TON/NPI
    // octet + BCD digits), empty when the length octet is 0.
    Bytes originator_address;
    Bytes destination_address;
    std::optional<Bytes> user_data; // the TPDU (mandatory in RP-DATA, optional IEI 0x41 otherwise)
    std::optional<std::uint8_t> cause; // RP-ERROR only: cause value (7 bits, table 8.4)
    std::optional<std::uint8_t> diagnostic;
};

tl::expected<RpMessage, std::string> decode_rp(std::span<const std::uint8_t> in);
Bytes encode_rp(const RpMessage& msg);

// ---- TP layer (TS 23.040 clause 9) ----

enum class Direction { MsToSc, ScToMs };

// TS 23.040 clause 9.2.3.1 (TP-MTI is direction-dependent).
enum class TpduType {
    Deliver,       // 00 SC->MS
    DeliverReport, // 00 MS->SC
    StatusReport,  // 10 SC->MS
    Command,       // 10 MS->SC
    Submit,        // 01 MS->SC
    SubmitReport,  // 01 SC->MS
    Reserved,      // 11
};

TpduType classify_tpdu(std::uint8_t first_octet, Direction dir);

// TS 23.040 clause 9.1.2.5 address field.
struct TpAddress {
    std::uint8_t type_of_address = 0x81; // ext bit 1, TON (bits 6-4), NPI (bits 3-0)
    std::string
        digits; // semi-octet value per clause 9.1.2.3 (0-9 * # a b c); empty if alphanumeric
    Bytes raw;  // the whole field: Address-Length + Type-of-Address + Address-Value

    std::uint8_t type_of_number() const { return (type_of_address >> 4) & 0x07; }
    bool alphanumeric() const { return type_of_number() == 0b101; }
};

// Decodes the address field starting at `in[0]`; `consumed` gets its total length in octets.
tl::expected<TpAddress, std::string> decode_tp_address(std::span<const std::uint8_t> in,
                                                       std::size_t& consumed);
// Encodes a semi-octet address (digits 0-9 * # a b c).
Bytes encode_tp_address(const std::string& digits, std::uint8_t type_of_address = 0x91);

// What an SMSF needs from an MS-originated TPDU.
struct MoTpdu {
    TpduType type = TpduType::Submit;
    std::optional<std::uint8_t> message_reference; // TP-MR (SUBMIT, COMMAND)
    std::optional<TpAddress> destination;          // TP-DA (SUBMIT, COMMAND)
};

tl::expected<MoTpdu, std::string> decode_mo_tpdu(std::span<const std::uint8_t> tpdu);

// TS 33.128 clause 7.4.5.2.1 "3GPP SMS Redaction" of an SMS-SUBMIT (MS->SC) or SMS-DELIVER
// (SC->MS) TPDU: the SM portion of TP-UD is replaced by "Space" in its original encoding for its
// full length (TP-UDL, minus the UDH when TP-UDHI is set); every UDH Information Element not
// classified "SMS Control" (TS 23.040 clause 9.2.3.24 IEI table) becomes a Filler IE (0x26, data
// zeroed, length kept, clause 9.2.3.24.17); no length field changes. Encodings (TS 23.038 clause
// 4): GSM 7 bit default alphabet -> septet 0x20; UCS2 -> 0x0020; 8 bit data -> octet 0x20.
// Disclosed interpretations: "Space" in 8 bit data (which has no defined character set) is taken
// as 0x20; a COMPRESSED TP-UD (TS 23.042, coding group 00xx/01xx bit 5) has no per-character
// space, so its SM octets are zeroed instead. Any other TPDU type is returned as an error (the
// clause applies to SMS-SUBMIT and SMS-DELIVER only).
tl::expected<Bytes, std::string> redact_tpdu(std::span<const std::uint8_t> tpdu, Direction dir);

// TS 23.040 clause 9.2.3.11: 7 semi-octet-swapped octets; tz in quarter hours (signed).
Bytes encode_scts(std::chrono::system_clock::time_point t, int tz_quarter_hours = 0);

// TS 23.040 clause 9.2.2.2a SMS-SUBMIT-REPORT, no optional parameters (TP-PI = 0).
Bytes encode_submit_report_ack(const Bytes& scts);
Bytes encode_submit_report_error(std::uint8_t failure_cause, const Bytes& scts);

} // namespace sms_core
