#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
std::string nextWord (int &ptr, std::string input, const char *sep)
{
    std::string ret="";
    while (ptr<input.size() && strchr (sep, input[ptr]))
        ++ptr;
    while (ptr<input.size() && !strchr (sep, input[ptr]))
        ret+=input[ptr++];
    return ret;
}
int main(int argc, char** argv)
{
    // Flush after every std::cout / std::cerr
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        std::cerr << "Failed to create server socket\n";
        return 1;
    }
    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        std::cerr << "setsockopt failed\n";
        return 1;
    }
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(6379);
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0)
    {
        std::cerr << "Failed to bind to port 6379\n";
        return 1;
    }
    int connection_backlog = 5;
    if (listen(server_fd, connection_backlog) != 0)
    {
        std::cerr << "listen failed\n";
        return 1;
    }
    struct sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);
    std::cout << "Waiting for a client to connect...\n";
    while (true)
    {
        int client_fd=accept(server_fd, (struct sockaddr*)&client_addr, (socklen_t*)&client_addr_len);
        std::cout << "Client connected\n";
        pid_t pid = fork();
        if (pid==0)
        {
            while (true)
            {
                char buff[4096]={0};
                ssize_t nr_bytes=read (client_fd, &buff, sizeof(buff));
                int ptr=0;
                std::string word;
                while (ptr<nr_bytes)
                {
                    word=nextWord (ptr, std::string(buff), "\n\r ");
                    if (word=="PING")
                        write (client_fd, "+PONG\r\n", strlen("+PONG\r\n"));
                }
            }
            close(client_fd);
        }
        close(client_fd);
    }
    close(server_fd);
    return 0;
}
