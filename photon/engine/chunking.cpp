#include "chunking.hpp"

#include "include.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

ChunkManager::ChunkManager() = default;

ChunkManager::~ChunkManager() {
    stop();
}

void ChunkManager::setChunkSizeMeters(float meters) {
    std::scoped_lock lock(mtx_);
    chunkSizeMeters_ = (std::max)(1.0f, meters);
}

void ChunkManager::setRadiusChunks(int radius) {
    std::scoped_lock lock(mtx_);
    radiusChunks_ = (std::max)(0, radius);
}

void ChunkManager::setRamBudgetBytes(size_t bytes) {
    std::scoped_lock lock(mtx_);
    ramBudgetBytes_ = std::max<size_t>(1024 * 1024, bytes);
}

void ChunkManager::setDiskCacheDir(std::string dir) {
    std::scoped_lock lock(mtx_);
    diskCacheDir_ = std::move(dir);
}

void ChunkManager::setGenerateIfMissing(bool enable) {
    std::scoped_lock lock(mtx_);
    generateIfMissing_ = enable;
}

void ChunkManager::setWorldOriginMeters(double originX, double originZ) {
    std::scoped_lock lock(mtx_);
    worldOriginMeters_ = glm::dvec2(originX, originZ);
}

void ChunkManager::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }

    // Ensure directory exists (best-effort)
    try {
        fs::create_directories(fs::path(diskCacheDir_));
    } catch (...) {
        // ignore
    }

    worker_ = std::thread(&ChunkManager::workerMain, this);
    logs("[Chunk] worker started");
}

void ChunkManager::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;
    }

    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    logs("[Chunk] worker stopped");
}

void ChunkManager::clear()
{
    std::scoped_lock lk(mtx_);
    desiredSet_.clear();
    readyQueue_.clear();
    requests_.clear();
    enqueued_.clear();
    chunks_.clear();
}

std::vector<std::pair<ChunkId, std::vector<uint8_t>>> ChunkManager::drainReadyChunks()
{
    std::vector<std::pair<ChunkId, std::vector<uint8_t>>> out;
    std::scoped_lock lk(mtx_);
    out.reserve(readyQueue_.size());
    while (!readyQueue_.empty()) {
        out.emplace_back(std::move(readyQueue_.front()));
        readyQueue_.pop_front();
    }
    return out;
}

bool ChunkManager::isDesired(const ChunkId& id) const {
    std::scoped_lock lk(mtx_);
    return desiredSet_.find(id) != desiredSet_.end();
}

static ChunkId worldToChunk2D(const glm::dvec2& pXZ, double chunkSizeMeters) {
    const double inv = 1.0 / chunkSizeMeters;
    // Using floor so negative positions behave correctly
    int32_t cx = static_cast<int32_t>(std::floor(pXZ.x * inv));
    int32_t cz = static_cast<int32_t>(std::floor(pXZ.y * inv));
    return ChunkId{cx, cz};
}

void ChunkManager::updateFromCamera(const glm::vec3& cameraWorldPos, uint64_t frameIndex) {
    std::scoped_lock lock(mtx_);

    desiredSet_.clear();

    // Camera position is in local meters; add global origin (EPSG:3857 meters) for global chunk ids.
    glm::dvec2 globalXZ = worldOriginMeters_ + glm::dvec2(cameraWorldPos.x, cameraWorldPos.z);
    const ChunkId center = worldToChunk2D(globalXZ, chunkSizeMeters_);

    // Build desired set in a radius
    for (int dz = -radiusChunks_; dz <= radiusChunks_; ++dz) {
        for (int dx = -radiusChunks_; dx <= radiusChunks_; ++dx) {
            ChunkId id{center.x + dx, center.z + dz};

            desiredSet_.insert(id);

            Chunk& chunk = getOrCreateLocked(id);
            chunk.lastUsedFrame = frameIndex;

            const ChunkState st = chunk.state.load(std::memory_order_relaxed);
            if (st == ChunkState::Unloaded) {
                // priority: euclidean distance in chunk space
                const float dist = std::sqrt(float(dx * dx + dz * dz));
                enqueueLocked(Request{id, dist, frameIndex});
            }
        }
    }

    enforceRamBudgetLocked(frameIndex);
    cv_.notify_one();
}

