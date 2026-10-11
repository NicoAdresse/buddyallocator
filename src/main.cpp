// src/main.cpp

#include <cstdint>
#include <cstddef>

[[maybe_unused]] static constexpr int MULTIBOOT_SEARCH { 8192 };
static constexpr uint32_t MULTIBOOT_HEADER_MAGIC { 0x1BADB002 };
static constexpr uint32_t MULTIBOOT_HEADER_FLAGS { 0x00000003 };
static constexpr uint32_t MULTIBOOT_CHECKSUM { (0u - (MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)) };

struct MultibootHeader
{
    uint32_t magic;
    uint32_t flags;
    uint32_t checksum;
};

[[gnu::section(".multiboot"), gnu::used, gnu::aligned(4)]]
const MultibootHeader multiboot_header = {
    MULTIBOOT_HEADER_MAGIC,
    MULTIBOOT_HEADER_FLAGS,
    MULTIBOOT_CHECKSUM
};

class BuddyAllocator
{
private:
    struct FreeBlock
    {
        FreeBlock* next;
    };

    struct BlockHeader
    {
        uint8_t order;
        bool isFree;
    };

    static constexpr size_t HEADER_SIZE { (sizeof(BlockHeader) + 7) & ~7u };

    std::byte* memoryStart { nullptr };
    size_t totalSize { 0 };
    uint8_t minOrder { 0 };
    uint8_t maxOrder { 0 };

    FreeBlock** freeLists { nullptr };

    static constexpr auto manualHasSingleBit(size_t n) -> bool
    {
        return n && !(n & (n - 1));
    }

    static constexpr auto manualBitWidth(size_t value) -> uint8_t
    {
        uint8_t bits = 0;
        while (value > 0)
        {
            bits++;
            value >>= 1;
        }
        return bits;
    }

    [[nodiscard]] auto orderToSize(uint8_t order) const -> size_t
    {
        return size_t { 1 } << order;
    }

    [[nodiscard]] auto sizeToOrder(size_t size) const -> uint8_t
    {
        if (size <= (size_t { 1 } << minOrder)) return minOrder;
        return manualBitWidth(size - 1);
    }

    [[nodiscard]] auto getBuddyOffset(size_t offset, size_t blockSize) const -> size_t
    {
        return offset ^ blockSize;
    }

    auto listAdd(uint8_t order, std::byte* blockPtr) -> void
    {
        auto* freeNode { reinterpret_cast<FreeBlock*>(blockPtr) };
        freeNode->next = freeLists[order];
        freeLists[order] = freeNode;
    }

    auto listRemove(uint8_t order, std::byte* blockPtr) -> bool
    {
        FreeBlock** current { &freeLists[order] };
        while (*current)
        {
            if (reinterpret_cast<std::byte*>(*current) == blockPtr)
            {
                *current = (*current)->next;
                return true;
            }
            current = &((*current)->next);
        }
        return false;
    }

public:
    explicit BuddyAllocator(void* rawMemory, size_t poolSize, void* trackingBuffer, size_t minBlockSize = 32)
    {
        if (!manualHasSingleBit(poolSize) || !manualHasSingleBit(minBlockSize)) return;
        if (minBlockSize < sizeof(FreeBlock) || !rawMemory || !trackingBuffer) return;

        memoryStart = static_cast<std::byte*>(rawMemory);
        totalSize = poolSize;
        maxOrder = static_cast<uint8_t>(manualBitWidth(poolSize - 1));
        minOrder = static_cast<uint8_t>(manualBitWidth(minBlockSize - 1));
        
        freeLists = static_cast<FreeBlock**>(trackingBuffer);
        for (uint8_t i = 0; i <= maxOrder; ++i)
        {
            freeLists[i] = nullptr;
        }

        auto* header { reinterpret_cast<BlockHeader*>(memoryStart) };
        header->order = maxOrder;
        header->isFree = true;

        listAdd(maxOrder, memoryStart);
    }

    ~BuddyAllocator() = default;

    BuddyAllocator(const BuddyAllocator&) = delete;
    BuddyAllocator& operator=(const BuddyAllocator&) = delete;

    [[nodiscard]] auto allocate(size_t size) -> void*
    {
        if (size == 0 || !freeLists) return nullptr;

        size_t totalNeeded { size + HEADER_SIZE };
        uint8_t targetOrder { sizeToOrder(totalNeeded) };

        if (targetOrder > maxOrder) return nullptr;

        uint8_t currentOrder { targetOrder };
        while (currentOrder <= maxOrder && !freeLists[currentOrder])
        {
            currentOrder++;
        }

        if (currentOrder > maxOrder) return nullptr;

        std::byte* blockStart { reinterpret_cast<std::byte*>(freeLists[currentOrder]) };
        freeLists[currentOrder] = freeLists[currentOrder]->next;

        while (currentOrder > targetOrder)
        {
            currentOrder--;
            size_t halfSize { orderToSize(currentOrder) };

            std::byte* buddyStart { blockStart + halfSize };
            auto* buddyHeader { reinterpret_cast<BlockHeader*>(buddyStart) };

            buddyHeader->order = currentOrder;
            buddyHeader->isFree = true;

            listAdd(currentOrder, buddyStart);
        }

        auto* header { reinterpret_cast<BlockHeader*>(blockStart) };
        header->order = targetOrder;
        header->isFree = false;
        return blockStart + HEADER_SIZE;
    }

    auto deallocate(void* ptr) -> void
    {
        if (!ptr || !freeLists) return;

        std::byte* blockPayload { static_cast<std::byte*>(ptr) };
        std::byte* blockStart { blockPayload - HEADER_SIZE };
        auto* header { reinterpret_cast<BlockHeader*>(blockStart) };

        header->isFree = true;
        uint8_t currentOrder { header->order };

        while (currentOrder < maxOrder)
        {
            size_t blockSize { orderToSize(currentOrder) };
            size_t offset { static_cast<size_t>(blockStart - memoryStart) };
            size_t buddyOffset { getBuddyOffset(offset, blockSize) };
            if (buddyOffset >= totalSize) break;
            std::byte* buddyStart { memoryStart + buddyOffset };
            auto* buddyHeader { reinterpret_cast<BlockHeader*>(buddyStart) };
            
            if (!buddyHeader->isFree || buddyHeader->order != currentOrder)
            {
                break;
            }

            if (!listRemove(currentOrder, buddyStart + HEADER_SIZE))
            {
                break;
            }

            if (buddyStart < blockStart)
            {
                blockStart = buddyStart;
            }

            currentOrder++;

            auto* mergedHeader = reinterpret_cast<BlockHeader*>(blockStart);
            mergedHeader->order = currentOrder;
        }

        listAdd(currentOrder, blockStart);
    }

    [[nodiscard]] explicit operator bool() const
    {
        return memoryStart != nullptr;
    }
};

alignas(16) static std::byte rawMemoryPool[1024 * 1024];
static void* allocatorTrackingBuffer[21];

extern "C" auto allocatorStart() -> void
{
    BuddyAllocator allocator(rawMemoryPool, sizeof(rawMemoryPool), allocatorTrackingBuffer, 64);

    if (!allocator)
    {
        return;
    }

    void* p1 { allocator.allocate(100) };
    void* p2 { allocator.allocate(5000) };
    void* p3 { allocator.allocate(100) };

    if (!p1 || !p2 || !p3)
    {
        return;
    }

    allocator.deallocate(p1);
    allocator.deallocate(p2);
    allocator.deallocate(p3);
}