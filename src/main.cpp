#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <variant>
#include <vector>
#include <set>
#include <sys/types.h>
#include <cassert>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <thread>
#include <any>
#include <functional>
#include <map>
#include <chrono>
#include <deque>
#include <mutex>

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
    std::string RESP_string;

    RESPToken(RESPType type, std::any value, std::string RESP_string)
    {
        this->type = type;
        this->value = value;
        this->RESP_string = RESP_string;
    }

    RESPToken()
    {
        this->type = RESPType::NA;
        this->value = std::any();
        this->RESP_string = "";
    }

    bool operator <(const RESPToken& a) const
    {
        if (a.type != this->type) return this->type < a.type;
        return this->RESP_string < a.RESP_string;
    }
};

std::string nextWord(int& ptr, std::string input, const char* sep)
{
    std::string ret = "";
    while (ptr < input.size() && strchr(sep, input[ptr])) ++ptr;
    while (ptr < input.size() && !strchr(sep, input[ptr])) ret += input[ptr++];
    return ret;
}

std::vector<RESPToken> tokenize(std::string input)
{
    std::vector<RESPToken> tokens;
    int ptr = 0;
    std::function<std::string()> nextArg;
    std::function<RESPToken()> processInteger;
    std::function<RESPToken()> processSimpleString;
    std::function<RESPToken()> processSimpleError;
    std::function<RESPToken()> processNullBulkString;
    std::function<RESPToken()> processBulkString;
    std::function<RESPToken()> processArray;
    std::function<RESPToken()> processElement;
    nextArg = [&]()
    {
        /// always returns a string, gets processed later, if needed
        std::string ret = "";
        while (ptr < input.size() && !(input[ptr] == '\r' && input[ptr + 1] == '\n')) ret += input[ptr++];
        ptr += 2;
        return ret;
    };
    processInteger = [&]()
    {
        int initial = ptr;
        ++ptr;
        int value = atoi(nextArg().c_str());
        return RESPToken{RESPType::INTEGER, atoi(nextArg().c_str()), input.substr(initial, ptr - initial)};
    };
    processSimpleString = [&]()
    {
        int initial = ptr;
        ++ptr;
        return RESPToken{RESPType::SIMPLE_STRING, nextArg(), input.substr(initial, ptr - initial)};
    };
    processSimpleError = [&]()
    {
        int initial = ptr;
        ++ptr;
        return RESPToken{RESPType::SIMPLE_ERROR, nextArg(), input.substr(initial, ptr - initial)};
    };
    processNullBulkString = [&]()
    {
        int initial = ptr;
        ptr += 5;
        return RESPToken{RESPType::NULL_BULK_STRING, "", input.substr(initial, ptr - initial)};
    };
    processBulkString = [&]()
    {
        int initial = ptr;
        ++ptr;
        int sz = atoi(nextArg().c_str());
        std::string bytes;
        for (int i = 1; i <= sz; i++) bytes.append(input, ptr++, 1);
        ptr += 2;
        return RESPToken{RESPType::BULK_STRING, bytes, input.substr(initial, ptr - initial)};
    };
    processArray = [&]()
    {
        int initial = ptr;
        ++ptr;
        std::vector<RESPToken> ret;
        int sz = atoi(nextArg().c_str());
        for (int i = 1; i <= sz; i++) ret.push_back(processElement());
        return RESPToken{RESPType::ARRAY, ret, input.substr(initial, ptr - initial)};
    };
    processElement = [&]()
    {
        RESPToken return_token;
        if (input[ptr] == ':') return_token = processInteger();
        else if (input[ptr] == '+') return_token = processSimpleString();
        else if (input[ptr] == '-') return_token = processSimpleError();
        else if (input[ptr] == '$')
        {
            if (input.substr(ptr + 1, ptr + 3) == "-1") return_token = processNullBulkString();
            else return_token = processBulkString();
        }
        else if (input[ptr] == '*') return_token = processArray();
        else
        {
            std::cout << "Error parsing";
            exit(1);
        }
        assert(return_token.type!=RESPType::NA);
        return return_token;
    };
    while (ptr < input.size())
    {
        tokens.push_back(processElement());
    }
    assert(ptr==input.size());
    return tokens;
}

