#include <string>
#include <syslog.h>
#include <unistd.h>
#include <wait.h>

#include <cassert>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
#include <memory>
#include <thread>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <type_traits>

#include <boost/container/static_vector.hpp>
#include <boost/static_string.hpp>

#include "influx_storage.h"
#include "sqlite_storage.h"

#ifndef likely
# define likely(x) (__builtin_expect(!!(x), 1))
#endif

#ifndef unlikely
# define unlikely(x) (__builtin_expect(!!(x), 0))
#endif

/**
 * @brief config for sqlite database
 */
struct sqlite_config
{
    std::string path_{ }; ///< path to sqlite database
};

/**
 * @brief config for influx database
 */
struct influx_config
{
    std::string host_{ };   ///< host of influxdb
    std::string org_{ };    ///< organization of influxdb
    std::string bucket_{ }; ///< bucket of influxdb
    std::string token_{ };  ///< token of influxdb
};

/**
 * @brief global config
 */
struct therm_config
{
    std::string   w1_slave_path_{ };    ///< path to w1_slave
    std::string   senor_name_{ };       ///< name of senor
    bool          daemonlize_{ false }; ///< daemonlize if set
    sqlite_config sqlite_db_{ };        ///< config for sqlite database
    influx_config influx_db_{ };        ///< config for influx database
};

struct storage_t
{
    storage_t(sqlite_storage && sqlite, influx_storage && influx)
        : sqlite_{ std::move(sqlite) }
        , influx_{ std::move(influx) }
    { }

    void insert(const char * name, double value, time_t now);

    size_t sqlite_count_{0}; ///< 执行 sqlite 插入的次数，不代表 sqlite 中的记录数
    sqlite_storage sqlite_;
    influx_storage influx_;
};

static auto s_running = false;

void storage_t::insert(const char * name, double value, time_t now)
{
    try
    {
        if (sqlite_count_ == 0)
        {
            influx_.insert(name, value, now);
            return;
        }

        ++sqlite_count_;
        sqlite_.insert(name, value, now);

        if ((sqlite_count_ % 10) != 0 || !influx_.is_bucket_exists())
        {
            return;
        }

        for (std::string data; ; data.clear())
        {
            long id;
            using user_data_t = std::tuple<influx_storage *, std::string *, long *>;
            user_data_t user{ &influx_, &data, &id };

            auto const callback = [](void * user, int argc, char ** argv, char ** col) -> int
            {
                assert(argc == 4);
                assert(col[0] == std::string_view{ "id" });
                assert(col[1] == std::string_view{ "name" });
                assert(col[2] == std::string_view{ "therm" });
                assert(col[3] == std::string_view{ "time" });
                auto const id = std::atol(argv[0]);
                auto const name = argv[1];
                auto const value = std::atof(argv[2]);
                auto const time = std::atol(argv[3]);
                auto const [influx, data, pid] = *static_cast<user_data_t *>(user);
                assert(*pid < id);
                *pid = id;
                influx->prepare_data(*data, name, value, time);
                return 0;
            };

            sqlite_.select(200, callback, &user);
            if (data.empty())
            {
                break;
            }

            influx_.insert(data);
            sqlite_.delete_where_id_not_greater_than(id);
            sqlite_count_ = 0;
        }
    }
    catch (influx_storage::runtime_error const & e)
    {
        syslog(LOG_USER | LOG_ERR, "influx error: %s\n", e.what());
    }
    catch (sqlite_storage::runtime_error const & e)
    {
        syslog(LOG_USER | LOG_ERR, "sqlite error: %s\n", e.what());
    }
    catch (std::exception const & e)
    {
        syslog(LOG_USER | LOG_ERR, "Unknown error: %s\n", e.what());
    }
}

inline void init_signal_handle()
{
    auto const handle = [](int){ s_running = false; };
    std::signal(SIGTERM, handle);
    std::signal(SIGINT, handle);
}

inline std::optional<int> w1_slave_read(const char * path)
{
    std::optional<int> ret;
    char buf[256];

    do
    {
        auto const destroyer = [](FILE * fp) { fclose(fp); };
        std::unique_ptr<FILE, decltype(destroyer)> const fp{ fopen(path, "r"), destroyer };
        if (!fp) break;

        std::string_view line;
        std::string_view const flag{ "t=" };
        char *endptr;
        int len;

        if (fscanf(fp.get(), "%[^\n]%n", buf, &len) != 1) break;

        line = std::string_view{ buf, static_cast<size_t>(len) };
        if (!line.ends_with("YES")) break;

        fgetc(fp.get()); // skip '\n'

        if (fscanf(fp.get(), "%[^\n]%n", buf, &len) != 1) break;

        line = std::string_view{ buf, static_cast<size_t>(len) };
        auto const pos = line.rfind(flag);
        if (pos == std::string_view::npos) break;

        auto const t = line.substr(pos + flag.size());
        auto const n = strtol(t.data(), &endptr, 10);
        auto const ok =
            endptr == t.end() &&
            std::numeric_limits<int>::min() <= n &&
            n <= std::numeric_limits<int>::max();
        if (!ok) break;
        ret = static_cast<int>(n);
        return ret;
    } while (false);

    syslog(LOG_USER | LOG_ERR, "Cannot parse w1_slave, data sample\n");
    syslog(LOG_USER | LOG_ERR, "%s\n", buf);
    return ret;
}

