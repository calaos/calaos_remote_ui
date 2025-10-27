#pragma once

#include "http_types.h"
#include <atomic>
#include <mutex>
#include <queue>
#include <thread>
#include <memory>
#include <map>

struct mg_mgr;
struct mg_connection;

struct HttpRequestInternal
{
    HttpRequest request;
    HttpResponseCallback callback;
    std::shared_ptr<HttpResponse> response;
    bool completed;
    uint32_t request_id;
    struct mg_connection* connection;
    uint64_t timeout_time;

    HttpRequestInternal():
        completed(false),
        request_id(0),
        connection(nullptr),
        timeout_time(0)
    {
    }
};

class HttpClient
{
public:
    HttpClient();
    ~HttpClient();

    NetworkResult init();
    void cleanup();

    NetworkResult sendRequest(const HttpRequest& request, HttpResponseCallback callback);
    NetworkResult sendRequestSync(const HttpRequest& request, HttpResponse& response);

    void cancelAllRequests();
    size_t getPendingRequestCount() const;

    void setDefaultTimeout(uint32_t timeoutMs);
    void setDefaultVerifySsl(bool verify);
    void setErrorCallback(NetworkErrorCallback callback);

    std::shared_ptr<HttpRequestInternal> findRequestForConnection(struct mg_connection* c);

private:
    void serviceThread();
    static void eventHandler(struct mg_connection* c, int ev, void* ev_data, void* fn_data);
    void destroyManager();
    std::string methodToString(HttpMethod method) const;
    HttpStatus intToHttpStatus(int status_code) const;

    struct mg_mgr* mgr_;
    std::atomic<bool> running_;
    std::thread service_thread_;
    mutable std::mutex requests_mutex_;

    std::queue<std::shared_ptr<HttpRequestInternal>> pending_requests_;
    std::map<struct mg_connection*, std::shared_ptr<HttpRequestInternal>> active_requests_;

    uint32_t next_request_id_;
    uint32_t default_timeout_ms_;
    bool default_verify_ssl_;

    NetworkErrorCallback error_callback_;
};