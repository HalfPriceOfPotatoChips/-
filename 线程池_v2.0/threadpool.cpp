#include "threadpool.h"

#include <iostream>

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_IDLE_TIME = 1; // 单位：秒


Semaphore::Semaphore(int limit)
	: resLimit_(limit)
{}

void Semaphore::wait()
{
	std::unique_lock<std::mutex> lock(mtx_);
	// 等待有资源
	cv_.wait(lock, [&]() { return resLimit_ > 0; });
	--resLimit_;
}

void Semaphore::post()
{
	std::unique_lock<std::mutex> lock(mtx_);
	++resLimit_;
	cv_.notify_all();
}
/*
Result::Result(std::shared_ptr<Task> task, bool isVal)
	: task_(task)
	, isVal_(isVal)
{
	task_->setResult(this);
}

Any Result::get()
{
	if (!isVal_) return "";
	sem_.wait();					// 子线程任务未执行完，阻塞
	return std::move(any_);
}

// 工作线程调用，将 Any 传给主线程
void Result::setVal(Any any)
{
	any_ = std::move(any);
	sem_.post();					// 资源已传递，信号量增加
}

Task::Task()
	: result_(nullptr)
{}

void Task::exec()
{
	if (result_ != nullptr) result_->setVal(run());
}

void Task::setResult(Result* result)
{
	result_ = result;
}
*/
int Thread::generateId = 0;

Thread::Thread(ThreadFunc func)
	: func_(func)
	, threadid_(generateId++)
{}

void Thread::start()
{
	std::thread t(func_, threadid_);
	t.detach();
}

int Thread::getid()const
{
	return threadid_;
}

ThreadPool::ThreadPool(int initThreadSize, PoolMode mode)
	: initThreadSize_(initThreadSize)
	, curTaskSize_(0)
	, idleThreadSize_(0)
	, curThreadSize_(0)
	, taskSizeThreshHold_(TASK_MAX_THRESHHOLD)
	, threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
	, poolMode_(mode)
	, isPoolRunning_(false)
{}

ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;
	//std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	exitCond_.wait(lock, [&]() { return curThreadSize_ == 0; });
}

// 设置task任务队列上线阈值
void ThreadPool::setTaskQueMaxThreshHold(int threshhold)
{
	if (checkRunningState())
		return;
	taskSizeThreshHold_ = threshhold;
}

// 设置线程池cached模式下线程阈值
void ThreadPool::setThreadSizeThreshHold(int threshhold)
{
	if (checkRunningState())
		return;
	if (poolMode_ == PoolMode::MODE_CACHED)
	{
		threadSizeThreshHold_ = threshhold;
	}
}

void ThreadPool::start()
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
/*
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	// 提交任务，最多阻塞1s
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]() { return curTaskSize_ < taskSizeThreshHold_; })) {

		std::cout << "task queue is full, submit task fail." << std::endl;
		return Result(sp, false);
	}

	// 任务队列不满，唤醒后添加任务并通知子线程处理
	taskQue_.emplace(sp);
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
	return Result(sp);
}
*/

void ThreadPool::threadFunc(int threadid)
{
	while (true)
	{
		auto lastTime = std::chrono::high_resolution_clock().now();;
		std::shared_ptr<Task> task;
		{
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid:" << std::this_thread::get_id()
				<< "尝试获取任务..." << std::endl;

			//std::this_thread::sleep_for(std::chrono::milliseconds(100));

			// 使用 while 能合并线程池关闭的两次判断，复用代码
			while (curTaskSize_ == 0 && isPoolRunning_) {
				//if (curTaskSize_ == 0) {

					/*
					//线程池关闭
					if (!isPoolRunning_) {
						threads_.erase(threadid);
						--curThreadSize_;
						--idleThreadSize_;

						std::cout << "threadid: " << std::this_thread::get_id() << " exit!" << std::endl;
						exitCond_.notify_one();
						return;
					}
					*/
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

bool ThreadPool::checkRunningState() const
{
	return isPoolRunning_;
}