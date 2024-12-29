#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include "block_queue.h"

using namespace std;

class Log{
private:
	Log();
	~Log();
	void* async_write_log()
	{
		string single_log;//要写入的日志
		while (m_log_queue->pop(single_log))
        {
            m_mutex.lock();//互斥锁上锁
            fputs(single_log.c_str(), file);
			fflush(file);
            m_mutex.unlock();//互斥锁解锁
        }
		return nullptr; // 返回一个空指针
	}

public:
	bool init(const char* file_name, int log_buf_size = 8192, int split_lines= 5000000,int max_queue_size=0);

	void write_log(int level,const char* str);

	static void* flush_log_thread(void *args)
	{
		Log::getinstance()->async_write_log();
		return nullptr; // 返回一个空指针
	}

	static Log* getinstance();

private:
	FILE* file;
	char dir_name[128];//路径名
	char log_name[128];//日志名

	int m_split_lines; //日志文件最大行数
	long long m_count; //日志当前的行数
	int m_today;       //日志创建的日期，记录是那一天

	int m_log_buf_size; //日志缓冲区的大小，用来存放日志信息字符串
	char* m_buf;        //日志信息字符串；因为后续要把时间和日志分级也加进来，所以开一个新的char *

	block_queue<string>* m_log_queue; //阻塞队列，封装生产者消费者模型
	bool  m_is_async;                 //异步标记，如果为true,表示使用异步写入方式，否则是同步写入方式
	locker m_mutex;                   //互斥锁类，内部封装了互斥锁，用来解决多线程竞争资源问题

};
#endif


