#include "Threadpool.h"
#include <functional>
#include<iostream>
#include<thread>
#include<condition_variable>
//////////////////////////////

Result::Result(std::shared_ptr<Task> task, bool isValid)
	:task_(task)
	, isValid_(isValid)
{
	task_->setResult(this);
}

//用户调用->获取返回值
Any Result::get()
{
	if (!isValid_) return "";
	sem_.wait();  // 等待资源
	return std::move(any_);
}
//任务执行完调用，获取任务执行完的返回值，为成员变量any_赋值
void Result::setVal(Any any)
{
	this->any_ = std::move(any);
	sem_.post();  // 增加信号量资源
}

///////////////////////////////////////
Task::Task()
	:result_(nullptr)
{
}
void Task::exec()
{
	if (result_ != nullptr)
	{
		result_->setVal(run());
	}

}
void Task::setResult(Result* res)
{
	result_ = res; //Task与Result相互绑定
}
////////////////
int Thread::generateId_ = 0;
int Thread::getID() const
{
	return threadId_;
}
Thread::Thread(ThreadFunc func)
	:func_(func)
	, threadId_(generateId_++)
{}

Thread::~Thread()
{}

void Thread::start()
{
	std::thread t(func_, threadId_);//func_类的成员变量
	t.detach();
}
//////////////////////////////////////////////////////////////////////////////////////////////////////
const int THREAD_MAX_IDLE_TIME = 10;
ThreadPool::ThreadPool()
	:initThreadSize_(4),
	taskQueMaxThreshHold_(1024),
	taskSize_(0),
	poolMode_(PoolMode::MODE_FIXED),
	ispoolrunning_(false),
	idleThreadSize_(0),
	threadSizeThreshHold_(10),
	curThreadSize_(0)
{
}

ThreadPool::~ThreadPool()
{
	ispoolrunning_ = false;

	// 等待线程池里所有线程返回，线程池里线程有两种状态：
	// 没有任务阻塞中、正在执行任务中
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; });
}

void ThreadPool::start(int initThreadSize)
{
	ispoolrunning_ = true;
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	for (int i = 0; i < initThreadSize_; i++) {

		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadID = ptr->getID();
		threads_.emplace(threadID, std::move(ptr));
	}

	for (int i = 0; i < initThreadSize_; i++)
	{
		threads_[i]->start();
		idleThreadSize_++;
	}

}

void ThreadPool::setMode(PoolMode mode)
{
	if (checkRunningState())
		return;
	poolMode_ = mode;
}
void ThreadPool::setTaskQueMaxThreshHold(int threshhold)
{
	taskQueMaxThreshHold_ = threshhold;
}
bool ThreadPool::checkRunningState() const
{
	return ispoolrunning_;
}

Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool {return taskQue_.size() < taskQueMaxThreshHold_; }))
	{
		std::cerr << "超时了";
		return Result(sp, false);
	}
	taskQue_.emplace(sp);
	taskSize_++;

	notEmpty_.notify_all();

	//cached模式 任务处理比较紧急 场景：小而快的任务 需要根据任务数量和空闲线程的数量，判断是否需要创建
	if (poolMode_ == PoolMode::MODE_CACHED
		&& taskSize_ > idleThreadSize_
		&& curThreadSize_ < threadSizeThreshHold_)
	{
		std::cout << "<<<creat new thread" << std::endl;
		//创建新线程
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadID = ptr->getID();
		threads_.emplace(threadID, std::move(ptr));

		//启动
		threads_[threadID]->start();
		idleThreadSize_++;
		curThreadSize_++;
	}

	return Result(sp);

}

//定义线程函数
void ThreadPool::threadFunc(int threadid)
{
	auto lastTime = std::chrono::high_resolution_clock().now();
	while (ispoolrunning_)
	{
		std::shared_ptr<Task> task;
		{
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid:" << std::this_thread::get_id() << "尝试获取任务" << std::endl;

			//cached模式下，有可能已经创建了很多的线程，但是空闲时间超过60s
			//应该把多余的线程结束回收掉(超过initThreadSize_的线程要进行回收)
			//当前时间 — 减上一次线程执行完毕时间 > 60s
			//锁+双重判断
			while (ispoolrunning_ && taskQue_.size() == 0)
			{
				if (poolMode_ == PoolMode::MODE_CACHED)
				{
					if (std::cv_status::timeout ==
						notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME
							&& curThreadSize_ > initThreadSize_)
						{
							//回收线程
							threads_.erase(threadid);
							curThreadSize_--;
							idleThreadSize_--;
							std::cout << "thread id:" << std::this_thread::get_id() << "exit" << std::endl;
							return;//线程结束

						}
					}
				}
				else
				{
					//等待notEmpty条件
					notEmpty_.wait(lock);
				}
				////①线程池要结束，回收线程资源，第一种例子：线程在阻塞过程中
				////每处notEmpty被唤醒判断是不是被线程池析构函数唤醒
				//if (!ispoolrunning_)
				//{
				//	threads_.erase(threadid);
				//	std::cout << "thread id:" << std::this_thread::get_id() << "exit" << std::endl;
				//	exitCond_.notify_all();
				//	return;//线程结束
				//}

			}
			if (!ispoolrunning_)
			{
				break;//出循环直接回收线程资源
			}

			idleThreadSize_--;

			std::cout << "tid:" << std::this_thread::get_id() << "获取任务成功..." << std::endl;

			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			if (taskSize_ > 0)
			{
				notEmpty_.notify_all();
			}
		}//释放锁,不要等待任务执行完成

		if (task != nullptr)
		{
			task->exec();
		}
		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now();
	}

	//②线程池析构时候，线程资源在正在执行情况下的退出，等待任务执行完之后跳出循环，进行回收。
	threads_.erase(threadid);
	std::cout << "thread id:" << std::this_thread::get_id() << "exit" << std::endl;
	exitCond_.notify_all();


	/*std::cout << "begin threadFunc id :" << std::this_thread::get_id() << std::endl;
	std::cout << "end threadFunc id:" << std::this_thread::get_id ()<< std::endl;*/

}