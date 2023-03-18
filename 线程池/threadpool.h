#pragma once
#include <functional>
#include <queue>
#include <unordered_map>

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>


// 能够接受任意类型的数据
class Any
{
public:
	Any() = default;
	~Any() = default;
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;

	template<typename T>
	Any(T data) : ptr_(std::make_unique<Derive<T>>(data))
	{}

	// 提取data数据，返回其实际类型
	template<typename T>
	T cast_()
	{
		auto pd = dynamic_cast<Derive<T>*>(ptr_.get());
		if (pd == nullptr) {
			throw "type is unmatch!";
		}
		return pd->data_;
	}
private:
	class Base
	{
	public:
		virtual ~Base() = default;
	};

	template<typename T>
	class Derive : public Base
	{
	public:
		Derive(T data) : data_(data)
		{};
		T data_;		// 保存任意类型
	};
private:
	std::unique_ptr<Base> ptr_;
};

// 信号类
class Semaphore
{
public:
	Semaphore(int limit = 0);

	// 消耗一份资源
	void wait();

	// 生成一份资源
	void post();
private:
	int resLimit_;
	std::mutex mtx_;
	std::condition_variable cv_;
};

class Task;

class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isVal = true);
	~Result() = default;

	// 保存任务执行完的返回值
	void setVal(Any any);

	// 用户调用这个方法获取task的返回值
	Any get();
private:
	Any any_;						// 存储任务的返回值
	std::shared_ptr<Task> task_;		// 指向对应的任务
	Semaphore sem_;					// 线程通信信号量
	bool isVal_;
};

// 任务抽象基类
class Task
{
public:
	Task();
	~Task() = default;
	void exec();
	void setResult(Result* res);

	// 用户可以自定义任意任务类型，从Task继承，重写run方法，实现自定义任务处理
	virtual Any run() = 0;

private:
	Result* result_; // Result对象的声明周期 》 Task的
};


class Thread
{
public:
	using ThreadFunc = std::function<void(int)>;
	Thread(ThreadFunc func);
	~Thread() = default;

	// 启动线程
	void start();

	// 获取线程id
	int getid()const;
private:
	ThreadFunc func_;		// 工作函数
	int threadid_;		// 线程id

	static int generateId;
};

// 线程池支持的模式
enum class PoolMode
{
	MODE_FIXED,  // 固定数量的线程
	MODE_CACHED, // 线程数量可动态增长
};

class ThreadPool
{
public:
	ThreadPool(int initThreadSize = std::thread::hardware_concurrency(),
		PoolMode mode = PoolMode::MODE_FIXED);
	~ThreadPool();

	// 设置task任务队列上线阈值
	void setTaskQueMaxThreshHold(int threshhold);

	// 设置线程池cached模式下线程阈值
	void setThreadSizeThreshHold(int threshhold);

	// 线程池开启
	void start();

	// 给线程池提交任务
	Result submitTask(std::shared_ptr<Task> sp);

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	// 线程工作函数
	void threadFunc(int threadid);

	// 检查pool的运行状态
	bool checkRunningState() const;

private:
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;	// 线程列表

	int initThreadSize_;				// 线程初始化数量
	int threadSizeThreshHold_;		// 线程最大数量
	std::atomic_int curThreadSize_;	// 当前线程数量
	std::atomic_int idleThreadSize_;	// 空闲线程数量

	std::queue<std::shared_ptr<Task>> taskQue_;	// 任务队列
	std::atomic_int curTaskSize_;					// 当前任务数量
	int taskSizeThreshHold_;						// 最大任务数量

	std::mutex taskQueMtx_;				// 线程安全锁
	std::condition_variable notFull_;		// 任务队列不为满
	std::condition_variable notEmpty_;		// 任务队列不为空
	std::condition_variable exitCond_;		// 等待线程资源全部回收

	PoolMode poolMode_;					// 线程工作模式
	std::atomic_bool isPoolRunning_;		// 线程池运行状态
};