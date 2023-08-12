#pragma once

#include <memory>

#include <sqlite3.h>

#include "data_storage_base.h"

class sqlite_storage
    : public data_storage_base
{
    struct sqlite3_deleter
    {
        void operator()(sqlite3 * db) const;
    };

    using sqlite3_ptr = std::unique_ptr<sqlite3, sqlite3_deleter>;

public:
    explicit sqlite_storage(const char * path);

    ~sqlite_storage();

    void insert(const char * name, double value, time_t now) override;

private:
    sqlite3_ptr db_{ };
};

inline void sqlite_storage::sqlite3_deleter::operator()(sqlite3 * db) const
{
    sqlite3_close(db);
}