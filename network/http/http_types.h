#pragma once

#include "network_types.h"
#include <map>
#include <string>

enum class HttpMethod
{
    GET,
    POST,
    PUT,
    DELETE,
    HEAD,
    OPTIONS
};

enum class HttpStatus
{
    UNKNOWN = 0,
    OK = 200,
    CREATED = 201,
    NO_CONTENT = 204,
    BAD_REQUEST = 400,
    UNAUTHORIZED = 401,
    FORBIDDEN = 403,
    NOT_FOUND = 404,
    INTERNAL_SERVER_ERROR = 500,
    BAD_GATEWAY = 502,
    SERVICE_UNAVAILABLE = 503
};

struct HttpHeader
{
    std::string name;
    std::string value;

    HttpHeader() = default;
    HttpHeader(const std::string& n, const std::string& v) : name(n), value(v) {}
};

using HttpHeaders = std::map<std::string, std::string>;

struct HttpRequest
{
    HttpMethod method;
    std::string url;
    HttpHeaders headers;
    NetworkBuffer body;
    uint32_t timeout_ms;
    bool verify_ssl;

    HttpRequest()
        : method(HttpMethod::GET)
        , timeout_ms(30000)
        , verify_ssl(true)
    {}
};

struct HttpResponse
{
    HttpStatus status_code;
    HttpHeaders headers;
    NetworkBuffer body;
    std::string error_message;

    HttpResponse() : status_code(HttpStatus::UNKNOWN) {}

    bool isSuccess() const
    {
        int code = static_cast<int>(status_code);
        return code >= 200 && code < 300;
    }
};

using HttpResponseCallback = std::function<void(const HttpResponse& response)>;