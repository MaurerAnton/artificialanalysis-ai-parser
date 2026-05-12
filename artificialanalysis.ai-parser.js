/**
 * artificialanalysis.ai-parser.js
 * 
 * Node.js parser for artificialanalysis.ai model data.
 * Extracts pricing, benchmarks, and speed from the public RSC endpoint
 * — no API key required.
 * 
 * ⚠️ BROWSER LIMITATION: The RSC endpoint requires the custom 'rsc' header
 * which triggers a CORS preflight. The server does NOT return
 * Access-Control-Allow-Headers, so browser fetch() is blocked.
 * Use in Node.js or through a CORS proxy.
 * 
 * Usage (Node.js):
 *   const { AAParser } = require('./artificialanalysis.ai-parser.js');
 *   const models = await AAParser.fetch();
 *   console.log(models[0].name, models[0].price_1m_input_tokens);
 * 
 * Usage (browser — requires CORS proxy):
 *   <script src="artificialanalysis.ai-parser.js"></script>
 *   <script>
 *     // Only works with a CORS proxy that strips/forwards custom headers
 *     const models = await AAParser.fetch();
 *   </script>
 * 
 * License: GPL-3.0 — Copyright (C) 2026 Anton Maurer
 */

(function (root, factory) {
    if (typeof define === 'function' && define.amd) {
        define([], factory);
    } else if (typeof module === 'object' && module.exports) {
        module.exports = factory();
    } else {
        root.AAParser = factory();
    }
}(typeof self !== 'undefined' ? self : this, function () {

    'use strict';

    var RSC_URL = 'https://artificialanalysis.ai/leaderboards/providers?_rsc=hgvan';
    var RSC_HEADERS = {
        'accept': '*/*',
        'rsc': '1',
        'next-router-prefetch': '1',
        'next-router-state-tree': '[["","pages",["leaderboards",["models",["__PAGE__",{},"/leaderboards/models","refresh"]]]],null,null,true]',
        'next-url': '/leaderboards/models'
    };

    /**
     * Download the RSC stream as raw text.
     * @returns {Promise<string>}
     */
    function fetchRSC() {
        if (typeof fetch === 'undefined') {
            return Promise.reject(new Error('fetch() not available — use Node.js 18+ or a browser'));
        }
        return fetch(RSC_URL, { headers: RSC_HEADERS })
            .then(function (resp) {
                if (!resp.ok) throw new Error('HTTP ' + resp.status);
                return resp.text();
            });
    }

    /**
     * Extract the 'hostsModels' JSON array from the RSC stream.
     * @param {string} raw
     * @returns {Array<Object>}
     */
    function extractHostsModels(raw) {
        var idx = raw.indexOf('"hostsModels"');
        if (idx === -1) throw new Error("'hostsModels' not found in response");

        var arrStart = raw.indexOf('[', idx);
        if (arrStart === -1 || arrStart - idx > 500)
            throw new Error("Could not find array start after 'hostsModels'");

        // Bracket-counting to find matching close bracket
        var depth = 1;
        var pos = arrStart + 1;
        while (depth > 0 && pos < raw.length) {
            var c = raw.charAt(pos);
            if (c === '[') depth++;
            else if (c === ']') depth--;
            if (depth > 0) pos++;
        }

        return JSON.parse(raw.slice(arrStart, pos + 1));
    }

    /**
     * Deduplicate by model_id, keeping first occurrence (full non-circular data).
     * @param {Array<Object>} entries
     * @returns {Array<Object>}
     */
    function deduplicate(entries) {
        var seen = {};
        var result = [];
        for (var i = 0; i < entries.length; i++) {
            var mid = entries[i].model_id;
            if (mid && !seen[mid]) {
                seen[mid] = true;
                result.push(entries[i]);
            }
        }
        return result;
    }

    /**
     * Clean a raw entry into a tidy model object.
     * @param {Object} entry
     * @returns {Object}
     */
    function cleanModel(entry) {
        var model = entry.model || {};
        var ts = entry.timescaleData || {};
        var host = entry.host || {};
        var creator = (model.model_creators) || {};

        return {
            name: model.name || entry.model_label || '?',
            creator: creator.name || host.name || '?',
            provider: host.name || '?',
            slug: model.slug || '',
            intelligence_index: model.intelligence_index != null ? model.intelligence_index : null,
            coding_index: model.coding_index != null ? model.coding_index : null,
            math_index: model.math_index != null ? model.math_index : null,
            agentic_index: model.agentic_index != null ? model.agentic_index : null,
            price_1m_input_tokens: entry.price_1m_input_tokens != null ? entry.price_1m_input_tokens : null,
            price_1m_output_tokens: entry.price_1m_output_tokens != null ? entry.price_1m_output_tokens : null,
            price_1m_cache_hit: entry.cache_hit_price != null ? entry.cache_hit_price : null,
            blended_price_3_1: entry.price_1m_blended_3_1 != null ? entry.price_1m_blended_3_1 : null,
            context_window_tokens: entry.context_window_tokens != null ? entry.context_window_tokens : null,
            output_tokens_per_second: ts.median_output_speed != null ? ts.median_output_speed : null,
            time_to_first_token_ms: ts.median_time_to_first_token != null
                ? Math.round(ts.median_time_to_first_token * 1000 * 10) / 10
                : null,
            reasoning: !!model.reasoning_model,
            open_weights: !!model.is_open_weights,
            release_date: model.release_date || null,
            gpqa: model.gpqa != null ? model.gpqa : null,
            mmlu_pro: model.mmlu_pro != null ? model.mmlu_pro : null,
            hle: model.hle != null ? model.hle : null
        };
    }

    /**
     * Strip to minimal fields for the cost calculator.
     * @param {Array<Object>} models
     * @returns {Array<Object>}
     */
    function compress(models) {
        var out = [];
        for (var i = 0; i < models.length; i++) {
            var m = models[i];
            out.push({
                name: m.name,
                creator: m.creator,
                slug: m.slug,
                intelligence_index: m.intelligence_index,
                coding_index: m.coding_index,
                math_index: m.math_index,
                price_1m_input_tokens: m.price_1m_input_tokens,
                price_1m_output_tokens: m.price_1m_output_tokens,
                price_1m_cache_hit: m.price_1m_cache_hit,
                blended_price_3_1: m.blended_price_3_1,
                context_window_tokens: m.context_window_tokens,
                output_tokens_per_second: m.output_tokens_per_second,
                time_to_first_token_ms: m.time_to_first_token_ms,
                reasoning: m.reasoning,
                open_weights: m.open_weights
            });
        }
        return out;
    }

    /**
     * Fetch, parse, deduplicate, and clean models from artificialanalysis.ai.
     * 
     * @param {Object} [opts]
     * @param {boolean} [opts.minimal=false] — return only calculator-essential fields
     * @param {string} [opts.raw] — parse an existing RSC text instead of fetching
     * @returns {Promise<Array<Object>>}
     */
    function fetchModels(opts) {
        opts = opts || {};
        var rawPromise;
        if (opts.raw) {
            rawPromise = Promise.resolve(opts.raw);
        } else {
            rawPromise = fetchRSC();
        }

        return rawPromise.then(function (raw) {
            var entries = extractHostsModels(raw);
            var deduped = deduplicate(entries);
            var models = deduped.map(cleanModel);

            // Keep only entries with pricing
            models = models.filter(function (m) {
                return m.price_1m_input_tokens != null && m.price_1m_output_tokens != null;
            });

            // Sort by intelligence index descending
            models.sort(function (a, b) {
                return (b.intelligence_index || 0) - (a.intelligence_index || 0);
            });

            if (opts.minimal) {
                models = compress(models);
            }

            return models;
        });
    }

    // ── Public API ──
    return {
        fetch: fetchModels,
        extractHostsModels: extractHostsModels,
        deduplicate: deduplicate,
        cleanModel: cleanModel,
        compress: compress,
        VERSION: '1.0.0'
    };

}));
