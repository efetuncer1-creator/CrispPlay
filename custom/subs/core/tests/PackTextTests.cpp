// SPDX-License-Identifier: AGPL-3.0-only
// CrispPlay subtitle core tests: CouchSubs' shared test vectors (tests/fixtures/golden-vectors.json,
// fuzzy-vectors.json, from core/Tests/CouchSubsCoreTests/Fixtures) run against the PackKey,
// PackFuzzy and LooseLabel ports.
// Based on CouchSubs by Sinan Yorulmaz (https://github.com/yorulmazsinan/couchsubs),
// commit e9019b36265548695c38cfd8599045e1e0ddd54c; ported and modified on 2026-11-12.
// Copyright (C) 2026 Sinan Yorulmaz (original); Copyright (C) 2026 Efe Tuncer / CrispPlay (port).
// Licensed under the GNU Affero General Public License version 3; see custom/subs/core/NOTICE.md.
#include "Test.h"

#include "crispplay/subs/Json.h"
#include "crispplay/subs/PackText.h"
#include "crispplay/subs/Unicode.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace crispplay::subs;

namespace {
json::Value fixture(const char* name) {
    std::ifstream in(std::filesystem::path(CRISPPLAY_SUBS_TEST_DIR) / "fixtures" / name, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    auto v = json::parse(s.str());
    if (!v) subtest::fail(__FILE__, __LINE__, std::string("cannot read fixture ") + name);
    return v ? *v : json::Value{};
}
}  // namespace

TEST(PackKey_Sha256KnownAnswer) {
    CHECK_EQ(PackKey::sha256Hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(PackKey::sha256Hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
}

TEST(PackKey_GoldenVectors) {
    const auto vectors = fixture("golden-vectors.json");
    CHECK(vectors.array.size() >= 15);
    for (const auto& v : vectors.array) {
        const auto& input = v.find("input")->string;
        CHECK_EQ(PackKey::canon(input), v.find("canon")->string);
        CHECK_EQ(PackKey::key(input).value_or(""), v.find("key")->string);
    }
}

TEST(PackKey_StripTags) {
    CHECK_EQ(PackKey::stripTags("<b><h>SCANNATURI</></> KNIFE"), std::string("  SCANNATURI   KNIFE"));
    CHECK_EQ(PackKey::stripTags("a < b"), std::string("a < b"));
}

TEST(PackFuzzy_SignatureVectors) {
    const auto vectors = fixture("fuzzy-vectors.json");
    const auto* signatures = vectors.find("signatures");
    CHECK(signatures && signatures->array.size() >= 14);
    if (!signatures) return;
    for (const auto& v : signatures->array) {
        const auto& input = v.find("input")->string;
        CHECK_EQ(uni::encode(PackFuzzy::folded(input)), v.find("folded")->string);
        CHECK_EQ(PackFuzzy::damaged(input), v.find("damaged")->boolean);
        const auto sig = PackFuzzy::signature(input);
        const auto* bloom = v.find("bloom");
        if (bloom->type == json::Value::Type::Null) {
            CHECK(!sig.has_value());
            continue;
        }
        CHECK(sig.has_value());
        if (!sig) continue;
        CHECK_EQ(sig->first, static_cast<int>(v.find("length")->number));
        CHECK_EQ(PackFuzzy::bits(sig->first), static_cast<int>(v.find("bits")->number));
        CHECK_EQ(sig->second, bloom->string);
    }
}

TEST(PackFuzzy_SimilarityVectors) {
    const auto vectors = fixture("fuzzy-vectors.json");
    for (const auto& v : vectors.find("similarities")->array) {
        const int bits = static_cast<int>(v.find("bits")->number);
        const double got = PackFuzzy::similarity(PackFuzzy::bloom(PackFuzzy::folded(v.find("query")->string), bits),
                                                 PackFuzzy::bloom(PackFuzzy::folded(v.find("entry")->string), bits), bits);
        CHECK(std::abs(got - v.find("similarity")->number) < 1e-6);
    }
}

TEST(PackFuzzy_ThresholdVectors) {
    const auto vectors = fixture("fuzzy-vectors.json");
    const auto* t = vectors.find("thresholds");
    CHECK_EQ(static_cast<int>(t->find("minLength")->number), PackFuzzy::MinLength);
    CHECK_EQ(t->find("margin")->number, PackFuzzy::Margin);
    for (const char* kind : {"clean", "damaged"}) {
        int previous = 0;
        for (const auto& row : t->find(kind)->array) {
            const int limit = static_cast<int>(row.array[0].number);
            const double value = row.array[1].number;
            for (int length = previous; length < limit; length += 3)
                CHECK_EQ(PackFuzzy::required(length, std::string(kind) == "damaged"), value);
            previous = limit;
        }
    }
}

TEST(LooseLabel_Vectors) {
    const auto vectors = fixture("fuzzy-vectors.json");
    for (const auto& v : vectors.find("looseLabels")->array) {
        const auto got = looseLabel(v.find("input")->string);
        const auto* label = v.find("label");
        if (label->type == json::Value::Type::Null) {
            CHECK(!got.has_value());
            continue;
        }
        CHECK(got.has_value());
        if (!got) continue;
        CHECK_EQ(got->first, label->string);
        CHECK_EQ(got->second, v.find("body")->string);
    }
}
