#pragma once

#include <cstddef>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace workbench {

// Fixed set of long-lived worker threads. parallel_for distributes index
// ranges across those threads. It does not create a thread per index.
//
// The queue mutex is held only while claiming an index. The task itself runs
// without that mutex. Destroying a pool joins every worker. parallel_for is
// not re-entrant and must be called from one control thread at a time.
class WorkerPool {
public:
    explicit WorkerPool(std::size_t worker_count);
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;
    ~WorkerPool();

    std::size_t worker_count() const;

    // Invokes task(index) exactly once for every index in [0, task_count).
    // Completion order is not index order. The call blocks until every index
    // has finished. If a task throws, the other indices still run, and the
    // first exception is rethrown after the batch finishes. The pool can be
    // used again after that exception.
    void parallel_for(std::size_t task_count, const std::function<void(std::size_t)>& task);

private:
    void worker_loop();

    std::size_t worker_count_ = 0;
    std::vector<std::thread> threads_;
    std::mutex mutex_;
    std::condition_variable work_cv_;
    std::condition_variable done_cv_;
    bool stopping_ = false;
    bool job_active_ = false;
    std::size_t next_index_ = 0;
    std::size_t task_count_ = 0;
    std::size_t remaining_ = 0;
    std::function<void(std::size_t)> task_;
    std::exception_ptr error_;
};

}
