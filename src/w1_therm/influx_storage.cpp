#include <cassert>
#include <cstddef>
#include <stdexcept>

#ifdef _DEBUG_
#include <iostream>
#endif

#include <curl/curl.h>
#include <rapidjson/document.h>

#include "influx_storage.h"

inline void influx_storage::curl_deleter::operator()(CURL * curl) const
{
    curl_easy_cleanup(curl);
}

inline void influx_storage::curl_list_deleter::operator()(curl_slist * list) const
{
    curl_slist_free_all(list);
}

influx_storage::influx_storage(std::string host,
                               std::string org,
                               std::string bucket,
                               std::string token,
                               std::string measurement,
                               std::string field)
    : host_{ std::move(host) }
    , org_{ std::move(org) }
    , bucket_{ std::move(bucket) }
    , token_{ std::move(token) }
    , measurement_{ std::move(measurement) }
    , field_{ std::move(field) }
{
    if (host_.empty())
        throw std::invalid_argument{ "host is empty" };
    if (org_.empty())
        throw std::invalid_argument{ "org is empty" };
    if (bucket_.empty())
        throw std::invalid_argument{ "bucket is empty" };
    if (token_.empty())
        throw std::invalid_argument{ "token is empty" };
    if (measurement_.empty())
        throw std::invalid_argument{ "measurement is empty" };
    if (field_.empty())
        throw std::invalid_argument{ "field is empty" };
}

void influx_storage::insert(const char * name, double value, time_t now)
{
    // construct influx line protocol
    std::string line;
    line.reserve(measurement_.size() + field_.size() + 64);
    line += measurement_;
    line += ",name=";
    line += name;
    line += ' ';
    line += field_;
    line += '=';
    line += std::to_string(value);
    line += ' ';
    line += std::to_string(now);
    line += '\n';

    // init curl
    curl_ptr curl{curl_easy_init()};
    if (!curl)
        throw std::runtime_error{"curl_easy_init failed"};

    // set url
    std::string url = "http://" + host_ + "/api/v2/write?bucket=" + bucket_ + "&org=" + org_ + "&precision=s";
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());

    // set request method to POST
    curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);

    // set headers
    curl_slist * headers = nullptr;
    std::string auth = "Authorization: Token " + token_;
    headers = curl_slist_append(headers, auth.c_str());
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Content-Type: text/plain; charset=utf-8");
    curl_list_ptr guard{ headers };
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);

    // set body
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, line.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, line.size());

    // set callback
    std::string response_body;
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response_body);

    // perform request
    CURLcode res = curl_easy_perform(curl.get());
    if (res != CURLE_OK)
        throw std::runtime_error{curl_easy_strerror(res)};

    // check response code
    long response_code = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response_code);
    if (response_code / 100 != 2)
        throw std::runtime_error{"influxdb response code: " + std::to_string(response_code)};

#ifdef _DEBUG_
    std::cerr << "new record: " << name << ',' << value << ',' << now << std::endl;
#endif
}

bool influx_storage::is_bucket_exists() const
{
    assert(!host_.empty());
    assert(!bucket_.empty());
    assert(!org_.empty());
    assert(!token_.empty());

    // init curl
    curl_ptr curl{ curl_easy_init() };
    if (!curl) return false;

    // set url
    std::string url = "http://" + host_ + "/api/v2/buckets?name=" + bucket_;
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());

    // set request method to GET
    curl_easy_setopt(curl.get(), CURLOPT_HTTPGET, 1L);

    // set headers
    curl_slist * headers = nullptr;
    std::string auth = "Authorization: Token " + token_;
    headers = curl_slist_append(headers, auth.c_str());
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_list_ptr guard{ headers };
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);

    // set write callback & userdata
    std::string response_body;
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response_body);

    // execute request
    CURLcode res = curl_easy_perform(curl.get());
    if (res != CURLE_OK) return false;

    // get response code
    long response_code = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response_code);
    if (response_code != 200) return false;

    // parse response body
    rapidjson::Document doc;
    doc.Parse(response_body.c_str());
    if (doc.HasParseError()) return false;

    // check if bucket exists
    auto const & buckets = doc["buckets"];
    if (!buckets.IsArray()) return false;

    for (auto const & bucket : buckets.GetArray())
    {
        if (!bucket.IsObject()) return false;

        auto const & name = bucket["name"];
        if (!name.IsString()) return false;

        if (name.GetString() == bucket_) return true;
    }

    return false;
}

size_t influx_storage::write_callback(char * ptr, size_t size, size_t nmemb, void * userdata)
{
    auto & body = *static_cast<std::string *>(userdata);
    auto const len = size * nmemb;
    body.append(ptr, len);
    return len;
}