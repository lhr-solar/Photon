#include "arena.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <limits>
#include <memory>
#include <sys/mman.h>
#include <iostream>
#include "../engine/include.hpp"
#include "imgui.h"

inline std::string fmtb(uint64_t b){
    static constexpr std::array<const char*,6> u{"B","KB","MB","GB","TB","PB"};
    double v = b;
    size_t i = 0;
    while(v >= 1024.0 && i < u.size()-1){ v /= 1024.0; ++i; }
    std::ostringstream s;
    s << std::fixed << std::setprecision(2) << v << ' ' << u[i];
    return s.str();
}

void Arena::status(){
    logs("arena size        : " << fmtb(arenaSize));
    logs("total signals     : " << totalSignals);
    logs("total pages       : " << totalPages);
    logs("bytes per signal  : " << fmtb(bytesPerSignal));
    logs("unused            : " << fmtb((arenaSize - (bytesPerSignal * totalSignals))));
    logs("points per signal : " << bytesPerSignal / sizeof(double));
    for(const auto& i : validIds){
        Message* msg = messages[i];
        if(!msg) continue;
        logs("message id        : " << msg->id);
        logs("message name      : " << msg->name);
        logs("dlc               : " << msg->dlc);
        logs("signal count      : " << msg->signalCount);
        logs("signal size       : " << msg->signalSize.load(std::memory_order_acquire));
        logs("transmitter       : " << msg->transmitter);
        logs("data rate         : " << msg->dataRate);
        logs("data transfer     : " << msg->dataTransfer);
        logs("bandwidth %       : " << msg->bandwidthPercentage);
        for(size_t s{0uz}; s < msg->signalCount; s++){
            Signal* sig = msg->signals[s];
            if(!sig) continue;
            logs("  signal index    : " << s);
            logs("  name            : " << sig->name);
            logs("  start bit       : " << sig->startBit);
            logs("  length          : " << sig->length);
            logs("  endianness      : " << sig->endianness);
            logs("  type            : " << sig->type);
            logs("  signed          : " << sig->isSigned);
            logs("  scale           : " << sig->scale);
            logs("  offset          : " << sig->offset);
            logs("  min             : " << sig->min);
            logs("  max             : " << sig->max);
            logs("  unit            : " << sig->unit);
            logs("  receiver        : " << sig->receiver);
            logs("  data ptr        : " << sig->data);
        }
    }
};