size_t ChunkManager::residentChunkCount() const {
    std::scoped_lock lock(mtx_);
    size_t count = 0;
    for (const auto& kv : chunks_) {
        if (kv.second.state.load(std::memory_order_relaxed) == ChunkState::ResidentCPU) {
            ++count;
        }
    }
    return count;
}

Chunk& ChunkManager::getOrCreateLocked(const ChunkId& id) {
    auto it = chunks_.find(id);
    if (it != chunks_.end()) {
        return it->second;
    }

    // Chunk contains a mutex/atomic and is not movable; construct it in-place.
    auto [insertedIt, inserted] = chunks_.try_emplace(id);
    if (inserted) {
        insertedIt->second.id = id;
    }
    return insertedIt->second;
}

void ChunkManager::enqueueLocked(const Request& req) {
    if (enqueued_.find(req.id) != enqueued_.end()) {
        return;
    }

    requests_.push_back(req);
    enqueued_.insert(req.id);

    // Keep queue roughly prioritized (small queue sizes; simple sort is fine)
    std::sort(requests_.begin(), requests_.end(), [](const Request& a, const Request& b) {
        return a.priority < b.priority;
    });
}

std::optional<ChunkManager::Request> ChunkManager::popNextRequestLocked() {
    if (requests_.empty()) {
        return std::nullopt;
    }
    Request r = requests_.front();
    requests_.pop_front();
    enqueued_.erase(r.id);
    return r;
}

void ChunkManager::workerMain() {
    while (running_.load(std::memory_order_relaxed)) {
        std::optional<Request> req;
        {
            std::unique_lock lock(mtx_);
            cv_.wait(lock, [&]() {
                return !running_.load(std::memory_order_relaxed) || !requests_.empty();
            });

            if (!running_.load(std::memory_order_relaxed)) {
                break;
            }

            req = popNextRequestLocked();
        }

        if (!req.has_value()) {
            continue;
        }

        // Mark Loading
        {
            std::scoped_lock lock(mtx_);
            Chunk& c = getOrCreateLocked(req->id);
            ChunkState expected = ChunkState::Unloaded;
            (void)c.state.compare_exchange_strong(expected, ChunkState::LoadingDisk);
        }

        // Disk read (no locks)
        std::vector<uint8_t> bytes = readChunkFileOrEmpty(req->id);

        // If missing, optionally generate a tiny placeholder and persist it
        if (bytes.empty() && generateIfMissing_) {
            // Format v0: "PHOT" + 2x int32 chunk coords + 16 bytes payload
            bytes.reserve(4 + 8 + 16);
            bytes.insert(bytes.end(), {'P','H','O','T'});
            auto push32 = [&](int32_t v) {
                uint32_t u = static_cast<uint32_t>(v);
                bytes.push_back(uint8_t(u & 0xFF));
                bytes.push_back(uint8_t((u >> 8) & 0xFF));
                bytes.push_back(uint8_t((u >> 16) & 0xFF));
                bytes.push_back(uint8_t((u >> 24) & 0xFF));
            };
            push32(req->id.x);
            push32(req->id.z);
            for (int i = 0; i < 16; ++i) {
                bytes.push_back(uint8_t(i));
            }
            writeChunkFile(req->id, bytes);
        }

        // If still missing and we are not generating placeholders, leave it Unloaded.
        if (bytes.empty() && !generateIfMissing_) {
            std::scoped_lock lock(mtx_);
            Chunk& c = getOrCreateLocked(req->id);
            c.state.store(ChunkState::Unloaded, std::memory_order_relaxed);
            continue;
        }

        // Publish into RAM + ready queue
        {
            std::scoped_lock lock(mtx_);
            Chunk& c = getOrCreateLocked(req->id);
            {
                std::scoped_lock chunkLock(c.mtx);
                c.cpu.bytes = std::move(bytes);
            }
            c.state.store(ChunkState::ResidentCPU, std::memory_order_relaxed);

            // Copy out for main thread consumption (bytes are opaque; small enough for prototype)
            {
                std::scoped_lock chunkLock(c.mtx);
                readyQueue_.emplace_back(c.id, c.cpu.bytes);
            }
        }
    }
}

