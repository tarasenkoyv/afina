#include "SimpleLRU.h"

namespace Afina {
namespace Backend {

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Put(const std::string &key, const std::string &value) { 
    auto it = _lru_index.find(key);
    lru_node* found_node = nullptr;
    if (it != _lru_index.end()) {
        found_node = &(it->second.get());
    }
    return ProcessPut(key, value, found_node);
 }

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::PutIfAbsent(const std::string &key, const std::string &value) { 
    auto it = _lru_index.find(key);
    if (it != _lru_index.end()) {
        return false;
    }
    return ProcessPut(key, value, nullptr);
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Set(const std::string &key, const std::string &value) { 
    auto it = _lru_index.find(key);
    lru_node* found_node = nullptr;
    if (it == _lru_index.end()) {
        return false;
    }
    else {
        found_node = &(it->second.get());
    }
    return ProcessPut(key, value, found_node);
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
    auto found_node = &(it->second.get());
    MoveNodeToTail(*found_node);
    value = (*found_node).value;
    return true;
 }

bool SimpleLRU::FreeSpace(std::size_t put_size) {
    if (put_size == 0) {
        return true;
    }

    if (put_size > _max_size) {
        return false;
    }

    // Remove the oldest elements until there is enough space.
    while(_current_size + put_size > _max_size) {
        _current_size -= _lru_head->key.size() + _lru_head->value.size();
        lru_node* new_head = _lru_head->next.get();
        new_head->prev = _lru_head->prev;
        _lru_head->next.release();
        _lru_index.erase(_lru_head->key);
        _lru_head.reset(new_head);
    }

    return true;
}

void SimpleLRU::MoveNodeToTail(lru_node& node) {
    if (_lru_head->prev == &node) {
        return;
    }

    auto lru_tail = _lru_head->prev;
    // Left node exists, 
    // and we can link left and right nodes
    // to exclude middle node.
    if (_lru_head.get() != &node) {
        auto right_node = node.next.get();
        auto left_node = node.prev;
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

void SimpleLRU::UpdateNode(lru_node& node, const std::string &new_value) {
    auto& old_value = node.value;
    _current_size += node.value.size() - old_value.size();
    old_value = new_value;
}

bool SimpleLRU::ProcessPut(const std::string &key, const std::string &value, lru_node* found_node) {
    std::size_t put_size = key.size() + value.size();
    std::size_t found_node_size = 0;

    // If we find node with this key, 
    // we move the found node to the tail,
    // then we free space which is equal delta = put_size - found_node_size
    // to have space in cache and replace the value in this found node.
    if (found_node != nullptr) {
        MoveNodeToTail(*found_node);
        found_node_size = key.size() + found_node->value.size();
    }
    std::size_t delta = put_size - found_node_size;
    if (!FreeSpace(delta)) {
        return false;
    }
    
    // Add key
    if (found_node != nullptr) {
        UpdateNode(*found_node, value);
    }
    else {
        InsertNode(key, value);
    }
    return true;
}

} // namespace Backend
} // namespace Afina
