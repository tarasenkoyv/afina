#include "SimpleLRU.h"

namespace Afina {
namespace Backend {

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Put(const std::string &key, const std::string &value) { 
    auto it = _lru_index.find(key);
    return _put(key, value, it);
 }

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::PutIfAbsent(const std::string &key, const std::string &value) { 
    auto it = _lru_index.find(key);
    if (it != _lru_index.end()) {
        return false;
    }
    return _put(key, value, it);
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Set(const std::string &key, const std::string &value) { 
    auto it = _lru_index.find(key);
    if (it == _lru_index.end()) {
        return false;
    }
    return _put(key, value, it);
 }

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Delete(const std::string &key) { 
    auto it = _lru_index.find(key);
    if (it == _lru_index.end()) {
        return false;
    }
    auto &lru_node = it->second.get();
    _lru_index.erase(key);
    _current_size -= key.size() + lru_node.value.size();
    auto prev = lru_node.prev;
    auto next = lru_node.next.get();
    if (next) {
        next->prev = prev;
    } 
    else {
        _lru_head->prev = prev;
    }
    if (_lru_head->key == key) {
        _lru_head->next.release();
        _lru_head.reset(next);
    } 
    else {
        prev->next.reset(next);
    }
    return true;
 }

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Get(const std::string &key, std::string &value) { 
    auto it = _lru_index.find(key);
    if (it == _lru_index.end()) {
        return false;
    }
    value = it->second.get().value;
    return true;
 }

bool SimpleLRU::_put(const std::string &key, const std::string &value, lru_map::iterator it) {
    std::size_t put_size = key.size() + value.size();
    if (put_size > _max_size) {
        return false;
    }
    std::size_t replaced_size = 0;

    // If we find key and replace the value in this key, then the total size is calculated minus the replaced lru_node.
    if (it != _lru_index.end()) {
        replaced_size = it->second.get().key.size() + it->second.get().value.size();
    }
    // Remove the oldest elements until there is enough space.
    while (_current_size - replaced_size + put_size > _max_size) {
        if (_lru_head->key == key) {
            replaced_size = 0;
        }
        _current_size -= _lru_head->key.size() + _lru_head->value.size();
        lru_node* new_head = _lru_head->next.get();
        new_head->prev = _lru_head->prev;
        _lru_head->next.release();
        _lru_index.erase(_lru_head->key);
        _lru_head.reset(new_head);
    }
    // Add key
    if (_lru_head) {
        if (it != _lru_index.end()) {
            auto& old_value = it->second.get().value;
            _current_size -= key.size() + old_value.size();
            old_value = value;
        }
        else {
            auto freshest = _lru_head->prev;
            freshest->next.reset(new lru_node{key, value, nullptr, nullptr});
            freshest->next->prev = freshest;
            _lru_head->prev = freshest->next.get();
            _lru_index.emplace(_lru_head->prev->key, *_lru_head->prev);
        }
    } else {
        _lru_head.reset(new lru_node{key, value, nullptr, nullptr});
        _lru_head->prev = _lru_head.get();
        _lru_index.emplace(_lru_head->prev->key, *_lru_head->prev);
    }
    _current_size += key.size() + value.size();
    return true;
}

} // namespace Backend
} // namespace Afina
