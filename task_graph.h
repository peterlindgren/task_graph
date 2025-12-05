#pragma once

#include <atomic>
#include <tuple>
#include <vector>

struct TaskBase;

struct ThreadPoolInterface
{
	virtual ~ThreadPoolInterface() = default;
	virtual void add_tasks(TaskBase **tasks, unsigned num_tasks, uint32_t *out_task_ids) = 0;
	virtual void add_dependencies(uint32_t *tasks, unsigned num_tasks, uint32_t *dependencies, unsigned num_dependencies) = 0;
	virtual void ready_tasks(uint32_t *tasks, unsigned num_tasks) = 0;
	virtual bool do_work() = 0;
	virtual void yield() = 0;
};

struct TaskBase
{
	TaskBase **inputs;
	unsigned count;
	TaskBase() : inputs(nullptr), count(0) {}
	virtual ~TaskBase() = default;
	virtual void operator()() = 0;
};

struct TaskGraphFence : TaskBase
{
	std::atomic<uint32_t> signal{0};
	virtual void operator()() override { signal.fetch_add(1); }
};

struct TaskGraph
{
	std::vector<TaskBase *> tasks;
	TaskGraphFence fence;

	template<typename... Tasks>
	TaskGraph(Tasks&... t) : tasks() , fence()
	{
		tasks.reserve((unsigned)sizeof...(Tasks));
		int dummy[] = {0, (tasks.push_back(static_cast<TaskBase *>(&t)), 0)...};
		(void)dummy;
	}

	TaskGraph(const std::vector<TaskBase *> &ts) : tasks(ts), fence() { }

	void submit(ThreadPoolInterface &pool);
	void wait(ThreadPoolInterface &pool);
};

template<typename... Ts>
struct Task : TaskBase
{
	enum { NUM_DEPS = sizeof...(Ts) };
	TaskBase *storage[NUM_DEPS];
	std::tuple<Ts&...> in;

	Task(Ts&... d) : in(d...)
	{
		unsigned i = 0;
		int dummy[] = {0, (storage[i++] = static_cast<TaskBase *>(&d), 0)...};
		(void)dummy;
		inputs = storage;
		count = NUM_DEPS;
	}

	template<unsigned N>
	auto input() -> decltype(get<N>(in)) { return get<N>(in); }
};

template<>
struct Task<> : TaskBase
{
	Task() {}
};
