#include "Async.hpp"

#include "Localization/Localization.hpp"

#include <atomic>
#include <map>

namespace ld {

std::optional<std::string> messageCode(std::string_view message) {
    if (message.empty() || message[0] != '[') return std::nullopt;
    auto end = message.find(']');
    if (end == std::string_view::npos) return std::nullopt;
    return std::string(message.substr(1, end - 1));
}

std::string readable(std::string_view message) {
    auto end = message.find(']');
    if (message.empty() || message[0] != '[' || end == std::string_view::npos) return localization::translateMessage(message);
    std::string_view rest = message.substr(end + 1);
    std::size_t start = rest.find_first_not_of(" \t");
    return start == std::string_view::npos ? std::string() : localization::translateMessage(rest.substr(start));
}

std::optional<std::string> ServiceError::code() const { return messageCode(what()); }

std::string ServiceError::reason() const { return readable(what()); }

std::string describe(std::exception_ptr error) {
    if (!error) return "";
    try {
        std::rethrow_exception(error);
    } catch (const std::exception& e) {
        return readable(e.what());
    } catch (...) {
        return tr("Errore sconosciuto");
    }
}

void ManualQueue::post(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> guard(lock);
        jobs.push_back(std::move(job));
    }
    signal.notify_all();
}

std::size_t ManualQueue::drain() {
    std::size_t count = 0;
    while (true) {
        std::function<void()> job;
        {
            std::lock_guard<std::mutex> guard(lock);
            if (jobs.empty()) break;
            job = std::move(jobs.front());
            jobs.pop_front();
        }
        job();
        ++count;
    }
    return count;
}

bool ManualQueue::pump(double seconds) {
    {
        std::unique_lock<std::mutex> guard(lock);
        if (jobs.empty()) signal.wait_for(guard, std::chrono::duration<double>(seconds), [this] { return !jobs.empty(); });
    }
    return drain() > 0;
}

ThreadPool::ThreadPool(unsigned threads) {
    for (unsigned i = 0; i < threads; ++i)
        workers.emplace_back([this] {
            while (true) {
                std::function<void()> job;
                {
                    std::unique_lock<std::mutex> guard(lock);
                    signal.wait(guard, [this] { return stopping || !jobs.empty(); });
                    if (stopping && jobs.empty()) return;
                    job = std::move(jobs.front());
                    jobs.pop_front();
                }
                job();
            }
        });
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> guard(lock);
        stopping = true;
    }
    signal.notify_all();
    for (auto& w : workers) w.join();
}

void ThreadPool::post(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> guard(lock);
        jobs.push_back(std::move(job));
    }
    signal.notify_one();
}

namespace {

std::atomic<Executor*> installed{nullptr};

struct TimerThread {
    std::mutex lock;
    std::condition_variable signal;
    std::multimap<std::chrono::steady_clock::time_point, std::function<void()>> due;
    std::thread worker;
    bool stopping = false;

    TimerThread() {
        worker = std::thread([this] {
            std::unique_lock<std::mutex> guard(lock);
            while (!stopping) {
                if (due.empty()) {
                    signal.wait(guard);
                    continue;
                }
                auto next = due.begin()->first;
                if (signal.wait_until(guard, next) == std::cv_status::timeout || std::chrono::steady_clock::now() >= next) {
                    auto now = std::chrono::steady_clock::now();
                    std::vector<std::function<void()>> ready;
                    while (!due.empty() && due.begin()->first <= now) {
                        ready.push_back(std::move(due.begin()->second));
                        due.erase(due.begin());
                    }
                    guard.unlock();
                    for (auto& job : ready) mainQueue().post(std::move(job));
                    guard.lock();
                }
            }
        });
    }

    ~TimerThread() {
        {
            std::lock_guard<std::mutex> guard(lock);
            stopping = true;
        }
        signal.notify_all();
        worker.join();
    }
};

TimerThread& timers() {
    static TimerThread instance;
    return instance;
}

}

ManualQueue& fallbackQueue() {
    static ManualQueue queue;
    return queue;
}

Executor& mainQueue() {
    Executor* e = installed.load();
    return e ? *e : fallbackQueue();
}

void setMainQueue(Executor* executor) { installed.store(executor); }

Executor& backgroundPool() {
    static ThreadPool pool(std::max(2u, std::thread::hardware_concurrency()));
    return pool;
}

void after(double seconds, std::function<void()> job) {
    auto& t = timers();
    {
        std::lock_guard<std::mutex> guard(t.lock);
        t.due.emplace(std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(std::max(0.0, seconds))), std::move(job));
    }
    t.signal.notify_all();
}

void spawnDetached(Async<void> task, std::function<void(std::exception_ptr)> finished) { task.start(std::move(finished)); }

}
