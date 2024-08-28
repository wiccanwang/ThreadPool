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

//�û�����->��ȡ����ֵ
Any Result::get()
{
	if (!isValid_) return "";
	sem_.wait();  // �ȴ���Դ
	return std::move(any_);
}
//����ִ������ã���ȡ����ִ����ķ���ֵ��Ϊ��Ա����any_��ֵ
void Result::setVal(Any any)
{
	this->any_ = std::move(any);
	sem_.post();  // �����ź�����Դ
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
	result_ = res; //Task��Result�໥��
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
	std::thread t(func_, threadId_);//func_��ĳ�Ա����
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

	// �ȴ��̳߳��������̷߳��أ��̳߳����߳�������״̬��
	// û�����������С�����ִ��������
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
		std::cerr << "��ʱ��";
		return Result(sp, false);
	}
	taskQue_.emplace(sp);
	taskSize_++;

	notEmpty_.notify_all();

	//cachedģʽ ������ȽϽ��� ������С��������� ��Ҫ�������������Ϳ����̵߳��������ж��Ƿ���Ҫ����
	if (poolMode_ == PoolMode::MODE_CACHED
		&& taskSize_ > idleThreadSize_
		&& curThreadSize_ < threadSizeThreshHold_)
	{
		std::cout << "<<<creat new thread" << std::endl;
		//�������߳�
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadID = ptr->getID();
		threads_.emplace(threadID, std::move(ptr));

		//����
		threads_[threadID]->start();
		idleThreadSize_++;
		curThreadSize_++;
	}

	return Result(sp);

}

//�����̺߳���
void ThreadPool::threadFunc(int threadid)
{
	auto lastTime = std::chrono::high_resolution_clock().now();
	while (ispoolrunning_)
	{
		std::shared_ptr<Task> task;
		{
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid:" << std::this_thread::get_id() << "���Ի�ȡ����" << std::endl;

			//cachedģʽ�£��п����Ѿ������˺ܶ���̣߳����ǿ���ʱ�䳬��60s
			//Ӧ�ðѶ�����߳̽������յ�(����initThreadSize_���߳�Ҫ���л���)
			//��ǰʱ�� �� ����һ���߳�ִ�����ʱ�� > 60s
			//��+˫���ж�
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
							//�����߳�
							threads_.erase(threadid);
							curThreadSize_--;
							idleThreadSize_--;
							std::cout << "thread id:" << std::this_thread::get_id() << "exit" << std::endl;
							return;//�߳̽���

						}
					}
				}
				else
				{
					//�ȴ�notEmpty����
					notEmpty_.wait(lock);
				}
				////���̳߳�Ҫ�����������߳���Դ����һ�����ӣ��߳�������������
				////ÿ��notEmpty�������ж��ǲ��Ǳ��̳߳�������������
				//if (!ispoolrunning_)
				//{
				//	threads_.erase(threadid);
				//	std::cout << "thread id:" << std::this_thread::get_id() << "exit" << std::endl;
				//	exitCond_.notify_all();
				//	return;//�߳̽���
				//}

			}
			if (!ispoolrunning_)
			{
				break;//��ѭ��ֱ�ӻ����߳���Դ
			}

			idleThreadSize_--;

			std::cout << "tid:" << std::this_thread::get_id() << "��ȡ����ɹ�..." << std::endl;

			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			if (taskSize_ > 0)
			{
				notEmpty_.notify_all();
			}
		}//�ͷ���,��Ҫ�ȴ�����ִ�����

		if (task != nullptr)
		{
			task->exec();
		}
		idleThreadSize_++;
		lastTime = std::chrono::high_resolution_clock().now();
	}

	//���̳߳�����ʱ���߳���Դ������ִ������µ��˳����ȴ�����ִ����֮������ѭ�������л��ա�
	threads_.erase(threadid);
	std::cout << "thread id:" << std::this_thread::get_id() << "exit" << std::endl;
	exitCond_.notify_all();


	/*std::cout << "begin threadFunc id :" << std::this_thread::get_id() << std::endl;
	std::cout << "end threadFunc id:" << std::this_thread::get_id ()<< std::endl;*/

}