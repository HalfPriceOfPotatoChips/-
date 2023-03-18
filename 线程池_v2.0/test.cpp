#include <iostream>
#include <future>
#include "threadpool.h"

using uLong = unsigned long long;


uLong MyTask(int begin, int end) {
	std::cout << "tid:" << std::this_thread::get_id()
		<< " begin!" << std::endl;
	std::this_thread::sleep_for(std::chrono::seconds(2));
	uLong sum = 0;
	for (uLong i = begin; i <= end; ++i) sum += i;
	std::cout << "tid:" << std::this_thread::get_id()
		<< " end!" << std::endl;

	return sum;
}

int main()
{
#if 1
	{
		ThreadPool threadPool(2, PoolMode::MODE_CACHED);
		threadPool.start();
		std::future<uLong> res1 = threadPool.submitTask(MyTask, 1, 100000000);
		std::future<uLong> res2 = threadPool.submitTask(MyTask, 100000001, 200000000);
		std::future<uLong> res3 = threadPool.submitTask(MyTask, 200000001, 300000000);
		threadPool.submitTask(MyTask, 200000001, 300000000);
		threadPool.submitTask(MyTask, 200000001, 300000000);
	}

	std::cout << "main over!" << std::endl;
	getchar();

#else 
	{
		ThreadPool threadPool(3);
		threadPool.start();
		std::future<uLong> res1 = threadPool.submitTask(MyTask, 1, 100000000);
		std::future<uLong> res2 = threadPool.submitTask(MyTask, 100000001, 200000000);
		std::future<uLong> res3 = threadPool.submitTask(MyTask, 200000001, 300000000);

		uLong sum1 = res1.get();
		uLong sum2 = res2.get();
		uLong sum3 = res3.get();
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