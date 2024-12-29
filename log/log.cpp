#include "log.h"

Log::Log()
{
	m_count = 0;
	m_is_async = false;
}

Log::~Log()
{
	if (file != NULL)
	{
		fflush(file);
		fclose(file);
	}

	
	if (m_buf != NULL)
	{
		delete[] m_buf;
		m_buf = nullptr;
	}
}

Log* Log::getinstance()
{
	static Log instance;
	return &instance;
}

bool Log::init(const char* file_name, int log_buf_size, int split_lines,int max_queue_size)
{   
	if (max_queue_size >= 1)
	{
		//设置写入方式flag
		m_is_async = true;  //设置为异步写入方式
		
		//创建并设置阻塞队列长度
		m_log_queue = new block_queue<string>(max_queue_size);
		pthread_t tid;
		
		//flush_log_thread为回调函数,这里表示创建线程异步写日志
		pthread_create(&tid, NULL, flush_log_thread, NULL);
	}

	m_log_buf_size = log_buf_size;
	m_buf = new char[m_log_buf_size];
	memset(m_buf,'\0', m_log_buf_size);// 开辟缓冲区，准备存放格式化的日志字符串
	
	m_split_lines = split_lines;       //设置最大行数

	time_t t = time(NULL);
	struct tm* sys_tm = localtime(&t);
	struct tm  my_tm  = *sys_tm;       //获取当前的时间

	const char* p = strrchr(file_name, '\\');
	char log_full_name[512] = { 0 };

	if (p == NULL)
	{
		snprintf(log_full_name, sizeof(log_full_name), "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
		strcpy(log_name, file_name);
	}
	else
	{
		strcpy(log_name, p + 1);
		strncpy(dir_name, file_name, p - file_name + 1);
		                              //规范化命名
		snprintf(log_full_name,sizeof(log_full_name), "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
	}

	m_today = my_tm.tm_mday;          //更新日志的创建时间

	file = fopen(log_full_name,"a");  //打开文件，打开方式：追加
	if (file == NULL)
	{
		return false;
	}
	return true;
}

void Log::write_log(int level,const char* str)
{
	if (file == NULL)
		return;
	

	time_t t = time(NULL); 
	struct tm* sys_tm = localtime(&t); 
	struct tm  my_tm = *sys_tm;       //获取当前的时间，用来后续跟日志的创建时间作对比

	char level_s[16] = { 0 };         //日志分级
	switch (level)
	{
	case 0:
		strcpy(level_s, "[debug]:");
		break;
	case 1:
		strcpy(level_s, "[info]:"); 
		break;
	case 2:
		strcpy(level_s, "[warn]:"); 
		break;
	case 3:
		strcpy(level_s, "[erro]:"); 
		break;
	default:
		strcpy(level_s, "[info]:"); 
		break;
	}

	m_mutex.lock();                   //互斥锁上锁
	m_count++;                        //日志行数+1

	if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) //如果创建时间！=当前时间或者行数达到上限
	{
		char new_log[512] = { 0 };    //新日志的文件名
		fflush(file);
		fclose(file);

		char time_now[16] = { 0 };    //格式化当前的时间
		snprintf(time_now, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

		if (m_today != my_tm.tm_mday) //如果是创建时间！=今天
		{
			snprintf(new_log, sizeof(new_log), "%s%s%s", dir_name, time_now, log_name);
			m_today = my_tm.tm_mday;  //更新创建时间
			m_count = 0;              //更新日志的行数
		}
		else                          //如果是行数达到文件上限
		{
			snprintf(new_log,sizeof(new_log),"%s%s%lld_%s", dir_name, time_now, m_count / m_split_lines,log_name);//加上版本后缀
		}

		file = fopen(new_log, "a");
	}

	m_mutex.unlock();                 //互斥锁解锁

	string log_str; 
	m_mutex.lock();


	int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d %s",
		my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
		my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec,level_s); 

	int m = snprintf(m_buf + n, m_log_buf_size-n-1,"%s",str);

	m_buf[n+m] = '\n';
	m_buf[n+m+1] = '\0';

	log_str = m_buf;
	m_mutex.unlock();
	if (m_is_async && !m_log_queue->full()) 
	 {
		m_log_queue->push(log_str); 
	}
	else//如果是同步的写入方式
	{
	    m_mutex.lock(); 
		fputs(log_str.c_str(), file);
		fflush(file);
		m_mutex.unlock(); 
	}
	//fputs(m_buf, file);

}
