#include <unistd.h>

#include <cassert>
#include <csignal>
#include <cstdlib>
#include <ctime>

#include <chrono>
#include <iostream>
#include <thread>

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

static int w1_slave_read(const char * path)
{
	(void)path;
	// TODO: read w1_slave
	return 0;
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
#ifdef _DEBUG_
		std::cerr << "new record: " << name << ',' << therm << ',' << now << std::endl;
#endif
	}
}

static void w1_therm_run(sqlite3 * handle, const char * path, const char * name)
{
	std::cerr << "w1_therm is started!" << std::endl;

	s_running = true;

	while (s_running)
	{
		auto const therm = w1_slave_read(path);
		auto const now = time(nullptr);
		insert_record(handle, name, therm, now);
		std::this_thread::sleep_for(std::chrono::minutes{ 5 });
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
