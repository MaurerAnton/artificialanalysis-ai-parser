#!/usr/bin/env python3
"""
fetch_aa.py — Extract model data from artificialanalysis.ai RSC endpoint
No API key required. Outputs clean JSON for the cost calculator.

Usage:
    python3 fetch_aa.py                    # fetch + parse, save to models.json
    python3 fetch_aa.py --file aa.txt      # parse existing RSC dump
    python3 fetch_aa.py --out models.json  # custom output path
    python3 fetch_aa.py --minimal          # only fields needed for calculator
"""

import json
import sys
import os
import argparse
import time
from urllib.request import Request, urlopen
from urllib.error import URLError

RSC_URL = "https://artificialanalysis.ai/leaderboards/providers?_rsc=hgvan"
RSC_HEADERS = {
    "accept": "*/*",
    "rsc": "1",
    "next-router-prefetch": "1",
    "next-router-state-tree": '[["","pages",["leaderboards",["models",["__PAGE__",{},"/leaderboards/models","refresh"]]]],null,null,true]',
    "next-url": "/leaderboards/models",
    "user-agent": "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/138.0.0.0 Safari/537.36",
}


def fetch_rsc(timeout=60):
    """Download the RSC stream from artificialanalysis.ai."""
    print(f"Downloading RSC data from {RSC_URL} ...")
    req = Request(RSC_URL, headers=RSC_HEADERS)
    try:
        with urlopen(req, timeout=timeout) as resp:
            if resp.status != 200:
                print(f"Error: HTTP {resp.status}")
                return None
            raw = resp.read()
            print(f"Downloaded {len(raw):,} bytes")
            return raw
    except URLError as e:
        print(f"Network error: {e}")
        return None


def extract_hosts_models(raw):
    """Extract the 'hostsModels' JSON array from the RSC stream."""
    idx = raw.find(b'"hostsModels"')
    if idx < 0:
        print("Error: 'hostsModels' not found in response")
        return None

    arr_start = raw.find(b'[', idx)
    if arr_start < 0 or arr_start - idx > 500:
        print("Error: could not find array start after 'hostsModels'")
        return None

    # Bracket-counting to find the matching close bracket
    depth = 1
    pos = arr_start + 1
    while depth > 0 and pos < len(raw):
        b = raw[pos]
        if b == 0x5B:     # [
            depth += 1
        elif b == 0x5D:   # ]
            depth -= 1
        if depth > 0:
            pos += 1

    json_bytes = raw[arr_start:pos + 1]
    try:
        return json.loads(json_bytes)
    except json.JSONDecodeError as e:
        print(f"JSON parse error at byte {e.pos}: {e.msg}")
        return None


def deduplicate_models(entries):
    """Deduplicate by model_id, keeping first occurrence (has full data)."""
    seen = {}
    for entry in entries:
        mid = entry.get("model_id")
        if mid and mid not in seen:
            seen[mid] = entry
    return list(seen.values())


def clean_model(entry):
    """Extract clean model data from a raw entry."""
    model = entry.get("model", {})
    ts = entry.get("timescaleData", {}) or {}
    host = entry.get("host", {}) or {}
    creator = model.get("model_creators", {}) or {}

    return {
        "name": model.get("name", entry.get("model_label", "?")),
        "creator": creator.get("name", host.get("name", "?")),
        "provider": host.get("name", "?"),
        "slug": model.get("slug", ""),
        "intelligence_index": model.get("intelligence_index"),
        "coding_index": model.get("coding_index"),
        "math_index": model.get("math_index"),
        "agentic_index": model.get("agentic_index"),
        "price_1m_input_tokens": entry.get("price_1m_input_tokens"),
        "price_1m_output_tokens": entry.get("price_1m_output_tokens"),
        "price_1m_cache_hit": entry.get("cache_hit_price"),
        "blended_price_3_1": entry.get("price_1m_blended_3_1"),
        "blended_price_7_2_1": entry.get("price_1m_blended_7_2_1"),
        "context_window_tokens": entry.get("context_window_tokens"),
        "output_tokens_per_second": ts.get("median_output_speed"),
        "time_to_first_token_ms": (
            round(ts["median_time_to_first_token"] * 1000, 1)
            if ts.get("median_time_to_first_token") else None
        ),
        "reasoning": model.get("reasoning_model", False),
        "open_weights": model.get("is_open_weights", False),
        "release_date": model.get("release_date"),
        "gpqa": model.get("gpqa"),
        "mmlu_pro": model.get("mmlu_pro"),
        "hle": model.get("hle"),
    }


