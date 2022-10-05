#pragma once
#include <vector>
#include <queue>
#include <functional>
#include <future>
#include <assert.h>
// ģ��Ķ���������������ͬһ���ļ���
class ThreadPool
{
public:
	using Task = std::function<void()>;// ����TaskΪ�ɵ��ö�������
	explicit ThreadPool():running_(false)// ��ֹ��ʽת��
	{}
	~ThreadPool()
	{
		if (running_)//����������̳߳أ�����Ҫ�ͷ�
			stop();
	}
	// ���ÿ����͸�ֵ����
	ThreadPool(const ThreadPool& t) = delete;
	ThreadPool& operator=(const ThreadPool& t) = delete;
	// ����ɱ������ʱ...���ڱ���ǰ�棬ʹ��ʱ���ڱ�������
	// �ύ����
	template <typename Fn,typename... Args>
	auto commit(Fn&& fn, Args&&... args)->std::future<decltype(fn(args...))>;
	// �����͹ر��̳߳�
	void start(int numThreads);
	void stop();
	int numThreads() const { return threads_.size(); }
private:
	std::mutex mutex_;
	std::condition_variable cvTask_;
	std::vector<std::thread> threads_;
	std::queue<Task> tasks_;// �������
	bool running_;
};
template <typename Fn, typename... Args>
auto ThreadPool::commit(Fn&& fn, Args&&... args)->std::future<decltype(fn(args...))>
{
	//���巵��ֵ����
	using RetType = decltype<fn(args...)>;
	// packaged_taskҲ�Ƿ�װһ���ɵ��ö�������functional��ֻ����pkg���Ի���첽���صĽ��
	auto task = std::make_shared<std::packaged_task<RetType()>>(
		std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...));
	auto future = task->get_future();// ��ȡ����״̬��std::future����
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!running_)
			throw std::runtime_error("ThreadPool is stopped");
		tasks_.emplace([task] {(*task)(); });// ����һ��lambda�ɵ��ö��󣬸ö������ֱ������
		// Ϊʲô������function��װ�Ŀɵ��ö������Ҫ��lambda��װ�ģ�
		// ��Ϊfunction��װ�Ļ��������е�����ֵ���ö��塣
		// lambda�൱�ڶ��η�װ������һ�������У�ֱ�ӵ��ÿɵ��ö��󣬻�����Ҫ����,���
	}
	cvTask_.notify_one();
	return future;
}
inline void ThreadPool::start(int numThreads)
{
	assert(threads_.empty());
	running_ = true;
	threads_.reserve(numThreads);
	for (int i = 0; i < numThreads; ++i)
	{
		// lambda���ʽ����ʽ����thread������
		threads_.emplace_back([this] {// ����ǰ�����̳߳أ���thisָ�룬��lambda���ʽӵ�к����Աͬ���ķ���Ȩ��
			while (true)
			{
				Task task;// ����һ���յĿɵ��ö���
				{
					std::unique_lock<std::mutex> lock(mutex_);
					// wait��һ�ε���ʱ�����Ƚ������ٶ�����������û��ж������Ƿ�Ϊtrue��Ϊtrue�ͼ���������������
					cvTask_.wait(lock, [this]
					{
						return !tasks_.empty() || !running_;
					});// �̳߳طǿջ��̳߳ش��ڹر�״̬����������ȥ
					if (tasks_.empty() && !running_) return;// �̳߳ؿյģ������̳߳ش��ڹر�ֱ�ӷ���
					task = std::move(tasks_.front());
					tasks_.pop();
				}
				if (task) task();//ִ������
			}});
	}
}
inline void ThreadPool::stop()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		running_ = false;
		cvTask_.notify_all();
	}
	for (auto& thread : threads_)
	{
		if (thread.joinable())
			thread.join();
	}
}