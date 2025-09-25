#include <iostream>
#include <fstream>
#include <dirent.h>
#include <string>
#include <vector>
#include <cctype>
#include <cstring>
#include <algorithm>

// Return all PIDs that match the given process name
std::vector<pid_t> get_pids_by_name(const std::string& process_name) {
    std::vector<pid_t> pids;
    DIR* dir = opendir("/proc");
    if (!dir) {
        perror("opendir");
        return pids;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        // Check if directory name is all digits (i.e., a PID)
        if (std::all_of(entry->d_name, entry->d_name + std::strlen(entry->d_name), ::isdigit)) {
            std::string pid_str(entry->d_name);
            std::string comm_path = "/proc/" + pid_str + "/comm";

            std::ifstream comm_file(comm_path);
            if (!comm_file.is_open()) continue;

            std::string name;
            std::getline(comm_file, name);

            if (name == process_name) {
                pids.push_back(std::stoi(pid_str));
            }
        }
    }
    closedir(dir);
    return pids;
}

// Example usage
int main() {
    auto pids = get_pids_by_name("Albion-Online"); // change to your target process
    if (pids.empty()) {
        std::cout << "No matching process found\n";
    } else {
        std::cout << "Found PIDs: ";
        for (auto pid : pids) {
            std::cout << pid << " ";
        }
        std::cout << std::endl;
    }
    return 0;
}
