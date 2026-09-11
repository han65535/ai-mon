#include "quota.hpp"
#include <algorithm>
#include <cmath>

namespace aimon {
static bool percentage(const cJSON *p, int &remaining) {
    if (!cJSON_IsNumber(p) || !std::isfinite(p->valuedouble) || p->valuedouble < 0 || p->valuedouble > 100)
        return false;
    remaining = static_cast<int>(std::floor((100 - p->valuedouble) * 100 + 0.000001));
    return true;
}
static bool window(const cJSON *p, bool claude, uint64_t minutes, QuotaWindow &result) {
    result = {};
    if (!p || cJSON_IsNull(p))
        return true;
    if (!cJSON_IsObject(p) ||
        !percentage(field(p, claude ? "used_percentage" : "used_percent"), result.remaining))
        return false;
    if (!claude && !number(field(p, "window_minutes"), minutes))
        return false;
    if (!minutes || minutes > 31 * 24 * 60)
        return false;
    result.minutes = minutes;
    auto reset = field(p, "resets_at");
    if (reset && !cJSON_IsNull(reset) && (!number(reset, result.resets) || result.resets > 32503680000ULL))
        return false;
    result.available = true;
    return true;
}
bool parse_codex_account(const cJSON *response, uint64_t observed, QuotaSnapshot &result) {
    if (!cJSON_IsObject(response) || !observed)
        return false;
    QuotaSnapshot quota;
    quota.observed = observed;
    auto account = str(response, "accountId");
    if (account.size() > 512)
        return false;
    if (!account.empty())
        quota.scope = hex(hash_bytes(account.data(), account.size()));
    const cJSON *limits = nullptr;
    auto buckets = field(response, "rateLimitsByLimitId");
    if (buckets && !cJSON_IsNull(buckets)) {
        if (!cJSON_IsObject(buckets))
            return false;
        // Never use map order or the legacy single-bucket view as a fallback.
        limits = field(buckets, "codex");
        if (!limits) {
            result = quota; // No main allowance: clear old windows, not another model's quota.
            return true;
        }
    } else {
        limits = field(response, "rateLimits");
    }
    if (!cJSON_IsObject(limits))
        return false;
    if (!field(limits, "primary") || !field(limits, "secondary"))
        return false;
    auto id = str(limits, "limitId");
    auto name = str(limits, "limitName");
    bool keyed_main = buckets && cJSON_IsObject(buckets);
    if ((!keyed_main && id != "codex") || (!id.empty() && id != "codex") ||
        (!name.empty() && name != "codex" && name != "Codex") || !str(limits, "normalModelSlug").empty()) {
        result = quota;
        return true;
    }
    quota.plan = str(limits, "planType");
    if (quota.plan.size() > 64)
        return false;
    for (const char *key : {"primary", "secondary"}) {
        auto p = field(limits, key);
        if (!p) // A full RPC snapshot must explicitly describe both windows.
            return false;
        if (cJSON_IsNull(p))
            continue;
        QuotaWindow value;
        if (!cJSON_IsObject(p) || !percentage(field(p, "usedPercent"), value.remaining) ||
            !number(field(p, "windowDurationMins"), value.minutes) || !value.minutes || value.minutes > 44640)
            return false;
        auto reset = field(p, "resetsAt");
        if (reset && !cJSON_IsNull(reset) && (!number(reset, value.resets) || value.resets > 32503680000ULL))
            return false;
        value.available = true;
        if (value.minutes == 10080) {
            if (quota.weekly.available)
                return false;
            quota.weekly = value;
        } else if (value.minutes <= 1440) {
            if (quota.short_term.available)
                return false;
            quota.short_term = value;
        }
    }
    result = quota;
    return true;
}
bool parse_claude_quota(const std::string &input, uint64_t observed, QuotaSnapshot &result) {
    if (input.size() > 1024 * 1024)
        return false;
    auto doc = parse(input);
    if (!doc || !cJSON_IsObject(doc.get()))
        return false;
    QuotaSnapshot quota;
    quota.observed = observed;
    auto limits = field(doc.get(), "rate_limits");
    // A fresh invocation with no quota clears a previous subscription snapshot.
    if (limits && !cJSON_IsNull(limits)) {
        if (!cJSON_IsObject(limits) || !window(field(limits, "five_hour"), true, 300, quota.short_term) ||
            !window(field(limits, "seven_day"), true, 10080, quota.weekly))
            return false;
    }
    result = quota;
    return true;
}
bool parse_claude_usage(const cJSON *usage, uint64_t observed, QuotaSnapshot &result) {
    auto limits = field(usage, "rate_limits");
    if (!cJSON_IsObject(usage) || !cJSON_IsBool(field(usage, "rate_limits_available")))
        return false;
    QuotaSnapshot quota;
    quota.observed = observed;
    if (cJSON_IsFalse(field(usage, "rate_limits_available"))) {
        result = quota;
        return true;
    }
    if (!cJSON_IsObject(limits))
        return false;
    quota.plan = str(usage, "subscription_type");
    if (quota.plan.size() > 64)
        return false;
    for (int i = 0; i < 2; ++i) {
        auto p = field(limits, i ? "seven_day" : "five_hour");
        if (!p || cJSON_IsNull(p))
            continue;
        auto &value = i ? quota.weekly : quota.short_term;
        if (!cJSON_IsObject(p) || !percentage(field(p, "utilization"), value.remaining))
            return false;
        auto reset = field(p, "resets_at");
        if (reset && !cJSON_IsNull(reset)) {
            if (!cJSON_IsString(reset) || !(value.resets = timestamp(reset->valuestring)))
                return false;
        }
        value.minutes = i ? 10080 : 300;
        value.available = true;
    }
    result = quota;
    return true;
}
bool quota_current(const QuotaWindow &value, uint64_t now) {
    return value.available && (!value.resets || value.resets > now);
}
static bool same(const QuotaWindow &a, const QuotaWindow &b) {
    return a.available == b.available && a.remaining == b.remaining && a.minutes == b.minutes &&
           a.resets == b.resets;
}
bool QuotaStore::insert(const QuotaSnapshot &q, uint64_t now) {
    if (!q.observed || q.observed > now + 300 || q.observed + 7 * 86400 < now)
        return false;
    if (q.observed == latest.observed && q.scope == latest.scope && q.plan == latest.plan &&
        same(q.short_term, latest.short_term) && same(q.weekly, latest.weekly))
        return false;
    if (q.observed >= latest.observed && q.scope != latest.scope) {
        latest = {};
        history.clear();
    }
    if (q.scope != latest.scope && q.observed < latest.observed)
        return false;
    bool newer = q.observed >= latest.observed;
    if (newer)
        latest = q;
    auto position = std::lower_bound(history.begin(), history.end(), q.observed,
                                     [](const auto &a, uint64_t t) { return a.observed < t; });
    if (position != history.end() && position->observed == q.observed) {
        *position = q;
        return newer;
    }
    // Keep changes plus one sample every 30 minutes for an unchanged allowance.
    if (position != history.begin()) {
        const auto &prev = *(position - 1);
        if (q.observed - prev.observed < 1800 && prev.plan == q.plan && same(prev.short_term, q.short_term) &&
            same(prev.weekly, q.weekly))
            return newer;
    }
    history.insert(position, q);
    history.erase(std::remove_if(history.begin(), history.end(),
                                 [&](const auto &p) { return p.observed + 7 * 86400 < now; }),
                  history.end());
    if (history.size() > MaxQuotaHistory)
        history.erase(history.begin(),
                      history.begin() + static_cast<ptrdiff_t>(history.size() - MaxQuotaHistory));
    return true;
}
static cJSON *write_window(const QuotaWindow &value) {
    if (!value.available)
        return cJSON_CreateNull();
    auto p = cJSON_CreateObject();
    put(p, "remaining", uint64_t(value.remaining));
    put(p, "minutes", value.minutes);
    put(p, "resets", value.resets);
    return p;
}
cJSON *quota_json(const QuotaSnapshot &quota) {
    auto p = cJSON_CreateObject();
    put(p, "observed", quota.observed);
    put(p, "plan", quota.plan);
    put(p, "scope", quota.scope);
    cJSON_AddItemToObject(p, "short", write_window(quota.short_term));
    cJSON_AddItemToObject(p, "week", write_window(quota.weekly));
    return p;
}
static bool read_window(const cJSON *p, QuotaWindow &value) {
    value = {};
    if (cJSON_IsNull(p))
        return true;
    uint64_t remaining = 0;
    if (!cJSON_IsObject(p) || !number(field(p, "remaining"), remaining) || remaining > 10000 ||
        !number(field(p, "minutes"), value.minutes) || !value.minutes || value.minutes > 44640 ||
        !number(field(p, "resets"), value.resets) || value.resets > 32503680000ULL)
        return false;
    value.remaining = static_cast<int>(remaining);
    value.available = true;
    return true;
}
bool quota_json(const cJSON *p, QuotaSnapshot &quota) {
    QuotaSnapshot result;
    if (!cJSON_IsObject(p) || !number(field(p, "observed"), result.observed) ||
        !read_window(field(p, "short"), result.short_term) || !read_window(field(p, "week"), result.weekly))
        return false;
    result.plan = str(p, "plan");
    result.scope = str(p, "scope");
    if (result.scope.size() > 64)
        return false;
    if (result.plan.size() > 64 || (result.short_term.available && result.short_term.minutes > 1440) ||
        (result.weekly.available && result.weekly.minutes != 10080))
        return false;
    quota = result;
    return true;
}
} // namespace aimon
