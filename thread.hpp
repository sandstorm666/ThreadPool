#pragma once
#include <vector>
#include <queue>
#include <functional>
#include <future>
#include <assert.h>
// 模板的定义和声明必须放在同一个文件中
class ThreadPool
{
public:
	using Task = std::function<void()>;// 定义Task为可调用对象类型
	explicit ThreadPool():running_(false)// 禁止隐式转换
	{}
	~ThreadPool()
	{
		if (running_)//如果创建了线程池，才需要释放
			stop();
	}
	// 禁用拷贝和赋值函数
	ThreadPool(const ThreadPool& t) = delete;
	ThreadPool& operator=(const ThreadPool& t) = delete;
	// 定义可变参类型时...放在变量前面，使用时放在变量后面
	// 提交任务
	template <typename Fn,typename... Args>
	auto commit(Fn&& fn, Args&&... args)->std::future<decltype(fn(args...))>;
	// 开启和关闭线程池
	void start(int numThreads);
	void stop();
	int numThreads() const { return threads_.size(); }
private:
	std::mutex mutex_;
	std::condition_variable cvTask_;
	std::vector<std::thread> threads_;
	std::queue<Task> tasks_;// 任务队列
	bool running_;
};
template <typename Fn, typename... Args>
auto ThreadPool::commit(Fn&& fn, Args&&... args)->std::future<decltype(fn(args...))>
{
	//定义返回值类型
	using RetType = decltype<fn(args...)>;
	// packaged_task也是封装一个可调用对象，类似functional，只不过pkg可以获得异步返回的结果
	auto task = std::make_shared<std::packaged_task<RetType()>>(
		std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...));
	auto future = task->get_future();// 获取共享状态的std::future对象
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!running_)
			throw std::runtime_error("ThreadPool is stopped");
		tasks_.emplace([task] {(*task)(); });// 插入一个lambda可调用对象，该对象可以直接运行
		// 为什么不插入function封装的可调用对象而是要用lambda封装的？
		// 因为function封装的话，容器中的类型值不好定义。
		// lambda相当于二次封装，在另一个函数中，直接调用可调用对象，还不需要传参,妙啊！
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
		// lambda表达式会隐式构造thread对象吗？
		threads_.emplace_back([this] {// 捕获当前对象（线程池）的this指针，让lambda表达式拥有和类成员同样的访问权限
			while (true)
			{
				Task task;// 创建一个空的可调用对象
				{
					std::unique_lock<std::mutex> lock(mutex_);
					// wait第一次调用时，会先解锁，再堵塞。后面调用会判断条件是否为true，为true就继续，否则阻塞。
					cvTask_.wait(lock, [this]
					{
						return !tasks_.empty() || !running_;
					});// 线程池非空或线程池处于关闭状态，继续走下去
					if (tasks_.empty() && !running_) return;// 线程池空的，或者线程池处于关闭直接返回
					task = std::move(tasks_.front());
					tasks_.pop();
				}
				if (task) task();//执行任务
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