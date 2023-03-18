#pragma once
#include <functional>
#include <future>
#include <queue>
#include <unordered_map>

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_IDLE_TIME = 1; // 单位：秒


class Thread
{
public:
	using ThreadFunc = std::function<void(int)>;
	Thread(ThreadFunc func)
		: func_(func)
		, threadid_(generateId++)
	{}
	~Thread() = default;

	// 启动线程
	void start()
	{
		std::thread t(func_, threadid_);
		t.detach();
	}

	// 获取线程id
	int getid()const
	{
		return threadid_;
	}
private:
	ThreadFunc func_;		// 工作函数
	int threadid_;		// 线程id

	static int generateId;
};

int Thread::generateId = 0;

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
		PoolMode mode = PoolMode::MODE_FIXED)
		: initThreadSize_(initThreadSize)
		, curTaskSize_(0)
		, idleThreadSize_(0)
		, curThreadSize_(0)
		, taskSizeThreshHold_(TASK_MAX_THRESHHOLD)
		, threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
		, poolMode_(mode)
		, isPoolRunning_(false)
	{}

	~ThreadPool()
	{
		isPoolRunning_ = false;
		//std::unique_lock<std::mutex> lock(taskQueMtx_);
		notEmpty_.notify_all();
		std::unique_lock<std::mutex> lock(taskQueMtx_);
		exitCond_.wait(lock, [&]() { return curThreadSize_ == 0; });
	}

	// 设置task任务队列上线阈值
	void setTaskQueMaxThreshHold(int threshhold)
	{
		if (checkRunningState())
			return;
		taskSizeThreshHold_ = threshhold;
	}

	// 设置线程池cached模式下线程阈值
	void setThreadSizeThreshHold(int threshhold)
	{
		if (checkRunningState())
			return;
		if (poolMode_ == PoolMode::MODE_CACHED)
		{
			threadSizeThreshHold_ = threshhold;
		}
	}

	// 线程池开启
	void start()
	{
		// 设置线程池运行状态
		isPoolRunning_ = true;

		curThreadSize_ = initThreadSize_;
		idleThreadSize_ = initThreadSize_;

		// 创建线程对象并开启线程
		for (int i = 0; i < initThreadSize_; ++i) {
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			threads_.emplace(ptr->getid(), std::move(ptr));
			threads_[i]->start();
		}
	}

	// 给线程池提交任务
	template<typename Func, typename... Args>
	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))> 
	{ 
		//对任务进行打包 packaged_task
		using RType = decltype(func(args...));

		// task为局部对象，使用智能指针延长其生命周期
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
		std::future<RType> res = task->get_future();

		std::unique_lock<std::mutex> lock(taskQueMtx_);
		// 提交任务，最多阻塞1s
		if (!notFull_.wait_for(lock, std::chrono::seconds(1),
			[&]() { return curTaskSize_ < taskSizeThreshHold_; })) {

			std::cout << "task queue is full, submit task fail." << std::endl;
			auto task = std::make_shared<std::packaged_task<RType()>>(
				[]()->RType { return RType(); });
			// 执行一次任务，信号量增加，主线程 get 返回值
			(*task)();
			return task->get_future();
		}

		// 任务队列不满，唤醒后添加任务并通知子线程处理
		//taskQue_.emplace(sp);
		// 在task外封装一层，使队列能存放任意类型的函数对象
		// lambda 生成一个函数对象，对象内有一个share_ptr，函数对象保存在任务队列，延长了task生命周期
		taskQue_.emplace([task]() { (*task)(); });
		++curTaskSize_;
		notEmpty_.notify_all();

		// cached模式下，动态创建线程
		if (poolMode_ == PoolMode::MODE_CACHED
			&& curTaskSize_ > idleThreadSize_
			&& curThreadSize_ < threadSizeThreshHold_) {
			std::cout << ">>> create new thread ..." << std::endl;

			// 创建线程对象
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			int threadId = ptr->getid();
			threads_.emplace(threadId, std::move(ptr));
			threads_[threadId]->start();
			++idleThreadSize_;
			++curThreadSize_;
		}
		return res;
	}

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	// 线程工作函数
	void threadFunc(int threadid)
	{
		while (true)
		{
			auto lastTime = std::chrono::high_resolution_clock().now();;
			Task task;
			{
				std::unique_lock<std::mutex> lock(taskQueMtx_);

				std::cout << "tid:" << std::this_thread::get_id()
					<< "尝试获取任务..." << std::endl;

				//std::this_thread::sleep_for(std::chrono::milliseconds(100));

				// 使用 while 能合并线程池关闭的两次判断，复用代码
				while (curTaskSize_ == 0 && isPoolRunning_) {
					if (poolMode_ == PoolMode::MODE_CACHED) {

						// 条件不成立时进行循环，判断是否销毁线程
						// 队列中有任务时，无需等待信号
						while (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
						{
							auto now = std::chrono::high_resolution_clock().now();
							auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
							if (dur.count() >= THREAD_MAX_IDLE_TIME
								&& curThreadSize_ > initThreadSize_) {
								// 回收线程资源
								threads_.erase(threadid);
								--curThreadSize_;
								--idleThreadSize_;

								//std::cout << "outtime" << std::endl;
								std::cout << "threadid: " << std::this_thread::get_id() << " exit!" << std::endl;
								return;
							}
						}
					}
					else {
						notEmpty_.wait(lock);
					}
				}
				if (!isPoolRunning_ && curTaskSize_ == 0) {
					threads_.erase(threadid);
					--curThreadSize_;
					--idleThreadSize_;

					std::cout << "threadid: " << std::this_thread::get_id() << " exit!" << std::endl;
					exitCond_.notify_one();
					return;
				}

				// 从任务队列中拿任务 通知可添加任务
				std::cout << "tid:" << std::this_thread::get_id()
					<< " 获取任务成功..." << std::endl;

				--curTaskSize_;
				--idleThreadSize_;


				task = taskQue_.front();
				taskQue_.pop();

				// 队列中还有任务，通知其他线程进行拿任务
				if (taskQue_.size() > 0)
				{
					notEmpty_.notify_all();
				}
				notFull_.notify_all();
			}

			// 执行任务
			if (task != nullptr) task();

			++idleThreadSize_;
		}
	}

	// 检查pool的运行状态
	bool checkRunningState() const
	{
		return isPoolRunning_;
	}

private:
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;	// 线程列表

	int initThreadSize_;				// 线程初始化数量
	int threadSizeThreshHold_;		// 线程最大数量
	std::atomic_int curThreadSize_;	// 当前线程数量
	std::atomic_int idleThreadSize_;	// 空闲线程数量

	using Task = std::function<void()>;
	std::queue<Task> taskQue_;	// 任务队列
	std::atomic_int curTaskSize_;					// 当前任务数量
	int taskSizeThreshHold_;						// 最大任务数量

	std::mutex taskQueMtx_;				// 线程安全锁
	std::condition_variable notFull_;		// 任务队列不为满
	std::condition_variable notEmpty_;		// 任务队列不为空
	std::condition_variable exitCond_;		// 等待线程资源全部回收

	PoolMode poolMode_;					// 线程工作模式
	std::atomic_bool isPoolRunning_;		// 线程池运行状态
};