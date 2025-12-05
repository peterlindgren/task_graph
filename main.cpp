#include "task_graph.h"

#define BIKESHED_IMPLEMENTATION
#include "bikeshed.h"

#define WIN32_MEAN_AND_LEAN
#include <windows.h>

#include <atomic>
#include <vector>

#include <stdlib.h>
#include <assert.h>

#define MAX_TASKS 1024
#define MAX_DEPENDENCIES 1024

#if defined(DEBUG) || defined(_DEBUG)
#	define DEBUG_PRINTF(fmt, ...) \
		do { \
			fprintf(stderr, "[%lu] " fmt "\n", GetCurrentThreadId(), ## __VA_ARGS__); \
			fflush(stderr); \
		} while (0);
#else
#	define DEBUG_PRINTF(fmt, ...)
#endif

struct ThreadPool : public Bikeshed_ReadyCallback, public ThreadPoolInterface
{
	Bikeshed shed;
	HANDLE semaphore;

	std::vector<HANDLE> threads;
	std::atomic<bool> quit{false};

	ThreadPool() : shed(nullptr)
	{
		SignalReady = &bikeshed_signal_ready;
		Bikeshed_SetAssert(bikeshed_assert);
		semaphore = CreateSemaphoreW(NULL, 0, MAX_TASKS, NULL);
		assert(semaphore);
	}

	~ThreadPool()
	{
		shutdown();
		CloseHandle(semaphore);
	}

	void start(unsigned num_threads)
	{
		threads.resize(num_threads);
		for (unsigned i = 0; i < num_threads; ++i) {
			threads[i] = CreateThread(NULL, 0, worker_entry, this, 0, NULL);
			assert(threads[i]);
		}
	}

	void shutdown()
	{
		if (threads.empty())
			return;

		quit.store(true, std::memory_order_release);
		ReleaseSemaphore(semaphore, (LONG)threads.size(), NULL);
		WaitForMultipleObjects((DWORD)threads.size(), threads.data(), TRUE, INFINITE);

		for (HANDLE h : threads)
			CloseHandle(h);

		threads.clear();
	}

	static DWORD WINAPI worker_entry(LPVOID param)
	{
		ThreadPool *pool = (ThreadPool *)param;
		DEBUG_PRINTF("thread start");
		while (true) {
			WaitForSingleObject(pool->semaphore, INFINITE);
			DEBUG_PRINTF("thread woke");
			if (pool->quit.load(std::memory_order_acquire))
				break;
			while (pool->do_work())
				;
		}
		DEBUG_PRINTF("thread exit");
		return 0;
	}

	static void bikeshed_assert(const char *expression, const char* file, int line)
	{
		fprintf(stderr, "Assertion failed: %s\n", expression);
		fprintf(stderr, "At: %s:%d\n", file, line);
		fflush(stderr);
		assert(false);
	}

	static void bikeshed_signal_ready(struct Bikeshed_ReadyCallback* ready_callback, uint8_t channel, uint32_t ready_count)
	{
		DEBUG_PRINTF("bikeshed_signal_ready ready_count=%u", ready_count);
		ThreadPool *self = (ThreadPool *)ready_callback;
		ReleaseSemaphore(self->semaphore, ready_count, NULL);
	}

	static Bikeshed_TaskResult bikeshed_trampoline(Bikeshed shed, Bikeshed_TaskID task_id, uint8_t channel, void *context)
	{
		DEBUG_PRINTF("bikeshed_trampoline");
		(*static_cast<TaskBase *>(context))();
		return BIKESHED_TASK_RESULT_COMPLETE;
	}

	virtual void add_tasks(TaskBase **tasks, unsigned num_tasks, uint32_t *out_task_ids) override
	{
		std::vector<BikeShed_TaskFunc> funcs;
		funcs.reserve(num_tasks);
		for (unsigned i = 0; i < num_tasks; ++i)
			funcs.push_back(bikeshed_trampoline);

		int ok = Bikeshed_CreateTasks(shed, num_tasks, funcs.data(), reinterpret_cast<void **>(tasks), out_task_ids);
		assert(ok);
	}

	virtual void add_dependencies(uint32_t *tasks, unsigned num_tasks, uint32_t *dependencies, unsigned num_dependencies) override
	{
		int ok = Bikeshed_AddDependencies(shed, num_tasks, tasks, num_dependencies, dependencies);
		assert(ok);
	}

	virtual void ready_tasks(uint32_t *tasks, unsigned num_tasks) override
	{
		Bikeshed_ReadyTasks(shed, num_tasks, tasks);
	}

	virtual bool do_work() override
	{
		return Bikeshed_ExecuteOne(shed, 0) == 1;
	}

	virtual void yield() override
	{
		YieldProcessor();
	}
};

struct a : Task<> {
	int value;
	a(int i) : value(i) {}
	virtual void operator()() override {
		printf("a=%d\n", value); fflush(stdout);
	}
};

struct b : Task<>  {
	int value;
	b(int i) : value(i) {}
	virtual void operator()() override {
		printf("b=%d\n", value); fflush(stdout);
	}
};

struct c : Task<a, b>  {
	using Task::Task;
	int value;
	virtual void operator()() override {
		a &a = std::get<0>(in);
		b &b = std::get<1>(in);
		value = a.value * b.value;
		printf("c=%d*%d=%d\n", a.value, b.value, value); fflush(stdout);
	}
};

struct d : Task<a, a>  {
	using Task::Task;
	int value;
	virtual void operator()() override {
		struct a &a1 = std::get<0>(in);
		struct a &a2 = std::get<1>(in);
		value = a1.value * a2.value;
		printf("d=%d*%d=%d\n", a1.value, a2.value, value); fflush(stdout);
	}
};

struct e : Task<c, d>  {
	using Task::Task;
	int value;
	virtual void operator()() override {
		c &c = std::get<0>(in);
		d &d = std::get<1>(in);
		value = c.value * d.value;
		printf("e=%d*%d=%d\n", c.value, d.value, value); fflush(stdout);
	}
};

int safe_main() {
	const unsigned bytes = BIKESHED_SIZE(MAX_TASKS, MAX_DEPENDENCIES, 1);
	void *mem = malloc(bytes);
	assert(mem);

	DEBUG_PRINTF("Bikeshed: %u bytes", bytes);

	ThreadPool pool;
	pool.shed = Bikeshed_Create(mem, MAX_TASKS, MAX_DEPENDENCIES, 1, &pool);
	pool.start(4);

	DEBUG_PRINTF("ThreadPool: %u threads", (unsigned)pool.threads.size());

	struct a a(2);
	struct b b(3);
	struct c c(a, b);
	struct d d(a, a);
	struct e e(c, d);

	struct TaskGraph g(a, b, c, d, e);
	g.submit(pool);
	g.wait(pool);

	printf("result: %d\n", e.value);

	// SR_SEMAPHORE_WAIT(*thread.semaphore);
	// while(thread.pool->do_work()) ;
	// Bikeshed_ExecuteOne(shed, 0);

	free(mem);
	return 1;
}

int main() {
	__try {
		return safe_main();
	}
	__except(EXCEPTION_EXECUTE_HANDLER) {
		fprintf(stderr, "Exception code: 0x%08x\n", GetExceptionCode());
		return 1;
	}
}
