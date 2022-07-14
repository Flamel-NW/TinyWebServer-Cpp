#ifndef PCH_H
#define PCH_H

#include <pthread.h>
#include <semaphore.h>
#include <list>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <unordered_map>
#include <fstream>
#include <mysql/mysql.h>
#include <sys/time.h>
#include <iostream>
#include <string>

#define STDERR_FUNC_LINE() fprintf(stderr, "func: %s, line: %d\n", __func__, __LINE__);
#define DEBUG_FUNC_LINE() fprintf(stderr, "func: %s, line: %d\n", __func__, __LINE__);
#define DEBUG(format, ...) fprintf(stderr, format, ##__VA_ARGS__); putchar('\n');

#endif
