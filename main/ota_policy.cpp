/**
 * @file ota_policy.cpp
 */

#include "ota_policy.h"

uint64_t OtaRetryPolicy::delayForFailures(int failures)
{
    uint64_t delay = kBaseDelayMs;
    for (int i = 1; i < failures && delay < kMaxDelayMs; i++)
        delay *= 2;
    return delay < kMaxDelayMs ? delay : kMaxDelayMs;
}

void OtaRetryPolicy::onFailure(uint64_t nowMs)
{
    failures_++;
    armed_ = true;
    nextAttemptMs_ = nowMs + delayForFailures(failures_);
}

void OtaRetryPolicy::reset()
{
    failures_ = 0;
    armed_ = false;
    nextAttemptMs_ = 0;
}
