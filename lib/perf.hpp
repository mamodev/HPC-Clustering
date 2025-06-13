#pragma once

#include <chrono>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <functional>

class PerfManager {
 
    // control and ack fifo from perf
    int ctl_fd = -1;
    int ack_fd = -1;
 
    // if perf is enabled
    bool enable = false;

    bool running = true;
 
    // commands and acks to/from perf
    static constexpr const char* enable_cmd = "enable";
    static constexpr const char* disable_cmd = "disable";
    static constexpr const char* ack_cmd = "ack\n";
 
    // send command to perf via fifo and confirm ack
    void send_command(const char* command) {
        if (enable) {
            write(ctl_fd, command, strlen(command));
            char ack[5];
            read(ack_fd, ack, 5);
            assert(strcmp(ack, ack_cmd) == 0);
        }
    }
 
  public:
 
    PerfManager() {
        // setup fifo file descriptors
        char* ctl_fd_env = std::getenv("PERF_CTL_FD");
        char* ack_fd_env = std::getenv("PERF_ACK_FD");
        if (ctl_fd_env && ack_fd_env) {
            enable = true;
            ctl_fd = std::stoi(ctl_fd_env);
            ack_fd = std::stoi(ack_fd_env);
        } else {
            std::cout << "[WARNING] PerfManager: "
                      << "PERF_CTL_FD and PERF_ACK_FD not set, "
                      << "perf will not be enabled." << std::endl;
        }
    }
 
    // public apis
 
    void pause() {
        if (running) {
            send_command(disable_cmd);
            running = false;
        }
    }
 
    void resume() {
        if (!running) {
            send_command(enable_cmd);
            running = true;
        }
    }
};


class DeltaTimer {

    struct delta {
        std::chrono::high_resolution_clock::time_point start;
        std::chrono::high_resolution_clock::time_point end;
        std::string name;
    };

    std::vector<delta> deltas;

    public:
    DeltaTimer() = default;

    // void start(const std::string& name) {
    std::function<void(void)> start(const std::string& name) {
        deltas.push_back({std::chrono::high_resolution_clock::now(), 
            std::chrono::high_resolution_clock::now()
            , name});

        auto curr_pos = deltas.size() - 1;

        return [this, curr_pos]() mutable {
            deltas[curr_pos].end = std::chrono::high_resolution_clock::now();
        };
    }

    std::string csv() const {
        std::string result;
        for (const auto& d : deltas) {
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(d.end - d.start).count();
            result += d.name + "," + std::to_string(duration) + "\n";
        }
        return result;
    }

    void to_file(const std::string& filename) const {
        std::ofstream file(filename);
        if (file.is_open()) {
            file << csv();
            file.close();
        } else {
            std::cerr << "Unable to open file: " << filename << std::endl;
        }
    }

};