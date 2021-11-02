#include "Connection.h"

#include <iostream>
#include <stdexcept>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace Afina {
namespace Network {
namespace MTnonblock {

// See Connection.h
void Connection::Start() { 
    _logger = pLogging->select("network");
    _logger->debug("Connection started on descriptor {}", _socket);

    _event.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
 }

// See Connection.h
void Connection::OnError() { 
    _logger->error("Failed to process connection on descriptor {}", _socket);
    _is_alive.store(false, std::memory_order::memory_order_release);
 }

// See Connection.h
void Connection::OnClose() { 
    _logger->debug("Close connection on descriptor {}", _socket);
    _is_alive.store(false, std::memory_order::memory_order_release);
 }

// See Connection.h
void Connection::DoRead() { 
    _logger->debug("Reading on descriptor {}", _socket);
    std::atomic_thread_fence(std::memory_order::memory_order_acquire);

    try {
        int readed_bytes = -1;
        while ((readed_bytes = read(_socket, _read_buffer + _read_buffer_offset, sizeof(_read_buffer) - _read_buffer_offset)) > 0) {
            _logger->debug("Got {} bytes from socket", readed_bytes);

            _read_buffer_offset += readed_bytes;
            // Single block of data readed from the socket could trigger inside actions a multiple times,
            // for example:
            // - read#0: [<command1 start>]
            // - read#1: [<command1 end> <argument> <command2> <argument for command 2> <command3> ... ]
            while (_read_buffer_offset > 0) {
                _logger->debug("Process {} bytes", _read_buffer_offset);
                // There is no command yet
                if (!_command_to_execute) {
                    std::size_t parsed = 0;
                    if (_parser.Parse(_read_buffer, _read_buffer_offset, parsed)) {
                        // There is no command to be launched, continue to parse input stream
                        // Here we are, current chunk finished some command, process it
                        _logger->debug("Found new command: {} in {} bytes", _parser.Name(), parsed);
                        _command_to_execute = _parser.Build(_arg_remains);
                        if (_arg_remains > 0) {
                            _arg_remains += 2;
                        }
                    }

                    // Parsed might fails to consume any bytes from input stream. In real life that could happens,
                    // for example, because we are working with UTF-16 chars and only 1 byte left in stream
                    if (parsed == 0) {
                        break;
                    } else {
                        std::memmove(_read_buffer, _read_buffer + parsed, _read_buffer_offset - parsed);
                        _read_buffer_offset -= parsed;
                    }
                }

                // There is command, but we still wait for argument to arrive...
                if (_command_to_execute && _arg_remains > 0) {
                    _logger->debug("Fill argument: {} bytes of {}", _read_buffer_offset, _arg_remains);
                    // There is some parsed command, and now we are reading argument
                    std::size_t to_read = std::min(_arg_remains, std::size_t(_read_buffer_offset));
                    _argument_for_command.append(_read_buffer, to_read);

                    std::memmove(_read_buffer, _read_buffer + to_read, _read_buffer_offset - to_read);
                    _arg_remains -= to_read;
                    _read_buffer_offset -= to_read;
                }

                // Thre is command & argument - RUN!
                if (_command_to_execute && _arg_remains == 0) {
                    _logger->debug("Start command execution");

                    std::string result;
                    if (_argument_for_command.size()) {
                        _argument_for_command.resize(_argument_for_command.size() - 2);
                    }
                    _command_to_execute->Execute(*pStorage, _argument_for_command, result);

                    // Send response
                    result += "\r\n";
                    _outgoing.emplace_back(std::move(result));
                    _event.events |= EPOLLOUT;
                    if (_outgoing.size() >= MAX_OUTGOING_QUEUE_SIZE) {
                        _event.events &= ~EPOLLIN;
                    }

                    // Prepare for the next command
                    _command_to_execute.reset();
                    _argument_for_command.resize(0);
                    _parser.Reset();
                }
            }
        }

        if (readed_bytes == -1) {
            /* If errno == EAGAIN or errno=EWOULDBLOCK, that means we have read all
                data. */
            if (errno != EAGAIN && errno!=EWOULDBLOCK) {
                _logger->error("Failed to process connection on descriptor {}: {}", _socket, std::string(strerror(errno)));
                _is_alive.store(false, std::memory_order::memory_order_relaxed);
            }
            return;
        } 
        else if (readed_bytes == 0) {
            /* End of file. The remote has closed the
            connection. */
            if (_event.events & EPOLLOUT) {
                _is_eof.store(true, std::memory_order::memory_order_relaxed);
            }
            else {
                _is_alive.store(false, std::memory_order::memory_order_relaxed);
            }
            return;
        }
    }
    catch(std::runtime_error &ex) {
        _logger->error("Failed to process connection on descriptor {}: {}", _socket, ex.what());
        _is_alive.store(false, std::memory_order::memory_order_relaxed);
    }
    catch(...) { 
        _logger->error("Failed to process connection on descriptor {}: {}", _socket, "unknown error");
        _is_alive.store(false, std::memory_order::memory_order_relaxed);
    }

    std::atomic_thread_fence(std::memory_order::memory_order_release);
 }

// See Connection.h
void Connection::DoWrite() { 
    std::atomic_thread_fence(std::memory_order::memory_order_acquire);

    if  (!_is_alive.load(std::memory_order_relaxed)) return;

    _logger->debug("Writing on descriptor {}", _socket);

    while(!_outgoing.empty()) {
        auto const &result = _outgoing.front();
        while(_head_offset < result.size()) {
            int written_bytes = write(_socket, &result[_head_offset], result.size() - _head_offset);
            if (written_bytes > 0 && written_bytes <= result.size()) {
                _head_offset += written_bytes;
                continue;
            }
            else {
                if (errno == EAGAIN or errno==EWOULDBLOCK) {
                    return;
                }
                else {
                    _logger->error("Failed to write connection on descriptor {}", _socket);
                    _is_alive.store(false, std::memory_order::memory_order_relaxed);
                    return;
                }
            }
        }
        _head_offset = 0;
        _outgoing.pop_front();

        if (!(_event.events & EPOLLIN) && _outgoing.size() < MAX_OUTGOING_QUEUE_SIZE) {
            _event.events |= EPOLLIN;
        }
    }

    _event.events &= ~EPOLLOUT;

    if (_is_eof.load(std::memory_order::memory_order_relaxed)) {
        _is_alive.store(false, std::memory_order::memory_order_relaxed);
    }

    std::atomic_thread_fence(std::memory_order::memory_order_release);
 }

} // namespace MTnonblock
} // namespace Network
} // namespace Afina
