#pragma once

#include <memory>

#include <sqlite3.h>

class sqlite_storage
{
public:
    struct runtime_error;

private:
    struct deleter
    {
        void operator()(sqlite3 * db) const;
    };

    using sqlite3_ptr = std::unique_ptr<sqlite3, deleter>;

public:
    explicit sqlite_storage(const char * path);

    sqlite_storage(const sqlite_storage &) = delete;

    sqlite_storage(sqlite_storage &&) noexcept = default;

    ~sqlite_storage();

    void operator=(const sqlite_storage &) = delete;

    sqlite_storage & operator=(sqlite_storage &&) noexcept = default;

    void insert(const char * name, double value, time_t now);

    void select(size_t count, int (*callback)(void*,int,char**,char**), void * user);

    void delete_where_id_not_greater_than(size_t id);

private:
    sqlite3_ptr db_{ };
};

struct sqlite_storage::runtime_error : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

inline void sqlite_storage::deleter::operator()(sqlite3 * db) const
{
    sqlite3_close(db);
}