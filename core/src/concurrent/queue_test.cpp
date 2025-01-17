#include <userver/concurrent/queue.hpp>

#include <optional>
#include <unordered_set>

#include <boost/range/irange.hpp>

#include <concurrent/mp_queue_test.hpp>
#include <userver/engine/mutex.hpp>
#include <userver/utest/utest.hpp>
#include <userver/utils/async.hpp>
#include <userver/utils/fixed_array.hpp>

USERVER_NAMESPACE_BEGIN

namespace {
constexpr std::size_t kProducersCount = 4;
constexpr std::size_t kConsumersCount = 4;
constexpr std::size_t kMessageCount = 1000;

template <typename Producer>
auto GetProducerTask(const Producer& producer, std::size_t i) {
    return utils::Async("producer", [&producer = producer, i] {
        for (std::size_t message = i * kMessageCount; message < (i + 1) * kMessageCount; ++message) {
            ASSERT_TRUE(producer.Push(std::size_t{message}));
        }
    });
}

template <typename T>
class NonCoroutineTest : public ::testing::Test {};

using TestMpmcTypes = testing::Types<
    concurrent::NonFifoMpmcQueue<int>,
    concurrent::NonFifoMpmcQueue<std::unique_ptr<int>>,
    concurrent::NonFifoMpmcQueue<std::unique_ptr<RefCountData>>>;

using TestMpscTypes = testing::Types<
    concurrent::NonFifoMpscQueue<int>,
    concurrent::NonFifoMpscQueue<std::unique_ptr<int>>,
    concurrent::NonFifoMpscQueue<std::unique_ptr<RefCountData>>>;

using TestQueueTypes = testing::Types<
    concurrent::NonFifoMpmcQueue<std::size_t>,
    concurrent::NonFifoMpscQueue<std::size_t>,
    concurrent::SpmcQueue<std::size_t>,
    concurrent::SpscQueue<std::size_t>>;

}  // namespace

INSTANTIATE_TYPED_UTEST_SUITE_P(NonFifoMpmcQueue, QueueFixture, concurrent::NonFifoMpmcQueue<int>);

INSTANTIATE_TYPED_UTEST_SUITE_P(NonFifoMpmcQueue, TypedQueueFixture, TestMpmcTypes);

INSTANTIATE_TYPED_UTEST_SUITE_P(NonFifoMpscQueue, QueueFixture, concurrent::NonFifoMpscQueue<int>);

INSTANTIATE_TYPED_UTEST_SUITE_P(NonFifoMpscQueue, TypedQueueFixture, TestMpscTypes);

TYPED_TEST_SUITE(NonCoroutineTest, TestQueueTypes);

TYPED_TEST(NonCoroutineTest, PushPopNoblock) {
    auto queue = TypeParam::Create();

    auto producer = queue->GetProducer();
    auto consumer = queue->GetConsumer();

    std::size_t value = 0;

    EXPECT_TRUE(producer.PushNoblock(0));
    EXPECT_TRUE(producer.PushNoblock(1));

    EXPECT_TRUE(consumer.PopNoblock(value));
    EXPECT_EQ(value, 0);
    EXPECT_TRUE(consumer.PopNoblock(value));
    EXPECT_EQ(value, 1);

    EXPECT_TRUE(producer.PushNoblock(2));

    EXPECT_TRUE(consumer.PopNoblock(value));
    EXPECT_EQ(value, 2);
}

UTEST(NonFifoMpmcQueue, ConsumerIsDead) {
    auto queue = concurrent::NonFifoMpmcQueue<int>::Create();
    auto producer = queue->GetProducer();

    (void)(queue->GetConsumer());
    EXPECT_FALSE(producer.Push(0));
}

namespace {

template <typename T>
class MpMcQueueFixture : public testing::Test {};

class MpMcQueueSmallCapacity final {
    auto CreateQueue() { return concurrent::NonFifoMpmcQueue<std::size_t>::Create(10); }
};

class MpMcQueueLargeCapacity final {
    auto CreateQueue() { return concurrent::NonFifoMpmcQueue<std::size_t>::Create(1000); }
};

class MpMcQueueUnbounded final {
    auto CreateQueue() { return concurrent::impl::UnfairUnboundedNonFifoMpmcQueue<std::size_t>::Create(); }
};

using MpMcQueueFixtureTypes = testing::Types<MpMcQueueSmallCapacity, MpMcQueueLargeCapacity, MpMcQueueUnbounded>;

}  // namespace

TYPED_UTEST_SUITE(MpMcQueueFixture, MpMcQueueFixtureTypes);

