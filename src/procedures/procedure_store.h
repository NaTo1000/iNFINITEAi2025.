#pragma once
// =============================================================================
// procedure_store.h — Persistent storage for innovation procedures
//
// Each innovation cycle that passes peak standards is saved as a named
// "procedure" with a randomly generated funny name (e.g. "SneezyNoodleV42").
// Stored in ESP32 NVS (non-volatile storage) and also written to SPIFFS JSON.
// =============================================================================
#include <string>
#include <vector>
#include "../config.h"

struct Procedure {
    std::string name;        // funny auto-generated name
    std::string description; // what the innovation does
    std::string code;        // the validated code/config snippet
    std::string timestamp;   // ISO-8601 creation time
    uint32_t    iteration;   // which innovator iteration produced it
};

class ProcedureStore {
public:
    ProcedureStore();

    bool begin();

    // Save a new procedure (generates a funny name automatically)
    bool save(const std::string& description,
              const std::string& code,
              uint32_t iteration);

    // Load all stored procedures
    std::vector<Procedure> loadAll() const;

    // Load a single procedure by name
    bool loadByName(const std::string& name, Procedure& out) const;

    // Serialise procedure list to JSON string
    std::string toJson() const;

    // Count of stored procedures
    size_t count() const;

    // Funny-name generator (public so it can be unit-tested)
    static std::string generateFunnyName(uint32_t seed);

private:
    bool _persist(const Procedure& p);
    std::string _indexPath() const;
    std::string _procedurePath(const std::string& name) const;
    std::string _currentTimestamp() const;

    std::vector<Procedure> _cache;
};