std::string ChunkManager::chunkFilename(const ChunkId& id) const {
    return diskCacheDir_ + "/chunk_" + std::to_string(id.x) + "_" + std::to_string(id.z) + ".bin";
}

std::vector<uint8_t> ChunkManager::readChunkFileOrEmpty(const ChunkId& id) {
    std::vector<uint8_t> bytes;
    const std::string pathStr = chunkFilename(id);

    try {
        std::ifstream f(pathStr, std::ios::binary);
        if (!f.good()) {
            return {};
        }
        f.seekg(0, std::ios::end);
        const std::streamsize n = f.tellg();
        if (n <= 0) {
            return {};
        }
        f.seekg(0, std::ios::beg);
        bytes.resize(static_cast<size_t>(n));
        f.read(reinterpret_cast<char*>(bytes.data()), n);
        if (!f.good()) {
            return {};
        }
        return bytes;
    } catch (...) {
        return {};
    }
}

void ChunkManager::writeChunkFile(const ChunkId& id, const std::vector<uint8_t>& bytes) {
    const std::string pathStr = chunkFilename(id);
    try {
        fs::create_directories(fs::path(diskCacheDir_));
        std::ofstream f(pathStr, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    } catch (...) {
        // ignore
    }
}

size_t ChunkManager::estimateRamUsageLocked() const {
    size_t total = 0;
    for (const auto& kv : chunks_) {
        const Chunk& c = kv.second;
        if (c.state.load(std::memory_order_relaxed) == ChunkState::ResidentCPU) {
            std::scoped_lock chunkLock(c.mtx);
            total += c.cpu.bytes.size();
        }
    }
    return total;
}

void ChunkManager::enforceRamBudgetLocked(uint64_t frameIndex) {
    // Simple eviction: evict farthest/oldest until under budget.
    // (Right now we just use lastUsedFrame and byte size.)

    size_t usage = estimateRamUsageLocked();
    if (usage <= ramBudgetBytes_) {
        return;
    }

    // Collect candidates
    struct Cand { ChunkId id; uint64_t last; size_t bytes; };
    std::vector<Cand> cands;
    cands.reserve(chunks_.size());

    for (auto& kv : chunks_) {
        Chunk& c = kv.second;
        if (c.state.load(std::memory_order_relaxed) != ChunkState::ResidentCPU) {
            continue;
        }
        std::scoped_lock chunkLock(c.mtx);
        cands.push_back(Cand{c.id, c.lastUsedFrame, c.cpu.bytes.size()});
    }

    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        return a.last < b.last; // oldest first
    });

    for (const Cand& cand : cands) {
        if (usage <= ramBudgetBytes_) {
            break;
        }
        auto it = chunks_.find(cand.id);
        if (it == chunks_.end()) {
            continue;
        }
        Chunk& c = it->second;
        if (c.lastUsedFrame + 60 >= frameIndex) {
            // don't evict very-recently used chunks
            continue;
        }
        {
            std::scoped_lock chunkLock(c.mtx);
            usage -= (std::min)(usage, c.cpu.bytes.size());
            c.cpu.bytes.clear();
            c.cpu.bytes.shrink_to_fit();
        }
        c.state.store(ChunkState::Unloaded, std::memory_order_relaxed);
    }
}
