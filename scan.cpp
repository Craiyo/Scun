// scan.cpp
// Compile: g++ -O3 -march=native -DNDEBUG -std=c++17 -o scan scan.cpp

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <dirent.h>
#include <cstring>
#include <cctype>
#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <algorithm>

struct MemoryRegion {
    uintptr_t start;
    uintptr_t end;
    std::string perms;
};

std::vector<MemoryRegion> list_memory_maps(pid_t pid) {
    std::vector<MemoryRegion> regions;
    std::ifstream file("/proc/" + std::to_string(pid) + "/maps");
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string addr, perms;
        if (!(iss >> addr >> perms)) continue;
        if (perms.find('r') == std::string::npos) continue;
        auto dash = addr.find('-');
        if (dash == std::string::npos) continue;
        try {
            uintptr_t start = std::stoull(addr.substr(0, dash), nullptr, 16);
            uintptr_t end   = std::stoull(addr.substr(dash + 1), nullptr, 16);
            regions.push_back({start, end, perms});
        } catch (...) {}
    }
    return regions;
}

ssize_t safe_read_mem(int fd, uintptr_t address, void* buffer, size_t size) {
    return pread(fd, buffer, size, address);
}

std::vector<pid_t> get_pids_by_name(const std::string& process_name) {
    std::vector<pid_t> pids;
    DIR* dir = opendir("/proc");
    if (!dir) return pids;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (std::all_of(entry->d_name, entry->d_name + std::strlen(entry->d_name), ::isdigit)) {
            std::ifstream comm("/proc/" + std::string(entry->d_name) + "/comm");
            if (!comm.is_open()) continue;
            std::string name;
            std::getline(comm, name);
            if (name == process_name) pids.push_back(std::stoi(entry->d_name));
        }
    }
    closedir(dir);
    return pids;
}

class AlbionScanner {
public:
    AlbionScanner(pid_t pid, bool debug_mode, size_t chunk_size = 1024 * 1024)
        : pid(pid), debug(debug_mode), chunk_size(chunk_size) {
        std::string mem_path = "/proc/" + std::to_string(pid) + "/mem";
        fd = open(mem_path.c_str(), O_RDONLY);
        if (fd == -1) throw std::runtime_error("failed to open /proc/<pid>/mem");
        regions = list_memory_maps(pid);
        init_maps();
    }
    ~AlbionScanner() { if (fd != -1) close(fd); }

    void scan() {
        std::vector<char> buffer(chunk_size);
        std::string tail, data;
        size_t overlap = 512; // bigger overlap for safety
        size_t bosses_needed = boss_name_map.size();

        std::string boss_marker      = "AVA_TEMPLE_HIGHLIGHT_UNCOMMON_STRAIGHT_";
        std::string construct_marker = "AVA_TEMPLE_HIGHLIGHT_UNCOMMON_STRAIGHT_Construct_01";
        std::string legendary_marker = "AVA_TEMPLE_HIGHLIGHT_LEGENDARY_BOSS_Grail_Sanctum_01";

        for (const auto& region : regions) {
            if (seen_bosses.size() == bosses_needed) break;
            uintptr_t pos = region.start;
            while (pos < region.end) {
                size_t to_read = std::min(chunk_size, region.end - pos);
                ssize_t n = safe_read_mem(fd, pos, buffer.data(), to_read);
                if (n <= 0) { pos += to_read; tail.clear(); continue; }

                data.assign(tail);
                data.append(buffer.data(), n);

                process_bosses(data, boss_marker);
                process_single_boss(data, construct_marker, "Construct");
                process_single_boss(data, legendary_marker, "Final");

                if (data.size() > overlap) tail.assign(data.end() - overlap, data.end());
                else tail = data;
                pos += to_read;

                if (seen_bosses.size() == bosses_needed) break;
            }
        }
    }

private:
    pid_t pid;
    int fd;
    bool debug;
    size_t chunk_size;
    std::vector<MemoryRegion> regions;
    std::unordered_set<std::string> seen_bosses;

    std::unordered_map<std::string, std::string> boss_name_map;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> layer_meanings;
    std::unordered_map<std::string, std::string> boss_table_map;

