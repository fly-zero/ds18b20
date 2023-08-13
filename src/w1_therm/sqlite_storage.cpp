#include <syslog.h>

#include <cassert>

#include <stdexcept>

#ifdef _DEBUG_
#include <iostream>
#endif

#include "sqlite_storage.h"

#ifndef likely
# define likely(x) (__builtin_expect(!!(x), 1))
#endif

#ifndef unlikely
# define unlikely(x) (__builtin_expect(!!(x), 0))
#endif

sqlite_storage::sqlite_storage(const char * path)
{
	assert(path);

	sqlite3 * handle{ nullptr };
	if (sqlite3_open(path, &handle) != SQLITE_OK)
	{
		syslog(LOG_USER | LOG_ERR, "Cannot initialize SQLite\n");
		sqlite3_close(handle);
		throw std::runtime_error{ "Cannot initialize SQLite" };
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
		throw std::runtime_error{ "Cannot create SQLite table" };
	}

	db_.reset(handle);

	syslog(LOG_USER | LOG_INFO, "SQLite is initialized!\n");
}

sqlite_storage::~sqlite_storage()
{
	sqlite3_close(db_.release());
	syslog(LOG_USER | LOG_INFO, "Database is closed!\n");
}

void sqlite_storage::insert(const char * name, const double value, time_t now)
{
	char sql[256];
	auto const len = snprintf(sql, sizeof sql,
		"insert into tb_therm (name,therm,time) values ('%s',%f,%lu)", name, value, now);
	assert(len > 0 && static_cast<size_t>(len) < sizeof sql);
	(void)len;

	char * errmsg;
	auto const err = sqlite3_exec(db_.get(), sql, nullptr, nullptr, &errmsg);
	if unlikely(err != SQLITE_OK)
	{
		syslog(LOG_USER | LOG_ERR, "Cannot insert record: %s\n", errmsg);
		sqlite3_free(errmsg);
	}
	else
	{
#ifdef _DEBUG_
		std::cerr << "new record: " << name << ',' << value << ',' << now << std::endl;
#endif
	}
}