std::vector<size_t> Arena::search(const std::string& query){
    auto normalize = [](std::string text){
        std::string out;
        out.reserve(text.size());
        for(unsigned char c : text)
            if(std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
        return out;
    };
    auto distance = [&](std::string a, std::string b){
        a = normalize(std::move(a));
        b = normalize(std::move(b));
        if(a.empty()) return 0;
        if(b.empty()) return std::numeric_limits<int>::max() / 4;
        if(b.find(a) != std::string::npos) return 0;

        const int n = static_cast<int>(a.size());
        const int m = static_cast<int>(b.size());
        auto levenshtein = [&](const std::string& x, const std::string& y){
            std::vector<int> prev(y.size() + 1), cur(y.size() + 1);
            for(size_t j = 0; j <= y.size(); j++) prev[j] = static_cast<int>(j);
            for(size_t i = 1; i <= x.size(); i++){
                cur[0] = static_cast<int>(i);
                for(size_t j = 1; j <= y.size(); j++){
                    const int cost = (x[i - 1] == y[j - 1]) ? 0 : 1;
                    cur[j] = std::min({ prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost });
                }
                prev.swap(cur);
            }
            return prev[y.size()];
        };

        if(n >= m) return levenshtein(a, b);
        int best = std::numeric_limits<int>::max();
        for(size_t i = 0; i + a.size() <= b.size(); i++)
            best = std::min(best, levenshtein(a, b.substr(i, a.size())));
        return best;
    };

    std::vector<size_t> out;
    if(query.empty()){
        out.resize(validIds.size());
        for(size_t i{0uz}; i < validIds.size(); i++) out[i] = i;
        return out;
    }

    std::string trimmed = query;
    trimmed.erase(std::remove_if(trimmed.begin(), trimmed.end(), [](unsigned char c){ return std::isspace(c); }), trimmed.end());
    uint32_t parsedId{};
    bool hasParsedId = false;
    try{
        if(trimmed.size() > 2 && trimmed[0] == '0' && (trimmed[1] == 'x' || trimmed[1] == 'X')){
            parsedId = static_cast<uint32_t>(std::stoul(trimmed, nullptr, 16));
            hasParsedId = true;
        }else
        if(!trimmed.empty() && std::all_of(trimmed.begin(), trimmed.end(), [](unsigned char c){ return std::isdigit(c); })){
            parsedId = static_cast<uint32_t>(std::stoul(trimmed, nullptr, 10));
            hasParsedId = true;
        }else
        if(!trimmed.empty() && std::all_of(trimmed.begin(), trimmed.end(), [](unsigned char c){ return std::isxdigit(c); })){
            parsedId = static_cast<uint32_t>(std::stoul(trimmed, nullptr, 16));
            hasParsedId = true;
        }
    } catch (...) {}

    struct Match { size_t idx; int score; };
    std::vector<Match> matches;
    matches.reserve(validIds.size());
    for(size_t i{0uz}; i < validIds.size(); i++){
        Message* msg = messages[validIds[i]];
        if(!msg) continue;

        std::ostringstream hex;
        hex << std::uppercase << std::hex << msg->id;
        int score = std::min({
            distance(query, msg->name),
            distance(query, msg->transmitter) + 2,
            distance(query, std::to_string(msg->id)) + 1,
            distance(query, hex.str()) + 1,
            distance(query, "0x" + hex.str()) + 1
        });
        if(hasParsedId && parsedId == msg->id) score = -100;
        for(size_t s{0uz}; s < msg->signalCount; s++)
            if(msg->signals[s]) score = std::min(score, distance(query, msg->signals[s]->name) + 1);
        matches.push_back({i, score});
    }
    std::sort(matches.begin(), matches.end(), [&](const Match& a, const Match& b){
        if(a.score != b.score) return a.score < b.score;
        return validIds[a.idx] < validIds[b.idx];
    });
    out.reserve(matches.size());
    for(const auto& match : matches) out.push_back(match.idx);
    return out;
}

void Arena::statusUI(){
    if(ImGui::Begin("Arena Status")){
        ImGuiTableFlags summaryFlags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings;
        if(ImGui::BeginTable("arena_summary", 2, summaryFlags)){
            ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 180.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            auto row = [](const char* key, const std::string& value){
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(key);
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(value.c_str());
            };
            row("arena size", fmtb(arenaSize));
            row("total signals", std::to_string(totalSignals));
            row("total pages", std::to_string(totalPages));
            row("bytes per signal", fmtb(bytesPerSignal));
            row("unused", fmtb(arenaSize - (bytesPerSignal * totalSignals)));
            row("points per signal", std::to_string(bytesPerSignal / sizeof(double)));
            ImGui::EndTable();
        }
        ImGui::SeparatorText("Messages");
        static char query[128]{};
        static std::string cachedQuery{};
        static std::vector<size_t> cachedMatches = search("");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##arena_search", "Search name, id, or signal", query, sizeof(query));
        if(cachedQuery != query){
            cachedQuery = query;
            cachedMatches = search(cachedQuery);
        }
        for(const auto& match : cachedMatches){
            const uint32_t id = validIds[match];
            Message* msg = messages[id];
            if(!msg) continue;
            std::ostringstream header;
            header << "0x" << std::hex << std::uppercase << msg->id << std::dec << "  " << msg->name << "##msg" << msg->id;
            std::string label = header.str();
            if(ImGui::CollapsingHeader(label.c_str())){
                if(ImGui::BeginTable(("message_meta##" + std::to_string(msg->id)).c_str(), 2, summaryFlags)){
                    ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 180.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();
                    auto row = [](const char* key, const std::string& value){
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(key);
                        ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(value.c_str());
                    };
                    row("id", std::to_string(msg->id));
                    row("name", msg->name);
                    row("dlc", std::to_string(msg->dlc));
                    row("signal count", std::to_string(msg->signalCount));
                    row("signal size", std::to_string(msg->signalSize.load(std::memory_order_acquire)));
                    row("transmitter", msg->transmitter);
                    row("data rate", std::to_string(msg->dataRate));
                    row("data transfer", std::to_string(msg->dataTransfer));
                    row("bandwidth %", std::to_string(msg->bandwidthPercentage));
                    ImGui::EndTable();
                }
                ImGuiTableFlags signalFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings;
                if(ImGui::BeginTable(("signals##" + std::to_string(msg->id)).c_str(), 13, signalFlags)){
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("Idx");
                    ImGui::TableSetupColumn("Name");
                    ImGui::TableSetupColumn("Start");
                    ImGui::TableSetupColumn("Len");
                    ImGui::TableSetupColumn("Endian");
                    ImGui::TableSetupColumn("Type");
                    ImGui::TableSetupColumn("Signed");
                    ImGui::TableSetupColumn("Scale");
                    ImGui::TableSetupColumn("Offset");
                    ImGui::TableSetupColumn("Min");
                    ImGui::TableSetupColumn("Max");
                    ImGui::TableSetupColumn("Unit");
                    ImGui::TableSetupColumn("Receiver");
                    ImGui::TableHeadersRow();
                    for(size_t s{0uz}; s < msg->signalCount; s++){
                        Signal* sig = msg->signals[s];
                        if(!sig) continue;
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0); ImGui::Text("%zu", s);
                        ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(sig->name.c_str());
                        ImGui::TableSetColumnIndex(2); ImGui::Text("%d", sig->startBit);
                        ImGui::TableSetColumnIndex(3); ImGui::Text("%d", sig->length);
                        ImGui::TableSetColumnIndex(4); ImGui::Text("%d", sig->endianness);
                        ImGui::TableSetColumnIndex(5); ImGui::Text("%d", sig->type);
                        ImGui::TableSetColumnIndex(6); ImGui::TextUnformatted(sig->isSigned ? "true" : "false");
                        ImGui::TableSetColumnIndex(7); ImGui::Text("%.3f", sig->scale);
                        ImGui::TableSetColumnIndex(8); ImGui::Text("%.3f", sig->offset);
                        ImGui::TableSetColumnIndex(9); ImGui::Text("%.3f", sig->min);
                        ImGui::TableSetColumnIndex(10); ImGui::Text("%.3f", sig->max);
                        ImGui::TableSetColumnIndex(11); ImGui::TextUnformatted(sig->unit.c_str());
                        ImGui::TableSetColumnIndex(12); ImGui::TextUnformatted(sig->receiver.c_str());
                    }
                    ImGui::EndTable();
                }
            }
        }
    }ImGui::End();
};

