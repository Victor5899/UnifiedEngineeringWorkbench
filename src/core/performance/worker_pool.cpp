#include "workbench/performance/worker_pool.hpp"

#include "workbench/performance/hardware_info.hpp"

#include <stdexcept>
#include <utility>

namespace workbench {

WorkerPool::WorkerPool(std::size_t worker_count) : worker_count_(worker_count) {
    if (worker_count == 0) {
        throw std::invalid_argument("Worker count must be positive.");
    }
    const unsigned limit = maximum_supported_workers();
    if (worker_count > 4) {
        throw std::invalid_argument("Worker count must be 1, 2, 3, or 4.");
    }
    if (worker_count > limit) {
        throw std::invalid_argument(
            "Worker count of " + std::to_string(worker_count) +
            " exceeds the " + std::to_string(limit) +
            " worker limit for the logical CPUs available to this process.");
    }

    threads_.reserve(worker_count);
    try {
        for (std::size_t index = 0; index < worker_count; ++index) {
            threads_.emplace_back([this] { worker_loop(); });
        }
    } catch (...) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        work_cv_.notify_all();
        for (std::thread& thread : threads_) {
            if (thread.joinable()) thread.join();
        }
        throw;
    }
}

WorkerPool::~WorkerPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    work_cv_.notify_all();
    for (std::thread& thread : threads_) {
        if (thread.joinable()) thread.join();
    }
}

std::size_t WorkerPool::worker_count() const {
    return worker_count_;
}

void WorkerPool::parallel_for(std::size_t task_count, const std::function<void(std::size_t)>& task) {
    if (task_count > 0 && !task) {
        throw std::invalid_argument("Parallel execution requires a task.");
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            throw std::runtime_error("Worker pool is stopping.");
        }
        if (job_active_) {
            throw std::logic_error("Worker pool is already running a job.");
        }
        error_ = nullptr;
        task_ = task;
        task_count_ = task_count;
        next_index_ = 0;
        remaining_ = task_count;
        job_active_ = task_count > 0;
    }

    if (task_count == 0) return;

    work_cv_.notify_all();
    std::exception_ptr error;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        done_cv_.wait(lock, [this] { return !job_active_; });
        task_ = nullptr;
        error = error_;
        error_ = nullptr;
    }
    if (error) std::rethrow_exception(error);
}

void WorkerPool::worker_loop() {
    for (;;) {
        std::size_t index = 0;
        std::function<void(std::size_t)> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            work_cv_.wait(lock, [this] {
                return stopping_ || (job_active_ && next_index_ < task_count_);
            });
            if (stopping_ && !(job_active_ && next_index_ < task_count_)) return;
            index = next_index_++;
            task = task_;
        }

        // The solve runs here, outside the queue mutex.
        try {
            task(index);
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!error_) error_ = std::current_exception();
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            --remaining_;
            if (remaining_ == 0) {
                job_active_ = false;
                done_cv_.notify_all();
            }
        }
    }
}

}
