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
    
    Start();
}

void Executor::Start() {
    //_logger = pLogging->select("network");
    //_logger->info("Start thread pull");
    counter_exist_threads = _low_watermark;
    for (std::size_t i = 0; i < _low_watermark; ++i) {
        std::thread([this]() { this->perform(true); }).detach();
    }

    state.store(Executor::State::kRun);
}

Executor::~Executor(){
    Stop(true);
}

void Executor::Stop(bool await) {
    if (state.load() == Executor::State::kStopped) return;
    state.store(Executor::State::kStopping);

    std::unique_lock<std::mutex> lock(mutex_counter_exist_threads);
    if (counter_exist_threads == 0) {
        state.store(Executor::State::kStopped);
    } 
    else {
        empty_condition.notify_all();
        if (await) {
            while (state.load() != Executor::State::kStopped) {
                can_stop_executor_condition.wait(lock);
            }
        }
    }
}

void Executor::perform(bool is_not_dying_thread) {

    while (state.load() == Executor::State::kRun) {
        std::function<void()> task;

        // Waiting for a task
        {
            std::unique_lock<std::mutex> lock(mutex_tasks);

            bool timeout = false;
            size_t remaining_time = _idle_time;
            while (state.load() == Executor::State::kRun && tasks.empty()) {
                auto begin = std::chrono::steady_clock::now();
                auto wait_res = empty_condition.wait_until(lock, begin + std::chrono::milliseconds(remaining_time));
                if (wait_res == std::cv_status::timeout) {
                    timeout = true;
                    break;
                } 
                else {
                    remaining_time -= std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count();
                }
            }

            // Queue may be empty if empty_condition notify was called from Stop()
            // (and after that executor state is equal Stopping)
            // or if timeout is true
            // or timeout + Stopping
            if (tasks.empty()) {
                if (timeout && is_not_dying_thread) {
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
                break;
            }
        }
        
        counter_busy_threads.fetch_add(1);
        // Execute the received task
        try {
            task();
        } 
        catch(const std::exception& ex) {
            //_logger->error("Failed to execute task: {}", ex.what());
        }
        counter_busy_threads.fetch_sub(1);
    }

    bool pull_became_empty = false;
    {
        std::unique_lock<std::mutex> lock(mutex_counter_exist_threads);
        --counter_exist_threads;

        pull_became_empty = (counter_exist_threads == 0);
    }
    if (pull_became_empty && state.load() == Executor::State::kStopping) {
        state.store(Executor::State::kStopped);
        can_stop_executor_condition.notify_all();
    } 
}
} // namespace Concurrency
} // namespace Afina
