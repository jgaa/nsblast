#pragma once

#include <cstdint>

namespace nsblast::lib::ns  {

// RFC 1035
static constexpr std::uint16_t TYPE_A = 1;
static constexpr std::uint16_t TYPE_NS = 2;
static constexpr std::uint16_t TYPE_CNAME = 5;
static constexpr std::uint16_t TYPE_SOA = 6;
static constexpr std::uint16_t TYPE_WKS = 11;
static constexpr std::uint16_t TYPE_PTR = 11;
static constexpr std::uint16_t TYPE_HINFO = 13;
static constexpr std::uint16_t TYPE_MINFO = 14;
static constexpr std::uint16_t TYPE_MX = 15;
static constexpr std::uint16_t TYPE_TXT = 16;

static constexpr std::uint16_t QTYPE_AXFR = 252;
//static constexpr std::uint16_t QTYPE_MAILB = 253;
static constexpr std::uint16_t QTYPE_ALL= 255;

static constexpr std::uint16_t CLASS_IN = 1;
//static constexpr std::uint16_t CLASS_CH = 3;
//static constexpr std::uint16_t CLASS_HS = 4;

// rfc 35962
static constexpr std::uint16_t TYPE_AAAA = 28;

}
