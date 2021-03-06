#ifndef CONFLUO_THREADS_TASK_QUEUE_H_
#define CONFLUO_THREADS_TASK_QUEUE_H_

#include <queue>
#include <mutex>
#include <future>
#include <functional>
#include <atomic.h>

#include "logger.h"

// TODO: Can potentially make this more efficient with lock-free concurrency
// Although seems unnecessary for now
/**
 * The task type structure. Contains the task function and a pointer to
 * the next task type.
 */
struct task_type {
  /** The task function */
  std::function<void()> func;
  /** Pointer to the next task type */
  task_type *next;

  /**
   * Constructs a task type from the passed in arguments
   *
   * @tparam ARGS The type of the arguments
   * @param args The arguments to pass into the task function
   */
  template<class ... ARGS>
  task_type(ARGS &&... args);
};

/**
 * The task queue class. Contains queue operations for adding and removing
 * tasks.
 */
class task_queue {
 public:
  /** Function that takes in no arguments */
  typedef std::function<void()> function_t;

  /**
   * Default constructor that initializes to a valid task queue
   */
  task_queue();

  /**
   * Default destructor that invalidates the task queue
   */
  ~task_queue();

  /**
   * Invalidates task queue by obtaining a lock
   */
  void invalidate(void);

  /**
   * Get the first value in the queue.
   * Will block until a value is available unless clear is called or the instance is destructed.
   * @param out A void function that will be overriden with the front of
   * the queue
   * @return Returns true if a value was successfully written to the out parameter, false otherwise.
   */
  bool dequeue(function_t &out);

  /**
   * Push a new value onto the queue.
   * @param f The future
   * @param args The arguments for the queue
   * @return The future value
   */
  template<class F, class ...ARGS>
  auto enqueue(F &&f, ARGS &&... args)
  -> std::future<typename std::result_of<F(ARGS...)>::type> {
    using return_type = typename std::result_of<F(ARGS...)>::type;

    auto task = std::make_shared<std::packaged_task<return_type()> >(
        std::bind(std::forward<F>(f), std::forward<ARGS>(args)...));
    std::future<return_type> res = task->get_future();

    {
      std::lock_guard<std::mutex> lock(mutex_);
      queue_.emplace([task]() { (*task)(); });
      condition_.notify_one();
    }

    return res;
  }

  /**
   * Check whether or not the queue is empty.
   * @return True if empty false otherwise
   */
  bool empty() const;

  /**
   * Clear all items from the queue.
   */
  void clear();

  /**
   * Checks whether or not this queue is valid.
   * @return True if valid false otherwise
   */
  bool is_valid() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return valid_;
  }

 private:
  atomic::type<bool> valid_;
  std::atomic_bool valid{true};
  mutable std::mutex mutex_;
  std::queue<function_t> queue_;
  std::condition_variable condition_;
};

/**
 * The task worker class. Supports operations for workers to execute
 * tasks on the queue.
 */
class task_worker {
 public:
  /**
   * Default constructor that initalizes work to a queue of tasks to do
   * @param queue The task queue the workers get tasks from
   */
  task_worker(task_queue &queue);

  /**
   * Default destructor that stops all the tasks on the queue
   */
  ~task_worker();

  /**
   * Starts worker on a new thread and performs each task on the queue
   */
  void start();

  /**
   * Joins all of the workers and their work if possible
   */
  void stop();

 private:
  atomic::type<bool> stop_;
  task_queue &queue_;
  std::thread worker_;
};

/**
 * Task pool class. Contains functionality to submit jobs into the task
 * pool. 
 */
class task_pool {
 public:

  /**
   * Constructor for task pool that initializes task workers
   * @param num_workers The number of task workers to start
   */
  task_pool(size_t num_workers = 1);

  /**
   * Default destructor that invalidates task queue and deletes all of
   * the workers
   */
  ~task_pool();

  /**
   * Submits work by enqueuing the future result
   * @param f The future
   * @param args Arguments for the future
   * @return The future result
   */
  template<class F, class ...ARGS>
  auto submit(F &&f, ARGS &&... args) -> std::future<typename std::result_of<F(ARGS...)>::type> {
    return queue_.enqueue(std::forward<F>(f), std::forward<ARGS>(args)...);
  }

 private:
  task_queue queue_;
  std::vector<task_worker *> workers_;
};

#endif /* CONFLUO_THREADS_TASK_QUEUE_H_ */
