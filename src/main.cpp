#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <variant>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>
#include <set>
#include <iomanip>
#include <fcntl.h>
#include <sys/types.h>
#include <ext/pb_ds/assoc_container.hpp>
#include <ext/pb_ds/tree_policy.hpp>
#include <cassert>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <thread>
#include <any>
#include <atomic>
#include <functional>
#include <map>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <fstream>
using namespace __gnu_pbds;
#define _USE_MATH_DEFINES
const long double EARTH_RADIUS_METERS = 6372797.560856;
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
        if (this -> type == RESPType::BULK_STRING)
            return std::any_cast<std::string>(this -> value)<std::any_cast<std::string>(a.value);
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
        while (ptr < input.size()-1 && !(input[ptr] == '\r' && input[ptr + 1] == '\n')) ret += input[ptr++];
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
        for (int i = 1; i <= sz; i++)
        {
            if (ptr>input.size())
                std::cout << "I am sorry for what was said before in this line (highly unprofessional). Anyway the RESP parsing goes out of bounds on the input string at some point, here is the input string: " << input << "\n";
            bytes.append(input, ptr++, 1);
        }
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
bool is_replica=0;
char *external_command;
std::condition_variable ack_cv;
std::mutex db_mutex;
std::mutex synced_mutex;
std::mutex wait_mutex;
std::mutex ack_mutex;
std::mutex stream_mutex;
std::mutex replicas_write_mutex;
std::map<RESPToken, std::multiset<std::chrono::time_point<std::chrono::system_clock>>> pop_wait;
std::map<RESPToken, RESPToken> variables;
std::map<RESPToken, std::vector<std::pair<RESPToken, std::vector<std::pair<RESPToken, RESPToken>>>>> streams;
std::map<RESPToken, std::pair<std::chrono::time_point<std::chrono::system_clock>, long long>> variable_time;
std::map<RESPToken, std::deque<RESPToken>> lists;
std::map<RESPToken, std::set<int>> watching_clients;
std::string replication_id="8371b4fb1155b71f4a04d3e1bc3e18c4a990aeeb";
int offset=0;
std::string rdb_dir=std::filesystem::current_path(), rdb_name, rdb_appendonly="no", rdb_appenddirname="appendonlydir", rdb_appendfilename="appendonly.aof", rdb_appendfsync="everysec";
std::map<int, int> replica_offset;
enum WATCH_STATE
{
    WATCHED,
    MODIFIED
};
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
std::vector<std::set <std::pair<RESPToken, WATCH_STATE>>> watched_keys;
std::set <int> ids;
std::set <int> replica_fds;
std::map <int, int> replica_ports;
std::map <int, bool> replica_synced;
std::atomic<long long> master_offset;
std::set <std::string> subscribed_allowed;
std::map <RESPToken, std::set<int>> channel;
typedef tree<std::pair<long double, RESPToken>, null_type, std::less<std::pair<long double, RESPToken>>, rb_tree_tag, tree_order_statistics_node_update> ordered_set;
std::map <RESPToken, ordered_set> sorted_sets;
std::map <RESPToken, std::map<RESPToken, long double>> sorted_sets_only_members;
long double longitude_limit=180, latitude_limit=85.05112878;
long double degreeToRadian (long double angle)
{
    return angle*M_PI/180.0;
}
void handleClient(int client_fd, int client_id, int mode)
{
    bool is_subscribed=0;
    int replicas_synced=0;
    std::set<RESPToken> subscriptions;
    std::vector <std::vector<RESPToken>> commands;
    bool queued=0;
    std::function<void(const RESPToken token, bool take_output)> processCommand;
    std::string outputs="";
    int sz_outputs=0;
    int last_command_bytes;
    bool replica_initialized=0, wr_exception=0;
    ssize_t bytes_processed=0;
    processCommand = [&](const RESPToken& token, bool take_output=0)
    {
        if (is_subscribed && token.type==RESPType::ARRAY)
        {
            RESPToken element = std::any_cast<std::vector<RESPToken>>(token.value)[0];
            if (element.type == RESPType::BULK_STRING)
            {
                std::string command = allUpper(std::any_cast<std::string>(element.value));
                if (subscribed_allowed.find(command) == subscribed_allowed.end())
                {
                    std::string wr = makeSimpleError ("ERR Can't execute '"+command+"': only (P|S)SUBSCRIBE / (P|S)UNSUBSCRIBE / PING / QUIT / RESET are allowed in this context\n");
                    if (!wr.empty())
                    {
                        if (is_replica && !wr_exception)
                            return;
                        if (take_output)
                            ++sz_outputs, outputs+=wr;
                        else if (mode)
                        {
                            ssize_t n=write (client_fd, wr.c_str(), wr.size());
                            if (replica_fds.find(client_fd)!=replica_fds.end())
                                master_offset+=n, replica_offset[client_fd]+=n;
                        }
                        return;
                    }
                }
            }
        }
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
        auto updateReplicaStatus = [&] ()
        {
            std::lock_guard lock(replicas_write_mutex);
            for (auto fd : replica_fds)
            {
                write (fd, token.RESP_string.c_str(), token.RESP_string.size());
                if (replica_synced[fd])
                    replica_synced[fd]=0;
            }
            master_offset+=token.RESP_string.size();
        };
        auto validCoordinates = [&] (long double longitude, long double latitude)
        {
            return (longitude<=longitude_limit && longitude>=-longitude_limit && latitude<=latitude_limit && latitude>=-latitude_limit);
        };
        auto spreadIntToLongLong = [&] (unsigned int x)
        {
            unsigned long long v=x;
            v = v & 0xFFFFFFFFULL;
            v = (v | (v << 16)) & 0x0000FFFF0000FFFFULL;
            v = (v | (v << 8)) & 0x00FF00FF00FF00FFULL;
            v = (v | (v << 4)) & 0x0F0F0F0F0F0F0F0FULL;
            v = (v | (v << 2)) & 0x3333333333333333ULL;
            v = (v | (v << 1)) & 0x5555555555555555ULL;
            return v;
        };
        auto compactLongLongToInt = [&] (unsigned long long x)
        {
            x = x & 0x5555555555555555ULL;
            x = (x | (x >> 1)) & 0x3333333333333333ULL;
            x = (x | (x >> 2)) & 0x0F0F0F0F0F0F0F0FULL;
            x = (x | (x >> 4)) & 0x00FF00FF00FF00FFULL;
            x = (x | (x >> 8)) & 0x0000FFFF0000FFFFULL;
            x = (x | (x >> 16)) & 0x00000000FFFFFFFFULL;
            return static_cast<unsigned int>(x);
        };
        auto interleave = [&] (long double longitude, long double latitude)
        {
            unsigned int normalized_longitude = 1.0*(1 << 26)*(longitude+longitude_limit)/(2.0*longitude_limit);
            unsigned int normalized_latitude = 1.0*(1 << 26)*(latitude+latitude_limit)/(2.0*latitude_limit);
            unsigned long long int x=spreadIntToLongLong(normalized_longitude) << 1;
            unsigned long long int y=spreadIntToLongLong(normalized_latitude);
            return x | y;
        };
        auto gridNumberToCoord = [&] (unsigned int latitude, unsigned int longitude)
        {
            long double grid_lat_min = -latitude_limit+2*latitude_limit*(latitude/pow(2, 26));
            long double grid_lat_max = -latitude_limit+2*latitude_limit*((latitude+1)/pow(2, 26));
            long double grid_long_min = -longitude_limit+2*longitude_limit*(longitude/pow(2, 26));
            long double grid_long_max = -longitude_limit+2*longitude_limit*((longitude+1)/pow(2, 26));
            return std::make_pair ((grid_long_min+grid_long_max)/2, (grid_lat_min+grid_lat_max)/2);
        };
        auto decodeScore = [&] (unsigned long long score)
        {
            unsigned long long y = score >> 1;
            unsigned long long x = score;
            unsigned int grid_lat_number = compactLongLongToInt(x);
            unsigned int grid_long_number = compactLongLongToInt(y);
            return gridNumberToCoord(grid_lat_number, grid_long_number);
        };
        auto haversineDistance = [&] (std::pair <long double, long double> x, std::pair <long double, long double> y)
        {
            long double latRad1 = degreeToRadian(x.second);
            long double latRad2 = degreeToRadian(y.second);
            long double longRad1 = degreeToRadian(x.first);
            long double longRad2 = degreeToRadian(y.first);
            double diffLa = latRad2 - latRad1;
            double diffLo = longRad2 - longRad1;
            double computation = asin(sqrt(sin(diffLa/2)*sin(diffLa/2)+cos(latRad1)*cos(latRad2)*sin(diffLo/2)*sin(diffLo/2)));
            return 2*computation*EARTH_RADIUS_METERS;
        };
        if (token.type == RESPType::ARRAY)
        {
            for (int i = 0; i < std::any_cast<std::vector<RESPToken>>(token.value).size(); i++)
            {
                wr_exception=0;
                std::string wr = "";
                RESPToken element = std::any_cast<std::vector<RESPToken>>(token.value)[i];
                if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "ECHO")
                {
                    RESPToken arg = std::any_cast<std::vector<RESPToken>>(token.value)[i + 1];
                    wr = makeBulkString(std::any_cast<std::string>(arg.value));
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "GEOSEARCH")
                {
                    RESPToken arg = std::any_cast<std::vector<RESPToken>>(token.value)[i + 1];
                    long double longitude = strtold(std::any_cast<std::string>(std::any_cast<std::vector<RESPToken>>(token.value)[i+3].value).c_str(), nullptr);
                    long double latitude = strtold(std::any_cast<std::string>(std::any_cast<std::vector<RESPToken>>(token.value)[i+4].value).c_str(), nullptr);
                    long double dist = strtold(std::any_cast<std::string>(std::any_cast<std::vector<RESPToken>>(token.value)[i+6].value).c_str(), nullptr);
                    int cnt=0;
                    if (sorted_sets.find(arg)!=sorted_sets.end())
                    {
                        for (const auto &[score, member] : sorted_sets[arg])
                        {
                            auto coord1 = std::make_pair (longitude, latitude);
                            auto coord2 = decodeScore(score);
                            if (haversineDistance(coord1, coord2)<=dist)
                                wr+=member.RESP_string, ++cnt;
                        }
                        wr=makeArray (wr, cnt);
                    }
                    else
                        wr="*-1\r\n";
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "GEODIST")
                {
                    RESPToken arg = std::any_cast<std::vector<RESPToken>>(token.value)[i + 1];
                    RESPToken member1 = std::any_cast<std::vector<RESPToken>>(token.value)[i + 2];
                    RESPToken member2 = std::any_cast<std::vector<RESPToken>>(token.value)[i + 3];
                    if (sorted_sets.find(arg)!=sorted_sets.end() && sorted_sets_only_members[arg].find(member1)!=sorted_sets_only_members[arg].end() && sorted_sets_only_members[arg].find(member2)!=sorted_sets_only_members[arg].end())
                    {
                        auto coord1 = decodeScore(sorted_sets_only_members[arg][member1]);
                        auto coord2 = decodeScore(sorted_sets_only_members[arg][member2]);
                        std::ostringstream oss;
                        oss << std::fixed << std::setprecision(12) << std::to_string(haversineDistance(coord1, coord2));
                        wr=makeBulkString(oss.str());
                    }
                    else
                        wr="$-1\r\n";
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "GEOPOS")
                {
                    RESPToken arg = std::any_cast<std::vector<RESPToken>>(token.value)[i + 1];
                    std::string wr2="";
                    int cnt=0;
                    for (int j=i+2;j<std::any_cast<std::vector<RESPToken>>(token.value).size();j++, cnt++)
                    {
                        RESPToken member = std::any_cast<std::vector<RESPToken>>(token.value)[j];
                        if (sorted_sets.find(arg)!=sorted_sets.end() && sorted_sets_only_members[arg].find(member)!=sorted_sets_only_members[arg].end())
                        {
                            std::ostringstream oss[2];
                            auto coord=decodeScore(sorted_sets_only_members[arg][member]);
                            oss[0] << std::fixed << std::setprecision (12) << coord.first;
                            oss[1] << std::fixed << std::setprecision (12) << coord.second;
                            wr2 += makeArray (makeBulkString(oss[0].str())+makeBulkString(oss[1].str()), 2);
                        }
                        else
                            wr2 += "*-1\r\n";
                    }
                    wr=makeArray (wr2, cnt);
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "GEOADD")
                {
                    RESPToken arg = std::any_cast<std::vector<RESPToken>>(token.value)[i + 1];
                    long double longitude = strtold(std::any_cast<std::string>(std::any_cast<std::vector<RESPToken>>(token.value)[i+2].value).c_str(), nullptr);
                    long double latitude = strtold(std::any_cast<std::string>(std::any_cast<std::vector<RESPToken>>(token.value)[i+3].value).c_str(), nullptr);
                    RESPToken member = std::any_cast<std::vector<RESPToken>>(token.value)[i+4];
                    if (validCoordinates (longitude, latitude))
                    {
                        long double score = interleave (longitude, latitude);
                        sorted_sets[arg].insert ({score, member});
                        sorted_sets_only_members[arg][member]=score;
                        wr = makeInteger ("1");
                    }
                    else
                        wr = makeSimpleError ("ERR invalid longitude,latitude pair "+std::to_string(longitude)+","+std::to_string(latitude));
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "ZREM")
                {
                    RESPToken arg = std::any_cast<std::vector<RESPToken>>(token.value)[i + 1];
                    RESPToken member = std::any_cast<std::vector<RESPToken>>(token.value)[i+2];
                    if (sorted_sets.find(arg)!=sorted_sets.end() && sorted_sets_only_members[arg].find(member)!=sorted_sets_only_members[arg].end())
                    {
                        wr=makeInteger ("1");
                        sorted_sets[arg].erase ({sorted_sets_only_members[arg][member], member});
                        sorted_sets_only_members[arg].erase (member);
                    }
                    else
                        wr=makeInteger ("0");
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "ZSCORE")
                {
                    RESPToken arg = std::any_cast<std::vector<RESPToken>>(token.value)[i + 1];
                    RESPToken member = std::any_cast<std::vector<RESPToken>>(token.value)[i + 2];
                    if (sorted_sets.find(arg)!=sorted_sets.end() && sorted_sets_only_members[arg].find(member)!=sorted_sets_only_members[arg].end())
                    {
                        std::ostringstream oss;
                        oss << std::fixed << std::setprecision (12) << sorted_sets_only_members[arg][member];
                        wr=makeBulkString(oss.str());
                    }
                    else
                        wr="$-1\r\n";
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "ZCARD")
                {
                    wr_exception=1;
                    RESPToken arg = std::any_cast<std::vector<RESPToken>>(token.value)[i + 1];
                    if (sorted_sets.find(arg)!=sorted_sets.end())
                        wr=makeInteger(std::to_string(sorted_sets[arg].size()));
                    else
                        wr=makeInteger("0");
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "ZRANGE")
                {
                    wr_exception=1;
                    RESPToken arg = std::any_cast<std::vector<RESPToken>>(token.value)[i + 1];
                    int begin = atoi(std::any_cast<std::string>(std::any_cast<std::vector<RESPToken>>(token.value)[i+2].value).c_str());
                    int end = atoi(std::any_cast<std::string>(std::any_cast<std::vector<RESPToken>>(token.value)[i+3].value).c_str());
                    if (sorted_sets.find(arg)!=sorted_sets.end())
                    {
                        if (begin<0)
                            begin=std::max (0, (int)sorted_sets[arg].size()+begin);
                        if (end<0)
                            end=std::max (0, (int)sorted_sets[arg].size()+end);
                        auto itr = sorted_sets[arg].begin(), itr2 = sorted_sets[arg].begin();
                        end++;
                        int cnt=0;
                        while (begin-- && itr!=sorted_sets[arg].end())
                            ++itr;
                        while (end-- && itr2!=sorted_sets[arg].end())
                            ++itr2;
                        for (;itr!=itr2;++itr)
                            ++cnt, wr+=(*itr).second.RESP_string;
                        wr=makeArray (wr, cnt);
                    }
                    else
                        wr=makeArray ("", 0);
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "ZRANK")
                {
                    wr_exception=1;
                    RESPToken arg = std::any_cast<std::vector<RESPToken>>(token.value)[i + 1];
                    RESPToken member = std::any_cast<std::vector<RESPToken>>(token.value)[i + 2];
                    if (sorted_sets.find(arg)!=sorted_sets.end() && sorted_sets_only_members[arg].find(member)!=sorted_sets_only_members[arg].end())
                        wr=makeInteger (std::to_string(sorted_sets[arg].order_of_key(std::make_pair(sorted_sets_only_members[arg][member], member))));
                    else
                        wr="$-1\r\n";
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "ZADD")
                {
                    updateReplicaStatus();
                    RESPToken arg = std::any_cast<std::vector<RESPToken>>(token.value)[i + 1];
                    long double score = strtold(std::any_cast<std::string>(std::any_cast<std::vector<RESPToken>>(token.value)[i + 2].value).c_str(), nullptr);
                    RESPToken member = std::any_cast<std::vector<RESPToken>>(token.value)[i + 3];
                    if (sorted_sets_only_members[arg].find(member)!=sorted_sets_only_members[arg].end())
                    {
                        wr = makeInteger ("0");
                        sorted_sets[arg].erase ({sorted_sets_only_members[arg][member], member});
                        sorted_sets[arg].insert ({score, member});
                        sorted_sets_only_members[arg][member]=score;
                    }
                    else
                    {
                        sorted_sets[arg].insert({score, member});
                        sorted_sets_only_members[arg][member]=score;
                        wr = makeInteger("1");
                    }
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "PUBLISH")
                {
                    RESPToken arg = std::any_cast<std::vector<RESPToken>>(token.value)[i + 1];
                    RESPToken msg = std::any_cast<std::vector<RESPToken>>(token.value)[i + 2];
                    wr = makeInteger(std::to_string(channel[arg].size()));
                    std::string wr2 = makeArray (makeBulkString("message")+makeBulkString(std::any_cast<std::string>(arg.value))+makeBulkString(std::any_cast<std::string>(msg.value)), 3);
                    for (const int& fd : channel[arg])
                        write (fd, wr2.c_str(), wr2.size());
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "UNSUBSCRIBE")
                {
                    RESPToken arg = std::any_cast<std::vector<RESPToken>>(token.value)[i + 1];
                    subscriptions.erase (arg);
                    channel[arg].erase (client_fd);
                    is_subscribed = !subscriptions.empty();
                    wr = makeArray (makeBulkString("unsubscribe")+makeBulkString(std::any_cast<std::string>(arg.value))+makeInteger(std::to_string(subscriptions.size())), 3);
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "SUBSCRIBE")
                {
                    RESPToken ch = std::any_cast<std::vector<RESPToken>>(token.value)[i+1];
                    subscriptions.insert(ch);
                    channel[ch].insert(client_fd);
                    is_subscribed=1;
                    wr=makeArray (makeBulkString ("subscribe")+makeBulkString(std::any_cast<std::string>(ch.value))+makeInteger(std::to_string(subscriptions.size())), 3);
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "KEYS")
                {
                    RESPToken arg = std::any_cast<std::vector<RESPToken>>(token.value)[i + 1];
                    if (std::any_cast<std::string>(arg.value)=="*")
                    {
                        for (auto [variable, value] : variables)
                            wr+=variable.RESP_string;
                        wr=makeArray (wr, (int)variables.size());
                    }
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "CONFIG")
                {
                    RESPToken arg = std::any_cast<std::vector<RESPToken>>(token.value)[i + 1];
                    if (std::any_cast<std::string>(arg.value)=="GET")
                    {
                        RESPToken name = std::any_cast<std::vector<RESPToken>>(token.value)[i + 2];
                        if (std::any_cast<std::string>(name.value) == "dir")
                            wr=makeArray (makeBulkString ("dir")+makeBulkString(rdb_dir), 2);
                        else if (std::any_cast<std::string>(name.value) == "appendonly")
                            wr=makeArray (makeBulkString ("appendonly")+makeBulkString(rdb_appendonly), 2);
                        else if (std::any_cast<std::string>(name.value) == "appenddirname")
                            wr=makeArray (makeBulkString ("appenddirname")+makeBulkString(rdb_appenddirname), 2);
                        else if (std::any_cast<std::string>(name.value) == "appendfilename")
                            wr=makeArray (makeBulkString ("appendfilename")+makeBulkString(rdb_appendfilename), 2);
                        else if (std::any_cast<std::string>(name.value) == "appendfsync")
                            wr=makeArray (makeBulkString ("appendfsync")+makeBulkString(rdb_appendfsync), 2);
                        else
                            wr=makeArray (makeBulkString("dbfilename")+makeBulkString(rdb_name), 2);
                        i+=1;
                    }
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "WAIT")
                {
                    int nr = atoi(std::any_cast<std::string>(std::any_cast<std::vector<RESPToken>>(token.value)[i+1].value).c_str());
                    int time = atoi(std::any_cast<std::string>(std::any_cast<std::vector<RESPToken>>(token.value)[i+2].value).c_str());
                    std::string wr2=makeArray (makeBulkString("REPLCONF")+makeBulkString("GETACK")+makeBulkString("*"), 3);
                    int target=master_offset.load();
                    if (target==0)
                    {
                        wr=makeInteger(std::to_string(replica_fds.size()));
                    }
                    else
                    {
                        {
                            std::lock_guard<std::mutex> lock(replicas_write_mutex);
                            for (int fd : replica_fds)
                                write (fd, wr2.c_str(), wr2.size());
                        }
                        master_offset+=wr2.size();
                        auto countR = [&] ()
                        {
                            int finnished=0;
                            for (auto [fd, offset] : replica_offset)
                                if (offset>=target)
                                {
                                    finnished++;
                                }
                            return finnished;
                        };
                        auto deadline=std::chrono::system_clock::now()+std::chrono::milliseconds(time);
                        std::unique_lock<std::mutex> lock(ack_mutex);
                        ack_cv.wait_until(lock, deadline, [&] {return countR()>=nr;});
                        lock.unlock();
                        wr=makeInteger (std::to_string(countR()));
                    }
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "PSYNC")
                {
                    wr=makeSimpleString("FULLRESYNC "+replication_id+" 0");
                    replica_initialized=1;
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "REPLCONF")
                {
                    RESPToken arg = std::any_cast<std::vector<RESPToken>>(token.value)[i+1];
                    if (arg.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(arg.value)) == "GETACK")
                        wr_exception=1, wr=makeArray (makeBulkString ("REPLCONF")+makeBulkString ("ACK")+makeBulkString(std::to_string(bytes_processed-37)), 3);
                    else if (arg.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(arg.value)) == "ACK")
                    {
                        replica_offset[client_fd]=atoi(std::any_cast<std::string>(std::any_cast<std::vector<RESPToken>>(token.value)[i+2].value).c_str());
                        ack_cv.notify_all();
                    }
                    else
                        wr=makeSimpleString ("OK");

                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "INFO")
                {
                    std::string replication=std::string("# Replication\nrole:")+(is_replica?"slave\n":"master\n")+"master_replid:"+replication_id+"\nmaster_repl_offset:"+std::to_string(offset);
                    wr=makeBulkString (replication);
                    wr_exception=1;
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
                    wr_exception=1;
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
                        wr_exception=1;
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
                    wr_exception=1;
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
                    updateReplicaStatus();
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
                    wr_exception=1;
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
                    updateReplicaStatus();
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
                    updateReplicaStatus();
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
                    updateReplicaStatus();
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
                    wr_exception=1;
                    RESPToken name = std::any_cast<std::vector<RESPToken>>(token.value)[i + 1];
                    wr = makeInteger(std::to_string(lists[name].size()));
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "LPOP")
                {
                    updateReplicaStatus();
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
                    wr_exception=1;
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
                    updateReplicaStatus();
                    RESPToken name = std::any_cast<std::vector<RESPToken>>(token.value)[i + 1];
                    RESPToken value = std::any_cast<std::vector<RESPToken>>(token.value)[i + 2];
                    auto time_now = std::chrono::system_clock::now();
                    {
                        std::lock_guard lock(db_mutex);
                        variables[name] = value;
                        for (const auto &id : watching_clients[name])
                        {
                            if (watched_keys[id].find ({name, WATCHED})!=watched_keys[id].end())
                            {
                                watched_keys[id].erase ({name, WATCHED});
                                watched_keys[id].insert ({name, MODIFIED});
                            }
                        }
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
                    else
                    {
                        std::lock_guard lock(db_mutex);
                        variable_time.erase(name);
                    }
                    wr = makeSimpleString("OK");
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "PING")
                {
                    if (is_subscribed)
                        wr=makeArray(makeBulkString("pong")+makeBulkString(""), 2);
                    else
                        wr=makeSimpleString("PONG");
                }
                else if (element.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(element.value)) == "GET")
                {
                    wr_exception=1;
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
                else processCommand(element, take_output);
                if (!wr.empty())
                {
                    if (is_replica && !wr_exception)
                        continue;
                    if (take_output)
                        ++sz_outputs, outputs+=wr;
                    else if (mode)
                    {
                        ssize_t n=write (client_fd, wr.c_str(), wr.size());
                        if (replica_fds.find(client_fd)!=replica_fds.end())
                            master_offset+=n, replica_offset[client_fd]+=n;
                    }
                }
            }
        }
    };
    auto isWriteCommand = [&] (RESPToken token)
    {
        std::string command = std::any_cast<std::string>(token.value);
        return (command=="XADD" || command=="SET" || command=="DEL" || command=="BLPOP" || command=="RPUSH" || command=="LPUSH" || command=="INCR");
    };
    auto appendToAOF = [&] (std::string input)
    {
        std::ofstream g (rdb_dir+"/"+rdb_appenddirname+"/"+rdb_appendfilename+".1.incr.aof", std::ios_base::app);
        g << input;
    };
    auto checkModQueue = [&] (const RESPToken& token)
    {
        auto clearWatchlist = [&] ()
        {
            for (const auto& [key, state] : watched_keys[client_id])
                watching_clients[key].erase (client_id);
            watched_keys[client_id].clear();
        };
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
                sz_outputs=0;
                outputs="";
                bool discard=0;
                for (const auto& [key, state] : watched_keys[client_id])
                    if (state==MODIFIED)
                        discard=1;
                if (!discard)
                {
                    for (const auto& tokens : commands)
                    {
                        if (isWriteCommand (std::any_cast<std::vector<RESPToken>>(tokens[0].value)[0]))
                        {
                            std::string wr="";
                            for (const auto& token : tokens)
                                wr+=token.RESP_string;
                            wr=makeArray(wr, tokens.size());
                            appendToAOF(wr);
                        }
                        for (const auto& token : tokens)
                            processCommand(token, 1);
                    }
                    wr=makeArray (outputs, sz_outputs);
                }
                else
                    wr="*-1\r\n";
                queued=0;
                commands.clear();
            }
            else
                wr=makeSimpleError("ERR EXEC without MULTI");
            clearWatchlist();
        }
        else if (command.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(command.value)) == "DISCARD")
        {
            ok=1;
            if (queued)
            {
                commands.clear();
                wr=makeSimpleString ("OK");
                queued=0;
            }
            else
                wr=makeSimpleError ("ERR DISCARD without MULTI");
            clearWatchlist();
        }
        else if (command.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(command.value)) == "WATCH")
        {
            ok=1;
            if (queued)
                wr=makeSimpleError ("ERR WATCH inside MULTI is not allowed");
            else
            {
                for (int i=1;i<std::any_cast<std::vector<RESPToken>>(token.value).size();i++)
                {
                    RESPToken key = std::any_cast<std::vector<RESPToken>>(token.value)[i];
                    watched_keys[client_id].insert ({key, WATCHED});
                    watching_clients[key].insert(client_id);
                }
                wr=makeSimpleString ("OK");
            }
        }
        else if (command.type == RESPType::BULK_STRING && allUpper(std::any_cast<std::string>(command.value)) == "UNWATCH")
        {
            wr=makeSimpleString ("OK");
            clearWatchlist();
        }
        if (is_replica)
            return ok;
        if (!wr.empty() && mode)
            write (client_fd, wr.c_str(), wr.size());
        return ok;
    };
    while (true)
    {
        char *buff;
        buff = (char *)malloc(sizeof(char));
        buff[0]='\0';
        ssize_t nr_bytes=0;
        if (mode==1)
        {
            buff = (char *)realloc(buff, sizeof(char)*4096);
            memset (buff, 0, sizeof(char)*4096);
            nr_bytes = read(client_fd, buff, sizeof(char)*4096);
        }
        else
        {
            buff = (char *)realloc(buff, sizeof(char)*(strlen(external_command)+1));
            strncpy (buff, external_command, strlen(external_command));
            buff[strlen(buff)]='\0';
            nr_bytes = strlen(external_command);
        }
        bytes_processed+=nr_bytes;
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
            if (isWriteCommand (std::any_cast<std::vector<RESPToken>>(tokens[0].value)[0]))
                appendToAOF(buff);
            for (const auto& token : tokens)
            {
                processCommand(token, 0);
            }
        }
        if (replica_initialized)
        {
            FILE *f;
            std::string wr="";
            if (f=fopen ("./empty.rdb", "r"))
            {
                char buff[4096]={0};
                while (fread (buff, 1, 4096, f))
                    wr.append (buff);
                wr=makeBulkString (wr);
                wr.pop_back();
                wr.pop_back();
                ssize_t n=write (client_fd, wr.c_str(), wr.size());
                replica_fds.insert (client_fd);
            }
            else
            {
                std::cout << "Failed to open database!";
                exit (1);
            }
            replica_initialized=0;
            replicas_synced++;
            replica_synced[client_fd]=1;
        }
        free (buff);
        if (!mode)
            break;
    }
    if (mode)
    {
        replica_fds.erase (client_fd);
        close(client_fd);
        ids.insert (client_id);
    }
}
std::string readUntil (int fd, std::string final)
{
    std::string input="";
    while (true)
    {
        char buff[1]={0};
        read (fd, &buff, 1);
        input.append (buff);
        if (input.size()>=final.size() && input.substr (input.size()-final.size(), final.size())==final)
            break;
    }
    return input;
}
std::string readBytes (int fd, int nr)
{
    std::string ret="";
    while (nr--)
    {
        char buff[1]={0};
        read (fd, &buff, 1);
        ret.append (buff);
    }
    return ret;
}
int main(int argc, char** argv)
{
    int port=6379;
    // Flush after every std::cout / std::cerr
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    int last_client=-1;
    auto nextId = [&] ()
    {
        if (ids.empty())
        {
            ++last_client;
            ids.insert (last_client);
            watched_keys.push_back ({});
        }
        auto id=*ids.begin();
        ids.erase(ids.begin());
        return id;
    };
    if (argc>=3)
    {
        for (int i=0;i<argc;i++)
        {
            int n=strlen (argv[i]);
            if (n>=2 && argv[i][0]=='-' && argv[i][1]=='-')
            {
                int ptr=0;
                std::string flag = nextWord (ptr, argv[i], "- \n");
                if (flag=="port")
                {
                    port = atoi (argv[i+1]);
                }
                else if (flag=="replicaof")
                {
                    is_replica=1;
                    int master_fd = socket (AF_INET, SOCK_STREAM, 0);
                    int ptr=0;
                    std::string master_addr_ipv4 = nextWord (ptr, argv[i+1], " ");
                    std::string master_port = nextWord (ptr, argv[i+1], " ");
                    struct sockaddr_in master_addr;
                    master_addr.sin_family = AF_INET;
                    master_addr.sin_addr.s_addr = inet_addr (master_addr_ipv4=="localhost"?"127.0.0.1":master_addr_ipv4.c_str());
                    master_addr.sin_port = htons (atoi(master_port.c_str()));
                    if (connect (master_fd, (struct sockaddr*)&master_addr, sizeof(master_addr)) != 0)
                    {
                        std::cout << "Failed to connect to master";
                        return 1;
                    }
                    std::string wr=makeArray (makeBulkString ("PING"), 1);
                    write (master_fd, wr.c_str(), wr.size());
                    char input[4096]={0};
                    if (ssize_t n=read (master_fd, &input, sizeof(input))!=0 && strcmp(input, "+PONG\r\n")==0)
                    {
                        wr=makeArray (makeBulkString ("REPLCONF")+makeBulkString("listening-port")+makeBulkString(std::to_string(port)), 3);
                        write (master_fd, wr.c_str(), wr.size());
                        memset (input, 0, sizeof(input));
                        if (ssize_t n=read (master_fd, &input, sizeof(input))!=0 && strcmp(input, "+OK\r\n")==0)
                        {
                            wr=makeArray (makeBulkString ("REPLCONF")+makeBulkString ("capa")+makeBulkString("psync2"), 3);
                            write (master_fd, wr.c_str(), wr.size());
                            memset (input, 0, sizeof(input));
                            if (ssize_t n=read (master_fd, &input, sizeof(input))!=0 && strcmp(input, "+OK\r\n")==0)
                            {
                                wr=makeArray (makeBulkString ("PSYNC")+makeBulkString ("?")+makeBulkString("-1"), 3);
                                write(master_fd, wr.c_str(), wr.size());
                                memset (input, 0, sizeof(input));
                                /// firstly we need to read the fullresync response...
                                std::string response = readUntil (master_fd, "\r\n");
                                /// then we read the (empty) db...
                                std::string rdb = readUntil (master_fd, "\r\n");
                                int sz=atoi(rdb.substr(1, rdb.size()-3).c_str());
                                rdb+=readBytes (master_fd, sz);
                                std::thread master_connection(handleClient, master_fd, nextId(), 1);
                                master_connection.detach();
                            }
                        }
                    }
                }
                else if (flag=="dir")
                    rdb_dir = argv[i+1];
                else if (flag=="dbfilename")
                    rdb_name = argv[i+1];
                else if (flag=="appendonly")
                    rdb_appendonly = argv[i+1];
                else if (flag=="appenddirname")
                    rdb_appenddirname = argv[i+1];
                else if (flag=="appendfilename")
                    rdb_appendfilename = argv[i+1];
                else if (flag=="appendfsync")
                    rdb_appendfsync = argv[i+1];
            }
        }
    }
    if (rdb_appendonly=="yes")
    {
        struct stat flags;
        if (stat ((rdb_dir+"/"+rdb_appenddirname).c_str(), &flags)!=0)
            std::cout << "cannot access rdb_dir/rdb_appenddirname\n";
        if (flags.st_mode & S_IFDIR)
        {
            std::ifstream f (rdb_dir+"/"+rdb_appenddirname+"/"+rdb_appendfilename+".manifest");
            std::string line;
            while (getline(f, line))
            {
                int ptr=0;
                bool found=0;
                while (ptr<line.size())
                {
                    std::string arg = nextWord (ptr, line, " ");
                    if (arg=="type")
                    {
                        std::string type = nextWord (ptr, line, " ");
                        if (type=="i")
                        {
                            found=1;
                            break;
                        }
                    }
                }
                if (found)
                {
                    ptr=0;
                    std::string name=nextWord (ptr, line, " ");
                    name=nextWord (ptr, line, " ");
                    FILE *f;
                    f=fopen ((rdb_dir+"/"+rdb_appenddirname+"/"+name).c_str(), "r");
                    char buff[4096]={0};
                    free (external_command);
                    external_command = (char *)malloc (sizeof(char));
                    external_command[0]='\0';
                    while (fread (buff, sizeof(*buff), sizeof(buff), f));
                    {
                        external_command = (char *)realloc (external_command, sizeof(char)*(strlen(external_command)+strlen(buff)+1));
                        if (strlen (external_command) == 0)
                            strncpy (external_command, buff, strlen(buff));
                        else
                            strncat (external_command, buff, strlen(buff));
                        external_command[strlen(external_command)]='\0';
                    }
                    handleClient (1, 1, 0);
                    free (external_command);
                    break;
                }
            }
        }
        else
        {
            system (("mkdir "+rdb_dir+"/"+rdb_appenddirname).c_str());
            std::ofstream f (rdb_dir+"/"+rdb_appenddirname+"/"+rdb_appendfilename+".1.incr.aof");
            std::ofstream man (rdb_dir+"/"+rdb_appenddirname+"/"+rdb_appendfilename+".manifest");
            man << "file " << rdb_appendfilename << ".1.incr.aof seq 1 type i\n";
        }
    }
    if (!rdb_name.empty())
    {
        FILE *f;
        if (f=fopen ((rdb_dir+"/"+rdb_name).c_str(), "rb"))
        {
            std::string input;
            char buff[4096]={0};
            unsigned long n;
            while (n=fread (&buff, sizeof(*buff), sizeof(buff), f))
                input.append(buff, n);
            int ptr=0;
            bool started=0;
            unsigned long time=-1;
            for (int i=0;i<input.size();i++)
            {
                unsigned char ch=input[i];
                if ((int)ch==0xfb)
                {
                    i+=2;
                    started=1;
                }
                if (started)
                {
                    if ((int)ch==0xff)
                        started=0;
                    else if ((int)ch==0x00)
                    {
                        int len = input[i+1];
                        std::string name;
                        i+=2;
                        while (len--)
                            name+=input[i++];
                        len = input[i++];
                        std::string value;
                        while (len--)
                            value+=input[i++];
                        if (time!=-1)
                        {
                            auto time_point=std::chrono::system_clock::now();
                            if (time>=std::chrono::duration_cast<std::chrono::milliseconds>(time_point.time_since_epoch()).count())
                            {
                                variables[{RESPType::BULK_STRING, name, makeBulkString(name)}]={RESPType::BULK_STRING, value, makeBulkString(value)};
                                variable_time[{RESPType::BULK_STRING, name, makeBulkString(name)}]={time_point, time-std::chrono::duration_cast<std::chrono::milliseconds>(time_point.time_since_epoch()).count()};
                            }
                            time=-1;
                        }
                        else
                            variables[{RESPType::BULK_STRING, name, makeBulkString(name)}]={RESPType::BULK_STRING, value, makeBulkString(value)};
                        i--;
                    }
                    else if ((int)ch==0xfc)
                    {
                        unsigned long p=1;
                        time=0;
                        for (int j=i+1;j<=i+8;j++, p*=256)
                            time+=p*(int)(unsigned char)input[j];
                        i+=8;
                    }
                    else if ((int)ch==0xfd)
                    {
                        unsigned long p=1;
                        time=0;
                        for (int j=i+1;j<=i+4;j++, p*=256)
                            time+=p*(int)(unsigned char)input[j];
                        time*=1000;
                        i+=4;
                    }
                }
            }
        }
    }
    subscribed_allowed.insert ("SUBSCRIBE");
    subscribed_allowed.insert ("PSUBSCRIBE");
    subscribed_allowed.insert ("UNSUBSCRIBE");
    subscribed_allowed.insert ("PING");
    subscribed_allowed.insert ("QUIT");
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
    server_addr.sin_port = htons(port);
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0)
    {
        std::cerr << "Failed to bind to port " << port << "\n";
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
    std::cout << "Current port: " << ntohs(server_addr.sin_port) << "\n";
    while (true)
    {
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, (socklen_t*)&client_addr_len);
        std::cout << "Client connected from port " << ntohs(client_addr.sin_port) << "\n";
        std::thread client(handleClient, client_fd, nextId(), 1);
        client.detach();
    }
    close(server_fd);
    return 0;
}