std::string makeBulkString(std::string input)
{
    std::string ret = "$" + std::to_string(input.size()) + "\r\n";
    for (const auto& ch : input) ret.push_back(ch);
    ret += "\r\n";
    return ret;
}

std::string makeInteger(std::string input)
{
    std::string ret = ":";
    for (const auto& ch : input) ret.push_back(ch);
    ret += "\r\n";
    return ret;
}

std::string makeSimpleString(std::string input)
{
    return "+" + input + "\r\n";
}

std::string makeArray(std::string input, int cnt)
{
    return "*" + std::to_string(cnt) + "\r\n" + input;
}
std::string makeSimpleError(std::string input)
{
    return "-"+input + "\r\n";
}
std::string allUpper(std::string input)
{
    for (auto& ch : input) ch = toupper(ch);
    return input;
}

std::mutex db_mutex;
std::mutex wait_mutex;
std::mutex stream_mutex;
std::map<RESPToken, std::multiset<std::chrono::time_point<std::chrono::system_clock>>> pop_wait;
std::map<RESPToken, RESPToken> variables;
std::map<RESPToken, std::vector<std::pair<RESPToken, std::vector<std::pair<RESPToken, RESPToken>>>>> streams;
std::map<RESPToken, std::pair<std::chrono::time_point<std::chrono::system_clock>, int>> variable_time;
std::map<RESPToken, std::deque<RESPToken>> lists;
std::pair <int, int> last_id={-1, -1};
std::pair<bool, RESPToken> valid (std::pair <long long int, int> id)
{
    std::pair<bool, RESPToken> invalid_error = {0, RESPToken {RESPType::SIMPLE_ERROR, "ERR The ID specified in XADD must be greater than 0-0", makeSimpleError("ERR The ID specified in XADD must be greater than 0-0")}};
    std::pair<bool, RESPToken> lesser_error = {0, RESPToken {RESPType::SIMPLE_ERROR, "ERR The ID specified in XADD is equal or smaller than the target stream top item", makeSimpleError ("ERR The ID specified in XADD is equal or smaller than the target stream top item")}};
    std::function<std::pair<bool, RESPToken>()> make_ret = [&] ()
    {
        last_id=id;
        return std::make_pair(1, RESPToken {RESPType::BULK_STRING, std::to_string(id.first)+"-"+std::to_string(id.second), makeBulkString(std::to_string(id.first)+"-"+std::to_string(id.second))});
    };
    if (id.first==-1)
    {
        auto point = std::chrono::system_clock::now();
        id.first = std::chrono::duration_cast<std::chrono::milliseconds>(point.time_since_epoch()).count();
        if (id.first == 0)
        {
            id.second = 1;
            return make_ret();
        }
        if (last_id.first == id.first)
        {
            id.second = last_id.second+1;
            return make_ret();
        }
        id.second = 0;
        return make_ret();
    }
    if (id.second==-1)
    {
        if (last_id.first == -1)
        {
            if (id.first == 0)
                id.second=1;
            else
                id.second=0;
            return make_ret();
        }
        if (id.first < last_id.first)
            return lesser_error;
        if (id.first == last_id.first)
        {
            id.second = last_id.second+1;
            return make_ret();
        }
        id.second = 0;
        return make_ret();
    }
    else
    {
        if (id.first == 0 && id.second == 0)
            return invalid_error;
        if (last_id.first == -1)
        {
            if ((id.first==0 && id.second>=1) || (id.first>0 && id.second>=0))
            {
                return make_ret();
            }
            return invalid_error;
        }
        else
        {
            if (id.first>last_id.first || (id.first==last_id.first && id.second>last_id.second))
            {
                return make_ret();
            }
            return lesser_error;
        }
    }
}
bool containsOnlyDigits (std::string input)
{
    bool ok=1;
    for (const auto &ch : input)
        ok &=isdigit(ch);
    return ok;
}
void handleClient(int client_fd)
{
    std::vector <std::vector<RESPToken>> commands;
    bool queued=0;
    std::function<void(const RESPToken token)> processCommand;
    processCommand = [&](const RESPToken& token)
    {
        auto tokenToId = [&] (RESPToken input)
        {
            int ptr=0;
            std::string checker = nextWord(ptr, std::any_cast<std::string>(input.value), " \r\n\t");
            if (checker=="-" || checker=="+")
                return std::make_pair(-1ll, -1ll);
            std::pair <long long, long long> ret;
            ptr=0;
            ret.first=atoll(nextWord (ptr, std::any_cast<std::string>(input.value), "-").c_str());
            ret.second=ptr==std::any_cast<std::string>(input.value).size()?0:atoll(nextWord (ptr, std::any_cast<std::string>(input.value), "\n\r \t-").c_str());
            return ret;
        };
        auto lowerBoundId = [&] (typeof (streams) streams, RESPToken name, std::pair <long long, long long> input)
        {
            int l=0, r=streams[name].size()-1, p=-1;
            while (l<=r)
            {
                int mid = (l+r) >> 1;
                if (input>tokenToId(streams[name][mid].first))
                    p=mid, l=mid+1;
                else
                    r=mid-1;
            }
            return p+1;
        };
        if (token.type == RESPType::ARRAY)
        {
            for (int i = 0; i < std::any_cast<std::vector<RESPToken>>(token.value).size(); i++)
            {
                std::string wr = "";
                RESPToken element = std::any_cast<std::vector<RESPToken>>(token.value)[i];
                if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "ECHO")
                {
                    RESPToken arg = std::any_cast<std::vector<RESPToken>>(token.value)[i + 1];
                    wr = makeBulkString(std::any_cast<std::string>(arg.value));
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "INCR")
                {
                    RESPToken name = std::any_cast<std::vector<RESPToken>>(token.value)[i+1];
                    if (variables.find(name) != variables.end())
                    {
                        if (containsOnlyDigits(std::any_cast<std::string>(variables[name].value)))
                        {
                            variables[name].value=std::to_string(atoi(std::any_cast<std::string>(variables[name].value).c_str())+1);
                            variables[name].RESP_string=makeBulkString(std::any_cast<std::string>(variables[name].value));
                            wr=makeInteger(std::any_cast<std::string>(variables[name].value));
                        }
                        else
                            wr=makeSimpleError("ERR value is not an integer or out of range");
                    }
                    else
                    {
                        variables[name]={RESPType::BULK_STRING, std::to_string(1), "$1"};
                        wr=":1\r\n";
                    }
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "XREAD")
                {
                    RESPToken arg = std::any_cast<std::vector<RESPToken>>(token.value)[i+1];
                    int req_time = -1;
                    auto start = std::chrono::system_clock::now();
                    if (arg.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(arg.value)) == "BLOCK")
                    {
                        req_time = atoi(std::any_cast<std::string>(std::any_cast<std::vector<RESPToken>>(token.value)[i+2].value).c_str());
                        i+=2;
                        arg = std::any_cast<std::vector<RESPToken>>(token.value)[i+1];
                    }
                    if (arg.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(arg.value)) == "STREAMS")
                    {
                        wr="";
                        std::string first_output="none";
                        std::vector <std::pair<bool, std::pair<long long, long long>>> only_new;
                        while ((req_time==-1 || std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()-start).count()<=req_time) || req_time==0)
                        {
                            wr="";
                            int cnt_streams = ((int)std::any_cast<std::vector<RESPToken>>(token.value).size()-i-2)/2;
                            only_new.resize(cnt_streams);
                            bool found=0;
                            {
                                std::lock_guard lock(stream_mutex);
                                for (int j=0;j<cnt_streams;j++)
                                {
                                    RESPToken name = std::any_cast<std::vector<RESPToken>>(token.value)[i+2+j];
                                    std::pair <long long, long long> id={-1, -1};
                                    if (std::any_cast<std::string>(std::any_cast<std::vector<RESPToken>>(token.value)[i+cnt_streams+j+2].value)=="$")
                                    {
                                        if (only_new[j].first==0)
                                        {
                                            only_new[j].first=1;
                                            only_new[j].second=tokenToId(streams[name].back().first);
                                            only_new[j].second.second++;
                                        }
                                        id=only_new[j].second;
                                    }
                                    else
                                    {
                                        id=tokenToId(std::any_cast<std::vector<RESPToken>>(token.value)[i+cnt_streams+j+2]);
                                        id.second++;
                                    }
                                    int p=lowerBoundId (streams, name, id);
                                    int cnt=0;
                                    std::string stream, full_entry;
                                    for (int i=p;i<streams[name].size();i++)
                                    {
                                        ++cnt;
                                        std::string stream_entry;
                                        for (const auto &entry : streams[name][i].second)
                                            stream_entry+=entry.first.RESP_string+entry.second.RESP_string;
                                        stream_entry=makeArray (stream_entry, streams[name][i].second.size()*2);
                                        full_entry=makeArray (makeArray (streams[name][i].first.RESP_string+stream_entry, 2), 1);
                                    }
                                    found |=(cnt!=0);
                                    stream=makeArray(name.RESP_string+full_entry, 2);
                                    if (cnt)
                                        wr+=stream;
                                }
                            }
                            if (found)
                            {
                                wr=makeArray (wr, cnt_streams);
                                break;
                            }
                            if (req_time==-1)
                                break;
                        }
                        if (wr.empty())
                            wr="*-1\r\n";
                    }
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "XRANGE")
                {
                    RESPToken name = std::any_cast<std::vector<RESPToken>>(token.value)[i+1];
                    std::pair <long long, long long> start=tokenToId(std::any_cast<std::vector<RESPToken>>(token.value)[i+2]), end=tokenToId(std::any_cast<std::vector<RESPToken>>(token.value)[i+3]);
                    int p=lowerBoundId (streams, name, start);
                    int cnt=0;
                    for (int i=p;i<streams[name].size();i++)
                    {
                        if (end!=std::make_pair(-1ll, -1ll) && tokenToId(streams[name][i].first)>end)
                            break;
                        ++cnt;
                        std::string stream_entry;
                        for (const auto &entry : streams[name][i].second)
                            stream_entry+=entry.first.RESP_string+entry.second.RESP_string;
                        stream_entry=makeArray (stream_entry, streams[name][i].second.size()*2);
                        wr+=makeArray (streams[name][i].first.RESP_string+stream_entry, 2);
                    }
                    wr=makeArray (wr, cnt);
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "XADD")
                {
                    RESPToken name = std::any_cast<std::vector<RESPToken>>(token.value)[i+1];
                    RESPToken id_token = std::any_cast<std::vector<RESPToken>>(token.value)[i+2];
                    i+=3;
                    std::string id_string = std::any_cast<std::string>(id_token.value);
                    int ptr=0;
                    std::string first_part=nextWord(ptr, id_string, "-");
                    std::string second_part=nextWord(ptr, id_string, "-\r\n \t");
                    std::pair <int, int> id = {first_part=="*"?-1:atoll(first_part.c_str()), (second_part=="*" || second_part.empty())?-1:atoi(second_part.c_str())};
                    auto res=valid (id);
                    {
                        std::lock_guard lock(stream_mutex);
                        if (res.first)
                        {
                            streams[name].push_back(std::pair<RESPToken, std::vector<std::pair<RESPToken, RESPToken>>>());
                            streams[name][streams[name].size()-1].first=res.second;
                        }
                        for (;i<std::any_cast<std::vector<RESPToken>>(token.value).size();i+=2)
                        {
                            RESPToken key = std::any_cast<std::vector<RESPToken>>(token.value)[i];
                            RESPToken value = std::any_cast<std::vector<RESPToken>>(token.value)[i+1];
                            if (res.first)
                                streams[name][streams[name].size()-1].second.push_back ({key, value});
                        }
                    }
                    wr = res.second.RESP_string;
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "TYPE")
                {
                    RESPToken name = std::any_cast<std::vector<RESPToken>>(token.value)[i+1];
                    if (variables.find(name) != variables.end())
                        wr = makeSimpleString ("string");
                    else if (streams.find(name) != streams.end())
                        wr = makeSimpleString ("stream");
                    else
                        wr = makeSimpleString ("none");
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "BLPOP")
                {
                    RESPToken name = std::any_cast<std::vector<RESPToken>>(token.value)[i + 1];
                    std::chrono::time_point<std::chrono::system_clock> req_time;
                    {
                        std::lock_guard lock(wait_mutex);
                        pop_wait[name].insert(req_time = std::chrono::system_clock::now());
                    }
                    double timeout = atof(std::any_cast<std::string>(std::any_cast<std::vector<RESPToken>>(token.value)[i + 2].value).c_str());
                    while (1)
                    {
                        auto time_now = std::chrono::system_clock::now();
                        auto elapsed = std::chrono::duration<double>(time_now - req_time).count();
                        if (elapsed>timeout && timeout)
                        {
                            wr = "*-1\r\n";
                            break;
                        }
                        {
                            std::lock_guard lock(wait_mutex);
                            if (!lists[name].empty() && *pop_wait[name].rbegin() == req_time)
                            {
                                pop_wait[name].erase(pop_wait[name].find(req_time));
                                RESPToken element;
                                {
                                    std::lock_guard lock(db_mutex);
                                    element = lists[name].front();
                                    lists[name].pop_front();
                                }
                                wr = makeArray(name.RESP_string + element.RESP_string, 2);
                                break;
                            }
                        }
                    }
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "RPUSH")
                {
                    RESPToken name = std::any_cast<std::vector<RESPToken>>(token.value)[i + 1];
                    i += 2;
                    for (; i < std::any_cast<std::vector<RESPToken>>(token.value).size(); i++)
                    {
                        RESPToken value = std::any_cast<std::vector<RESPToken>>(token.value)[i];
                        {
                            std::lock_guard lock(db_mutex);
                            lists[name].push_back(value);
                        }
                    }
                    i--;
                    wr = makeInteger(std::to_string(lists[name].size()));
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "LPUSH")
                {
                    RESPToken name = std::any_cast<std::vector<RESPToken>>(token.value)[i + 1];
                    i += 2;
                    for (; i < std::any_cast<std::vector<RESPToken>>(token.value).size(); i++)
                    {
                        RESPToken value = std::any_cast<std::vector<RESPToken>>(token.value)[i];
                        {
                            std::lock_guard lock(db_mutex);
                            lists[name].push_front(value);
                        }
                    }
                    i--;
                    wr = makeInteger(std::to_string(lists[name].size()));
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "LLEN")
                {
                    RESPToken name = std::any_cast<std::vector<RESPToken>>(token.value)[i + 1];
                    wr = makeInteger(std::to_string(lists[name].size()));
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "LPOP")
                {
                    RESPToken name = std::any_cast<std::vector<RESPToken>>(token.value)[i + 1];
                    if (lists.find(name) == lists.end()) wr = "$-1\r\n";
                    else
                    {
                        int cnt = 1;
                        if (i + 2 < std::any_cast<std::vector<RESPToken>>(token.value).size()) cnt = atoi(std::any_cast<std::string>(std::any_cast<std::vector<RESPToken>>(token.value)[i + 2].value).c_str());
                        for (int i = 1; i <= cnt; i++)
                        {
                            wr += lists[name].front().RESP_string;
                            {
                                std::lock_guard lock(db_mutex);
                                lists[name].pop_front();
                            }
                        }
                        if (cnt > 1) wr = makeArray(wr, cnt);
                    }
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "LRANGE")
                {
                    RESPToken name = std::any_cast<std::vector<RESPToken>>(token.value)[i + 1];
                    std::cout << std::any_cast<std::string>(name.value) << " " << lists[name].size() << "\n";
                    if (lists.find(name) == lists.end())
                    {
                        wr = makeArray("", 0);
                    }
                    else
                    {
                        std::deque<RESPToken>::iterator start, stop;
                        int dif_begin = atoi(std::any_cast<std::string>(std::any_cast<std::vector<RESPToken>>(token.value)[i + 2].value).c_str()), dif_end = atoi(std::any_cast<std::string>(std::any_cast<std::vector<RESPToken>>(token.value)[i + 3].value).c_str());
                        if (dif_begin >= 0)
                        {
                            start = lists[name].begin();
                            while (dif_begin-- && start != lists[name].end()) ++start;
                        }
                        else
                        {
                            start = lists[name].end();
                            dif_begin = abs(dif_begin);
                            while (dif_begin--)
                            {
                                if (start == lists[name].begin()) break;
                                --start;
                            }
                        }
                        if (dif_end >= 0)
                        {
                            stop = lists[name].begin();
                            ++dif_end;
                            while (dif_end-- && stop != lists[name].end()) ++stop;
                        }
                        else
                        {
                            stop = lists[name].end();
                            dif_end = abs(dif_end);
                            --dif_end;
                            while (dif_end--)
                            {
                                if (stop == lists[name].begin()) break;
                                --stop;
                            }
                        }
                        int cnt = 0;
                        for (auto itr = start; itr != stop; ++itr)
                        {
                            ++cnt;
                            wr += itr->RESP_string;
                        }
                        wr = makeArray(wr, cnt);
                    }
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "SET")
                {
                    RESPToken name = std::any_cast<std::vector<RESPToken>>(token.value)[i + 1];
                    RESPToken value = std::any_cast<std::vector<RESPToken>>(token.value)[i + 2];
                    auto time_now = std::chrono::system_clock::now();
                    {
                        std::lock_guard lock(db_mutex);
                        variables[name] = value;
                    }
                    if (i + 3 < std::any_cast<std::vector<RESPToken>>(token.value).size())
                    {
                        RESPToken potential_arg = std::any_cast<std::vector<RESPToken>>(token.value)[i + 3];
                        if (potential_arg.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(potential_arg.value)) == "PX")
                        {
                            int ptr = 0;
                            {
                                std::lock_guard lock(db_mutex);
                                variable_time[name] = {time_now, atoi(nextWord(ptr, std::any_cast<std::string>(std::any_cast<std::vector<RESPToken>>(token.value)[i + 4].value), ":\r\n ").c_str())};
                            }
                        }
                    }
                    else variable_time.erase(name);
                    wr = makeSimpleString("OK");
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "GET")
                {
                    RESPToken name = std::any_cast<std::vector<RESPToken>>(token.value)[i + 1];
                    if (variables.find(name) != variables.end())
                    {
                        if (variable_time.find(name) == variable_time.end()) wr = makeBulkString(std::any_cast<std::string>(variables[name].value));
                        else
                        {
                            auto time_now = std::chrono::system_clock::now();
                            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(time_now - variable_time[name].first);
                            if (elapsed.count() > variable_time[name].second) wr = "$-1\r\n";
                            else wr = makeBulkString(std::any_cast<std::string>(variables[name].value));
                        }
                    }
                    else wr = "$-1\r\n";
                }
                else processCommand(element);
                if (!wr.empty()) write(client_fd, wr.c_str(), wr.size());
            }
        }
        if (token.type == RESPType::BULK_STRING)
        {
            if (allUpper(std::any_cast<std::string>(token.value)) == "PING")
            {
                ssize_t n = write(client_fd, "+PONG\r\n", strlen("+PONG\r\n"));
                std::cout << "\nSent: " << n << "\nWanted: " << strlen("+PONG\r\n") << "\n";;
            }
        }
    };
    auto checkModQueue = [&] (const RESPToken& token)
    {
        bool ok=0;
        std::string wr="";
        RESPToken command=std::any_cast<std::vector<RESPToken>>(token.value)[0];
        if (command.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(command.value)) == "MULTI")
        {
            ok=1;
            wr=makeSimpleString("OK");
            queued=1;
        }
        else if (command.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(command.value)) == "EXEC")
        {
            ok=1;
            if (queued)
            {
                queued=0;
                int cnt=0;
                for (const auto& tokens : commands)
                {
                    for (const auto& token : tokens)
                        processCommand(token);
                }
                wr=makeArray ("", 0);
            }
            else
                wr=makeSimpleError("ERR EXEC without MULTI");
        }
        if (!wr.empty())
            write (client_fd, wr.c_str(), wr.size());
        return ok;
    };
    while (true)
    {
        char buff[4096] = {0};
        ssize_t nr_bytes = read(client_fd, &buff, sizeof(buff));
        int ptr = 0;
        std::vector<RESPToken> tokens = tokenize(std::string(buff));
        if (tokens.empty())
            continue;
        if (checkModQueue (tokens[0]))
            continue;
        if (queued)
        {
            commands.push_back (tokens);
            write (client_fd, "+QUEUED\r\n", strlen("+QUEUED\r\n"));
        }
        else
        {
            for (const auto& token : tokens)
            {
                processCommand(token);
            }
        }
    }
    close(client_fd);
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
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, (socklen_t*)&client_addr_len);
        std::cout << "Client connected\n";
        std::thread client(handleClient, client_fd);
        client.detach();
    }
    close(server_fd);
    return 0;
}
