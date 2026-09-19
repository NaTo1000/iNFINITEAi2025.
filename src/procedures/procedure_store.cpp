// =============================================================================
// procedure_store.cpp — Persistent procedure storage with recovery
// =============================================================================
#include "procedure_store.h"
#include "../common/native_json.h"

#ifndef NATIVE_TEST
#include <Arduino.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <esp_log.h>
#include <time.h>
#else
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#define ESP_LOGI(tag, fmt, ...) printf("[" tag "] " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[" tag "][ERR] " fmt "\n", ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[" tag "][WRN] " fmt "\n", ##__VA_ARGS__)
namespace fs = std::filesystem;
#endif

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

std::string ProcedureStore::generateFunnyName(uint32_t seed) {
    uint32_t s = seed ^ 0xDEADBEEFU;
    const size_t adjI = s % NUM_ADJ;
    const size_t nounI = (s / NUM_ADJ) % NUM_NOUN;
    const uint32_t ver = (s % 99U) + 1U;
    return std::string(ADJECTIVES[adjI]) + std::string(NOUNS[nounI]) + "V" + std::to_string(ver);
}

#ifndef NATIVE_TEST
ProcedureStore::ProcedureStore() {}
#else
ProcedureStore::ProcedureStore(std::string rootDir)
    : _rootDir(std::move(rootDir)) {}
#endif

bool ProcedureStore::begin() {
#ifndef NATIVE_TEST
    if (!SPIFFS.begin(true)) {
        ESP_LOGE(LOG_TAG_PROC, "SPIFFS mount failed");
        return false;
    }
#else
    std::error_code ec;
    fs::create_directories(_rootDir, ec);
    if (ec) {
        ESP_LOGE(LOG_TAG_PROC, "%s", "Failed to create native procedure directory");
        return false;
    }
#endif
    _cache = loadAll();
    ESP_LOGI(LOG_TAG_PROC, "Loaded %u procedures from storage", static_cast<unsigned>(_cache.size()));
    return true;
}

bool ProcedureStore::save(const std::string& description,
                          const std::string& code,
                          uint32_t iteration) {
    if (description.empty() || description.size() > PROCEDURE_MAX_DESCRIPTION_BYTES) {
        ESP_LOGW(LOG_TAG_PROC, "%s", "Rejected procedure with invalid description size");
        return false;
    }
    if (code.empty() || code.size() > PROCEDURE_MAX_CODE_BYTES) {
        ESP_LOGW(LOG_TAG_PROC, "%s", "Rejected procedure with invalid body size");
        return false;
    }
    if (_cache.size() >= PROCEDURE_MAX_COUNT) {
        ESP_LOGW(LOG_TAG_PROC, "%s", "Procedure store limit reached");
        return false;
    }
    if (!_hasFreeSpaceFor(code.size() + description.size() + 256U)) {
        ESP_LOGW(LOG_TAG_PROC, "%s", "Insufficient free space for procedure persistence");
        return false;
    }

    Procedure p;
    p.description = description;
    p.code = code;
    p.iteration = iteration;
    p.timestamp = _currentTimestamp();

    uint32_t seed = iteration;
    for (char c : p.timestamp) {
        seed = seed * 31U + static_cast<unsigned char>(c);
    }
    p.name = _sanitizeName(generateFunnyName(seed));
    const std::string baseName = p.name;
    uint32_t suffix = 1U;
    while (std::any_of(_cache.begin(), _cache.end(), [&](const Procedure& existing) {
        return existing.name == p.name;
    })) {
        p.name = baseName + "_" + std::to_string(suffix++);
    }

    if (!_persist(p)) {
        return false;
    }
    _cache.push_back(p);
    return true;
}

std::vector<Procedure> ProcedureStore::loadAll() const {
    std::vector<Procedure> result;
    std::vector<std::string> names = _loadIndexNames();
    if (names.empty()) {
        names = _scanProcedureNames();
        if (!names.empty()) {
            _writeIndex(names);
        }
    }

    for (const std::string& name : names) {
        Procedure p;
        if (loadByName(name, p)) {
            result.push_back(p);
        }
    }
    return result;
}

