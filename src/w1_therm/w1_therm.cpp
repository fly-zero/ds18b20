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
#include <type_traits>

#include "data_storage_base.h"
#include "influx_storage.h"
#include "sqlite_storage.h"

#ifndef likely
# define likely(x) (__builtin_expect(!!(x), 1))
#endif

#ifndef unlikely
# define unlikely(x) (__builtin_expect(!!(x), 0))
#endif

enum class storage_type : char
{
	sqlite,
	influxdb,
	unspecified,
};

struct sqlite_db_config
{
	std::string path_{ };
};

struct influx_db_config
{
	std::string host_{ };
	std::string org_{ };
	std::string bucket_{ };
	std::string token_{ };
};

using db_config_t = std::aligned_union_t
	< std::max(sizeof (sqlite_db_config), sizeof (influx_db_config))
	, sqlite_db_config
	, influx_db_config
	>;

struct therm_config
{
	std::string  path_{ };
	std::string  name_{ };
	storage_type type_{ storage_type::sqlite };
	bool         daemonlize_{ false };
	db_config_t  db_{ };
};

using storage_t = std::aligned_union_t
	< std::max({ sizeof (sqlite_storage), sizeof (influx_storage), sizeof(void *) })
	, sqlite_storage
	, influx_storage
	>;

static auto s_running = false;
static storage_t s_storage;

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

inline void w1_therm_run(data_storage_base * storage, const therm_config & config)
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
			auto const therm = w1_slave_read(config.path_.c_str());
			if likely(therm)
			{
				auto const utc_now = time(nullptr);
				storage->insert(config.name_.c_str(), *therm / double(1000), utc_now);
			}
		}

		std::this_thread::sleep_for(std::chrono::milliseconds{ 250 });
	}

	syslog(LOG_USER | LOG_INFO, "w1_therm is stopped!\n");
}

inline data_storage_base * init_sqlite(storage_t & storage, const therm_config & config)
{
	auto & db = reinterpret_cast<const sqlite_db_config &>(config.db_);
	auto const ret = new (&storage) sqlite_storage(db.path_.c_str());
	return ret;
}

inline data_storage_base * init_influxdb(storage_t & storage, const therm_config & config)
{
	auto & db = reinterpret_cast<const influx_db_config &>(config.db_);
	auto const ret = new (&storage) influx_storage(
		db.host_, db.org_, db.bucket_, db.token_, "home", "temperature");
	return ret;
}

inline data_storage_base * init_storage(storage_t & storage, const therm_config & config)
{
	switch (config.type_)
	{
	case storage_type::sqlite:
		return init_sqlite(storage, config);
	case storage_type::influxdb:
		return init_influxdb(storage, config);
	default:
		assert(false);
		return nullptr;
	}
}

inline void cleanup_data_storage(storage_t & storage)
{
	auto const ptr = reinterpret_cast<data_storage_base *>(&storage);
	ptr->~data_storage_base();
}

inline void init_database_config(therm_config & config, const char * str)
{
	// valid settings:
	//   "sqlite://path/to/db"
	//   "influxdb://host/org/bucket/token"

	assert(str);

	if (strncmp(str, "sqlite://", 9) == 0)
	{
		config.type_ = storage_type::sqlite;
		auto const db = new (&config.db_) sqlite_db_config;
		db->path_.assign(str + 9);
	}
	else if (strncmp(str, "influxdb://", 11) == 0)
	{
		config.type_ = storage_type::influxdb;
		auto const db = new (&config.db_) influx_db_config;

		// set host
		auto bgn = str + 11;
		auto end = strchr(bgn, '/');
		if (!end) throw std::invalid_argument{ "invalid db settings" };
		db->host_.assign(bgn, static_cast<size_t>(end - bgn));

		// set org
		bgn = end + 1;
		end = strchr(bgn, '/');
		if (!end) throw std::invalid_argument{ "invalid db settings" };
		db->org_.assign(bgn, static_cast<size_t>(end - bgn));

		// set bucket
		bgn = end + 1;
		end = strchr(bgn, '/');
		if (!end) throw std::invalid_argument{ "invalid db settings" };
		db->bucket_.assign(bgn, static_cast<size_t>(end - bgn));

		// set token
		bgn = end + 1;
		if (*bgn == 0) throw std::invalid_argument{ "invalid db settings" };
		db->token_.assign(bgn);
	}
	else
	{
		throw std::invalid_argument{ "invalid db settings" };
	}
}

inline therm_config parse_arguments(int const argc, char ** argv)
{
	therm_config config;

	try
	{
		if (argc < 3)
			throw std::invalid_argument{ "too few argument" };

		constexpr auto * opts{ "p:n:b:d" };

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

			case 'b':
				init_database_config(config, optarg);
				break;

			default:
				throw std::invalid_argument{ "invalid argument" };
			}
		}

		if (config.name_.empty() || config.name_.empty())
			throw std::invalid_argument{ "invalid argument" };

		// set default database
		if (config.type_ == storage_type::unspecified)
			init_database_config(config, "sqlite://w1_therm.db");
	}
	catch (std::invalid_argument const &)
	{
		std::cerr
			<< "usage: " << argv[0] << " [options] -p <path> -n <name>" << std::endl
			<< '\t' << "-p <path>" << '\t' << "Set the w1_slave path" << std::endl
			<< '\t' << "-n <name>" << '\t' << "Set the senor name" << std::endl
			<< '\t' << "-b <db>  " << '\t' << "Set the database, must be sqlite(default) or influxdb" << std::endl
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

	auto const storage = init_storage(s_storage, config);

	w1_therm_run(storage, config);

	cleanup_data_storage(s_storage);

	deinit_log();
}
