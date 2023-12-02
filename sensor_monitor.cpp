#include <iostream>
#include <fstream>  // file operations
#include <cstdlib>
#include <chrono>
#include <ctime>
#include <thread>
#include <unistd.h>
#include "json.hpp" // json handling
#include "mqtt/client.h" // paho mqtt
#include <iomanip>
#include <sstream>
#include <sys/sysinfo.h>

#define QOS 1
#define BROKER_ADDRESS "tcp://localhost:1883"

using namespace std;

string getMachineId() {
    char hostname[1024];
    gethostname(hostname, 1024);
    return string(hostname);
}

long getUsedRAM() {
    struct sysinfo info;

    if (sysinfo(&info) != 0) {
        std::cerr << "Error getting system information" << std::endl;
        return -1;
    }

    // Calculate used RAM (total RAM - free RAM)
    double usedRAMInMB = static_cast<double>(info.totalram - info.freeram) / (1024 * 1024);

    return usedRAMInMB;
}

// Function to get CPU usage as a percentage
double getCPUUsage() {
    static long long lastTotalUser, lastTotalUserLow, lastTotalSys, lastTotalIdle;

    std::ifstream fileStat("/proc/stat");
    std::string line;

    if (!fileStat.is_open()) {
        std::cerr << "Error opening /proc/stat" << std::endl;
        return -1.0;
    }

    // Read the first line from /proc/stat
    std::getline(fileStat, line);
    std::istringstream ss(line);

    // Skip the "cpu" prefix
    std::string cpuLabel;
    ss >> cpuLabel;

    // Read CPU time values
    long long user, nice, sys, idle, iowait, irq, softirq, steal, guest, guest_nice;
    ss >> user >> nice >> sys >> idle >> iowait >> irq >> softirq >> steal >> guest >> guest_nice;

    // Calculate total CPU time
    long long totalUser = user - lastTotalUser;
    long long totalUserLow = nice - lastTotalUserLow;
    long long totalSys = sys - lastTotalSys;
    long long totalIdle = idle - lastTotalIdle;
    long long total = totalUser + totalUserLow + totalSys + totalIdle;

    // Calculate CPU usage percentage
    double cpuUsage = (static_cast<double>(total - totalIdle) / total) * 100.0;

    // Update last values for the next calculation
    lastTotalUser = user;
    lastTotalUserLow = nice;
    lastTotalSys = sys;
    lastTotalIdle = idle;

    return cpuUsage;
}

int main(int argc, char* argv[]) {
    
    string clientId = "sensor-monitor";
    mqtt::client client(BROKER_ADDRESS, clientId);

    // Connect to the MQTT broker.
    mqtt::connect_options connOpts;
    connOpts.set_keep_alive_interval(20);
    connOpts.set_clean_session(true);

    try {
        client.connect(connOpts);
    } catch (mqtt::exception& e) {
        cerr << "Error: " << e.what() << endl;
        return EXIT_FAILURE;
    }
    clog << "connected to the broker" << endl;

    // Get the unique machine identifier, in this case, the hostname.
    std::string machineId = getMachineId();

    // Initial message
    nlohmann::json initialMessage;
    initialMessage["machine_id"] = machineId;
    initialMessage["sensors"] = {
        {
            {"sensor_id", "cpu_temperature"},
            {"data_type", "int"},
            {"data_interval", 1000}  // Example interval in milliseconds
        },
        // Add more sensors as needed
    };

    string initialTopic = "/sensor_monitors";
    mqtt::message initialMsg(initialTopic, initialMessage.dump(), QOS, false);
    client.publish(initialMsg);
    clog << "Initial message published - topic: " << initialTopic << " - message: " << initialMessage.dump() << endl;

    while (true) {
       // Get the current time in ISO 8601 format.
        auto now = chrono::system_clock::now();
        time_t now_c = chrono::system_clock::to_time_t(now);
        tm* now_tm = localtime(&now_c);
        stringstream ss;
        ss << put_time(now_tm, "%FT%TZ");
        string timestamp = ss.str();

        // Generate a random value.
        long ramUsed = getUsedRAM();
        
        // Construct the JSON message.
        nlohmann::json j;
        j["timestamp"] = timestamp;
        j["ram_used"] = ramUsed;  // Convert to float

        // // Publish the JSON message to the appropriate topic.
        std::string topic = "/sensors/" + machineId + "/ram_used";
        mqtt::message msg(topic, j.dump(), QOS, false);
        clog << "message published - topic: " << topic << " - message: " << j.dump() << endl;
        client.publish(msg);

        // Sleep for some time.
        this_thread::sleep_for(chrono::seconds(1));
    }

    return EXIT_SUCCESS;
}