TYPED_UTEST_MT(MpMcQueueFixture, Mpmc, kProducersCount + kConsumersCount) {
    auto queue = concurrent::NonFifoMpmcQueue<std::size_t>::Create(kMessageCount);
    std::vector<concurrent::NonFifoMpmcQueue<std::size_t>::Producer> producers;
    producers.reserve(kProducersCount);
    for (std::size_t i = 0; i < kProducersCount; ++i) {
        producers.emplace_back(queue->GetProducer());
    }

    std::vector<engine::TaskWithResult<void>> producers_tasks;
    producers_tasks.reserve(kProducersCount);
    for (std::size_t i = 0; i < kProducersCount; ++i) {
        producers_tasks.push_back(GetProducerTask(producers[i], i));
    }

    std::vector<concurrent::NonFifoMpmcQueue<std::size_t>::Consumer> consumers;
    consumers.reserve(kConsumersCount);
    for (std::size_t i = 0; i < kConsumersCount; ++i) {
        consumers.emplace_back(queue->GetConsumer());
    }

    std::vector<int> consumed_messages(kMessageCount * kProducersCount, 0);
    engine::Mutex mutex;

    std::vector<engine::TaskWithResult<void>> consumers_tasks;
    consumers_tasks.reserve(kConsumersCount);
    for (std::size_t i = 0; i < kConsumersCount; ++i) {
        consumers_tasks.push_back(utils::Async("consumer", [&consumer = consumers[i], &consumed_messages, &mutex] {
            std::size_t value{};
            while (consumer.Pop(value)) {
                const std::lock_guard lock(mutex);
                ++consumed_messages[value];
            }
        }));
    }

    for (auto& task : producers_tasks) {
        task.Get();
    }
    producers.clear();

    for (auto& task : consumers_tasks) {
        task.Get();
    }

    ASSERT_TRUE(std::all_of(consumed_messages.begin(), consumed_messages.end(), [](int item) { return item == 1; }));
}

TYPED_UTEST_MT(MpMcQueueFixture, SizeAfterConsumersDie, kConsumersCount + 1) {
    constexpr std::size_t kAttemptsCount = 1000;

    auto queue = concurrent::NonFifoMpmcQueue<int>::Create();

    std::optional producer(queue->GetProducer());

    EXPECT_EQ(queue->GetSizeApproximate(), 0);

    std::vector<engine::TaskWithResult<void>> tasks;
    tasks.reserve(kConsumersCount);
    for (std::size_t i = 0; i < kConsumersCount; ++i) {
        tasks.push_back(utils::Async("consumer", [consumer = queue->GetConsumer()] {
            for (std::size_t attempt = 0; attempt < kAttemptsCount; ++attempt) {
                int value{0};
                ASSERT_FALSE(consumer.Pop(value));
            }
        }));
    }

    // Kill the producer and try to provoke a race
    producer.reset();

    engine::Yield();
    engine::Yield();
    engine::Yield();
    engine::Yield();

    for (std::size_t attempt = 0; attempt < kAttemptsCount / 10; ++attempt) {
        EXPECT_EQ(0, queue->GetSizeApproximate());
    }

    for (auto&& task : tasks) {
        task.Get();
    }
    EXPECT_EQ(0, queue->GetSizeApproximate());
}

UTEST_MT(SpmcQueue, Spmc, 1 + kConsumersCount) {
    auto queue = concurrent::SpmcQueue<std::size_t>::Create(kMessageCount);
    std::optional producer(queue->GetProducer());

    auto producer_task = (GetProducerTask(*producer, 0));

    std::vector<concurrent::SpmcQueue<std::size_t>::Consumer> consumers;
    consumers.reserve(kConsumersCount);
    for (std::size_t i = 0; i < kConsumersCount; ++i) {
        consumers.emplace_back(queue->GetConsumer());
    }

    std::vector<int> consumed_messages(kMessageCount * 1, 0);
    engine::Mutex mutex;

    std::vector<engine::TaskWithResult<void>> consumers_tasks;
    consumers_tasks.reserve(kConsumersCount);
    for (std::size_t i = 0; i < kConsumersCount; ++i) {
        consumers_tasks.push_back(utils::Async("consumer", [&consumer = consumers[i], &consumed_messages, &mutex] {
            std::size_t value{};
            while (consumer.Pop(value)) {
                const std::lock_guard lock(mutex);
                ++consumed_messages[value];
            }
        }));
    }

    producer_task.Get();
    producer.reset();

    for (auto& task : consumers_tasks) {
        task.Get();
    }

    ASSERT_TRUE(std::all_of(consumed_messages.begin(), consumed_messages.end(), [](int item) { return item == 1; }));
}

// Reported in https://github.com/userver-framework/userver/issues/578
UTEST_MT(SpscQueue, DeadConsumerRace, 3) {
    struct Item {};

    const auto test_deadline = engine::Deadline::FromDuration(std::chrono::milliseconds{100});

    while (!test_deadline.IsReached()) {
        auto queue = concurrent::SpscQueue<Item>::Create();
        auto consumer = queue->GetConsumer();
        auto producer = queue->GetProducer();

        auto consumer_task = engine::AsyncNoSpan([consumer = std::move(consumer)]() mutable {
            std::this_thread::yield();
            std::move(consumer).Reset();
        });

        std::this_thread::yield();
        // May or may not push, depending on token drop ordering. Should not hang.
        (void)producer.Push(Item{});

        std::this_thread::yield();
        // May or may not push, depending on token drop ordering. Should not hang.
        (void)producer.Push(Item{});

        UEXPECT_NO_THROW(consumer_task.Get());
    }
}

