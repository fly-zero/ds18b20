#pragma once

#include <ctime>

struct data_storage_base
{
    virtual ~data_storage_base() = default;

    virtual void insert(const char * name, double const value, time_t now) = 0;
};
