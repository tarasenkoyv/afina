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
public:
    SimpleLRU(size_t max_size = 1024) : _max_size(max_size), _current_size(0) {}

    ~SimpleLRU() {
        _lru_index.clear();

        // To avoid stack overflow, we do reset() in a loop,
        // starting from the last element, which next unique_ptr is nullptr.
        auto del = _lru_head ? _lru_head->prev : nullptr;
        while(del != _lru_head.get())
        {
            auto prev = del->prev;
            prev->next.reset();
            del = prev;
        }
        _lru_head.reset();
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
    // LRU cache node
    using lru_node = struct lru_node {
        const std::string key;
        std::string value;
        lru_node* prev;
        std::unique_ptr<lru_node> next;
    };

    using lru_map =
        std::map<std::reference_wrapper<const std::string>, std::reference_wrapper<lru_node>, std::less<std::string>>;

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

private:
    bool _put(const std::string &key, const std::string &value, lru_map::iterator it);
};

} // namespace Backend
} // namespace Afina

#endif // AFINA_STORAGE_SIMPLE_LRU_H
