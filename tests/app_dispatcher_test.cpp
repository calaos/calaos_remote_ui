#include <gtest/gtest.h>
#include "support/ws_harness.h"
#include "app_dispatcher.h"
#include <atomic>
#include <cstdio>
#include <memory>
#include <map>
#include <mutex>
#include <thread>

using nlohmann::json;

TEST(AppDispatcherStress, EveryEventOfEveryProducerIsDeliveredInOrderWhileSubscribersComeAndGo)
{
    constexpr int kProducers = 4;
    constexpr int kEventsEach = 300;
    constexpr int kSubscribers = 3;
    constexpr int kSubscriptionsEach = 10;

    // A subscriber cannot be removed: what it touches must outlive this test
    struct Tally
    {
        std::mutex mutex;
        std::map<int, int> next;
        std::atomic<int> delivered{0};
        std::atomic<int> outOfOrder{0};
        std::atomic<int> late{0};
    };
    auto tally = std::make_shared<Tally>();

    EventProbe::instance();   // AppStore and the probe subscribe first, as in every test
    AppDispatcher::getInstance().subscribe(AppEventType::IoCommandRefused, [tally](const AppEvent& event)
    {
        const auto* d = event.getData<IoCommandRefusedData>();
        int producer = 0, seq = 0;
        // The id says who sent the event and which one it is
        if (!d || std::sscanf(d->ioId.c_str(), "stress:%d:%d", &producer, &seq) != 2)
            return;
        std::lock_guard<std::mutex> lock(tally->mutex);
        if (tally->next[producer] != seq)
            tally->outOfOrder++;
        tally->next[producer] = seq + 1;
        tally->delivered++;
    });

    std::vector<std::thread> threads;
    for (int p = 0; p < kProducers; p++)
        threads.emplace_back([p]()
        {
            for (int i = 0; i < kEventsEach; i++)
                AppDispatcher::getInstance().dispatch(AppEvent(AppEventType::IoCommandRefused,
                    IoCommandRefusedData{"stress:" + std::to_string(p) + ":" + std::to_string(i)}));
        });
    // Subscribers added while the worker is delivering
    for (int s = 0; s < kSubscribers; s++)
        threads.emplace_back([tally]()
        {
            for (int i = 0; i < kSubscriptionsEach; i++)
            {
                AppDispatcher::getInstance().subscribe(AppEventType::IoCommandRefused,
                    [tally](const AppEvent&) { tally->late++; });
                std::this_thread::yield();
            }
        });
    for (auto& t : threads)
        t.join();

    EventProbe::instance().settle();
    EXPECT_EQ(tally->delivered.load(), kProducers * kEventsEach);
    EXPECT_EQ(tally->outOfOrder.load(), 0);
    for (int p = 0; p < kProducers; p++)
        EXPECT_EQ(tally->next[p], kEventsEach) << "producer " << p;
    EXPECT_GT(tally->late.load(), 0);
}
