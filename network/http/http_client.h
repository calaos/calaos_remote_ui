#pragma once

#include "http_types.h"
#include <atomic>
#include <mutex>
#include <queue>
#include <thread>
#include <memory>
#include <libwebsockets.h>

struct HttpRequestInternal
{
    HttpRequest request;
    HttpResponseCallback callback;
    std::shared_ptr<HttpResponse> response;
    bool completed;
    uint32_t request_id;
    
    HttpRequestInternal() : completed(false), request_id(0) {}
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

private:
    void serviceThread();
    static int httpCallback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);
    NetworkResult createContext();
    void destroyContext();
    std::string methodToString(HttpMethod method) const;
    HttpStatus intToHttpStatus(int status_code) const;
    
    struct lws_context* context_;
    std::atomic<bool> running_;
    std::thread service_thread_;
    mutable std::mutex requests_mutex_;
    
    std::queue<std::shared_ptr<HttpRequestInternal>> pending_requests_;
    std::map<struct lws*, std::shared_ptr<HttpRequestInternal>> active_requests_;
    
    uint32_t next_request_id_;
    uint32_t default_timeout_ms_;
    bool default_verify_ssl_;
    
    NetworkErrorCallback error_callback_;
};