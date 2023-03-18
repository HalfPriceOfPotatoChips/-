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
const int THREAD_MAX_IDLE_TIME = 1; // ��λ����


class Thread
{
public:
	using ThreadFunc = std::function<void(int)>;
	Thread(ThreadFunc func)
		: func_(func)
		, threadid_(generateId++)
	{}
	~Thread() = default;

	// �����߳�
	void start()
	{
		std::thread t(func_, threadid_);
		t.detach();
	}

	// ��ȡ�߳�id
	int getid()const
	{
		return threadid_;
	}
private:
	ThreadFunc func_;		// ��������
	int threadid_;		// �߳�id

	static int generateId;
};

int Thread::generateId = 0;

// �̳߳�֧�ֵ�ģʽ
enum class PoolMode
{
	MODE_FIXED,  // �̶��������߳�
	MODE_CACHED, // �߳������ɶ�̬����
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

	// ����task�������������ֵ
	void setTaskQueMaxThreshHold(int threshhold)
	{
		if (checkRunningState())
			return;
		taskSizeThreshHold_ = threshhold;
	}

	// �����̳߳�cachedģʽ���߳���ֵ
	void setThreadSizeThreshHold(int threshhold)
	{
		if (checkRunningState())
			return;
		if (poolMode_ == PoolMode::MODE_CACHED)
		{
			threadSizeThreshHold_ = threshhold;
		}
	}

	// �̳߳ؿ���
	void start()
	{
		// �����̳߳�����״̬
		isPoolRunning_ = true;

		curThreadSize_ = initThreadSize_;
		idleThreadSize_ = initThreadSize_;

		// �����̶߳��󲢿����߳�
		for (int i = 0; i < initThreadSize_; ++i) {
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
			threads_.emplace(ptr->getid(), std::move(ptr));
			threads_[i]->start();
		}
	}

	// ���̳߳��ύ����
	template<typename Func, typename... Args>
	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))> 
	{ 
		//��������д�� packaged_task
		using RType = decltype(func(args...));

		// taskΪ�ֲ�����ʹ������ָ���ӳ�����������
		auto task = std::make_shared<std::packaged_task<RType()>>(
			std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
		std::future<RType> res = task->get_future();

		std::unique_lock<std::mutex> lock(taskQueMtx_);
		// �ύ�����������1s
		if (!notFull_.wait_for(lock, std::chrono::seconds(1),
			[&]() { return curTaskSize_ < taskSizeThreshHold_; })) {

			std::cout << "task queue is full, submit task fail." << std::endl;
			auto task = std::make_shared<std::packaged_task<RType()>>(
				[]()->RType { return RType(); });
			// ִ��һ�������ź������ӣ����߳� get ����ֵ
			(*task)();
			return task->get_future();
		}

		// ������в��������Ѻ��������֪ͨ���̴߳���
		//taskQue_.emplace(sp);
		// ��task���װһ�㣬ʹ�����ܴ���������͵ĺ�������
		// lambda ����һ���������󣬶�������һ��share_ptr���������󱣴���������У��ӳ���task��������
		taskQue_.emplace([task]() { (*task)(); });
		++curTaskSize_;
		notEmpty_.notify_all();

		// cachedģʽ�£���̬�����߳�
		if (poolMode_ == PoolMode::MODE_CACHED
			&& curTaskSize_ > idleThreadSize_
			&& curThreadSize_ < threadSizeThreshHold_) {
			std::cout << ">>> create new thread ..." << std::endl;

			// �����̶߳���
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
	// �̹߳�������
	void threadFunc(int threadid)
	{
		while (true)
		{
			auto lastTime = std::chrono::high_resolution_clock().now();;
			Task task;
			{
				std::unique_lock<std::mutex> lock(taskQueMtx_);

				std::cout << "tid:" << std::this_thread::get_id()
					<< "���Ի�ȡ����..." << std::endl;

				//std::this_thread::sleep_for(std::chrono::milliseconds(100));

				// ʹ�� while �ܺϲ��̳߳عرյ������жϣ����ô���
				while (curTaskSize_ == 0 && isPoolRunning_) {
					if (poolMode_ == PoolMode::MODE_CACHED) {

						// ����������ʱ����ѭ�����ж��Ƿ������߳�
						// ������������ʱ������ȴ��ź�
						while (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
						{
							auto now = std::chrono::high_resolution_clock().now();
							auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
							if (dur.count() >= THREAD_MAX_IDLE_TIME
								&& curThreadSize_ > initThreadSize_) {
								// �����߳���Դ
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

				// ����������������� ֪ͨ���������
				std::cout << "tid:" << std::this_thread::get_id()
					<< " ��ȡ����ɹ�..." << std::endl;

				--curTaskSize_;
				--idleThreadSize_;


				task = taskQue_.front();
				taskQue_.pop();

				// �����л�������֪ͨ�����߳̽���������
				if (taskQue_.size() > 0)
				{
					notEmpty_.notify_all();
				}
				notFull_.notify_all();
			}

			// ִ������
			if (task != nullptr) task();

			++idleThreadSize_;
		}
	}

	// ���pool������״̬
	bool checkRunningState() const
	{
		return isPoolRunning_;
	}

private:
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;	// �߳��б�

	int initThreadSize_;				// �̳߳�ʼ������
	int threadSizeThreshHold_;		// �߳��������
	std::atomic_int curThreadSize_;	// ��ǰ�߳�����
	std::atomic_int idleThreadSize_;	// �����߳�����

	using Task = std::function<void()>;
	std::queue<Task> taskQue_;	// �������
	std::atomic_int curTaskSize_;					// ��ǰ��������
	int taskSizeThreshHold_;						// �����������

	std::mutex taskQueMtx_;				// �̰߳�ȫ��
	std::condition_variable notFull_;		// ������в�Ϊ��
	std::condition_variable notEmpty_;		// ������в�Ϊ��
	std::condition_variable exitCond_;		// �ȴ��߳���Դȫ������

	PoolMode poolMode_;					// �̹߳���ģʽ
	std::atomic_bool isPoolRunning_;		// �̳߳�����״̬
};