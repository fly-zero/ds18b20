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
		sqlite3_close(handle);
		throw std::runtime_error{ "Cannot create SQLite table" };
	}

	db_.reset(handle);
}

sqlite_storage::~sqlite_storage()
{
	sqlite3_close(db_.release());
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
		throw std::runtime_error{ "Cannot insert record: " + std::string{ errmsg } };
		sqlite3_free(errmsg);
	}
	else
	{
#ifdef _DEBUG_
		std::cerr << "new record: " << name << ',' << value << ',' << now << std::endl;
#endif
	}
}

void sqlite_storage::select(size_t count, int (*callback)(void*,int,char**,char**), void * user)
{
	char sql[256];
	auto const len = snprintf(sql, sizeof sql, "select * from tb_therm limit %zu", count);
	assert(len > 0 && static_cast<size_t>(len) < sizeof sql);
	(void)len;

	char * errmsg;
	auto const err = sqlite3_exec(db_.get(), "select * from tb_therm", callback, user, &errmsg);
	if unlikely(err != SQLITE_OK)
	{
		throw std::runtime_error{ "Cannot select records: " + std::string{ errmsg } };
		sqlite3_free(errmsg);
	}
}

void sqlite_storage::delete_where_id_not_greater_than(size_t id)
{
	char sql[256];
	auto const len = snprintf(sql, sizeof sql, "delete from tb_therm where id <= %zu", id);
	assert(len > 0 && static_cast<size_t>(len) < sizeof sql);
	(void)len;

	char * errmsg;
	auto const err = sqlite3_exec(db_.get(), sql, nullptr, nullptr, &errmsg);
	if unlikely(err != SQLITE_OK)
	{
		throw std::runtime_error{ "Cannot delete records: " + std::string{ errmsg } };
		sqlite3_free(errmsg);
	}
}
