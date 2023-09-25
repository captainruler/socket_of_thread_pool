#include "thread_socket.h"

int main()
{

    // 创建监听套接字
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1)
    {
        perror("socket");
        return -1;
    }
    // 绑定本地地址的ip
    struct sockaddr_in saddr; // 先使用socket_in这个结构体，便于赋值，后面再通过强制转化，转换为socketaddr
    saddr.sin_family = AF_INET;

    // 端口
    saddr.sin_port = htons(9999);
    // ip地址
    saddr.sin_addr.s_addr = INADDR_ANY; // INADDR_ANY为零，使得其会自动读取本地网卡
    int ret = bind(fd, (struct sockaddr *)&saddr, sizeof(saddr));
    if (ret == -1)
    {
        perror("bind");
        return -1;
    }
    // 设置监听
    ret = listen(fd, 128);
    if (ret == -1)
    {
        perror("listen");
        return -1;
    }
    int max = sizeof(info) / sizeof(info[0]);
    for (int i = 0; i < max; i++)
    {
        bzero(&info[i], sizeof(info[i]));
        info[i].fd = -1;
    }

    printf("等待客户端连接中.......\n");

    // 阻塞并等待客户端
    int addrlen = sizeof(struct sockaddr_in);
    ThreadPool *pool = threadPoolCreate(3, 6, 100);
    while (1)
    {
        struct sockinfo *pinfo;
        for (int i = 0; i < max; i++)
        {
            if (info[i].fd == -1)
            {
                pinfo = &info[i];
                break;
            }
        }

        int cfd = accept(fd, (struct sockaddr *)&pinfo->addr, &addrlen);
        pinfo->fd = cfd;
        if (cfd == -1)
        {
            perror("accept");
            break;
        }
        threadPoolAdd(pool, working, pinfo);
    }
    close(fd);
    threadPoolDestroy(pool);
    return 0;
}

void working(void *arg)
{
    struct sockinfo *pinfo = (struct sockinfo *)arg;

    // 如果链接成功，则打印客户端的IP和端口信息
    char ip[32];
    printf("客户端的IP: %s, 端口： %d\n",
           inet_ntop(AF_INET, &pinfo->addr.sin_addr.s_addr, ip, sizeof(ip)),
           ntohs(pinfo->addr.sin_port));
    // 通信
    while (1)
    {
        // 接收数据
        char buff[1024];
        char arr;
        int len = recv(pinfo->fd, buff, sizeof(buff), 0);
        if (len > 0)
        {
            arr = buff[21];
            // 打印接收到的数据，然后将接收到的数据发送回去
            printf("client say:%s\n", buff);
            if (arr == '8')
            {
                send(pinfo->fd, "You are right!", len, 0);
            }
            send(pinfo->fd, buff, len, 0);
        }
        else if (len == 0)
        {
            printf("客户端断开连接.......\n");
            break;
        }
        else
        {
            perror("recv");
            break;
        }
    }
    // 关闭文件

    pinfo->fd = -1;
}

// 线程池函数

