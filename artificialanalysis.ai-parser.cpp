/**
 * artificialanalysis.ai-parser.cpp
 *
 * C++ parser for artificialanalysis.ai model data.
 * Extracts pricing, benchmarks, and speed from the public RSC endpoint
 * — no API key required.
 *
 * Dependencies: libcurl (system), json.hpp (vendored in ./vendor/)
 *
 * Build:
 *   g++ -std=c++17 -O2 artificialanalysis.ai-parser.cpp -lcurl -o aaparser
 *
 * Usage:
 *   ./aaparser                     # fetch + parse, write models.json
 *   ./aaparser --file aa.txt      # parse existing RSC dump
 *   ./aaparser --out models.json  # custom output path
 *   ./aaparser --minimal          # only calculator-essential fields
 *   ./aaparser --pretty           # pretty-print output
 *
 * License: GPL-3.0 — Copyright (C) 2026 Anton Maurer
 */

#include <curl/curl.h>
#include "vendor/json.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using json = nlohmann::json;

// ── HTTP fetch via libcurl ──────────────────────────────────────────────

static size_t write_cb(void* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::string*>(userdata);
    buf->append(static_cast<const char*>(ptr), size * nmemb);
    return size * nmemb;
}

static bool fetch_rsc(std::string& out) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL,
        "https://artificialanalysis.ai/leaderboards/providers?_rsc=hgvan");

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "accept: */*");
    headers = curl_slist_append(headers, "rsc: 1");
    headers = curl_slist_append(headers, "next-router-prefetch: 1");
    headers = curl_slist_append(headers,
        "next-router-state-tree: [[\"\",\"pages\",[\"leaderboards\","
        "[\"models\",[\"__PAGE__\",{},\"/leaderboards/models\",\"refresh\"]]]],null,null,true]");
    headers = curl_slist_append(headers, "next-url: /leaderboards/models");
    headers = curl_slist_append(headers,
        "user-agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36");

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return res == CURLE_OK;
}

// ── RSC extraction ──────────────────────────────────────────────────────

static bool extract_hosts_models(const std::string& raw, json& out) {
    auto idx = raw.find("\"hostsModels\"");
    if (idx == std::string::npos) return false;

    auto arr = raw.find('[', idx);
    if (arr == std::string::npos || arr - idx > 500) return false;

    int depth = 1;
    size_t pos = arr + 1;
    while (depth > 0 && pos < raw.size()) {
        char c = raw[pos];
        if (c == '[') depth++;
        else if (c == ']') depth--;
        if (depth > 0) pos++;
    }

    try {
        out = json::parse(raw.substr(arr, pos - arr + 1));
        return out.is_array();
    } catch (...) {
        return false;
    }
}

// ── Deduplication ───────────────────────────────────────────────────────

static std::vector<json> deduplicate(const json& entries) {
    std::map<std::string, int> seen;
    std::vector<json> result;
    for (const auto& e : entries) {
        auto mid = e.value("model_id", "");
        if (!mid.empty() && seen.find(mid) == seen.end()) {
            seen[mid] = 1;
            result.push_back(e);
        }
    }
    return result;
}

// ── Clean model ─────────────────────────────────────────────────────────

static json clean_model(const json& e) {
    const auto& m   = e.value("model", json::object());
    const auto& ts  = e.value("timescaleData", json::object());
    const auto& h   = e.value("host", json::object());
    const auto& cr  = m.value("model_creators", json::object());

    auto n = [](const json& j, const char* k) -> json {
        auto it = j.find(k);
        return (it != j.end() && !it->is_null()) ? *it : json(nullptr);
    };

    return {
        {"name",                     m.value("name", e.value("model_label", "?"))},
        {"creator",                  cr.value("name", h.value("name", "?"))},
        {"provider",                 h.value("name", "?")},
        {"slug",                     m.value("slug", "")},
        {"intelligence_index",       n(m, "intelligence_index")},
        {"coding_index",             n(m, "coding_index")},
        {"math_index",               n(m, "math_index")},
        {"agentic_index",            n(m, "agentic_index")},
        {"price_1m_input_tokens",    n(e, "price_1m_input_tokens")},
        {"price_1m_output_tokens",   n(e, "price_1m_output_tokens")},
        {"price_1m_cache_hit",       n(e, "cache_hit_price")},
        {"blended_price_3_1",        n(e, "price_1m_blended_3_1")},
        {"context_window_tokens",    n(e, "context_window_tokens")},
        {"output_tokens_per_second", n(ts, "median_output_speed")},
        {"reasoning",                m.value("reasoning_model", false)},
        {"open_weights",             m.value("is_open_weights", false)},
        {"release_date",             n(m, "release_date")},
        {"gpqa",                     n(m, "gpqa")},
        {"mmlu_pro",                 n(m, "mmlu_pro")},
        {"hle",                      n(m, "hle")},
    };
}