def compress_for_calculator(models):
    """Return minimal fields needed by the aiprice.html calculator."""
    result = []
    for m in models:
        result.append({
            "name": m["name"],
            "creator": m["creator"],
            "slug": m["slug"],
            "intelligence_index": m["intelligence_index"],
            "coding_index": m["coding_index"],
            "math_index": m["math_index"],
            "price_1m_input_tokens": m["price_1m_input_tokens"],
            "price_1m_output_tokens": m["price_1m_output_tokens"],
            "price_1m_cache_hit": m["price_1m_cache_hit"],
            "blended_price_3_1": m["blended_price_3_1"],
            "context_window_tokens": m["context_window_tokens"],
            "output_tokens_per_second": m["output_tokens_per_second"],
            "time_to_first_token_ms": m["time_to_first_token_ms"],
            "reasoning": m["reasoning"],
            "open_weights": m["open_weights"],
        })
    return result


def main():
    parser = argparse.ArgumentParser(description="Fetch AI model data from artificialanalysis.ai")
    parser.add_argument("--file", help="Parse existing RSC dump file (skip download)")
    parser.add_argument("--out", default="models.json", help="Output JSON file (default: models.json)")
    parser.add_argument("--minimal", action="store_true", help="Output only calculator-essential fields")
    parser.add_argument("--pretty", action="store_true", help="Pretty-print JSON output")
    args = parser.parse_args()

    # Step 1: Get raw data
    if args.file:
        if not os.path.exists(args.file):
            print(f"Error: file '{args.file}' not found")
            sys.exit(1)
        with open(args.file, "rb") as f:
            raw = f.read()
        print(f"Read {len(raw):,} bytes from {args.file}")
    else:
        raw = fetch_rsc()
        if raw is None:
            sys.exit(1)

    # Step 2: Extract hostsModels
    entries = extract_hosts_models(raw)
    if entries is None:
        sys.exit(1)
    print(f"Extracted {len(entries)} raw entries (host-model pairs)")

    # Step 3: Deduplicate
    deduped = deduplicate_models(entries)
    print(f"Deduplicated to {len(deduped)} unique models")

    # Step 4: Clean
    models = [clean_model(e) for e in deduped]

    # Remove entries without pricing
    models_with_price = [m for m in models if m["price_1m_input_tokens"] and m["price_1m_output_tokens"]]
    print(f"Models with pricing: {len(models_with_price)}")

    # Sort by intelligence index descending
    models_with_price.sort(key=lambda m: m["intelligence_index"] or 0, reverse=True)

    # Step 5: Output
    if args.minimal:
        output = compress_for_calculator(models_with_price)
    else:
        output = models_with_price

    indent = 2 if args.pretty else None
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(output, f, indent=indent, ensure_ascii=False)

    size = os.path.getsize(args.out)
    print(f"\nSaved {len(output)} models to {args.out} ({size:,} bytes)")

    # Stats
    if output:
        top = output[0]
        print(f"\nTop model: {top['name']} ({top['creator']})")
        print(f"  IQ: {top['intelligence_index']} | Coding: {top['coding_index']} | Math: {top['math_index']}")
        print(f"  Price: ${top['price_1m_input_tokens']:.2f} in / ${top['price_1m_output_tokens']:.2f} out")
        if top.get("output_tokens_per_second"):
            print(f"  Speed: {top['output_tokens_per_second']:.0f} tok/s")


if __name__ == "__main__":
    main()
