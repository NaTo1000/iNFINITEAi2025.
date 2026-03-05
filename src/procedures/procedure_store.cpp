// =============================================================================
// procedure_store.cpp — Persistent procedure storage with funny names
// =============================================================================
#include "procedure_store.h"

#ifndef NATIVE_TEST
#include <Arduino.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <esp_log.h>
#include <time.h>
#else
#include <cstdio>
#include <ctime>
#include <cstring>
#include <algorithm>
#define ESP_LOGI(tag, fmt, ...) printf("[" tag "] " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[" tag "][ERR] " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[" tag "][WRN] " fmt "\n", ##__VA_ARGS__)
#endif

// ---------------------------------------------------------------------------
// Funny name word lists
// ---------------------------------------------------------------------------
static const char* const ADJECTIVES[] = {
    "Sneezy", "Wobbly", "Chunky", "Fluffy", "Grumpy",
    "Zappy",  "Funky",  "Slimy", "Jazzy",  "Bouncy",
    "Crispy", "Floppy", "Gloopy","Peppy",  "Snarky",
    "Turbo",  "Mega",   "Ultra", "Hyper",  "Nifty",
};
static const char* const NOUNS[] = {
    "Noodle",  "Waffle",  "Biscuit", "Pickle",  "Muffin",
    "Potato",  "Crumpet", "Banana",  "Burrito", "Pretzel",
    "Sausage", "Pancake", "Nugget",  "Taco",    "Dumpling",
    "Zeppelin","Hamster", "Penguin", "Goblin",  "Gremlin",
};
static const size_t NUM_ADJ  = sizeof(ADJECTIVES) / sizeof(ADJECTIVES[0]);
static const size_t NUM_NOUN = sizeof(NOUNS)      / sizeof(NOUNS[0]);

// ---------------------------------------------------------------------------
std::string ProcedureStore::generateFunnyName(uint32_t seed) {
    uint32_t s    = seed ^ 0xDEADBEEF;
    size_t   adjI = s % NUM_ADJ;
    size_t   nounI= (s / NUM_ADJ) % NUM_NOUN;
    uint32_t ver  = (s % 99) + 1;
    return std::string(ADJECTIVES[adjI]) + std::string(NOUNS[nounI]) +
           "V" + std::to_string(ver);
}

// ---------------------------------------------------------------------------
ProcedureStore::ProcedureStore() {}

bool ProcedureStore::begin() {
#ifndef NATIVE_TEST
    if (!SPIFFS.begin(true)) {
        ESP_LOGE(LOG_TAG_PROC, "SPIFFS mount failed");
        return false;
    }
#endif
    // Load existing procedures into cache
    _cache = loadAll();
    ESP_LOGI(LOG_TAG_PROC, "Loaded %u procedures from storage",
             (unsigned)_cache.size());
    return true;
}

// ---------------------------------------------------------------------------
bool ProcedureStore::save(const std::string& description,
                          const std::string& code,
                          uint32_t iteration) {
    Procedure p;
    p.description = description;
    p.code        = code;
    p.iteration   = iteration;
    p.timestamp   = _currentTimestamp();

    // Generate a unique funny name seeded by iteration + timestamp hash
    uint32_t seed = iteration;
    for (char c : p.timestamp) seed = seed * 31 + static_cast<unsigned char>(c);
    p.name = generateFunnyName(seed);

    // Ensure uniqueness (append iteration if collision)
    for (const auto& existing : _cache) {
        if (existing.name == p.name) {
            p.name += "_" + std::to_string(iteration);
            break;
        }
    }

    ESP_LOGI(LOG_TAG_PROC, "Saving procedure '%s' (iter %u)",
             p.name.c_str(), iteration);

    if (!_persist(p)) return false;
    _cache.push_back(p);
    return true;
}

