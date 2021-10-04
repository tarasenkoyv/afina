#ifndef AFINA_STORAGE_SIMPLE_LRU_H
#define AFINA_STORAGE_SIMPLE_LRU_H

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <afina/Storage.h>

namespace Afina {
namespace Backend {

/**
 * # Map based implementation
 * That is NOT thread safe implementaiton!!
 */
class SimpleLRU : public Afina::Storage {
private:
    // LRU cache node
    using lru_node = struct lru_node {
        const std::string key;
        std::string value;
        lru_node* prev;
        std::unique_ptr<lru_node> next;
    };

    using lru_map =
        std::map<std::reference_wrapper<const std::string>, std::reference_wrapper<lru_node>, std::less<std::string>>;
public:
    SimpleLRU(size_t max_size = 1024) : _max_size(max_size), _current_size(0) {}

    ~SimpleLRU() {
        _lru_index.clear();

        // To avoid stack overflow, we do reset() in a loop,
        // starting from the head element.
        while(_lru_head)
        {
            std::unique_ptr<lru_node> tmp;
            std::swap(_lru_head->next, tmp);
            std::swap(_lru_head, tmp);
        }
    }

    // Implements Afina::Storage interface
    bool Put(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool PutIfAbsent(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool Set(const std::string &key, const std::string &value) override;

    // Implements Afina::Storage interface
    bool Delete(const std::string &key) override;

    // Implements Afina::Storage interface
    bool Get(const std::string &key, std::string &value) override;

private:
    bool FreeSpace(std::size_t delta);

    void MoveNodeToTail(lru_node& node);
    
    void InsertNode(const std::string &key, const std::string &value);

    void UpdateNode(lru_node& node, const std::string &new_value);
    
    bool ProcessPut(const std::string &key, const std::string &value, lru_node* found_node);

private:
    // Maximum number of bytes could be stored in this cache.
    // i.e all (keys+values) must be not greater than the _max_size
    std::size_t _max_size;

    // Current number of bytes in this cache.
    std::size_t _current_size;

    // Main storage of lru_nodes, elements in this list ordered descending by "freshness": in the head
    // element that wasn't used for longest time.
    //
    // List owns all nodes
    std::unique_ptr<lru_node> _lru_head;

    // Index of nodes from list above, allows fast random access to elements by lru_node#key
    lru_map _lru_index;
};

} // namespace Backend
} // namespace Afina

#endif // AFINA_STORAGE_SIMPLE_LRU_H