// 创建线程池并初始化，有着三个参数，最大，最小，和线程队列大小
ThreadPool *threadPoolCreate(int min, int max, int queueSize)
{
    ThreadPool *pool = (ThreadPool *)malloc(sizeof(ThreadPool));
    do
    {
        if (pool == NULL)
        {
            printf("malloc threadpool fail...\n");
            break;
        }

        pool->threadIDs = (pthread_t *)malloc(sizeof(pthread_t) * max);
        if (pool->threadIDs == NULL)
        {
            printf("malloc threadIDs fail...\n");
            break;
        }
        memset(pool->threadIDs, 0, sizeof(pthread_t) * max);
        pool->minNum = min;
        pool->maxNum = max;
        pool->busyNum = 0;
        pool->liveNum = min; // 和最小个数相等
        pool->exitNum = 0;
        // 初始化互斥锁和条件变量
        if (pthread_mutex_init(&pool->mutexPool, NULL) != 0 ||
            pthread_mutex_init(&pool->mutexBusy, NULL) != 0 ||
            pthread_cond_init(&pool->notEmpty, NULL) != 0 ||
            pthread_cond_init(&pool->notFull, NULL) != 0)
        {
            printf("mutex or condition init fail...\n");
            break;
        }

        // 任务队列
        pool->taskQ = (Task *)malloc(sizeof(Task) * queueSize);
        pool->queueCapacity = queueSize;
        pool->queueSize = 0;
        pool->queueFront = 0;
        pool->queueRear = 0;

        pool->shutdown = 0;

        // 创建管理者线程
        pthread_create(&pool->managerID, NULL, manager, pool);
        // 创建工作者线程
        for (int i = 0; i < min; ++i)
        {
            pthread_create(&pool->threadIDs[i], NULL, worker, pool);
        }
        return pool;
    } while (0); // 这个while是一个很好的小技巧，因为我们申请了空间，如果在中途产生了错误，如果我们直接return，就会造成我们的部分资源不能释放，因而使用while，break,之后就是我们的释放结束步骤，就很安全。

    // 释放资源
    if (pool && pool->threadIDs) // pool这个类分配成功之后才释放，很严谨
        free(pool->threadIDs);
    if (pool && pool->taskQ)
        free(pool->taskQ);
    if (pool)
        free(pool);

    return NULL;
}
// 销毁线程池
int threadPoolDestroy(ThreadPool *pool)
{
    if (pool == NULL)
    {
        return -1;
    }

    // 关闭线程池
    pool->shutdown = 1;
    // 阻塞回收管理者线程
    pthread_join(pool->managerID, NULL);
    // 唤醒阻塞的消费者线程
    for (int i = 0; i < pool->liveNum; ++i)
    {
        pthread_cond_signal(&pool->notEmpty);
    }
    // 释放堆内存
    if (pool->taskQ)
    {
        free(pool->taskQ);
    }
    if (pool->threadIDs)
    {
        free(pool->threadIDs);
    }

    pthread_mutex_destroy(&pool->mutexPool);
    pthread_mutex_destroy(&pool->mutexBusy);
    pthread_cond_destroy(&pool->notEmpty);
    pthread_cond_destroy(&pool->notFull);

    free(pool);
    pool = NULL;

    return 0;
}
// 添加任务函数
void threadPoolAdd(ThreadPool *pool, void (*func)(void *), void *arg)
{
    pthread_mutex_lock(&pool->mutexPool);
    while (pool->queueSize == pool->queueCapacity && !pool->shutdown)
    {
        // 阻塞生产者线程
        pthread_cond_wait(&pool->notFull, &pool->mutexPool);
    }
    if (pool->shutdown)
    {
        pthread_mutex_unlock(&pool->mutexPool);
        return;
    }
    // 添加任务
    // 给任务结构体赋值，决定要执行的函数
    pool->taskQ[pool->queueRear].function = func;
    pool->taskQ[pool->queueRear].arg = arg;
    // 循环队列，队列数据改变
    pool->queueRear = (pool->queueRear + 1) % pool->queueCapacity;
    pool->queueSize++;
    // 唤醒线程
    pthread_cond_signal(&pool->notEmpty);
    pthread_mutex_unlock(&pool->mutexPool);
}

// 获取线程池中工作的线程数
int threadPoolBusyNum(ThreadPool *pool)
{
    pthread_mutex_lock(&pool->mutexBusy);
    int busyNum = pool->busyNum;
    pthread_mutex_unlock(&pool->mutexBusy);
    return busyNum;
}

