#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

int main()
{
    // 创建通信的套接字
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1)
    {
        perror("socket");
        return -1;
    }
    // 绑定本地地址的ip
    struct sockaddr_in saddr;
    saddr.sin_family = AF_INET;
    // 端口
    saddr.sin_port = htons(9999);
    // ip地址
    inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr.s_addr);

    int ret = connect(fd, (struct sockaddr *)&saddr, sizeof(saddr));
    if (ret == -1)
    {
        perror("connect");
        return -1;
    }

    printf("连接中.......\n");
    // 如果链接成功，则打印客户端的IP和端口信息

    // 通信
    int number = 0;
    while (1)
    {
        // 接收数据
        char buff[1024];
        sprintf(buff, "S你好,hello world!.%d\n", number++);
        send(fd, buff, strlen(buff) + 1, 0);

        // 接收数据
        memset(buff, 0, sizeof(buff));
        int len = recv(fd, buff, sizeof(buff), 0);
        if (len > 0)
        {
            printf("server say:%s\n", buff);
        }
        else if (len == 0)
        {
            printf("服务器断开连接.......\n");
            break;
        }
        else
        {
            perror("recv");
            break;
        }
        sleep(2);
    }
    // 关闭文件
    close(fd);

    return 0;
}
