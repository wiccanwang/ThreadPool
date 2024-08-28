#pragma once

#include <vector>
#include<memory>
#include<queue>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include<functional>
#include<unordered_map>

class Any
{
public:
	template<typename T>
	Any(T data) :base_(std::make_unique<Derive<T>>(data)) {}

	template<typename T>
	T cast_()
	{
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
		return pd->data_;
	}

	Any() = default;
	~Any() = default;
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;
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
		Derive(T data) :data_(data) {}
		T data_;
	};

	std::unique_ptr<Base> base_;
};

class Semaphore
{
public:
	Semaphore(int limit = 0)
		:resLimit(limit)
		,isExit_(false)
	{}
	~Semaphore()
	{
		isExit_ = true;
	}
	void wait()
	{
		if (isExit_) return;
		std::unique_lock<std::mutex> lock(mtx_);
		cond_.wait(lock, [&]()->bool {return resLimit > 0; });
		resLimit--;
	}
	void post()
	{
		if (isExit_) return;
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit++;
		cond_.notify_all();
	}

private:
	int resLimit;
	std::mutex mtx_;
	std::condition_variable cond_;
	std::atomic_bool isExit_;
};

class Task;

class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;

	//用户调用->获取返回值
	Any get();

	//任务执行完调用，获取任务执行完的返回值，为成员变量any_赋值
	void setVal(Any any);

private:
	Any any_;
	std::shared_ptr<Task> task_;
	Semaphore sem_;
	std::atomic_bool isValid_;
};

enum class PoolMode {
	MODE_FIXED,
	MODE_CACHED
};

class Task
{
public:
	Task();
	~Task() = default;

	virtual Any run() = 0;
	void exec();
	void setResult(Result* res);
private:
	Result* result_;  //防止强智能指针交叉引用
};



//线程类型
class Thread
{
public:

	using ThreadFunc = std::function<void(int)>;
	Thread(ThreadFunc func);
	~Thread();

	//启动线程
	void start();
	int getID() const;

private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;

};

//线程池类型
class ThreadPool
{
public:
	ThreadPool();
	~ThreadPool();
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const Thread&) = delete;

	void setMode(PoolMode mode);
	void setTaskQueMaxThreshHold(int threshhold);

	Result submitTask(std::shared_ptr<Task> sp);//提交任务
	void start(int initThreadSize = 4);       //线程池启动

private:
	void threadFunc(int threadid);

	bool checkRunningState() const;

	//std::vector<std::unique_ptr<Thread>> threads_;   //线程列表
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;

	int initThreadSize_;                       //初始的线程数量
	std::atomic_int idleThreadSize_;           //空闲线程数量
	std::atomic_int curThreadSize_;            //当前线程数量
	int threadSizeThreshHold_;                 //线程数量上限阈值

	std::queue<std::shared_ptr<Task>> taskQue_;//任务队列
	std::atomic_int taskSize_;                 //原子类型
	int taskQueMaxThreshHold_;                 //任务上限阈值

	std::mutex taskQueMtx_;                    //负责任务队列的安全
	std::condition_variable notFull_;
	std::condition_variable notEmpty_;

	std::condition_variable exitCond_;         //资源回收控制
	PoolMode poolMode_;

	std::atomic_bool ispoolrunning_;

};

