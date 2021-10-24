#include <afina/concurrency/Executor.h>

#include <chrono>
#include <functional>

//#include <spdlog/logger.h>
//#include <afina/logging/Service.h>

namespace Afina {
namespace Concurrency {

Executor::Executor(std::size_t low_watermark, std::size_t high_watermark, 
                   std::size_t max_queue_size, std::size_t idle_time) : _low_watermark(low_watermark), 
                   _high_watermark(high_watermark), 
                   _max_queue_size(max_queue_size), _idle_time(idle_time) {
    counter_exist_threads = 0;
    state = State::kInit;
}

void Executor::Start() {
    std::unique_lock<std::mutex> lock(_mtx);
    if (state == State::kRun) return;

    //_logger = pLogging->select("network");
    //_logger->info("Start thread pull");
    counter_exist_threads = _low_watermark;
    for (std::size_t i = 0; i < _low_watermark; ++i) {
        std::thread([this]() { this->perform(true); }).detach();
    }
    state = State::kRun;
}

Executor::~Executor(){
    Stop(true);
}

void Executor::Stop(bool await) {
    std::unique_lock<std::mutex> lock(_mtx);
    
    if (state == State::kStopped) return;
    
    if (counter_exist_threads == 0) {
        state = State::kStopped;
        return;
    }
    
    state = State::kStopping;
    if (counter_exist_threads > counter_busy_threads) {
        empty_condition.notify_all();
    }

    if (await) {
        while (counter_exist_threads > 0) {
            await_condition.wait(lock);
        }
    }
}

void Executor::perform(bool is_not_dying_thread) {
    std::unique_lock<std::mutex> lock(_mtx);
    while (state == State::kRun) {
        std::function<void()> task;

        // Waiting for a task
        bool timeout = false;
        auto timeout_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(_idle_time);
        while (state == State::kRun && tasks.empty()) {
            auto wait_res = empty_condition.wait_until(lock, timeout_time);
            if (wait_res == std::cv_status::timeout) {
                timeout = true;
                break;
            }
        }

        if (tasks.empty()) {
            if (is_not_dying_thread) {
                continue;
            }
            // Exit from while to die
            break;
        }
        else {
            if (!timeout || is_not_dying_thread) {
                task = std::move(tasks.front());
                tasks.pop();
            }
            // Exit from while to die
            else {
                break;
            }
        }

        ++counter_busy_threads;
        lock.unlock();

        // Execute the received task
        try {
            task();
        } 
        catch(const std::exception& ex) {
            //_logger->error("Failed to execute task: {}", ex.what());
        }
        catch(...) {
            //_logger->error("Failed to execute task");
        }

        lock.lock();
        --counter_busy_threads;
    }

    --counter_exist_threads;
    if (counter_exist_threads == 0 && state == State::kStopping) {
        state = State::kStopped;
        await_condition.notify_all();
    }
}
} // namespace Concurrency
} // namespace Afina