void Arena::init(const arenaConfig& config){
    if(config.validIds.empty()) return;
    validIds = config.validIds;
    std::sort(validIds.begin(), validIds.end());
    for(const auto& m : messages) if(m) clear(m->id);
    arenaSize = MINIMUM_ARENA_SIZE;
    if(config.arenaSize > MINIMUM_ARENA_SIZE) arenaSize = config.arenaSize;
    pool = mmap(nullptr, arenaSize, 
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    for(const auto& idx : validIds){
        uint32_t count = config.signalCounts[idx];
        if(count > 32) count = 32;
        totalSignals += count;
    };
    cursor = static_cast<uint8_t*>(pool);
    remaining = arenaSize;
    totalPages = arenaSize / PAGE_SIZE;
    pagesPerSignal = totalPages / totalSignals;
    bytesPerSignal = PAGE_SIZE * pagesPerSignal; 

    for(const auto& idx : validIds){
        messages[idx] = new(Message);
        Message& msg = *messages[idx];
        msg.id = idx;
        msg.signalCount = config.signalCounts[idx];
        msg.signalSize.store(0, std::memory_order_relaxed);
        if(msg.signalCount > 32) msg.signalCount = 32;
        for(auto i{0uz}; i < msg.signalCount; i++){
            msg.signals[i] = new(Signal);
            void* mem = alloc(bytesPerSignal, PAGE_SIZE);
            msg.signals[i]->data = mem;
        };
    }
}

void* Arena::alloc(size_t bytes, size_t align){
    void* p = cursor;
    if (!std::align(align, bytes, p, remaining)) return nullptr;
    cursor = static_cast<uint8_t*>(p) + bytes;
    remaining -= bytes;
    return p;
};

// clears the existing message
// if no message exists, simply returns
void Arena::clear(uint32_t id){
    if(id >= messages.size() || !messages[id]) return;
    messages[id]->signalSize.store(0, std::memory_order_release);
};

// thread safe read
// returns a pointer of the signals buffer
// returns the current populated size of the buffer 
void Arena::read(uint32_t id, uint32_t signal, void** data, uint32_t* size){
    if(data) *data = nullptr;
    if(size) *size = 0;
    if(id >= messages.size() || !messages[id]) return;

    Message& msg = *messages[id];
    if(signal >= msg.signalCount || !msg.signals[signal]) return;

    const uint32_t published = msg.signalSize.load(std::memory_order_acquire);
    if(data) *data = msg.signals[signal]->data;
    if(size) *size = published;
};

// thread safe write
// appends the data to the signal buffer
bool Arena::write(uint32_t id, uint32_t signal, void* data, uint32_t size){
    if(id >= messages.size() || !messages[id] || !data) return false;
    Message& msg = *messages[id];
    if(signal >= msg.signalCount || !msg.signals[signal]) return false;

    const uint32_t offset = msg.signalSize.load(std::memory_order_relaxed);
    if(offset > bytesPerSignal || size > bytesPerSignal - offset) return false;

    auto* dst = static_cast<uint8_t*>(msg.signals[signal]->data) + offset;
    std::memcpy(dst, data, size);
    msg.signalSize.store(offset + size, std::memory_order_release);
    return true;
};

void Arena::destroy(){ 
    for(const auto& id : validIds) clear(id);
    munmap(pool, arenaSize);
}