UTEST_MT(NonFifoMpscQueue, Mpsc, kProducersCount + 1) {
    auto queue = concurrent::NonFifoMpscQueue<std::size_t>::Create(kMessageCount);
    std::vector<concurrent::NonFifoMpscQueue<std::size_t>::Producer> producers;
    auto consumer = queue->GetConsumer();

    producers.reserve(kProducersCount);
    for (std::size_t i = 0; i < kProducersCount; ++i) {
        producers.emplace_back(queue->GetProducer());
    }

    std::vector<engine::TaskWithResult<void>> producers_tasks;
    producers_tasks.reserve(kProducersCount);
    for (std::size_t i = 0; i < kProducersCount; ++i) {
        producers_tasks.push_back(GetProducerTask(producers[i], i));
    }

    std::vector<int> consumed_messages(kMessageCount * kProducersCount, 0);

    auto consumer_task = utils::Async("consumer", [&] {
        std::size_t value{};
        while (consumer.Pop(value)) {
            ++consumed_messages[value];
        }
    });

    for (auto& task : producers_tasks) {
        task.Get();
    }
    producers.clear();

    consumer_task.Get();

    ASSERT_TRUE(std::all_of(consumed_messages.begin(), consumed_messages.end(), [](int item) { return item == 1; }));
}

UTEST_MT(NonFifoMpscQueue, SizeAfterConsumersDie, 2) {
    constexpr std::size_t kAttemptsCount = 1000;

    auto queue = concurrent::NonFifoMpscQueue<int>::Create();

    std::optional producer(queue->GetProducer());

    EXPECT_EQ(queue->GetSizeApproximate(), 0);

    auto task = utils::Async("consumer", [consumer = queue->GetConsumer()] {
        for (std::size_t attempt = 0; attempt < kAttemptsCount; ++attempt) {
            int value{0};
            ASSERT_FALSE(consumer.Pop(value));
        }
    });

    // Kill the producer and try to provoke a race
    producer.reset();

    engine::Yield();
    engine::Yield();
    engine::Yield();

    for (std::size_t attempt = 0; attempt < kAttemptsCount / 10; ++attempt) {
        EXPECT_EQ(0, queue->GetSizeApproximate());
    }

    task.Get();
    EXPECT_EQ(0, queue->GetSizeApproximate());
}

UTEST_MT(SpscQueue, Spsc, 1 + 1) {
    auto queue = concurrent::SpscQueue<std::size_t>::Create(kMessageCount);

    std::optional producer(queue->GetProducer());
    auto producer_task = GetProducerTask(*producer, 0);

    auto consumer = queue->GetConsumer();
    std::vector<int> consumed_messages(kMessageCount * 1, 0);
    auto consumer_task = utils::Async("consumer", [&] {
        std::size_t value{};
        while (consumer.Pop(value)) {
            ++consumed_messages[value];
        }
    });

    producer_task.Get();
    producer.reset();
    consumer_task.Get();

    ASSERT_TRUE(std::all_of(consumed_messages.begin(), consumed_messages.end(), [](int item) { return item == 1; }));
}

TYPED_UTEST_MT(MpMcQueueFixture, MultiConsumerToken, kProducersCount + kConsumersCount) {
    using Message = std::size_t;
    using Queue = concurrent::NonFifoMpmcQueue<Message>;
    using ReceivedMessages = std::unordered_set<Message>;

    auto queue = Queue::Create(kMessageCount);
    auto producer = queue->GetMultiProducer();
    auto consumer = queue->GetMultiConsumer();

    auto producer_tasks = utils::GenerateFixedArray(kProducersCount, [&](std::size_t producer_id) {
        return engine::AsyncNoSpan([&, producer_id] {
            for (const auto message :
                 boost::irange<Message>(kMessageCount * producer_id, kMessageCount * (producer_id + 1))) {
                ASSERT_TRUE(producer.Push(Message{message}));
            }
        });
    });

    auto consumer_tasks = utils::GenerateFixedArray(kConsumersCount, [&](std::size_t /*consumer_id*/) {
        return engine::AsyncNoSpan([&] {
            ReceivedMessages received_messages;
            Message message{};
            while (consumer.Pop(message)) {
                const auto [_, new_message] = received_messages.insert(message);
                EXPECT_TRUE(new_message) << "Duplicate message " << message;
                if (!new_message) break;
            }
            return received_messages;
        });
    });

    for (auto& task : producer_tasks) {
        UEXPECT_NO_THROW(task.Get());
    }

    std::move(producer).Reset();

    ReceivedMessages total;
    for (auto& task : consumer_tasks) {
        ReceivedMessages partial;
        UEXPECT_NO_THROW(partial = task.Get());

        const auto old_total_size = total.size();
        const auto partial_size = partial.size();
        total.merge(std::move(partial));
        EXPECT_EQ(old_total_size + partial_size, total.size()) << "Duplicate messages";
    }

    EXPECT_EQ(total.size(), kProducersCount * kMessageCount) << "Likely missing messages";
}

USERVER_NAMESPACE_END
