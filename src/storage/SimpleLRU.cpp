#include <cassert>

#include "SimpleLRU.h"

namespace Afina {
namespace Backend {

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Put(const std::string &key, const std::string &value) {
    std::size_t put_size = key.size() + value.size();
    if (put_size > _max_size) {
        return false;
    }
    auto it = _lru_index.find(key);
    if (it != _lru_index.end()) {
        UpdateNode(it->second.get(), value);
        return true;
    }
    else {
        InsertNode(key, value);
        return true;
    }
 }

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::PutIfAbsent(const std::string &key, const std::string &value) {
    std::size_t put_size = key.size() + value.size();
    if (put_size > _max_size) {
        return false;
    }
    auto it = _lru_index.find(key);
    if (it != _lru_index.end()) {
        return false;
    }
    InsertNode(key, value);
    return true;
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Set(const std::string &key, const std::string &value) {
    std::size_t put_size = key.size() + value.size();
    if (put_size > _max_size) {
        return false;
    }
    auto it = _lru_index.find(key);
    if (it == _lru_index.end()) {
        return false;
    }
    else {
        UpdateNode(it->second.get(), value);
        return true;
    }
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
    auto& found_node = it->second.get();
    MoveNodeToTail(found_node);
    value = found_node.value;
    return true;
 }

void SimpleLRU::FreeSpace(std::size_t put_size) {
    assert(put_size > 0);
    assert(put_size <= _max_size);

    // Remove the oldest elements until there is enough space.
    while(_current_size + put_size > _max_size) {
        _current_size -= _lru_head->key.size() + _lru_head->value.size();
        lru_node* new_head = _lru_head->next.get();
        new_head->prev = _lru_head->prev;
        _lru_head->next.release();
        _lru_index.erase(_lru_head->key);
        _lru_head.reset(new_head);
    }
}

void SimpleLRU::MoveNodeToTail(lru_node& node) {
    if (_lru_head->prev == &node) {
        return;
    }

    auto* lru_tail = _lru_head->prev;
    // Left node exists, 
    // and we can link left and right nodes
    // to exclude middle node.
    if (_lru_head.get() != &node) {
        auto* right_node = node.next.get();
        auto* left_node = node.prev;
        right_node->prev = left_node;
        left_node->next.release();
        std::swap(left_node->next, node.next);
    }
    // Node is head, move _lru_head to the right node
    else {
        _lru_head.release();
        std::swap(_lru_head, node.next);
    }

    lru_tail->next.reset(&node);
    node.prev = lru_tail;
    
    _lru_head->prev = &node;
}

void SimpleLRU::InsertNode(const std::string &key, const std::string &value) {
    std::size_t put_size = key.size() + value.size();
    assert(put_size <=_max_size);
    if (put_size > 0) {
        FreeSpace(put_size);
    }

    if (_lru_head) {
        auto freshest = _lru_head->prev;
        auto new_node = new lru_node{ key, value, nullptr, nullptr };
        freshest->next.reset(new_node);
        new_node->prev = freshest;
        _lru_head->prev = new_node;
    }
    else {
        _lru_head.reset(new lru_node{key, value, nullptr, nullptr});
        _lru_head->prev = _lru_head.get();
    }
    // Add to index
    _lru_index.emplace(_lru_head->prev->key, *_lru_head->prev);
    // Update the current size of cache
    _current_size += key.size() + value.size();
}

void SimpleLRU::UpdateNode(lru_node& node, const std::string& new_value) {
    int delta = new_value.size() - node.value.size();
    assert(node.key.size() + new_value.size() <=_max_size);
    
    MoveNodeToTail(node);
    
    if (delta > 0) {
        FreeSpace(delta);
    }

    auto& old_value = node.value;
    _current_size += node.value.size() - old_value.size();
    old_value = new_value;
}

} // namespace Backend
} // namespace Afina