// ── Compress for calculator ─────────────────────────────────────────────

static json compress_for_calculator(const json& m) {
    auto n = [](const json& j, const char* k) -> json {
        auto it = j.find(k);
        return (it != j.end() && !it->is_null()) ? *it : json(nullptr);
    };
    return {
        {"name",                     n(m, "name")},
        {"creator",                  n(m, "creator")},
        {"slug",                     n(m, "slug")},
        {"intelligence_index",       n(m, "intelligence_index")},
        {"coding_index",             n(m, "coding_index")},
        {"math_index",               n(m, "math_index")},
        {"price_1m_input_tokens",    n(m, "price_1m_input_tokens")},
        {"price_1m_output_tokens",   n(m, "price_1m_output_tokens")},
        {"price_1m_cache_hit",       n(m, "price_1m_cache_hit")},
        {"blended_price_3_1",        n(m, "blended_price_3_1")},
        {"context_window_tokens",    n(m, "context_window_tokens")},
        {"output_tokens_per_second", n(m, "output_tokens_per_second")},
        {"time_to_first_token_ms",   n(m, "time_to_first_token_ms")},
        {"reasoning",                n(m, "reasoning")},
        {"open_weights",             n(m, "open_weights")},
    };
}

// ── Main ────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    std::string file_arg, out_arg = "models.json";
    bool minimal = false, pretty = false;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--file" && i + 1 < argc) file_arg = argv[++i];
        else if (a == "--out"  && i + 1 < argc) out_arg  = argv[++i];
        else if (a == "--minimal") minimal = true;
        else if (a == "--pretty")  pretty  = true;
        else if (a == "--help") {
            std::cout << "Usage: aaparser [--file FILE] [--out FILE] [--minimal] [--pretty]\n";
            return 0;
        }
    }

    // Step 1: get raw data
    std::string raw;
    if (!file_arg.empty()) {
        std::ifstream f(file_arg, std::ios::binary);
        if (!f) { std::cerr << "Cannot open " << file_arg << "\n"; return 1; }
        raw.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        std::cerr << "Read " << raw.size() << " bytes from " << file_arg << "\n";
    } else {
        std::cerr << "Downloading RSC data...\n";
        if (!fetch_rsc(raw)) { std::cerr << "Download failed\n"; return 1; }
        std::cerr << "Downloaded " << raw.size() << " bytes\n";
    }

    // Step 2: extract hostsModels
    json entries;
    if (!extract_hosts_models(raw, entries)) {
        std::cerr << "Failed to extract hostsModels\n";
        return 1;
    }
    std::cerr << "Extracted " << entries.size() << " raw entries\n";

    // Step 3: deduplicate
    auto deduped = deduplicate(entries);
    std::cerr << "Deduplicated to " << deduped.size() << " unique models\n";

    // Step 4: clean
    std::vector<json> models;
    for (auto& e : deduped) {
        auto m = clean_model(e);
        if (!m["price_1m_input_tokens"].is_null() &&
            !m["price_1m_output_tokens"].is_null())
            models.push_back(m);
    }
    std::cerr << "Models with pricing: " << models.size() << "\n";

    // Sort by IQ
    std::sort(models.begin(), models.end(), [](const json& a, const json& b) {
        double ai = a["intelligence_index"].is_null() ? 0 : a["intelligence_index"].get<double>();
        double bi = b["intelligence_index"].is_null() ? 0 : b["intelligence_index"].get<double>();
        return ai > bi;
    });

    // Step 5: output
    json out = json::array();
    for (auto& m : models)
        out.push_back(minimal ? compress_for_calculator(m) : m);

    std::ofstream of(out_arg);
    of << (pretty ? out.dump(2) : out.dump()) << "\n";
    of.close();

    std::cerr << "Saved " << out.size() << " models to " << out_arg
              << " (" << std::ifstream(out_arg, std::ios::ate).tellg() << " bytes)\n";

    if (!out.empty()) {
        auto& t = out[0];
        std::cerr << "\nTop model: " << t["name"] << " (" << t["creator"] << ")\n"
                  << "  IQ: " << t["intelligence_index"]
                  << " | Coding: " << t["coding_index"]
                  << " | Math: " << t["math_index"] << "\n"
                  << "  Price: $" << t["price_1m_input_tokens"]
                  << " in / $" << t["price_1m_output_tokens"] << " out\n";
        if (!t["output_tokens_per_second"].is_null())
            std::cerr << "  Speed: " << t["output_tokens_per_second"].get<int>() << " tok/s\n";
    }

    return 0;
}
