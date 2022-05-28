#include <syslog.h>
#include <unistd.h>
#include <wait.h>

#include <cassert>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <chrono>
#include <iostream>
#include <limits>
#include <memory>
#include <thread>
#include <optional>
#include <stdexcept>
#include <string_view>

#include <sqlite3.h>

#ifndef likely
# define likely(x) (__builtin_expect(!!(x), 1))
#endif

#ifndef unlikely
# define unlikely(x) (__builtin_expect(!!(x), 0))
#endif

struct therm_config
{
	const char * path_{ nullptr };
	const char * name_{ nullptr };
	bool         daemonlize_{ false };
};

static auto s_running = false;

inline void init_signal_handle()
{
	auto const handle = [](int){ s_running = false; };
	std::signal(SIGTERM, handle);
	std::signal(SIGINT, handle);
}

inline std::optional<int> w1_slave_read(const char * path)
{
	std::optional<int> ret;

	do
	{
		auto const destroyer = [](FILE * fp) { fclose(fp); };
		std::unique_ptr<FILE, decltype(destroyer)> const fp{ fopen(path, "r"), destroyer };
		if (!fp) break;

		std::string_view line;
		std::string_view const flag{ "t=" };
		char buf[256], *endptr;
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

	syslog(LOG_USER | LOG_ERR, "Cannot parse w1_slave\n");
	return ret;
}

inline void insert_record(sqlite3 * handle, const char * name, int therm, time_t now)
{
	char sql[256];
	auto const len = snprintf(sql, sizeof sql,
		"insert into tb_therm (name,therm,time) values ('%s',%d,%lu)", name, therm, now);
	assert(len > 0 && static_cast<size_t>(len) < sizeof sql);
	(void)len;

	char * errmsg;
	auto const err = sqlite3_exec(handle, sql, nullptr, nullptr, &errmsg);
	if unlikely(err != SQLITE_OK)
	{
		syslog(LOG_USER | LOG_ERR, "Cannot insert record: %s\n", errmsg);
		sqlite3_free(errmsg);
	}
	else
	{
#ifdef _DEBUG_
		std::cerr << "new record: " << name << ',' << therm << ',' << now << std::endl;
#endif
	}
}

inline void w1_therm_run(sqlite3 * handle, const therm_config & config)
{
	if (config.daemonlize_ && daemon(1, 0))
	{
		syslog(LOG_USER | LOG_ERR, "Cannot daemonlize\n");
		exit(EXIT_FAILURE);
	}

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
			auto const therm = w1_slave_read(config.path_);
			if likely(therm)
			{
				auto const utc_now = time(nullptr);
				insert_record(handle, config.name_, *therm, utc_now);
			}
		}

		std::this_thread::sleep_for(std::chrono::milliseconds{ 250 });
	}

	syslog(LOG_USER | LOG_INFO, "w1_therm is stopped!\n");
}

inline sqlite3 * init_sqlite()
{
	sqlite3 * handle{ nullptr };
	if (sqlite3_open("w1_therm.db", &handle) != SQLITE_OK)
	{
		syslog(LOG_USER | LOG_ERR, "Cannot initialize SQLite\n");
		sqlite3_close(handle);
		exit(EXIT_FAILURE);
	}

	auto const sql =
		"create table if not exists tb_therm("
			"id    integer  primary key autoincrement,"
			"name  text(64) not null,"
			"therm integer  not null,"
			"time  integer  not null"
		")";

	char * errmsg{ nullptr };
	auto const err = sqlite3_exec(handle, sql, nullptr, nullptr, &errmsg);
	if (err != SQLITE_OK)
	{
		syslog(LOG_USER | LOG_ERR, "Cannot create SQLite table: %s\n", errmsg);
		sqlite3_close(handle);
		exit(EXIT_FAILURE);
	}

	syslog(LOG_USER | LOG_INFO, "Database is opened!\n");
	return handle;
}

inline void cleanup_sqlite(sqlite3 * handle)
{
	sqlite3_close(handle);
	syslog(LOG_USER | LOG_INFO, "Database is closed!\n");
}

inline therm_config parse_arguments(int const argc, char ** argv)
{
	therm_config config;

	try
	{
		if (argc < 3)
			throw std::invalid_argument{ "too few argument" };

		constexpr auto * opts{ "p:n:d" };

		for (int r; (r = getopt(argc, argv, opts)) != -1; )
		{
			switch(r)
			{
			case 'p':
				config.path_ = optarg;
				break;

			case 'n':
				config.name_ = optarg;
				break;

			case 'd':
				config.daemonlize_ = true;
				break;

			default:
				throw std::invalid_argument{ "invalid argument" };
			}
		}

		if (!config.name_ || !config.name_)
			throw std::invalid_argument{ "invalid argument" };
	}
	catch (std::invalid_argument const &)
	{
		std::cerr
			<< "usage: " << argv[0] << "[options] -p <path> -n <name>" << std::endl
			<< '\t' << "-p <path>" << '\t' << "Set the w1_slave path" << std::endl
			<< '\t' << "-n <name>" << '\t' << "Set the senor name" << std::endl
			<< "\t" << "-d \t"     << '\t' << "daemonlize if set" << std::endl;
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

int main(int argc, char ** argv)
{
	auto const config = parse_arguments(argc, argv);

	init_log(argv[0]);

	init_signal_handle();

	auto const handle = init_sqlite();

	w1_therm_run(handle, config);

	cleanup_sqlite(handle);

	deinit_log();
}
