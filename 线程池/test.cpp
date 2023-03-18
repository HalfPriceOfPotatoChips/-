#include <iostream>
#include "threadpool.h"

using uLong = unsigned long long;

class MyTask : public Task
{
public:
	MyTask(int begin, int end)
		: begin_(begin)
		, end_(end)
	{}
	// 自定义运行任务
	Any run()
	{
		// 累加
		std::cout << "tid:" << std::this_thread::get_id()
			<< " begin!" << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(2));
		uLong sum = 0;
		for (uLong i = begin_; i <= end_; ++i) sum += i;
		std::cout << "tid:" << std::this_thread::get_id()
			<< " end!" << std::endl;

		return sum;
	}
private:
	int begin_;
	int end_;
};

int main()
{
#if 0
	{
		ThreadPool threadPool(2, PoolMode::MODE_CACHED);
		threadPool.start();
		Result res1 = threadPool.submitTask(std::make_shared<MyTask>(1, 100000000));
		Result res2 = threadPool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
		Result res3 = threadPool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
		threadPool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
		threadPool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
	}

	std::cout << "main over!" << std::endl;
	getchar();
	//std::this_thread::sleep_for(std::chrono::seconds(7));

#else 
	{
		ThreadPool threadPool(7);
		threadPool.start();
		Result res1 = threadPool.submitTask(std::make_shared<MyTask>(1, 100000000));
		Result res2 = threadPool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
		Result res3 = threadPool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
		threadPool.submitTask(std::make_shared<MyTask>(1, 100000000));
		threadPool.submitTask(std::make_shared<MyTask>(1, 100000000));
		threadPool.submitTask(std::make_shared<MyTask>(1, 100000000));
		threadPool.submitTask(std::make_shared<MyTask>(1, 100000000));

		uLong sum1 = res1.get().cast_<uLong>();
		uLong sum2 = res2.get().cast_<uLong>();
		uLong sum3 = res3.get().cast_<uLong>();
		std::cout << (sum1 + sum2 + sum3) << std::endl;
	}
	/*
	uLong sum = 0;
	for (uLong i = 1; i <= 300000000; i++)
		sum += i;
	std::cout << sum << std::endl;
	*/
	std::cout << "main over!" << std::endl;
	//getchar();
#endif
	return 0;
}