bool ProcedureStore::loadByName(const std::string& name, Procedure& out) const {
#ifndef NATIVE_TEST
    const std::string path = _procedurePath(name);
    if (!SPIFFS.exists(path.c_str())) {
        return false;
    }
    File f = SPIFFS.open(path.c_str(), "r");
    if (!f) {
        return false;
    }
    JsonDocument doc;
    if (deserializeJson(doc, f)) {
        f.close();
        return false;
    }
    f.close();
    out.name = doc["name"] | "";
    out.description = doc["description"] | "";
    out.code = doc["code"] | "";
    out.timestamp = doc["timestamp"] | "";
    out.iteration = doc["iteration"] | 0U;
    return !out.name.empty();
#else
    std::ifstream in(_procedurePath(name));
    if (!in) {
        return false;
    }
    std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!nativejson::extractStringField(raw, "name", out.name) ||
        !nativejson::extractStringField(raw, "description", out.description) ||
        !nativejson::extractStringField(raw, "code", out.code) ||
        !nativejson::extractStringField(raw, "timestamp", out.timestamp) ||
        !nativejson::extractUIntField(raw, "iteration", out.iteration)) {
        return false;
    }
    return true;
#endif
}

std::string ProcedureStore::toJson() const {
#ifndef NATIVE_TEST
    JsonDocument doc;
    JsonArray arr = doc["procedures"].to<JsonArray>();
    for (const Procedure& p : _cache) {
        JsonObject obj = arr.add<JsonObject>();
        obj["name"] = p.name.c_str();
        obj["description"] = p.description.c_str();
        obj["timestamp"] = p.timestamp.c_str();
        obj["iteration"] = p.iteration;
    }
    std::string out;
    serializeJson(doc, out);
    return out;
#else
    std::string out = "{\"procedures\":[";
    for (size_t i = 0; i < _cache.size(); ++i) {
        if (i) out += ',';
        out += "{\"name\":\"" + nativejson::escapeString(_cache[i].name) +
               "\",\"description\":\"" + nativejson::escapeString(_cache[i].description) +
               "\",\"timestamp\":\"" + nativejson::escapeString(_cache[i].timestamp) +
               "\",\"iteration\":" + std::to_string(_cache[i].iteration) + "}";
    }
    out += "]}";
    return out;
#endif
}

size_t ProcedureStore::count() const {
    return _cache.size();
}

bool ProcedureStore::_persist(const Procedure& p) {
#ifndef NATIVE_TEST
    JsonDocument doc;
    doc["name"] = p.name.c_str();
    doc["description"] = p.description.c_str();
    doc["code"] = p.code.c_str();
    doc["timestamp"] = p.timestamp.c_str();
    doc["iteration"] = p.iteration;

    const std::string path = _procedurePath(p.name);
    File pf = SPIFFS.open(_tempPath(path).c_str(), "w");
    if (!pf) {
        return false;
    }
    if (serializeJson(doc, pf) == 0) {
        pf.close();
        SPIFFS.remove(_tempPath(path).c_str());
        return false;
    }
    pf.close();
    SPIFFS.remove(path.c_str());
    if (!SPIFFS.rename(_tempPath(path).c_str(), path.c_str())) {
        return false;
    }
#else
    const std::string path = _procedurePath(p.name);
    const std::string temp = _tempPath(path);
    std::ofstream out(temp, std::ios::trunc);
    if (!out) {
        return false;
    }
    out << "{\"name\":\"" << nativejson::escapeString(p.name)
        << "\",\"description\":\"" << nativejson::escapeString(p.description)
        << "\",\"code\":\"" << nativejson::escapeString(p.code)
        << "\",\"timestamp\":\"" << nativejson::escapeString(p.timestamp)
        << "\",\"iteration\":" << p.iteration << "}";
    out.close();
    std::error_code ec;
    fs::rename(temp, path, ec);
    if (ec) {
        fs::remove(temp, ec);
        return false;
    }
#endif

    std::vector<std::string> names;
    names.reserve(_cache.size() + 1U);
    for (const Procedure& existing : _cache) {
        names.push_back(existing.name);
    }
    names.push_back(p.name);
    return _writeIndex(names);
}

bool ProcedureStore::_writeIndex(const std::vector<std::string>& names) const {
#ifndef NATIVE_TEST
    JsonDocument index;
    JsonArray arr = index["procedures"].to<JsonArray>();
    for (const std::string& name : names) {
        arr.add(name.c_str());
    }
    File idx = SPIFFS.open(_tempPath(_indexPath()).c_str(), "w");
    if (!idx) {
        return false;
    }
    if (serializeJson(index, idx) == 0) {
        idx.close();
        SPIFFS.remove(_tempPath(_indexPath()).c_str());
        return false;
    }
    idx.close();
    SPIFFS.remove(_indexPath().c_str());
    return SPIFFS.rename(_tempPath(_indexPath()).c_str(), _indexPath().c_str());
#else
    std::ofstream out(_tempPath(_indexPath()), std::ios::trunc);
    if (!out) {
        return false;
    }
    out << "{\"procedures\":[";
    for (size_t i = 0; i < names.size(); ++i) {
        if (i) out << ',';
        out << '"' << nativejson::escapeString(names[i]) << '"';
    }
    out << "]}";
    out.close();
    std::error_code ec;
    fs::rename(_tempPath(_indexPath()), _indexPath(), ec);
    if (ec) {
        fs::remove(_tempPath(_indexPath()), ec);
        return false;
    }
    return true;
#endif
}