// ---------------------------------------------------------------------------
std::vector<Procedure> ProcedureStore::loadAll() const {
    std::vector<Procedure> result;
#ifndef NATIVE_TEST
    if (!SPIFFS.exists(_indexPath().c_str())) return result;

    File f = SPIFFS.open(_indexPath().c_str(), "r");
    if (!f) return result;
    JsonDocument index;
    if (deserializeJson(index, f)) { f.close(); return result; }
    f.close();

    for (JsonVariant entry : index["procedures"].as<JsonArray>()) {
        std::string name = entry.as<const char*>();
        Procedure p;
        if (loadByName(name, p)) result.push_back(p);
    }
#endif
    return result;
}

bool ProcedureStore::loadByName(const std::string& name, Procedure& out) const {
#ifndef NATIVE_TEST
    std::string path = _procedurePath(name);
    if (!SPIFFS.exists(path.c_str())) return false;
    File f = SPIFFS.open(path.c_str(), "r");
    if (!f) return false;
    JsonDocument doc;
    if (deserializeJson(doc, f)) { f.close(); return false; }
    f.close();
    out.name        = doc["name"]        | "";
    out.description = doc["description"] | "";
    out.code        = doc["code"]        | "";
    out.timestamp   = doc["timestamp"]   | "";
    out.iteration   = doc["iteration"]   | 0u;
    return true;
#else
    (void)name; (void)out;
    return false;
#endif
}

// ---------------------------------------------------------------------------
std::string ProcedureStore::toJson() const {
#ifndef NATIVE_TEST
    JsonDocument doc;
    JsonArray arr = doc["procedures"].to<JsonArray>();
    for (const auto& p : _cache) {
        JsonObject obj = arr.add<JsonObject>();
        obj["name"]        = p.name.c_str();
        obj["description"] = p.description.c_str();
        obj["timestamp"]   = p.timestamp.c_str();
        obj["iteration"]   = p.iteration;
    }
    std::string out;
    serializeJson(doc, out);
    return out;
#else
    std::string out = "{\"procedures\":[";
    for (size_t i = 0; i < _cache.size(); ++i) {
        if (i) out += ",";
        out += "{\"name\":\"" + _cache[i].name + "\"}";
    }
    out += "]}";
    return out;
#endif
}

size_t ProcedureStore::count() const { return _cache.size(); }

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------
bool ProcedureStore::_persist(const Procedure& p) {
#ifndef NATIVE_TEST
    // Write procedure file
    File pf = SPIFFS.open(_procedurePath(p.name).c_str(), "w");
    if (!pf) {
        ESP_LOGE(LOG_TAG_PROC, "Cannot write %s", _procedurePath(p.name).c_str());
        return false;
    }
    JsonDocument doc;
    doc["name"]        = p.name.c_str();
    doc["description"] = p.description.c_str();
    doc["code"]        = p.code.c_str();
    doc["timestamp"]   = p.timestamp.c_str();
    doc["iteration"]   = p.iteration;
    serializeJson(doc, pf);
    pf.close();

    // Update index
    JsonDocument index;
    File idxRd = SPIFFS.open(_indexPath().c_str(), "r");
    if (idxRd) {
        deserializeJson(index, idxRd);
        idxRd.close();
    }
    index["procedures"].to<JsonArray>(); // ensure array exists
    index["procedures"].as<JsonArray>().add(p.name.c_str());

    File idxWr = SPIFFS.open(_indexPath().c_str(), "w");
    if (!idxWr) return false;
    serializeJson(index, idxWr);
    idxWr.close();
#else
    (void)p;
#endif
    return true;
}

std::string ProcedureStore::_indexPath() const {
    return "/proc/index.json";
}

std::string ProcedureStore::_procedurePath(const std::string& name) const {
    return "/proc/" + name + ".json";
}

std::string ProcedureStore::_currentTimestamp() const {
#ifndef NATIVE_TEST
    time_t now = time(nullptr);
    struct tm t;
    gmtime_r(&now, &t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &t);
    return std::string(buf);
#else
    return "2025-01-01T00:00:00Z";
#endif
}
