#ifndef AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H
#define AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H

#include <deque>
#include <cstring>

#include <sys/epoll.h>

#include <afina/execute/Command.h>
#include <afina/logging/Service.h>

#include "protocol/Parser.h"

namespace spdlog {
class logger;
}

namespace Afina {
namespace Network {
namespace STnonblock {

#define MAXLEN 4096
#define MAX_OUTGOING_QUEUE_SIZE 128

class Connection {
public:
    Connection(int s, std::shared_ptr<Afina::Storage> ps, std::shared_ptr<Logging::Service> pl) : _socket(s), 
        pStorage(ps), pLogging(pl)  {
        std::memset(&_event, 0, sizeof(struct epoll_event));
        _event.data.ptr = this;
        _is_alive = true;
        _is_eof = false;
        _head_offset = 0;
        _arg_remains = 0;
        std::memset(_read_buffer, 0, MAXLEN);
        _read_buffer_offset = 0;
    }

    inline bool isAlive() const { return _is_alive; }

    void Start();

protected:
    void OnError();
    void OnClose();
    void DoRead();
    void DoWrite();

    /**
     * Logging service to be used in order to report application progress
     */
    std::shared_ptr<Afina::Logging::Service> pLogging;

    std::shared_ptr<Afina::Storage> pStorage;

private:
    friend class ServerImpl;

    int _socket;
    struct epoll_event _event;

    std::deque<std::string> _outgoing;
    uint32_t _head_offset;

    bool _is_alive;
    bool _is_eof;

    // Here is connection state
    // - parser: parse state of the stream
    // - command_to_execute: last command parsed out of stream
    // - arg_remains: how many bytes to read from stream to get command argument
    // - argument_for_command: buffer stores argument
    std::size_t _arg_remains;
    Protocol::Parser _parser;
    std::string _argument_for_command;
    std::unique_ptr<Execute::Command> _command_to_execute;
    char _read_buffer[MAXLEN];
    int _read_buffer_offset;

    // Logger instance
    std::shared_ptr<spdlog::logger> _logger;
};

} // namespace STnonblock
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H
