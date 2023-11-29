#pragma once

#include <memory>
#include <string>
#include <stdexcept>

#include <curl/curl.h>

class influx_storage
{
public:
    struct runtime_error;

private:
    struct curl_deleter
    {
        void operator()(CURL * curl) const;
    };

    struct curl_list_deleter
    {
        void operator()(curl_slist * list) const;
    };

    using curl_ptr = std::unique_ptr<CURL, curl_deleter>;
    using curl_list_ptr = std::unique_ptr<curl_slist, curl_list_deleter>;

public:
    influx_storage(std::string host,
                   std::string org,
                   std::string bucket,
                   std::string token,
                   std::string measurement,
                   std::string field);

    influx_storage(const influx_storage &) = delete;

    influx_storage(influx_storage &&) noexcept = default;

    ~influx_storage() = default;

    influx_storage & operator=(const influx_storage &) = delete;

    influx_storage & operator=(influx_storage &&) noexcept = default;

    void insert(const char * name, double value, time_t now);

    void prepare_data(std::string & data, const char * name, double value, time_t now);

    void insert(const std::string & data);

    bool is_bucket_exists() const;

protected:
    static size_t write_callback(
        char * ptr, size_t size, size_t nmemb, void * userdata);

private:
    std::string host_;
    std::string org_;
    std::string bucket_;
    std::string token_;
    std::string measurement_;
    std::string field_;
};

struct influx_storage::runtime_error : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

inline void influx_storage::insert(const char * name, double value, time_t now)
{
    std::string data;
    prepare_data(data, name, value, now);
    insert(data);
}
