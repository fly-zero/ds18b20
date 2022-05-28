#include <unistd.h>

#include <cassert>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include <chrono>
#include <iostream>
#include <limits>
#include <memory>
#include <thread>
#include <optional>
#include <string_view>

#include <sqlite3.h>

#ifndef likely
# define likely(x) (__builtin_expect(!!(x), 1))
#endif

#ifndef unlikely
# define unlikely(x) (__builtin_expect(!!(x), 0))
#endif

static auto s_running = false;

static void init_signal_handle()
{
	auto const handle = [](int){ s_running = false; };
	std::signal(SIGTERM, handle);
	std::signal(SIGINT, handle);
}

std::optional<int> w1_slave_read(const char * path)
{
	std::optional<int> ret;
	auto const destroyer = [](FILE * fp) { fclose(fp); };
	std::unique_ptr<FILE, decltype(destroyer)> const fp{ fopen(path, "r"), destroyer };
	if (!fp) return ret;

	do
	{
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

	std::cerr << "Cannot parse w1_slave" << std::endl;
	return ret;
}

static void insert_record(sqlite3 * handle, const char * name, int therm, time_t now)
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
		std::cerr << "Cannot insert record: " << errmsg << std::endl;
		sqlite3_free(errmsg);
	}
	else
	{
		std::cerr << "new record: " << name << ',' << therm << ',' << now << std::endl;
	}
}

static void w1_therm_run(sqlite3 * handle, const char * path, const char * name)
{
	std::cerr << "w1_therm is started!" << std::endl;

	s_running = true;

	std::chrono::minutes const interval{ 5 };
	std::chrono::steady_clock::time_point next_read_time;

	while (s_running)
	{
		auto const now = std::chrono::steady_clock::now();
		if (next_read_time <= now)
		{
			next_read_time = now + interval;
			auto const therm = w1_slave_read(path);
			if likely(therm)
			{
				auto const utc_now = time(nullptr);
				insert_record(handle, name, *therm, utc_now);
			}
		}

		std::this_thread::sleep_for(std::chrono::milliseconds{ 250 });
	}

	std::cerr << "w1_therm is stoped!" << std::endl;
}

static sqlite3 * init_sqlite()
{
	sqlite3 * handle{ nullptr };
	if (sqlite3_open("w1_therm.db", &handle) != SQLITE_OK)
	{
		sqlite3_close(handle);
		std::cerr << "Cannot initialize SQLite" << std::endl;
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
		std::cerr << "Cannot create SQLite table: " << errmsg << std::endl;
		sqlite3_close(handle);
		exit(EXIT_FAILURE);
	}

	std::cerr << "Database is opened!" << std::endl;
	return handle;
}

static void cleanup_sqlite(sqlite3 * handle)
{
	sqlite3_close(handle);
	std::cerr << "Database is closed!" << std::endl;
}

static void check_arguments(int const argc, char ** argv)
{
	if (argc < 3)
	{
		std::cerr << "usage: " << argv[0] << " <w1_slave_path> <name>" << std::endl;
		exit(EXIT_FAILURE);
	}
}

int main(int argc, char ** argv)
{
	check_arguments(argc, argv);

	init_signal_handle();

	auto const handle = init_sqlite();
	auto const path = argv[1];
	auto const name = argv[2];
	w1_therm_run(handle, path, name);

	cleanup_sqlite(handle);
}
