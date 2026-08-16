#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <variant>
#include <vector>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <any>
#include <functional>
enum class RESPType
{
    SIMPLE_STRING,
    SIMPLE_ERROR,
    INTEGER,
    BULK_STRING,
    NULL_BULK_STRING,
    ARRAY,
    NA
};
struct RESPToken
{
    RESPType type;
    std::any value;
    RESPToken (RESPType type, std::any value)
    {
        this -> type = type;
        this -> value = value;
    }
    RESPToken ()
    {
        this -> type = RESPType::NA;
        this -> value = std::any();
    }
};
std::string nextWord (int &ptr, std::string input, const char *sep)
{
    std::string ret="";
    while (ptr<input.size() && strchr (sep, input[ptr]))
        ++ptr;
    while (ptr<input.size() && !strchr (sep, input[ptr]))
        ret+=input[ptr++];
    return ret;
}
std::vector <RESPToken> tokenize (std::string input)
{
    std::vector <RESPToken> tokens;
    int ptr=0;
    std::function<std::string()>nextArg;
    std::function<RESPToken()>processInteger;
    std::function<RESPToken()>processSimpleString;
    std::function<RESPToken()>processSimpleError;
    std::function<RESPToken()>processNullBulkString;
    std::function<RESPToken()>processBulkString;
    std::function<RESPToken()>processArray;
    std::function<RESPToken()>processElement;
    nextArg = [&] ()
    {
        /// always returns a string, gets processed later, if needed
        std::string ret="";
        while (ptr<input.size() && !(input[ptr]=='\r' && input[ptr+1]=='\n'))
            ret+=input[ptr++];
        ptr+=2;
        return ret;
    };
    processInteger = [&] ()
    {
        ++ptr;
        int value = atoi(nextArg().c_str());
        return RESPToken{RESPType::INTEGER, atoi(nextArg().c_str())};
    };
    processSimpleString = [&] ()
    {
        ++ptr;
        return RESPToken{RESPType::SIMPLE_STRING, nextArg()};
    };
    processSimpleError = [&] ()
    {
        ++ptr;
        return RESPToken{RESPType::SIMPLE_ERROR, nextArg()};
    };
    processNullBulkString = [&] ()
    {
        ptr+=5;
        return RESPToken{RESPType::NULL_BULK_STRING, ""};
    };
    processBulkString = [&] ()
    {
        ++ptr;
        int sz = atoi(nextArg().c_str());
        std::string bytes;
        for (int i=1;i<=sz;i++)
            bytes.append (input, ptr++, 1);
        ptr+=2;
        return RESPToken{RESPType::BULK_STRING, bytes};
    };
    processArray = [&] ()
    {
        ++ptr;
        std::vector <RESPToken> ret;
        int sz = atoi(nextArg().c_str());
        for (int i=1;i<=sz;i++)
        {
            ret.push_back (processElement());
        }
        return RESPToken{RESPType::ARRAY, ret};
    };
    processElement = [&] ()
    {
        RESPToken return_token;
        if (input[ptr]==':')
            return_token = processInteger();
        else if (input[ptr]=='+')
            return_token = processSimpleString();
        else if (input[ptr]=='-')
            return_token = processSimpleError();
        else if (input[ptr]=='$')
        {
            if (input.substr (ptr+1, ptr+3)=="-1")
                return_token = processNullBulkString();
            else
                return_token = processBulkString();
        }
        else if (input[ptr]=='*')
            return_token = processArray();
        return return_token;
    };
    while (ptr<input.size())
    {
        tokens.push_back (processElement ());
    }
    return tokens;
}
std::string makeBulkString (std::string input)
{
    std::string ret="$"+std::to_string(input.size())+"\r\n";
    for (const auto &ch : input)
        ret.push_back (ch);
    ret+="\r\n";
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
            std::function<void(const RESPToken token)>processCommand;
            processCommand = [&] (const RESPToken &token)
            {
                if (token.type == RESPType::ARRAY)
                {
                    for (int i=0;i<std::any_cast<std::vector<RESPToken>>(token.value).size();i++)
                    {
                        RESPToken element=std::any_cast<std::vector<RESPToken>>(token.value)[i];
                        if (element.type == RESPType::BULK_STRING && std::any_cast<std::string>(element.value)=="ECHO")
                        {
                            RESPToken arg = std::any_cast<std::vector<RESPToken>>(token.value)[i+1];
                            std::string wr=makeBulkString (std::any_cast<std::string>(arg.value));
                            write (client_fd, wr.c_str(), wr.size());
                        }
                        else
                            processCommand (element);
                    }
                }
                if (token.type==RESPType::BULK_STRING)
                {
                    if (std::any_cast<std::string>(token.value)=="PING")
                    {
                        ssize_t n=write (client_fd, "+PONG\r\n", strlen("+PONG\r\n"));
                        std::cout << "\nSent: " << n << "\nWanted: " << strlen("+PONG\r\n") << "\n";;
                    }
                }
            };
            while (true)
            {
                char buff[4096]={0};
                ssize_t nr_bytes=read (client_fd, &buff, sizeof(buff));
                int ptr=0;
                std::vector <RESPToken> tokens=tokenize (std::string (buff));
                for (const auto &token : tokens)
                {
                    processCommand (token);
                }
            }
            close(client_fd);
        }
        close(client_fd);
    }
    close(server_fd);
    return 0;
}