std::vector<std::string> ProcedureStore::_loadIndexNames() const {
    std::vector<std::string> names;
#ifndef NATIVE_TEST
    if (!SPIFFS.exists(_indexPath().c_str())) {
        return names;
    }
    File f = SPIFFS.open(_indexPath().c_str(), "r");
    if (!f) {
        return names;
    }
    JsonDocument doc;
    if (deserializeJson(doc, f)) {
        f.close();
        return names;
    }
    f.close();
    for (JsonVariant entry : doc["procedures"].as<JsonArray>()) {
        if (entry.is<const char*>()) {
            names.emplace_back(entry.as<const char*>());
        }
    }
#else
    std::ifstream in(_indexPath());
    if (!in) {
        return names;
    }
    std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string arrayJson;
    if (!nativejson::extractArrayField(raw, "procedures", arrayJson)) {
        return names;
    }
    names = nativejson::extractStringArrayValues(arrayJson);
#endif
    return names;
}

std::vector<std::string> ProcedureStore::_scanProcedureNames() const {
    std::vector<std::string> names;
#ifndef NATIVE_TEST
    File dir = SPIFFS.open("/proc");
    if (!dir || !dir.isDirectory()) {
        return names;
    }
    File entry = dir.openNextFile();
    while (entry) {
        std::string entryName = entry.name();
        if (entryName.size() > 5 && entryName != _indexPath()) {
            const size_t slash = entryName.find_last_of('/');
            const std::string fileName = slash == std::string::npos ? entryName : entryName.substr(slash + 1);
            if (fileName.size() > 5 && fileName.substr(fileName.size() - 5) == ".json") {
                names.push_back(fileName.substr(0, fileName.size() - 5));
            }
        }
        entry = dir.openNextFile();
    }
#else
    std::error_code ec;
    if (!fs::exists(_rootDir, ec)) {
        return names;
    }
    for (const auto& entry : fs::directory_iterator(_rootDir, ec)) {
        if (ec || !entry.is_regular_file()) {
            continue;
        }
        const std::string fileName = entry.path().filename().string();
        if (fileName == "index.json" || fileName.size() < 6 || fileName.substr(fileName.size() - 5) != ".json") {
            continue;
        }
        names.push_back(fileName.substr(0, fileName.size() - 5));
    }
#endif
    std::sort(names.begin(), names.end());
    return names;
}

std::string ProcedureStore::_indexPath() const {
#ifdef NATIVE_TEST
    return _rootDir + "/index.json";
#else
    return "/proc/index.json";
#endif
}

std::string ProcedureStore::_procedurePath(const std::string& name) const {
#ifdef NATIVE_TEST
    return _rootDir + "/" + _sanitizeName(name) + ".json";
#else
    return "/proc/" + _sanitizeName(name) + ".json";
#endif
}

std::string ProcedureStore::_tempPath(const std::string& path) const {
    return path + ".tmp";
}

std::string ProcedureStore::_currentTimestamp() const {
    time_t now = time(nullptr);
#ifndef NATIVE_TEST
    struct tm t;
    gmtime_r(&now, &t);
#else
    struct tm t;
    gmtime_r(&now, &t);
#endif
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &t);
    return std::string(buf);
}

std::string ProcedureStore::_sanitizeName(const std::string& rawName) const {
    std::string out;
    out.reserve(rawName.size());
    for (char ch : rawName) {
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-') {
            out += ch;
        }
    }
    if (out.empty()) {
        out = "Procedure";
    }
    if (out.size() > 48U) {
        out.resize(48U);
    }
    return out;
}

bool ProcedureStore::_hasFreeSpaceFor(size_t bytes) const {
#ifndef NATIVE_TEST
    const size_t freeBytes = SPIFFS.totalBytes() - SPIFFS.usedBytes();
    return freeBytes >= bytes + PROCEDURE_MIN_FREE_BYTES;
#else
    std::error_code ec;
    const auto info = fs::space(_rootDir, ec);
    return !ec && info.available >= bytes + PROCEDURE_MIN_FREE_BYTES;
#endif
}

#ifdef NATIVE_TEST
bool ProcedureStore::corruptIndexForTest(const std::string& rawContents) {
    std::ofstream out(_indexPath(), std::ios::trunc);
    if (!out) {
        return false;
    }
    out << rawContents;
    return true;
}
#endif