    void init_maps() {
        boss_name_map = {
            {"Arch-Mage", "Dancing"}, {"Knight-Captain", "KC"},
            {"High-Priest", "FreeBoss"}, {"Basilisk-Rider", "Basi"},
            {"Construct", "Construct"}, {"Grail_Sanctum", "Final"},
        };
        layer_meanings["default"] = {
            {"Layer_08", "2GOLDS"}, {"Layer_09", "1GOLD"},
            {"Layer_10", "1PURPLE"},{"Layer_11", "2PURPLE"}
        };
        layer_meanings["Construct"] = {
            {"Layer_06", "2GOLDS"}, {"Layer_07", "1GOLD"},
            {"Layer_08", "1PURPLE"},{"Layer_09", "2PURPLE"}
        };
        layer_meanings["Legendary_Boss"] = {
            {"Layer_02", "2GOLDS"},{"Layer_04", "1GOLD"},
            {"Layer_05", "2PURPLE"}
        };
        boss_table_map = { {"Construct","Construct"}, {"Final","Legendary_Boss"} };
    }

    std::string normalize_boss_id(const std::string& raw_id) {
        auto base = raw_id.substr(0, raw_id.find('_'));
        if (boss_name_map.count(base)) return boss_name_map[base];
        return base;
    }

    std::string get_table_for_boss(const std::string& boss) {
        if (boss_table_map.count(boss)) return boss_table_map[boss];
        return "default";
    }

    bool find_first_valid_layer(const std::string& data, size_t start_pos,
                                const std::string& table, std::string& loot, std::string& layer) {
        const std::string marker = "Layer_";
        size_t pos = start_pos;
        while ((pos = data.find(marker, pos)) != std::string::npos) {
            size_t i = pos + marker.size();
            std::string digits;
            while (i < data.size() && std::isdigit(data[i])) digits.push_back(data[i++]);

            if (!digits.empty()) {
                layer = "Layer_" + digits;
                if (layer_meanings[table].count(layer)) {
                    loot = layer_meanings[table][layer];
                    return true;
                } else if (debug) {
                    std::cout << "[DEBUG] Found Layer candidate: " << layer
                              << " (not valid for table " << table << ")\n";
                }
            }
            pos = i;
        }
        return false;
    }

    void process_bosses(const std::string& data, const std::string& marker) {
        size_t found = data.find(marker);
        while (found != std::string::npos) {
            size_t start = found + marker.size();
            std::string raw_id;
            while (start < data.size() && (std::isalnum(data[start]) || data[start] == '_' || data[start] == '-'))
                raw_id.push_back(data[start++]);
            std::string boss_name = normalize_boss_id(raw_id);
            if (!boss_name.empty() && !seen_bosses.count(boss_name)) {
                std::string table = get_table_for_boss(boss_name);
                std::string loot, layer;
                if (find_first_valid_layer(data, found, table, loot, layer)) {
                    seen_bosses.insert(boss_name);
                    std::cout << "👹 " << boss_name << " → " << loot << " (" << layer << ")\n";
                } else if (debug) {
                    std::cout << "[DEBUG] Found boss marker: " << boss_name
                              << " (no valid layer nearby)\n";
                }
            }
            found = data.find(marker, found + 1);
        }
    }

    void process_single_boss(const std::string& data, const std::string& marker, const std::string& boss_name) {
        if (seen_bosses.count(boss_name)) return;
        size_t found = data.find(marker);
        if (found != std::string::npos) {
            std::string table = get_table_for_boss(boss_name);
            std::string loot, layer;
            if (find_first_valid_layer(data, found, table, loot, layer)) {
                seen_bosses.insert(boss_name);
                std::cout << "👹 " << boss_name << " → " << loot << " (" << layer << ")\n";
            } else if (debug) {
                std::cout << "[DEBUG] Found " << boss_name << " marker but no valid layer\n";
            }
        }
    }
};

// ----------------- MAIN -----------------
int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <pid | process_name> [--debug]\n";
        return 1;
    }

    bool debug = false;
    std::string arg = argv[1];
    if (argc > 2 && std::string(argv[2]) == "--debug") {
        debug = true;
    }

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<pid_t> pids;
    if (std::all_of(arg.begin(), arg.end(), ::isdigit)) pids.push_back(std::stoi(arg));
    else pids = get_pids_by_name(arg);

    for (auto pid : pids) {
        try {
            AlbionScanner scanner(pid, debug);
            scanner.scan();
        } catch (const std::exception& e) {
            std::cerr << "[FATAL] " << e.what() << "\n";
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "\n[INFO] Scan completed in " << ms << " ms\n";
    return 0;
}
