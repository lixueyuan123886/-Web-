#include <sys/socket.h>
#include <errno.h>
#include <netinet/in.h>
#include <vector>

#include <iostream>
#include <string.h>
#include <unistd.h>

#include <pthread.h>
#include <sys/poll.h>
#include <sys/epoll.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "log/log.h"

#define BUFFER_LENGTH		1024

char FILEPATH[BUFFER_LENGTH] =	"index.html";  //若放在同一文件夹下则此处无需修改

typedef int (*RCALLBACK)(int fd);

struct conn_item {
	int fd;
	
	//char rbuffer[BUFFER_LENGTH];
	vector<char> rbuffer; // 动态数组
	int rlen;
	//char wbuffer[BUFFER_LENGTH];
	vector<char> wbuffer;
	int wlen;

	char resource[BUFFER_LENGTH]; 

	union {
		RCALLBACK accept_callback;
		RCALLBACK recv_callback;
	} recv_t;
	RCALLBACK send_callback;
};
typedef struct conn_item connection_t;

struct conn_item connlist[1024] = {0};

int epfd = 0;


int http_request(connection_t *conn);
int http_response(connection_t *conn);
int send_cb(int fd);
void set_event(int fd, int event, int flag);
int accept_cb(int fd);
int recv_cb(int fd);

int main(int argc, char *argv[])
{
	Log::getinstance()->init("log.txt",8192, 1000, 0);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    
    struct sockaddr_in serveraddr;
    memset(&serveraddr, 0, sizeof(struct sockaddr_in));
    serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(8080);

    if (-1 == bind(sockfd, (struct sockaddr*)&serveraddr, sizeof(struct sockaddr))) {
		perror("bind");
		return -1;
	}
	
    if(-1 == listen(sockfd, 10)){
        perror("listen");
        return -1;
    }

    connlist[sockfd].fd = sockfd;
	connlist[sockfd].recv_t.accept_callback = accept_cb;

    epfd = epoll_create(1);

    set_event(sockfd, EPOLLIN, 1);

    struct epoll_event events[1024] = {0};

    while (1) { 

    int nready = epoll_wait(epfd, events, 1024, -1); 

    for (int i = 0; i < nready; i++) {

        int connfd = events[i].data.fd;

        if (events[i].events & EPOLLIN) { // 判定可读事件
            int count;
            if (connfd == sockfd) {
                // 如果是监听套接字，则调用 accept_callback 处理新的连接请求
                count = connlist[connfd].recv_t.accept_callback(connfd);
            } 
            else {
                // 否则调用 recv_callback 处理数据接收
                count = connlist[connfd].recv_t.recv_callback(connfd);
            }
        } 
        else if (events[i].events & EPOLLOUT) { // 判定可写事件
            int count = connlist[connfd].send_callback(connfd);
        }
    }
}
    getchar();
    return 0;
}

int send_cb(int fd) {

	vector<char>& buffer = connlist[fd].wbuffer;
	int idx = connlist[fd].wlen;

	int count = send(fd, buffer.data(), idx, 0);

	set_event(fd, EPOLLIN, 0);

	return count;
}


void set_event(int fd, int event, int flag) {

	if (flag) {       // 1 add, 0 mod
		struct epoll_event ev;
		ev.events = event ;
		ev.data.fd = fd;
		epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
	} else {
	
		struct epoll_event ev;
		ev.events = event;
		ev.data.fd = fd;
		epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
	}

}

int accept_cb(int fd) {

	struct sockaddr_in clientaddr;
	socklen_t len = sizeof(clientaddr);
	
	int clientfd = accept(fd, (struct sockaddr*)&clientaddr, &len);
	if (clientfd < 0) {
		return -1;
	}

	set_event(clientfd, EPOLLIN, 1);

	connlist[clientfd].fd = clientfd;

	connlist[clientfd].rbuffer.resize(BUFFER_LENGTH, 0); // 初始化为0
	connlist[clientfd].wbuffer.resize(BUFFER_LENGTH, 0); // 初始化为0
	connlist[clientfd].rlen = 0;
	connlist[clientfd].wlen = 0;

	connlist[clientfd].recv_t.recv_callback = recv_cb;
	connlist[clientfd].send_callback = send_cb;

	return clientfd;
}

int recv_cb(int fd) { 

	vector<char>& buffer = connlist[fd].rbuffer;
	int idx = connlist[fd].rlen;
	
    // 从套接字 fd 中接收数据，并将其存储在 connlist[fd].rbuffer 缓冲区中
    // buffer 是指向接收缓冲区的指针，idx 是当前缓冲区中的数据长度
    // BUFFER_LENGTH 是缓冲区的总长度，BUFFER_LENGTH-idx 是缓冲区剩余的可用空间
	buffer.resize(BUFFER_LENGTH);
	int count = recv(fd, buffer.data() + idx, BUFFER_LENGTH - idx, 0);

	if (count == 0) {
		epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);		
		close(fd);
		return -1;
	}
	
	connlist[fd].rlen += count;
	//printf("Received data: %.*s\n", count, buffer + idx);
	
	http_request(&connlist[fd]);
	http_response(&connlist[fd]);
	set_event(fd, EPOLLOUT, 0);

	return count;
}

