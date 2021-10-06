#ifndef AFINA_STORAGE_STRIPED_LOCK_LRU_H
#define AFINA_STORAGE_STRIPED_LOCK_LRU_H

#include <cassert>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "SimpleLRU.h"
#include "ThreadSafeSimpleLRU.h"

namespace Afina {
namespace Backend {

class StripedLockLRU : public Afina::Storage {
public:
    StripedLockLRU(size_t max_size = 1024, size_t n_stripes = 4) : _n_stripes(n_stripes) {
        assert(_n_stripes > 0);
        assert(max_size > 0);

        size_t stripe_max_size = max_size / n_stripes;
        assert(stripe_max_size > 0);

        _stripes.resize(_n_stripes);
        for (std::size_t i = 0 ; i < _n_stripes; i++) {
            _stripes[i].reset(new ThreadSafeSimplLRU(stripe_max_size));
        }
    }

    ~StripedLockLRU() {}

    // see SimpleLRU.h
    bool Put(const std::string &key, const std::string &value) override {
        return _stripes[_hash(key) % _n_stripes]->Put(key, value);
    }

    // see SimpleLRU.h
    bool PutIfAbsent(const std::string &key, const std::string &value) override {
        return _stripes[_hash(key) % _n_stripes]->PutIfAbsent(key, value);
    }

    // see SimpleLRU.h
    bool Set(const std::string &key, const std::string &value) override {
        return _stripes[_hash(key) % _n_stripes]->Set(key, value);
    }

    // see SimpleLRU.h
    bool Delete(const std::string &key) override {
        return _stripes[_hash(key) % _n_stripes]->Delete(key);
    }

    // see SimpleLRU.h
    bool Get(const std::string &key, std::string &value) override {
        return _stripes[_hash(key) % _n_stripes]->Get(key, value);
    }

private:
    std::hash<std::string> _hash;
    std::size_t _n_stripes;
    std::vector<std::unique_ptr<ThreadSafeSimplLRU>> _stripes;
};

}
}
#endif // AFINA_STORAGE_STRIPED_LOCK_LRU_H