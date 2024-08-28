#include"Threadpool.h"
#include<iostream>
#include<chrono>
#include<thread>
using ULong = unsigned long long;

class MyTask :public Task
{
public:
	MyTask(int begin, int end)
		:begin_(begin)
		, end_(end)
	{}
	Any run()
	{
		std::cout << "tid" << std::this_thread::get_id()
			<< "begin!" << std::endl;
		ULong sum = 0;

		std::this_thread::sleep_for(std::chrono::seconds(3));

		for (ULong i = begin_; i <= end_; i++)
			sum += i;
		std::cout << "tid" << std::this_thread::get_id()
			<< "end!" << std::endl;

		return sum;
	}
private:
	int begin_;
	int end_;
};
int main()
{
	{
		ThreadPool pool;
		pool.setMode(PoolMode::MODE_CACHED);
		pool.start(4);

		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
		Result res3 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));


		pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
		pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
		pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));

		ULong sum1 = res1.get().cast_<ULong>();
		ULong sum2 = res2.get().cast_<ULong>();
		ULong sum3 = res3.get().cast_<ULong>();


		std::cout << sum1 + sum2 + sum3 << std::endl;


		/*ULong sum = 0;
		for (ULong i = 0; i <= 300000000; i++)
			sum += i;

		std::cout << sum;*/
		/*std::this_thread::sleep_for(std::chrono::seconds(5));*/
	}
	std::getchar();


}