const char* get_mime_type(const char *filename) {
    // 获取文件名的扩展名
    const char *ext = strrchr(filename, '.'); // 查找最后一个 '.' 的位置

    if (!ext || ext == filename) {
        return "application/octet-stream"; // 无扩展名，默认二进制流
    }

    // 根据扩展名返回对应的 MIME 类型
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) {
        return "text/html";
    } else if (strcmp(ext, ".css") == 0) {
        return "text/css";
    } else if (strcmp(ext, ".js") == 0) {
        return "application/javascript";
    } else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) {
        return "image/jpeg";
    } else if (strcmp(ext, ".png") == 0) {
        return "image/png";
    } else if (strcmp(ext, ".gif") == 0) {
        return "image/gif";
    } else if (strcmp(ext, ".svg") == 0) {
        return "image/svg+xml";
    } else if (strcmp(ext, ".json") == 0) {
        return "application/json";
    } else if (strcmp(ext, ".txt") == 0) {
        return "text/plain";
    } else if (strcmp(ext, ".xml") == 0) {
        return "application/xml";
    } else if (strcmp(ext, ".pdf") == 0) {
        return "application/pdf";
    } else if (strcmp(ext, ".mp4") == 0) {
        return "video/mp4";
    } else if (strcmp(ext, ".mp3") == 0) {
        return "audio/mpeg";
    } else if (strcmp(ext, ".ico") == 0) {
        return "image/vnd.microsoft.icon";
    }

    // 如果扩展名不在已知列表中，返回默认类型
    return "application/octet-stream";
}

int http_response(connection_t *conn) {

    int filefd = open(FILEPATH, O_RDONLY);
	time_t now = time(0); // 获取当前时间
	char date_buffer[100];
	strftime(date_buffer, sizeof(date_buffer), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&now)); // 转为GMT时间格式

	if(filefd < 0) {
		conn->wbuffer.resize(BUFFER_LENGTH);
		conn->wlen = sprintf(conn->wbuffer.data(), 
            	"HTTP/1.1 404 Not Found\r\n"
                "Content-Length: %ld\r\n"
                "Content-Type: text/html\r\n"
                "Date: %s\r\n\r\n"
                "<html><head><title>404 Not Found</title></head><body><h1>404 Not Found</h1><p>The requested resource was not found on this server.</p></body></html>",

                strlen("<html><head><title>404 Not Found</title></head><body><h1>404 Not Found</h1><p>The requested resource was not found on this server.</p></body></html>"),
				date_buffer
                );

		return conn->wlen;	
	}
	struct stat stat_buf;
	fstat(filefd, &stat_buf);

	const char *requested_file = FILEPATH; // 示例文件名
	const char *mime_type = get_mime_type(requested_file); // 获取 MIME 类型
	conn->wbuffer.resize(stat_buf.st_size + BUFFER_LENGTH);
	conn->wlen = sprintf(conn->wbuffer.data(), 

                "HTTP/1.1 200 OK\r\n"
                "Accept-Ranges: bytes\r\n"
                "Content-Length: %ld\r\n"
				"Content-Type: %s\r\n"
                "Date: %s\r\n\r\n",
		
                stat_buf.st_size,
				mime_type,
				date_buffer
				);

	conn->wbuffer.resize(conn->wlen + conn->wbuffer.size());
	int count = read(filefd, conn->wbuffer.data() + conn->wlen, conn->wbuffer.size() - conn->wlen);
	
    if (count < 0) { 
        perror("read"); 
        close(filefd); 
        return -1; 
    }
    conn->wlen += count;
    close(filefd);
	return conn->wlen;
}

// GET / HTTP/1.1

int http_request(connection_t *conn) { 

	char mothed[16] = {0};
	char resource[256] = {0};
	char protocol[16] = {0};
	int index = 0;
	char *p = conn->rbuffer.data();
	
	for(p = conn->rbuffer.data(); *p != ' '; p++) {
		mothed[index++] = *p;   
	}
	mothed[index++] = '\0'; // 添加字符串结束符
	p++;

	for(index = 0; *p != ' '; p++) {
    resource[index++] = *p;
    }
	resource[index++] = '\0'; // 添加字符串结束符
	p++;

	for(index = 0; *p != '\r' && *p != '\0'; p++) {
		protocol[index++] = *p;
	}
	protocol[index++] = '\0'; // 添加字符串结束符

	if(strcmp(mothed, "GET") == 0) {
		if(strcmp(resource, "/") == 0) {
			char str[20] = "index.html";
			strcpy(FILEPATH,str);
		}
		else { 
			strcpy(FILEPATH,resource);
			memmove(FILEPATH, FILEPATH + 1, strlen(FILEPATH));
		}
	}
	Log::getinstance()->write_log(1,FILEPATH);
	// 打印 FILEPATH 的内容
	printf("FILEPATH: %s\n", FILEPATH);
	return 0;
}
