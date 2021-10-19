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
    state.store(Executor::State::kInit);
}

void Executor::Start() {
    //_logger = pLogging->select("network");
    //_logger->info("Start thread pull");

    if (state.load() != Executor::State::kInit) return;
    state.store(Executor::State::kRun);
    
    counter_exist_threads = _low_watermark;
    for (std::size_t i = 0; i < _low_watermark; ++i) {
        std::thread([this]() { this->perform(true); }).detach();
    }

    std::thread(&Executor::OnRun, this).detach();
} 

void Executor::OnRun() {
    while (state.load() == Executor::State::kRun) {
        // Create new thread if it's necessary
        {
            std::unique_lock<std::mutex> lock(mutex_counter_exist_threads);
            if (counter_exist_threads < _high_watermark) {
                ++counter_exist_threads;
                std::thread([this](){ this->perform(false); }).detach();
            }
        }
    }
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
            while (tasks.empty()) {
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

            if (tasks.empty()) {
                if (is_not_dying_thread) continue;
                // Exit to die
                break;
            }
            else {
                task = std::move(tasks.front());
                tasks.pop();
            }
        }
        
        // Execute the received task
        try {
            task();
        } 
        catch(const std::exception& ex) {
            //_logger->error("Failed to execute task: {}", ex.what());
        }
    }

    bool pull_became_empty = false;
    {
        std::unique_lock<std::mutex> lock(mutex_counter_exist_threads);
        --counter_exist_threads;

        pull_became_empty = counter_exist_threads == 0;
    }
    if (pull_became_empty && state.load() == Executor::State::kStopping) {
        state.store(Executor::State::kStopped);
        can_stop_executor_condition.notify_all();
    } 
}
} // namespace Concurrency
} // namespace Afina
