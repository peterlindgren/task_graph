#pragma once

#include <atomic>
#include <tuple>
#include <vector>
#include <functional>

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
	unsigned num_inputs;
	TaskBase() : inputs(nullptr), num_inputs(0) {}
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

	template<typename... Tasks,
		typename = std::enable_if_t<(std::is_base_of_v<TaskBase, std::remove_reference_t<Tasks>> && ...)>>
	TaskGraph(Tasks&... t) : tasks() , fence()
	{
		tasks.reserve((unsigned)sizeof...(Tasks));
		int dummy[] = {0, (tasks.push_back(static_cast<TaskBase *>(&t)), 0)...};
		(void)dummy;
	}

	TaskGraph(const std::vector<TaskBase *> &ts) : tasks(ts), fence() { }

	template<typename T>
	TaskGraph(std::vector<T> &ts) : tasks(), fence()
	{
		tasks.reserve(ts.size());
		for (auto &t : ts)
			tasks.push_back(static_cast<TaskBase *>(&t));
	}

	void submit(ThreadPoolInterface &pool);
	void wait(ThreadPoolInterface &pool);
};

// Task function object interface

template<typename... Deps>
struct Task : TaskBase
{
	enum { NUM_DEPS = sizeof...(Deps) };
	TaskBase *storage[NUM_DEPS];
	std::tuple<Deps&...> in;

	Task(Deps&... d) : in(d...)
	{
		unsigned i = 0;
		int dummy[] = {0, (storage[i++] = static_cast<TaskBase *>(&d), 0)...};
		(void)dummy;
		inputs = storage;
		num_inputs = NUM_DEPS;
	}

	template<unsigned N>
	auto input() -> decltype(get<N>(in)) { return get<N>(in); }
};

template<>
struct Task<> : TaskBase
{
	Task() {}
};

// Task function interface

template<typename F, typename... Deps>
struct TaskFn : TaskBase
{
	enum { NUM_DEPS = sizeof...(Deps) > 0 ? sizeof...(Deps) : 1 };
	F func;
	TaskBase *storage[NUM_DEPS];

	TaskFn(F f, Deps&... d) : func(std::move(f))
	{
		unsigned i = 0;
		int dummy[] = {0, (storage[i++] = static_cast<TaskBase *>(&d), 0)...};
		(void)dummy;
		inputs = storage;
		num_inputs = sizeof...(Deps);
	}

	virtual void operator()() override { func(); }
};

template<typename F, typename... Deps>
TaskFn(F, Deps&...) -> TaskFn<F, Deps...>;

// Task slicing interface

struct SliceSettings
{
	unsigned max_chunks = 20;
	unsigned min_chunk_size = 1;
	unsigned alignment = 1;
};

inline unsigned chunk_size(unsigned count, const SliceSettings &s)
{
	unsigned size = 0;
	if (s.max_chunks)
		size = (count + s.max_chunks - 1) / s.max_chunks;
	if (size < s.min_chunk_size)
		size = s.min_chunk_size;
	if (size > count)
		size = count;
	if (s.alignment > 1)
		size = (size + s.alignment - 1) & ~(s.alignment - 1);
	return size;
}

inline unsigned num_chunks(unsigned count, const SliceSettings &s)
{
	if (count == 0)
		return 0;
	unsigned cs = chunk_size(count, s);
	return (count + cs - 1) / cs;
}

template<typename T, typename R>
struct Slice
{
	T *data;
	R &result;
	unsigned count;
};

template<typename T, typename R, typename F>
struct TaskSlice : TaskBase
{
	F func;
	T *data;
	unsigned count;
	R result{};

	TaskSlice(F f, T *d, unsigned c)
		: func(std::move(f)), data(d), count(c) {}

	virtual void operator()() override {
		func(Slice<T, R>{data, result, count});
	}
};

template<typename R, typename T, typename F>
auto slice(unsigned count, T *data, F &&f, SliceSettings s = {})
	-> std::vector<TaskSlice<T, R, std::decay_t<F>>>
{
	using Task = TaskSlice<T, R, std::decay_t<F>>;
	std::vector<Task> tasks;
	unsigned n = num_chunks(count, s);
	tasks.reserve(n);
	for (unsigned i = 0; i < n; ++i) {
		unsigned cs = chunk_size(count, s);
		unsigned off = cs * i;
		unsigned len = (off + cs > count) ? count - off : cs;
		tasks.emplace_back(std::forward<F>(f), data + off, len);
	}
	return tasks;
}
