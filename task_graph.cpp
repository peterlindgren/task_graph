#include "task_graph.h"

#include "stack_allocator.h"

#include <set>

template <typename T>
using StackSet = std::set<T, std::less<T>, StackAllocator<T>>;

template <typename T>
using StackVector = std::vector<T, StackAllocator<T>>;

void TaskGraph::submit(ThreadPoolInterface &pool)
{
	StackArena<4096> arena;

	tasks.push_back(&fence);

	StackVector<uint32_t> ids(StackAllocator<uint32_t>{arena});
	ids.resize(tasks.size());
	pool.add_tasks(tasks.data(), tasks.size(), ids.data());

	tasks.pop_back();

	StackVector<uint32_t> roots(StackAllocator<uint32_t>{arena});
	roots.reserve(tasks.size());

	StackSet<TaskBase *> has_deps(std::less<TaskBase *>{}, StackAllocator<TaskBase *>{arena});
	for (unsigned i = 0; i < tasks.size(); ++i) {
		TaskBase *t = tasks[i];

		if (t->count == 0) {
			roots.push_back(ids[i]);
			continue;
		}

		std::vector<uint32_t> deps;
		for (unsigned j = 0; j < t->count; ++j) {
			TaskBase *dep  = t->inputs[j];
			auto it = std::find(tasks.begin(), tasks.end(), dep);
			unsigned dep_idx = std::distance(tasks.begin(), it);
			uint32_t dep_id = ids[dep_idx];
			deps.push_back(dep_id);

			has_deps.insert(dep);
		}

		if (deps.size() > 0) {
			pool.add_dependencies(&ids[i], 1, deps.data(), deps.size());
		}
	}

	StackVector<uint32_t> leaves(StackAllocator<uint32_t>{arena});
	leaves.reserve(has_deps.size());
	for (unsigned i = 0; i < tasks.size(); ++i)
		if (std::find(has_deps.begin(), has_deps.end(), tasks[i]) == has_deps.end())
			leaves.push_back(ids[i]);

	uint32_t completion_id = ids.back();
	pool.add_dependencies(&completion_id, 1, leaves.data(), leaves.size());

	pool.ready_tasks(roots.data(), roots.size());
}

void TaskGraph::wait(ThreadPoolInterface &pool)
{
	while (fence.signal.load(std::memory_order_acquire) == 0)
		if (!pool.do_work())
			pool.yield();
}
