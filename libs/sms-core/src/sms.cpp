#include "sms_core/sms.hpp"

#include <ctime>
#include <format>

namespace sms_core {

namespace {

tl::unexpected<std::string> fail(std::string why) {
    return tl::unexpected(std::move(why));
}

// TS 23.040 clause 9.1.2.3 / TS 24.008 called BCD number: 1010 = '*', 1011 = '#', 1100..1110 =
// a..c, 1111 = filler.
char semi_octet_char(std::uint8_t v) {
    static constexpr char kMap[] = "0123456789*#abc";
    return v < 15 ? kMap[v] : '\0';
}

std::optional<std::uint8_t> semi_octet_value(char c) {
    if (c >= '0' && c <= '9') {
        return static_cast<std::uint8_t>(c - '0');
    }
    switch (c) {
        case '*':
            return 0x0A;
        case '#':
            return 0x0B;
        case 'a':
            return 0x0C;
        case 'b':
            return 0x0D;
        case 'c':
            return 0x0E;
        default:
            return std::nullopt;
    }
}

// Reads an LV element at `pos`, advancing it.
tl::expected<Bytes, std::string>
read_lv(std::span<const std::uint8_t> in, std::size_t& pos, const char* what) {
    if (pos >= in.size()) {
        return fail(std::format("{}: missing length octet", what));
    }
    const std::size_t len = in[pos++];
    if (pos + len > in.size()) {
        return fail(
            std::format("{}: length {} exceeds remaining {} octets", what, len, in.size() - pos));
    }
    Bytes out(in.begin() + static_cast<std::ptrdiff_t>(pos),
              in.begin() + static_cast<std::ptrdiff_t>(pos + len));
    pos += len;
    return out;
}

void put_lv(Bytes& out, const Bytes& v) {
    out.push_back(static_cast<std::uint8_t>(v.size()));
    out.insert(out.end(), v.begin(), v.end());
}

constexpr std::uint8_t kRpUserDataIei = 0x41; // TS 24.011 figure 8.7

std::uint8_t bcd2(int v) { // semi-octet swapped two-digit field (TS 23.040 clause 9.1.2.3)
    return static_cast<std::uint8_t>(((v % 10) << 4) | ((v / 10) % 10));
}

} // namespace

// ---- CP ----

tl::expected<CpMessage, std::string> decode_cp(std::span<const std::uint8_t> in) {
    if (in.size() < 2) {
        return fail("CP message shorter than 2 octets");
    }
    if ((in[0] & 0x0F) != kProtocolDiscriminatorSms) {
        return fail(std::format("CP: protocol discriminator {:#x} is not SMS (0x9)", in[0] & 0x0F));
    }
    CpMessage m;
    m.ti_flag = (in[0] & 0x80) != 0;
    m.ti_value = (in[0] >> 4) & 0x07;
    if (m.ti_value == 7) {
        return fail("CP: extended transaction identifier (TIO=7) not supported");
    }
    switch (in[1]) {
        case static_cast<std::uint8_t>(CpMessageType::Data): {
            m.type = CpMessageType::Data;
            std::size_t pos = 2;
            auto ud = read_lv(in, pos, "CP-User data");
            if (!ud) {
                return tl::unexpected(ud.error());
            }
            if (ud->size() > 248) {
                return fail("CP-User data exceeds 248 octets (TS 24.011 clause 8.1.4.1)");
            }
            m.rpdu = std::move(*ud);
            break;
        }
        case static_cast<std::uint8_t>(CpMessageType::Ack):
            m.type = CpMessageType::Ack;
            break;
        case static_cast<std::uint8_t>(CpMessageType::Error):
            m.type = CpMessageType::Error;
            if (in.size() < 3) {
                return fail("CP-ERROR without CP-Cause");
            }
            m.cause = in[2] & 0x7F;
            break;
        default:
            return fail(std::format("CP: unknown message type {:#x}", in[1]));
    }
    return m;
}

Bytes encode_cp(const CpMessage& msg) {
    Bytes out;
    out.push_back(static_cast<std::uint8_t>(
        (msg.ti_flag ? 0x80 : 0x00) | ((msg.ti_value & 0x07) << 4) | kProtocolDiscriminatorSms));
    out.push_back(static_cast<std::uint8_t>(msg.type));
    if (msg.type == CpMessageType::Data) {
        put_lv(out, msg.rpdu);
    } else if (msg.type == CpMessageType::Error) {
        out.push_back(msg.cause & 0x7F);
    }
    return out;
}

CpMessage make_cp_ack(const CpMessage& received) {
    CpMessage m;
    m.ti_flag = !received.ti_flag;
    m.ti_value = received.ti_value;
    m.type = CpMessageType::Ack;
    return m;
}

CpMessage make_cp_data_reply(const CpMessage& received, Bytes rpdu) {
    CpMessage m;
    m.ti_flag = !received.ti_flag;
    m.ti_value = received.ti_value;
    m.type = CpMessageType::Data;
    m.rpdu = std::move(rpdu);
    return m;
}

// ---- RP ----

tl::expected<RpMessage, std::string> decode_rp(std::span<const std::uint8_t> in) {
    if (in.size() < 2) {
        return fail("RP message shorter than 2 octets");
    }
    const std::uint8_t mti = in[0] & 0x07;
    if (mti == 0b111) {
        return fail("RP: reserved MTI 111");
    }
    RpMessage m;
    m.mti = static_cast<RpMti>(mti);
    m.message_reference = in[1];
    std::size_t pos = 2;

    auto read_optional_user_data = [&]() -> tl::expected<void, std::string> {
        if (pos < in.size()) {
            if (in[pos] != kRpUserDataIei) {
                return fail(std::format("RP: unexpected IEI {:#x}", in[pos]));
            }
            ++pos;
            auto ud = read_lv(in, pos, "RP-User data");
            if (!ud) {
                return tl::unexpected(ud.error());
            }
            m.user_data = std::move(*ud);
        }
        return {};
    };

    switch (m.mti) {
        case RpMti::DataMsToNetwork:
        case RpMti::DataNetworkToMs: {
            auto oa = read_lv(in, pos, "RP-Originator Address");
            if (!oa) {
                return tl::unexpected(oa.error());
            }
            auto da = read_lv(in, pos, "RP-Destination Address");
            if (!da) {
                return tl::unexpected(da.error());
            }
            auto ud = read_lv(in, pos, "RP-User data");
            if (!ud) {
                return tl::unexpected(ud.error());
            }
            if (ud->empty()) {
                return fail("RP-DATA with an empty RP-User data (TPDU is mandatory)");
            }
            m.originator_address = std::move(*oa);
            m.destination_address = std::move(*da);
            m.user_data = std::move(*ud);
            break;
        }
        case RpMti::AckMsToNetwork:
        case RpMti::AckNetworkToMs: {
            if (auto r = read_optional_user_data(); !r) {
                return tl::unexpected(r.error());
            }
            break;
        }
        case RpMti::ErrorMsToNetwork:
        case RpMti::ErrorNetworkToMs: {
            auto cause = read_lv(in, pos, "RP-Cause");
            if (!cause) {
                return tl::unexpected(cause.error());
            }
            if (cause->empty()) {
                return fail("RP-Cause with zero length");
            }
            m.cause = (*cause)[0] & 0x7F;
            if (cause->size() > 1) {
                m.diagnostic = (*cause)[1];
            }
            if (auto r = read_optional_user_data(); !r) {
                return tl::unexpected(r.error());
            }
            break;
        }
        case RpMti::SmmaMsToNetwork:
            break;
    }
    return m;
}

Bytes encode_rp(const RpMessage& msg) {
    Bytes out;
    out.push_back(static_cast<std::uint8_t>(msg.mti));
    out.push_back(msg.message_reference);
    switch (msg.mti) {
        case RpMti::DataMsToNetwork:
        case RpMti::DataNetworkToMs:
            put_lv(out, msg.originator_address);
            put_lv(out, msg.destination_address);
            put_lv(out, msg.user_data.value_or(Bytes{}));
            break;
        case RpMti::ErrorMsToNetwork:
        case RpMti::ErrorNetworkToMs: {
            Bytes cause{static_cast<std::uint8_t>(msg.cause.value_or(41) & 0x7F)};
            if (msg.diagnostic) {
                cause.push_back(*msg.diagnostic);
            }
            put_lv(out, cause);
            [[fallthrough]];
        }
        case RpMti::AckMsToNetwork:
        case RpMti::AckNetworkToMs:
            if (msg.user_data) {
                out.push_back(kRpUserDataIei);
                put_lv(out, *msg.user_data);
            }
            break;
        case RpMti::SmmaMsToNetwork:
            break;
    }
    return out;
}

// ---- TP ----

TpduType classify_tpdu(std::uint8_t first_octet, Direction dir) {
    switch (first_octet & 0x03) {
        case 0b00:
            return dir == Direction::ScToMs ? TpduType::Deliver : TpduType::DeliverReport;
        case 0b01:
            return dir == Direction::MsToSc ? TpduType::Submit : TpduType::SubmitReport;
        case 0b10:
            return dir == Direction::ScToMs ? TpduType::StatusReport : TpduType::Command;
        default:
            return TpduType::Reserved;
    }
}

tl::expected<TpAddress, std::string> decode_tp_address(std::span<const std::uint8_t> in,
                                                       std::size_t& consumed) {
    if (in.size() < 2) {
        return fail("TP address shorter than 2 octets");
    }
    const std::size_t semi_octets = in[0];
    const std::size_t value_octets = (semi_octets + 1) / 2;
    const std::size_t total = 2 + value_octets;
    if (total > 12) {
        return fail("TP address exceeds 12 octets (TS 23.040 clause 9.1.2.5)");
    }
    if (total > in.size()) {
        return fail("TP address truncated");
    }
    TpAddress a;
    a.type_of_address = in[1];
    a.raw.assign(in.begin(), in.begin() + static_cast<std::ptrdiff_t>(total));
    if (!a.alphanumeric()) {
        for (std::size_t i = 0; i < semi_octets; ++i) {
            const std::uint8_t octet = in[2 + i / 2];
            const std::uint8_t v = (i % 2 == 0) ? (octet & 0x0F) : (octet >> 4);
            // Clause 9.1.2.3: a 1111 before the last semi-octet is skipped.
            if (const char c = semi_octet_char(v); c != '\0') {
                a.digits.push_back(c);
            }
        }
    }
    consumed = total;
    return a;
}

Bytes encode_tp_address(const std::string& digits, std::uint8_t type_of_address) {
    Bytes out;
    out.push_back(static_cast<std::uint8_t>(digits.size()));
    out.push_back(type_of_address);
    for (std::size_t i = 0; i < digits.size(); i += 2) {
        const std::uint8_t lo = semi_octet_value(digits[i]).value_or(0x0F);
        const std::uint8_t hi =
            i + 1 < digits.size() ? semi_octet_value(digits[i + 1]).value_or(0x0F) : 0x0F;
        out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    return out;
}

tl::expected<MoTpdu, std::string> decode_mo_tpdu(std::span<const std::uint8_t> tpdu) {
    if (tpdu.empty()) {
        return fail("empty TPDU");
    }
    MoTpdu out;
    out.type = classify_tpdu(tpdu[0], Direction::MsToSc);
    std::size_t da_at = 0;
    switch (out.type) {
        case TpduType::Submit: // octet 1: flags, 2: TP-MR, 3..: TP-DA (clause 9.2.2.2)
            da_at = 2;
            break;
        case TpduType::Command: // flags, TP-MR, TP-PID, TP-CT, TP-MN, TP-DA (clause 9.2.2.4)
            da_at = 5;
            break;
        default: // SMS-DELIVER-REPORT carries neither TP-MR nor TP-DA
            return out;
    }
    if (tpdu.size() < da_at + 2) {
        return fail("TPDU truncated before TP-DA");
    }
    out.message_reference = tpdu[1];
    std::size_t consumed = 0;
    auto da = decode_tp_address(tpdu.subspan(da_at), consumed);
    if (!da) {
        return tl::unexpected("TP-DA: " + da.error());
    }
    out.destination = std::move(*da);
    return out;
}

Bytes encode_scts(std::chrono::system_clock::time_point t, int tz_quarter_hours) {
    const auto shifted = t + std::chrono::minutes(15 * tz_quarter_hours);
    const std::time_t tt = std::chrono::system_clock::to_time_t(shifted);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    Bytes out{bcd2(tm.tm_year % 100),
              bcd2(tm.tm_mon + 1),
              bcd2(tm.tm_mday),
              bcd2(tm.tm_hour),
              bcd2(tm.tm_min),
              bcd2(tm.tm_sec)};
    const int q = tz_quarter_hours < 0 ? -tz_quarter_hours : tz_quarter_hours;
    std::uint8_t tz = bcd2(q % 80);
    if (tz_quarter_hours < 0) {
        tz |= 0x08; // bit 3 of the seventh octet: negative
    }
    out.push_back(tz);
    return out;
}

Bytes encode_submit_report_ack(const Bytes& scts) {
    Bytes out{0x01, 0x00}; // TP-MTI 01 (SUBMIT-REPORT), TP-PI = 0
    out.insert(out.end(), scts.begin(), scts.end());
    return out;
}

Bytes encode_submit_report_error(std::uint8_t failure_cause, const Bytes& scts) {
    Bytes out{0x01, failure_cause, 0x00}; // TP-MTI, TP-FCS, TP-PI = 0
    out.insert(out.end(), scts.begin(), scts.end());
    return out;
}

} // namespace sms_core
