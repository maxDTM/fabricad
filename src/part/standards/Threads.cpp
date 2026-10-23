// SPDX-License-Identifier: LGPL-2.1-or-later
#include "part/standards/Threads.h"

#include <cmath>
#include <format>
#include <numeric>

namespace fabricad::part::standards {

namespace {

struct InchRow {
  const char* size;    ///< "#10", "1/4"
  double major;        ///< inches
  int tpi;
  double tapDrill;     ///< inches (ASME B1.1 / Machinery's Handbook, ~75 % thread)
  int series;          ///< 1 UNC, 2 UNF
};

// ASME B1.1 unified threads #0 .. 1 1/2". Tap drills: number/letter/fraction drill sizes.
constexpr InchRow kInchRows[] = {
    {"#0", 0.0600, 80, 0.0469, 2},   {"#1", 0.0730, 64, 0.0595, 1},   {"#1", 0.0730, 72, 0.0595, 2},
    {"#2", 0.0860, 56, 0.0700, 1},   {"#2", 0.0860, 64, 0.0700, 2},   {"#3", 0.0990, 48, 0.0785, 1},
    {"#3", 0.0990, 56, 0.0820, 2},   {"#4", 0.1120, 40, 0.0890, 1},   {"#4", 0.1120, 48, 0.0935, 2},
    {"#5", 0.1250, 40, 0.1015, 1},   {"#5", 0.1250, 44, 0.1040, 2},   {"#6", 0.1380, 32, 0.1065, 1},
    {"#6", 0.1380, 40, 0.1130, 2},   {"#8", 0.1640, 32, 0.1360, 1},   {"#8", 0.1640, 36, 0.1360, 2},
    {"#10", 0.1900, 24, 0.1495, 1},  {"#10", 0.1900, 32, 0.1590, 2},  {"#12", 0.2160, 24, 0.1770, 1},
    {"#12", 0.2160, 28, 0.1820, 2},  {"1/4", 0.2500, 20, 0.2010, 1},  {"1/4", 0.2500, 28, 0.2130, 2},
    {"5/16", 0.3125, 18, 0.2570, 1}, {"5/16", 0.3125, 24, 0.2720, 2}, {"3/8", 0.3750, 16, 0.3125, 1},
    {"3/8", 0.3750, 24, 0.3320, 2},  {"7/16", 0.4375, 14, 0.3680, 1}, {"7/16", 0.4375, 20, 0.3906, 2},
    {"1/2", 0.5000, 13, 0.4219, 1},  {"1/2", 0.5000, 20, 0.4531, 2},  {"9/16", 0.5625, 12, 0.4844, 1},
    {"9/16", 0.5625, 18, 0.5156, 2}, {"5/8", 0.6250, 11, 0.5312, 1},  {"5/8", 0.6250, 18, 0.5781, 2},
    {"3/4", 0.7500, 10, 0.6562, 1},  {"3/4", 0.7500, 16, 0.6875, 2},  {"7/8", 0.8750, 9, 0.7656, 1},
    {"7/8", 0.8750, 14, 0.8125, 2},  {"1", 1.0000, 8, 0.8750, 1},     {"1", 1.0000, 12, 0.9219, 2},
    {"1 1/4", 1.2500, 7, 1.1094, 1}, {"1 1/4", 1.2500, 12, 1.1719, 2}, {"1 1/2", 1.5000, 6, 1.3438, 1},
    {"1 1/2", 1.5000, 12, 1.4219, 2},
};

std::string trim(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  return s;
}

}  // namespace

const std::vector<ThreadSpec>& inchThreads() {
  static const std::vector<ThreadSpec> kTable = [] {
    std::vector<ThreadSpec> out;
    for (const InchRow& r : kInchRows) {
      ThreadSpec t;
      t.designation = std::format("{}-{}", r.size, r.tpi);
      t.nominal = r.major * kInch;
      t.pitch = kInch / r.tpi;
      t.coarse = r.series == 1;
      t.series = r.series;
      t.tapDrillTable = r.tapDrill * kInch;
      out.push_back(t);
    }
    return out;
  }();
  return kTable;
}

const std::vector<std::string>& inchDesignations() {
  static const std::vector<std::string> kNames = [] {
    std::vector<std::string> out;
    for (const auto& t : inchThreads()) out.push_back(t.designation);
    return out;
  }();
  return kNames;
}

const ThreadSpec* findInchThread(const std::string& designationIn) {
  std::string d = trim(designationIn);
  for (const char* suffix : {" UNC", " UNF", "UNC", "UNF"}) {
    const std::string s(suffix);
    if (d.size() > s.size() && d.compare(d.size() - s.size(), s.size(), s) == 0) {
      d = trim(d.substr(0, d.size() - s.size()));
      break;
    }
  }
  if (d.size() > 1 && d.back() == '-') d.pop_back();
  for (const auto& t : inchThreads())
    if (t.designation == d) return &t;
  for (const auto& t : inchThreads())  // size alone = UNC (or the only series, #0)
    if (sizeOf(t.designation) == d && (t.coarse || t.designation == "#0-80")) return &t;
  return nullptr;
}

const ThreadSpec* findThread(const std::string& designation) {
  if (const ThreadSpec* t = findIsoThread(designation)) return t;
  return findInchThread(designation);
}

std::string sizeOf(const std::string& designation) {
  if (!designation.empty() && designation[0] == 'M') {
    auto x = designation.find('x');
    return x == std::string::npos ? designation : designation.substr(0, x);
  }
  auto dash = designation.rfind('-');
  return dash == std::string::npos ? designation : designation.substr(0, dash);
}

std::string defaultThreadClass(const ThreadSpec& t, bool internal) {
  if (t.isInch()) return internal ? "2B" : "2A";
  return internal ? "6H" : "6g";
}

std::string inchText(double inches) {
  const double n64 = inches * 64.0;
  if (std::abs(n64 - std::round(n64)) < 1e-6 && inches < 4 && inches > 0) {
    long num = std::lround(n64), den = 64;
    long whole = num / den;
    num %= den;
    if (num == 0) return std::format("{}", whole);
    long g = std::gcd(num, den);
    num /= g;
    den /= g;
    return whole ? std::format("{} {}/{}", whole, num, den) : std::format("{}/{}", num, den);
  }
  return std::format("{:.4g}", inches);
}

}  // namespace fabricad::part::standards
