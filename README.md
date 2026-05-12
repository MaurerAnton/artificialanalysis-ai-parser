# artificialanalysis-ai-parser

Parser for [artificialanalysis.ai](https://artificialanalysis.ai) — extracts AI model data (pricing, benchmarks, speed) **without an API key**.

## Why?

The idea started from [demianarc/artificialanalysisscrapper](https://github.com/demianarc/artificialanalysisscrapper) — a Python scraper that fetched model data from the Artificial Analysis Next.js RSC endpoint. It was a clever approach: the site's React Server Components stream exposed the full dataset (`hostsModels`) in a single 10 MB response, no authentication needed.

However, after the site's redesign ("A new look for Artificial Analysis"), the old line-based parser broke completely. The RSC format changed from simple `key:value` pairs to a chunk-referenced wire format with `I[...]` inline references and `$c:props:...` circular links.

This project:

- **Rewrites the extraction** using regex + bracket-counting instead of line-based parsing
- **Deduplicates** 867 host-model pairs down to 326 unique models (keeping first occurrence with full non-circular data)
- **Cleans** the output to only essential fields (pricing, IQ, speed, context window)
- **Outputs** `models.json` — 314 models with full input/output/cache pricing, ready for downstream use

The result is a self-contained Python script with zero dependencies beyond the standard library.

## Quick start

### C++

```bash
g++ -std=c++17 -O2 artificialanalysis.ai-parser.cpp -lcurl -o aaparser
./aaparser --minimal --pretty          # fetch + save to models.json
```

Requires: `libcurl`, `nlohmann/json` (header-only, auto-downloaded if missing).

### Python

```bash
python3 artificialanalysis.ai-parser.py --minimal --pretty
```

### JavaScript (Node.js)

```js
// Node.js — works without CORS restrictions
const { AAParser } = require('./artificialanalysis.ai-parser.js');
const models = await AAParser.fetch({ minimal: true });
console.log(models[0].name, models[0].price_1m_input_tokens);
```

> **Note:** The JS parser does **not** work directly in the browser. The RSC endpoint requires the custom `rsc` header which triggers a CORS preflight, and the server does not return `Access-Control-Allow-Headers`. Use in Node.js or through a CORS proxy.

### Output

```
Downloading RSC data from https://artificialanalysis.ai/leaderboards/providers?_rsc=hgvan ...
Downloaded 10,481,155 bytes
Extracted 867 raw entries (host-model pairs)
Deduplicated to 326 unique models
Models with pricing: 314

Saved 314 models to models.json (134,549 bytes)

Top model: GPT-5.5 (xhigh) (OpenAI)
  IQ: 60.24 | Coding: 59.12 | Math: None
  Price: $5.00 in / $30.00 out
  Speed: 57 tok/s
```

## models.json structure

Each entry:

| Field | Description |
|---|---|
| `name` | Model name |
| `creator` | AI lab / company |
| `slug` | URL-friendly identifier |
| `intelligence_index` | AA Intelligence Index score |
| `coding_index` | AA Coding Index score |
| `math_index` | AA Math Index score |
| `price_1m_input_tokens` | Input price per 1M tokens (USD) |
| `price_1m_output_tokens` | Output price per 1M tokens (USD) |
| `price_1m_cache_hit` | Cache hit price per 1M tokens (USD) |
| `blended_price_3_1` | Blended price at 3:1 input:output ratio |
| `context_window_tokens` | Context window size |
| `output_tokens_per_second` | Generation speed |
| `time_to_first_token_ms` | Latency to first token |
| `reasoning` | Whether it's a reasoning model |
| `open_weights` | Whether weights are open |

## Data coverage

| Metric | Coverage |
|---|---|
| Pricing (input/output) | 100% (314/314) |
| Intelligence Index | 87% |
| Coding Index | 90% |
| Math Index | 60% |
| Speed (tok/s) | 100% |
| Cache pricing | 33% |

## How it works

```text
artificialanalysis.ai
  └─ /leaderboards/providers?_rsc=hgvan
       └─ Next.js RSC stream (10 MB, text/x-component)
            └─ Contains "hostsModels":[{...}] with ~867 entries
                 └─ Extract JSON via bracket-counting
                      └─ Deduplicate by model_id
                           └─ Clean & output models.json
```

The RSC endpoint requires specific headers (`rsc: 1`, `next-router-state-tree`, `next-url`) but no cookies or authentication.

## Limitations

- **No API key = fragile.** The RSC endpoint is an internal Next.js mechanism. If the site changes its chunk format again, the bracket-counting may need updating.
- **Circular references.** From the 2nd entry onward, some nested model fields use `$c:props:...` reference strings instead of actual values. We keep only the *first* occurrence per `model_id` (which has full data).
- **Official API is preferred** for production use. This parser is a workaround for when you don't have (or don't want) an API key. See [artificialanalysis.ai/documentation](https://artificialanalysis.ai/documentation) for the free API tier (1,000 req/day).

## Companion: interactive cost calculator

`dashboard.html` — a dark-themed token cost dashboard that lets you see how much you'd spend using different AI model providers.

1. Open `dashboard.html` in a browser (or serve via any HTTP server)
2. It loads `paths.json` → `data.json` + `models.json`
3. Select a model — prices auto-fill from Artificial Analysis data
4. Tweak token counts — costs recalculate instantly

Example files included:
- `example-paths.json` — points to `example-data.json` and `models.json`
- `example-data.json` — 7 days of synthetic token data for demo

To use your own data, rename `example-paths.json` → `paths.json`, point it at your data file, and update your `data.json` with real token counts.

## License

GPL-3.0 — Copyright (C) 2026 Anton Maurer

## Credits

- Original scraping concept by [demianarc/artificialanalysisscrapper](https://github.com/demianarc/artificialanalysisscrapper)
- Model data source: [artificialanalysis.ai](https://artificialanalysis.ai)
