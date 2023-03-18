#pragma once
#include <functional>
#include <queue>
#include <unordered_map>

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>


// �ܹ������������͵�����
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

	// ��ȡdata���ݣ�������ʵ������
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
		T data_;		// ������������
	};
private:
	std::unique_ptr<Base> ptr_;
};

// �ź���
class Semaphore
{
public:
	Semaphore(int limit = 0);

	// ����һ����Դ
	void wait();

	// ����һ����Դ
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

	// ��������ִ����ķ���ֵ
	void setVal(Any any);

	// �û��������������ȡtask�ķ���ֵ
	Any get();
private:
	Any any_;						// �洢����ķ���ֵ
	std::shared_ptr<Task> task_;		// ָ���Ӧ������
	Semaphore sem_;					// �߳�ͨ���ź���
	bool isVal_;
};

// ����������
class Task
{
public:
	Task();
	~Task() = default;
	void exec();
	void setResult(Result* res);

	// �û������Զ��������������ͣ���Task�̳У���дrun������ʵ���Զ���������
	virtual Any run() = 0;

private:
	Result* result_; // Result������������� �� Task��
};


class Thread
{
public:
	using ThreadFunc = std::function<void(int)>;
	Thread(ThreadFunc func);
	~Thread() = default;

	// �����߳�
	void start();

	// ��ȡ�߳�id
	int getid()const;
private:
	ThreadFunc func_;		// ��������
	int threadid_;		// �߳�id

	static int generateId;
};

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
		PoolMode mode = PoolMode::MODE_FIXED);
	~ThreadPool();

	// ����task�������������ֵ
	void setTaskQueMaxThreshHold(int threshhold);

	// �����̳߳�cachedģʽ���߳���ֵ
	void setThreadSizeThreshHold(int threshhold);

	// �̳߳ؿ���
	void start();

	// ���̳߳��ύ����
	Result submitTask(std::shared_ptr<Task> sp);

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	// �̹߳�������
	void threadFunc(int threadid);

	// ���pool������״̬
	bool checkRunningState() const;

private:
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;	// �߳��б�

	int initThreadSize_;				// �̳߳�ʼ������
	int threadSizeThreshHold_;		// �߳��������
	std::atomic_int curThreadSize_;	// ��ǰ�߳�����
	std::atomic_int idleThreadSize_;	// �����߳�����

	std::queue<std::shared_ptr<Task>> taskQue_;	// �������
	std::atomic_int curTaskSize_;					// ��ǰ��������
	int taskSizeThreshHold_;						// �����������

	std::mutex taskQueMtx_;				// �̰߳�ȫ��
	std::condition_variable notFull_;		// ������в�Ϊ��
	std::condition_variable notEmpty_;		// ������в�Ϊ��
	std::condition_variable exitCond_;		// �ȴ��߳���Դȫ������

	PoolMode poolMode_;					// �̹߳���ģʽ
	std::atomic_bool isPoolRunning_;		// �̳߳�����״̬
};