// 获取线程池中活着的线程数
int threadPoolAliveNum(ThreadPool *pool)
{
    pthread_mutex_lock(&pool->mutexPool);
    int aliveNum = pool->liveNum;
    pthread_mutex_unlock(&pool->mutexPool);
    return aliveNum;
}
// 工作者函数
void *worker(void *arg)
{
    ThreadPool *pool = (ThreadPool *)arg;

    while (1)
    {
        pthread_mutex_lock(&pool->mutexPool);
        // 当前任务队列是否为空
        while (pool->queueSize == 0 && !pool->shutdown)
        {
            // 阻塞工作线程
            pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);

            // 判断是不是要销毁线程
            if (pool->exitNum > 0)
            {
                pool->exitNum--;
                if (pool->liveNum > pool->minNum)
                {
                    pool->liveNum--;
                    pthread_mutex_unlock(&pool->mutexPool);
                    threadExit(pool);
                }
            }
        }

        // 判断线程池是否被关闭了
        if (pool->shutdown)
        {
            pthread_mutex_unlock(&pool->mutexPool);
            threadExit(pool);
        }

        // 从任务队列中取出一个任务
        Task task;
        task.function = pool->taskQ[pool->queueFront].function;
        task.arg = pool->taskQ[pool->queueFront].arg;
        // 移动头结点
        pool->queueFront = (pool->queueFront + 1) % pool->queueCapacity;
        pool->queueSize--;
        // 解锁
        pthread_cond_signal(&pool->notFull);
        pthread_mutex_unlock(&pool->mutexPool);

        printf("thread %ld start working...\n", pthread_self());
        pthread_mutex_lock(&pool->mutexBusy);
        pool->busyNum++;
        pthread_mutex_unlock(&pool->mutexBusy);

        task.function(task.arg); // 执行工作函数
        //  free(task.arg);
        // task.arg = NULL;

        printf("thread %ld end working...\n", pthread_self());
        pthread_mutex_lock(&pool->mutexBusy);
        pool->busyNum--;
        pthread_mutex_unlock(&pool->mutexBusy);
    }
    return NULL;
}
// 管理者函数
void *manager(void *arg)
{
    ThreadPool *pool = (ThreadPool *)arg;
    while (!pool->shutdown)
    {
        // 每隔3s检测一次
        sleep(3);

        // 取出线程池中任务的数量和当前线程的数量
        pthread_mutex_lock(&pool->mutexPool);
        int queueSize = pool->queueSize;
        int liveNum = pool->liveNum;
        pthread_mutex_unlock(&pool->mutexPool);

        // 取出忙的线程的数量
        pthread_mutex_lock(&pool->mutexBusy);
        int busyNum = pool->busyNum;
        pthread_mutex_unlock(&pool->mutexBusy);

        // 添加线程
        // 任务的个数>存活的线程个数 && 存活的线程数<最大线程数,livenum - busynum 表示得是空闲的线程数,更精细的进行控制，防止有空闲线程时，还继续创建，可以节省空间
        // if (queueSize > liveNum - busyNum && liveNum < pool->maxNum)
        if (queueSize > 0 && liveNum < pool->maxNum)
        {
            pthread_mutex_lock(&pool->mutexPool);
            int counter = 0;
            for (int i = 0; i < pool->maxNum && counter < NUMBER && pool->liveNum < pool->maxNum; ++i)
            {
                if (pool->threadIDs[i] == 0)
                {
                    pthread_create(&pool->threadIDs[i], NULL, worker, pool);
                    counter++;
                    pool->liveNum++;
                }
            }
            pthread_mutex_unlock(&pool->mutexPool);
        }
        // 销毁线程
        // 忙的线程*2 < 存活的线程数 && 存活的线程>最小线程数
        if (busyNum * 2 < liveNum && liveNum > pool->minNum)
        {
            pthread_mutex_lock(&pool->mutexPool);
            pool->exitNum = NUMBER;
            pthread_mutex_unlock(&pool->mutexPool);
            // 让工作的线程自杀
            for (int i = 0; i < NUMBER; ++i)
            {
                pthread_cond_signal(&pool->notEmpty);
            }
        }
    }
    return NULL;
}
// 单个线程结束
void threadExit(ThreadPool *pool)
{
    pthread_t tid = pthread_self();
    for (int i = 0; i < pool->maxNum; ++i)
    {
        if (pool->threadIDs[i] == tid)
        {
            pool->threadIDs[i] = 0;
            printf("threadExit() called, %ld exiting...\n", tid);
            break;
        }
    }
    pthread_exit(NULL);
}
