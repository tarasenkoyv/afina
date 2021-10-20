#ifndef AFINA_CONCURRENCY_EXECUTOR_H
#define AFINA_CONCURRENCY_EXECUTOR_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

//namespace spdlog {
//class logger;
//}

namespace Afina {
namespace Concurrency {

/**
 * # Thread pool
 */
class Executor {
    enum class State {
        kInit,
        // Threadpool is fully operational, tasks could be added and get executed
        kRun,
        // Threadpool is on the way to be shutdown, no new task could be added, but existing will be
        // completed as requested
        kStopping,

        // Threadppol is stopped
        kStopped
    };

public:
    Executor(std::size_t low_watermark, std::size_t high_watermark, 
             std::size_t max_queue_size, std::size_t idle_time);

    ~Executor();

    /**
     * Starts executor.
     */
    void Start();

    /**
     * Signal thread pool to stop, it will stop accepting new jobs and close threads just after each become
     * free. All enqueued jobs will be complete.
     *
     * In case if await flag is true, call won't return until all background jobs are done and all threads are stopped
     */
    void Stop(bool await = false);

    /**
     * Add function to be executed on the threadpool. Method returns true in case if task has been placed
     * onto execution queue, i.e scheduled for execution and false otherwise.
     *
     * That function doesn't wait for function result. Function could always be written in a way to notify caller about
     * execution finished by itself
     */
    template <typename F, typename... Types> bool Execute(F &&func, Types... args) {
        {
            std::unique_lock<std::mutex> lock(_mtx);
            if (state != State::kRun || tasks.size() > _max_queue_size) return false;

            // Prepare "task"
            auto exec = std::bind(std::forward<F>(func), std::forward<Types>(args)...);

            // Enqueue new task
            tasks.push(exec);

            // Create new thread if it's necessary
            if (counter_busy_threads == counter_exist_threads && 
                counter_exist_threads < _high_watermark) {
                ++counter_exist_threads;
                std::thread([this](){ this->perform(false); }).detach();
            }
        }
        empty_condition.notify_one();
        return true;
    }

private:
    // No copy/move/assign allowed
    Executor(const Executor &);            // = delete;
    Executor(Executor &&);                 // = delete;
    Executor &operator=(const Executor &); // = delete;
    Executor &operator=(Executor &&);      // = delete;

    //std::shared_ptr<spdlog::logger> _logger;

    /**
     * Main function that all pool threads are running. It polls internal task queue and execute tasks
     */
    void perform(bool is_not_dying_thread);

    /**
     * Mutex to protect task queue, counters from concurrent modification
     */
    std::mutex _mtx;

    
    // Counter of existing threads
    size_t counter_exist_threads;

    // Counter of busy threads
    size_t counter_busy_threads;

    // Conditional variable to await completion all threads after stopping executor
    std::condition_variable can_stop_executor_condition;
    
    /**
     * Conditional variable to await new data in case of empty queue
     */
    std::condition_variable empty_condition;

    /**
     * Task queue
     */
    std::queue<std::function<void()>> tasks;

    /**
     * Flag to stop threads
     */
    State state;

    const std::size_t _low_watermark;
    const std::size_t _high_watermark;
    const std::size_t _max_queue_size;
    const std::size_t _idle_time;
};

} // namespace Concurrency
} // namespace Afina

#endif // AFINA_CONCURRENCY_EXECUTOR_H
