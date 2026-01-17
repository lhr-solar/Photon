#pragma once

#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec2.hpp>

struct ChunkId {
    int32_t x{};
    int32_t z{};

    friend bool operator==(const ChunkId& a, const ChunkId& b) {
        return a.x == b.x && a.z == b.z;
    }
};

struct ChunkIdHash {
    size_t operator()(const ChunkId& id) const noexcept {
        // 64-bit mix of two 32-bit ints
        uint64_t x = static_cast<uint32_t>(id.x);
        uint64_t z = static_cast<uint32_t>(id.z);
        uint64_t h = (x << 32) ^ z;
        // final avalanche
        h ^= (h >> 33);
        h *= 0xff51afd7ed558ccdULL;
        h ^= (h >> 33);
        h *= 0xc4ceb9fe1a85ec53ULL;
        h ^= (h >> 33);
        return static_cast<size_t>(h);
    }
};

enum class ChunkState : uint8_t {
    Unloaded,
    LoadingDisk,
    ResidentCPU,
};

struct ChunkCPUData {
    std::vector<uint8_t> bytes; // opaque for now (your mesh/height/instances later)
};

struct Chunk {
    ChunkId id{};
    std::atomic<ChunkState> state{ChunkState::Unloaded};
    uint64_t lastUsedFrame = 0;

    mutable std::mutex mtx;
    ChunkCPUData cpu;
};

class ChunkManager {
public:
    ChunkManager();
    ~ChunkManager();

    // Configure
    void setChunkSizeMeters(float meters);
    void setRadiusChunks(int radius);
    void setRamBudgetBytes(size_t bytes);
    void setDiskCacheDir(std::string dir);
    // If false, missing chunk files will stay empty (no placeholder generation).
    void setGenerateIfMissing(bool enable);
    // Global origin in meters (x=east, z=north). Camera local meters will be offset by this.
    void setWorldOriginMeters(double originX, double originZ);

    // Lifecycle
    void start();
    void stop();
    
    // Drops all cached chunk state (RAM + pending/ready queues). Safe to call while running.
    void clear();

    // Call from main thread (e.g., Photon::nextFrame)
    void updateFromCamera(const glm::vec3& cameraWorldPos, uint64_t frameIndex);
    
    // Drains chunks whose bytes are ready (loaded from disk or generated).
    // Returns (chunkId, bytes).
    std::vector<std::pair<ChunkId, std::vector<uint8_t>>> drainReadyChunks();

    // True if the chunk is within the most recent desired radius around camera.
    bool isDesired(const ChunkId& id) const;

    // Debug/telemetry
    size_t residentChunkCount() const;

private:
    struct Request {
        ChunkId id;
        float priority = 0.0f; // smaller = more urgent
        uint64_t frameIndex = 0;
    };

    Chunk& getOrCreateLocked(const ChunkId& id);

    void workerMain();
    void enqueueLocked(const Request& req);
    std::optional<Request> popNextRequestLocked();

    // Disk IO helpers
    std::vector<uint8_t> readChunkFileOrEmpty(const ChunkId& id);
    void writeChunkFile(const ChunkId& id, const std::vector<uint8_t>& bytes);
    std::string chunkFilename(const ChunkId& id) const;

    // RAM eviction
    void enforceRamBudgetLocked(uint64_t frameIndex);
    size_t estimateRamUsageLocked() const;

private:
    // Settings
    float chunkSizeMeters_ = 128.0f;
    int radiusChunks_ = 2;
    size_t ramBudgetBytes_ = 256ull * 1024 * 1024;
    std::string diskCacheDir_ = "cache/chunks_3857";
    bool generateIfMissing_ = true;
    glm::dvec2 worldOriginMeters_{0.0, 0.0};

    // State
    std::atomic<bool> running_{false};
    std::thread worker_;

    mutable std::mutex mtx_;
    std::condition_variable cv_;

    std::unordered_map<ChunkId, Chunk, ChunkIdHash> chunks_;
    std::deque<Request> requests_;
    std::unordered_set<ChunkId, ChunkIdHash> enqueued_;

    // Desired set for eviction decisions on main thread
    std::unordered_set<ChunkId, ChunkIdHash> desiredSet_;

    // Queue of finished chunk bytes to be consumed by main thread
    std::deque<std::pair<ChunkId, std::vector<uint8_t>>> readyQueue_;
};