inline void w1_therm_run(storage_t & storage, const therm_config & config)
{
    syslog(LOG_USER | LOG_INFO, "w1_therm is started!\n");

    s_running = true;

    std::chrono::minutes constexpr interval{ 5 };
    std::chrono::steady_clock::time_point next_read_time;

    while (s_running)
    {
        auto const now = std::chrono::steady_clock::now();
        if (next_read_time <= now)
        {
            next_read_time = now + interval;
            auto const therm = w1_slave_read(config.w1_slave_path_.c_str());
            if likely(therm)
            {
                auto const utc_now = time(nullptr);
                storage.insert(config.senor_name_.c_str(), *therm / double(1000), utc_now);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{ 250 });
    }

    syslog(LOG_USER | LOG_INFO, "w1_therm is stopped!\n");
}

inline storage_t init_storage(const therm_config & config)
{
    sqlite_storage sqlite{config.sqlite_db_.path_.c_str()};
    influx_storage influx{config.influx_db_.host_,
                          config.influx_db_.org_,
                          config.influx_db_.bucket_,
                          config.influx_db_.token_,
                          "home",
                          "temperature"};
    syslog(LOG_USER | LOG_INFO, "sqlite3 and influxdb are initialized!\n");
    return { std::move(sqlite), std::move(influx) };
}

inline void init_influx_config(therm_config & config, const char * str)
{
    // valid settings: "host/org/bucket/token"
    assert(str);

    // set host
    auto bgn = str;
    auto end = strchr(bgn, '/');
    if (!end)
        throw std::invalid_argument{ "invalid db settings" };
    config.influx_db_.host_.assign(bgn, static_cast<size_t>(end - bgn));

    // set org
    bgn = end + 1;
    end = strchr(bgn, '/');
    if (!end)
        throw std::invalid_argument{ "invalid db settings" };
    config.influx_db_.org_.assign(bgn, static_cast<size_t>(end - bgn));

    // set bucket
    bgn = end + 1;
    end = strchr(bgn, '/');
    if (!end)
        throw std::invalid_argument{ "invalid db settings" };
    config.influx_db_.bucket_.assign(bgn, static_cast<size_t>(end - bgn));

    // set token
    bgn = end + 1;
    if (*bgn == 0)
        throw std::invalid_argument{ "invalid db settings" };
    config.influx_db_.token_.assign(bgn);
}

inline void load_config_file(therm_config & config, const char * path)
{
    // demo config file:
    // sqlite w1_therm.db
    // influx host/org/bucket/token

    assert(path);
    auto const file_deleter = [](FILE * fp) { fclose(fp); };
    std::unique_ptr<FILE, decltype(file_deleter)> fp{ fopen(path, "r"), file_deleter };
    if (!fp)
        throw std::runtime_error{ "Cannot open config file" };

    char buf[256];
    while (fgets(buf, sizeof buf, fp.get()))
    {
        auto const len = strlen(buf);
        if (len == 0 || buf[len - 1] != '\n')
            throw std::runtime_error{ "Invalid config file" };
        buf[len - 1] = 0;

        if (strncmp(buf, "sqlite ", 7) == 0)
            config.sqlite_db_.path_.assign(buf + 7);
        else if (strncmp(buf, "influx ", 7) == 0)
            init_influx_config(config, buf + 7);
        else
            throw std::runtime_error{ "Invalid config file" };
    }
}

inline therm_config parse_arguments(int const argc, char ** argv)
{
    therm_config config;

    try
    {
        if (argc < 3)
            throw std::invalid_argument{ "too few argument" };

        constexpr auto * opts{ "p:n:c:d" };

        for (int r; (r = getopt(argc, argv, opts)) != -1; )
        {
            switch(r)
            {
            case 'p':
                config.w1_slave_path_ = optarg;
                break;

            case 'n':
                config.senor_name_ = optarg;
                break;

            case 'd':
                config.daemonlize_ = true;
                break;

            case 'c':
                load_config_file(config, optarg);
                break;

            default:
                throw std::invalid_argument{ "invalid argument" };
            }
        }

        if (config.senor_name_.empty() || config.senor_name_.empty())
            throw std::invalid_argument{ "invalid argument" };
    }
    catch (std::invalid_argument const &)
    {
        std::cerr
            << "usage: " << argv[0] << " [options] -p <path> -n <name>" << std::endl
            << '\t' << "-p <path>" << '\t' << "Set the w1_slave path" << std::endl
            << '\t' << "-n <name>" << '\t' << "Set the senor name" << std::endl
            << '\t' << "-d       " << '\t' << "daemonlize if set" << std::endl;
        exit(EXIT_FAILURE);
    }

    return config;
}

inline void init_log(const char * arg0)
{
    std::string_view tmp{ arg0 };
    auto const pos = tmp.rfind('/');
    if (pos != std::string_view::npos)
        tmp = tmp.substr(pos + 1);
    openlog(tmp.data(), LOG_PERROR | LOG_PID | LOG_NDELAY, LOG_USER);
}

inline void deinit_log()
{
    closelog();
}

inline void daemonlize()
{
    if (daemon(1, 0))
    {
        syslog(LOG_USER | LOG_ERR, "Cannot daemonlize\n");
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char ** argv)
{
    auto const config = parse_arguments(argc, argv);

    if (config.daemonlize_)
        daemonlize();

    init_log(argv[0]);

    init_signal_handle();

    auto storage = init_storage(config);

    w1_therm_run(storage, config);

    deinit_log();
}
