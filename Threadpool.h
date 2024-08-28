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

	//�û�����->��ȡ����ֵ
	Any get();

	//����ִ������ã���ȡ����ִ����ķ���ֵ��Ϊ��Ա����any_��ֵ
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
	Result* result_;  //��ֹǿ����ָ�뽻������
};



//�߳�����
class Thread
{
public:

	using ThreadFunc = std::function<void(int)>;
	Thread(ThreadFunc func);
	~Thread();

	//�����߳�
	void start();
	int getID() const;

private:
	ThreadFunc func_;
	static int generateId_;
	int threadId_;

};

//�̳߳�����
class ThreadPool
{
public:
	ThreadPool();
	~ThreadPool();
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const Thread&) = delete;

	void setMode(PoolMode mode);
	void setTaskQueMaxThreshHold(int threshhold);

	Result submitTask(std::shared_ptr<Task> sp);//�ύ����
	void start(int initThreadSize = 4);       //�̳߳�����

private:
	void threadFunc(int threadid);

	bool checkRunningState() const;

	//std::vector<std::unique_ptr<Thread>> threads_;   //�߳��б�
	std::unordered_map<int, std::unique_ptr<Thread>> threads_;

	int initThreadSize_;                       //��ʼ���߳�����
	std::atomic_int idleThreadSize_;           //�����߳�����
	std::atomic_int curThreadSize_;            //��ǰ�߳�����
	int threadSizeThreshHold_;                 //�߳�����������ֵ

	std::queue<std::shared_ptr<Task>> taskQue_;//�������
	std::atomic_int taskSize_;                 //ԭ������
	int taskQueMaxThreshHold_;                 //����������ֵ

	std::mutex taskQueMtx_;                    //����������еİ�ȫ
	std::condition_variable notFull_;
	std::condition_variable notEmpty_;

	std::condition_variable exitCond_;         //��Դ���տ���
	PoolMode poolMode_;

	std::atomic_bool ispoolrunning_;

};

