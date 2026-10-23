// Fallback fuzz driver for compilers without libFuzzer (gcc): replays every corpus file given on
// the command line (files or directories), then mutates them at random (bit flips, byte
// insert/erase/overwrite, chunk duplication, splicing two inputs) and runs each mutant.
//   fuzz_xxx [-runs=N] [-max_total_time=SECONDS] [-seed=S] corpus...
// A crash or sanitizer report aborts the process; the input that caused it is written to
// ./crash-<n> first, as libFuzzer does.
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "FuzzCommon.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

namespace {

std::vector<std::string> gCorpus;

std::string readAll(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

void saveCrashCandidate(const std::string& input) {
  std::ofstream("crash-candidate", std::ios::binary) << input;
}

void runOne(const std::string& input) {
  saveCrashCandidate(input);
  LLVMFuzzerTestOneInput(reinterpret_cast<const uint8_t*>(input.data()), input.size());
}

std::string mutate(std::string s, std::mt19937_64& rng) {
  auto pick = [&](size_t n) { return n ? static_cast<size_t>(rng() % n) : 0; };
  static const char* tokens[] = {"{", "}", "[", "]", ":", ",", "\"", "null", "1e308", "-1", "0", "nan",
                                 "\n", " ", "#", "'", "\"type\"", "\"id\"", "ENDSEC;", "#1=", "solid ", "facet"};
  const int rounds = 1 + static_cast<int>(rng() % 8);
  for (int r = 0; r < rounds; ++r) {
    switch (rng() % 7) {
      case 0: if (!s.empty()) s[pick(s.size())] ^= static_cast<char>(1u << (rng() % 8)); break;
      case 1: s.insert(pick(s.size() + 1), 1, static_cast<char>(rng())); break;
      case 2: if (!s.empty()) s.erase(pick(s.size()), 1 + pick(16)); break;
      case 3: if (!s.empty()) s[pick(s.size())] = static_cast<char>(rng()); break;
      case 4: if (!s.empty()) { size_t a = pick(s.size()); s.insert(pick(s.size() + 1), s.substr(a, 1 + pick(64))); } break;
      case 5: s.insert(pick(s.size() + 1), tokens[pick(std::size(tokens))]); break;
      case 6: if (!gCorpus.empty()) { const std::string& o = gCorpus[pick(gCorpus.size())]; s = s.substr(0, pick(s.size() + 1)) + o.substr(pick(o.size() + 1)); } break;
    }
  }
  if (s.size() > (1u << 20)) s.resize(1u << 20);
  return s;
}

}  // namespace

int main(int argc, char** argv) {
  long runs = 1000;
  double maxTime = 0;
  unsigned long long seed = std::random_device{}();
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a.rfind("-runs=", 0) == 0) runs = std::stol(a.substr(6));
    else if (a.rfind("-max_total_time=", 0) == 0) maxTime = std::stod(a.substr(16));
    else if (a.rfind("-seed=", 0) == 0) seed = std::stoull(a.substr(6));
    else if (a.rfind("-", 0) == 0) continue;  // other libFuzzer flags are ignored
    else if (std::filesystem::is_directory(a)) {
      for (const auto& e : std::filesystem::directory_iterator(a))
        if (e.is_regular_file()) gCorpus.push_back(readAll(e.path()));
    } else {
      gCorpus.push_back(readAll(a));
    }
  }
  LLVMFuzzerInitialize(&argc, &argv);
  for (const std::string& c : gCorpus) runOne(c);
  if (gCorpus.empty()) gCorpus.emplace_back();
  std::mt19937_64 rng(seed);
  const auto start = std::chrono::steady_clock::now();
  long done = 0;
  for (; runs < 0 || done < runs; ++done) {
    if (maxTime > 0 && std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() > maxTime) break;
    runOne(mutate(gCorpus[rng() % gCorpus.size()], rng));
  }
  std::remove("crash-candidate");
  std::printf("Done %ld runs (seed %llu, corpus %zu)\n", done, seed, gCorpus.size());
  return 0;
}
