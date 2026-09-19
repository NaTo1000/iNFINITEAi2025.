#pragma once
// =============================================================================
// procedure_store.h — Persistent storage for validated procedures
// =============================================================================
#include <string>
#include <vector>
#include "../config.h"

struct Procedure {
    std::string name;
    std::string description;
    std::string code;
    std::string timestamp;
    uint32_t    iteration = 0;
};

class ProcedureStore {
public:
#ifdef NATIVE_TEST
    explicit ProcedureStore(std::string rootDir = "/tmp/infiniteai-procedures");
#else
    ProcedureStore();
#endif

    bool begin();
    bool save(const std::string& description,
              const std::string& code,
              uint32_t iteration);
    std::vector<Procedure> loadAll() const;
    bool loadByName(const std::string& name, Procedure& out) const;
    std::string toJson() const;
    size_t count() const;
    static std::string generateFunnyName(uint32_t seed);

#ifdef NATIVE_TEST
    bool corruptIndexForTest(const std::string& rawContents);
#endif

private:
    bool _persist(const Procedure& p);
    bool _writeIndex(const std::vector<std::string>& names) const;
    std::vector<std::string> _loadIndexNames() const;
    std::vector<std::string> _scanProcedureNames() const;
    std::string _indexPath() const;
    std::string _procedurePath(const std::string& name) const;
    std::string _tempPath(const std::string& path) const;
    std::string _currentTimestamp() const;
    std::string _sanitizeName(const std::string& rawName) const;
    bool _hasFreeSpaceFor(size_t bytes) const;

    std::vector<Procedure> _cache;
#ifdef NATIVE_TEST
    std::string _rootDir;
#endif
};
