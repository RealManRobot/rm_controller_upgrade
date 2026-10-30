// main.cpp — command-line driver for the controller upgrade flow.
//
// Flow:
//   1. uploadUpgradeFile   transfer the upgrade package to the controller
//   2. getExtractList      list the items extracted from the package
//   3. startUpgrade        start upgrading the selected items
//   4. getUpgradeRate      poll progress until finished, then exit

#include <algorithm>
#include <chrono>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "upgrade_client.h"

namespace {

void printUsage(const char* prog) {
    std::cout <<
        "Usage: " << prog << " <host> <package_file> [options]\n"
        "\n"
        "Connects to the controller at http://<host>:8090.\n"
        "\n"
        "Options:\n"
        "  --stored-name <name>   Name stored on the controller (default: basename)\n"
        "  --select <a,b,c>       Only upgrade these item names (default: all)\n"
        "  --extract-timeout <s>  Max seconds to wait for extraction (default 120)\n"
        "  --upgrade-timeout <s>  Max seconds to wait for upgrade   (default 600)\n"
        "  --poll <s>             Progress poll interval in seconds  (default 2)\n"
        "  --clear-first          Call clearUpgrade before uploading\n"
        "  --no-reboot            Do not reboot the controller after upgrading\n"
        "  --connect-timeout <s>  Connect timeout in seconds         (default 10)\n"
        "  --transfer-timeout <s> Read/write timeout in seconds      (default 300)\n"
        "  -h, --help             Show this help\n"
        "\n"
        "Example:\n"
        "  " << prog << " 192.168.1.100 ./upgrade_pkg.tar\n";
}

std::vector<std::string> splitCSV(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        // trim spaces
        size_t a = item.find_first_not_of(" \t");
        size_t b = item.find_last_not_of(" \t");
        if (a != std::string::npos) out.push_back(item.substr(a, b - a + 1));
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    // Print the library version at startup.
    std::cout << "rm_upgrade (upgrade_client " << upgrade::version() << ")\n";

    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }
    // ----- positional ------------------------------------------------------
    std::string host    = argv[1];
    std::string package = argv[2];

    // ----- options ---------------------------------------------------------
    bool        clear_first      = false;
    bool        reboot           = true;   // reboot the controller when done
    std::string stored_name;
    std::vector<std::string> select;     // empty => select all
    int extract_timeout  = 120;
    int upgrade_timeout  = 600;
    int poll_seconds     = 2;
    int connect_timeout  = 10;
    int transfer_timeout = 300;

    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << name << "\n";
                std::exit(1);
            }
            return argv[++i];
        };
        if (a == "--clear-first")          clear_first = true;
        else if (a == "--no-reboot")       reboot = false;
        else if (a == "--stored-name")     stored_name = next("--stored-name");
        else if (a == "--select")          select = splitCSV(next("--select"));
        else if (a == "--extract-timeout") extract_timeout = std::atoi(next("--extract-timeout").c_str());
        else if (a == "--upgrade-timeout") upgrade_timeout = std::atoi(next("--upgrade-timeout").c_str());
        else if (a == "--poll")            poll_seconds = std::atoi(next("--poll").c_str());
        else if (a == "--connect-timeout") connect_timeout = std::atoi(next("--connect-timeout").c_str());
        else if (a == "--transfer-timeout")transfer_timeout = std::atoi(next("--transfer-timeout").c_str());
        else if (a == "-h" || a == "--help") { printUsage(argv[0]); return 0; }
        else { std::cerr << "Unknown option: " << a << "\n"; return 1; }
    }

    upgrade::UpgradeClient client(host);
    client.setTimeouts(connect_timeout, transfer_timeout);

    std::cout << "Target: http://" << host << ":8090\n";

    // ----- optional pre-clear ---------------------------------------------
    if (clear_first) {
        std::cout << "[0/4] clearUpgrade ...\n";
        auto r = client.clearUpgrade();
        if (!r) std::cerr << "  warning: clearUpgrade failed: " << r.error << "\n";
    }

    // ----- 1. upload -------------------------------------------------------
    std::cout << "[1/4] uploadUpgradeFile: " << package << " ...\n";
    {
        auto r = client.uploadUpgradeFile(package, stored_name);
        if (!r) {
            std::cerr << "  upload failed: " << r.error << "\n";
            return 2;
        }
        std::cout << "  upload ok (HTTP " << r.http_status << ")\n";
    }

    // ----- 2. extract list -------------------------------------------------
    std::cout << "[2/4] getExtractList (waiting up to " << extract_timeout
              << "s) ...\n";
    upgrade::ExtractInfo ext;
    {
        const auto start    = std::chrono::steady_clock::now();
        const auto deadline = start + std::chrono::seconds(extract_timeout);
        bool ready = false;
        for (;;) {
            const int elapsed = static_cast<int>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - start).count());
            upgrade::ExtractInfo info;
            if (client.getExtractList(info) && info.ready) {
                ext   = info;
                ready = true;
                std::cout << "  waited " << elapsed << "s, extract list ready.\n";
                break;
            }
            std::cout << "  waiting for extraction ... " << elapsed << "s\n";
            if (std::chrono::steady_clock::now() >= deadline) break;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (!ready) {
            std::cerr << "  failed to obtain extract list: " << client.lastError()
                      << "\n";
            return 3;
        }
    }
    if (ext.list.empty()) {
        std::cerr << "  extraction reported no items; nothing to upgrade.\n";
        return 3;
    }
    std::cout << "  extracted items:\n";
    for (const auto& it : ext.list)
        std::cout << "    - " << it.name << " (type " << it.type << ")\n";

    // ----- 3. choose selection & start ------------------------------------
    std::set<std::string> wanted(select.begin(), select.end());
    std::vector<upgrade::FileItem> toSend;
    std::set<std::string> selectedNames;  // names we will wait on
    for (auto it : ext.list) {
        it.check = wanted.empty() ? true
                                  : (wanted.find(it.name) != wanted.end());
        if (it.check) selectedNames.insert(it.name);
        toSend.push_back(it);
    }
    if (selectedNames.empty()) {
        std::cerr << "  --select matched none of the extracted items.\n";
        return 4;
    }
    std::cout << "[3/4] startUpgrade for " << selectedNames.size()
              << " item(s) ...\n";
    {
        auto r = client.startUpgrade(toSend);
        if (!r) {
            std::cerr << "  startUpgrade failed: " << r.error << "\n";
            return 4;
        }
        std::cout << "  upgrade started (HTTP " << r.http_status << ")\n";
    }

    // ----- 4. poll progress ------------------------------------------------
    std::cout << "[4/4] getUpgradeRate (poll every " << poll_seconds
              << "s, timeout " << upgrade_timeout << "s) ...\n";
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(upgrade_timeout);
    int consecutiveErrors = 0;
    for (;;) {
        upgrade::RateInfo rate;
        if (client.getUpgradeRate(rate)) {
            consecutiveErrors = 0;

            // Progress line for the selected items.
            std::ostringstream line;
            line << "  state=" << rate.state << " | ";
            bool allDone = true;
            int  matched = 0;
            for (const auto& it : rate.list) {
                if (selectedNames.find(it.name) == selectedNames.end()) continue;
                ++matched;
                line << it.name << ":" << it.rate << "% ";
                if (it.rate < 100) allDone = false;
            }
            // If the rate endpoint hasn't started reporting our items yet,
            // don't treat that as "done".
            if (matched < static_cast<int>(selectedNames.size()))
                allDone = false;
            std::cout << line.str() << "\n";

            if (allDone) {
                std::cout << "Upgrade completed successfully.\n";
                // Reboot the controller so the new program takes effect.
                if (reboot) {
                    std::cout << "[5/5] sysReboot ...\n";
                    auto rb = client.sysReboot();
                    if (!rb)
                        std::cerr << "  sysReboot failed: " << rb.error << "\n";
                    else
                        std::cout << "  reboot requested (HTTP "
                                  << rb.http_status << ")\n";
                }
                return 0;
            }
        } else {
            if (++consecutiveErrors >= 5) {
                std::cerr << "  giving up after repeated poll errors: "
                          << client.lastError() << "\n";
                return 5;
            }
            std::cerr << "  poll error (" << consecutiveErrors
                      << "/5): " << client.lastError() << "\n";
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            std::cerr << "Timed out waiting for upgrade to finish.\n";
            return 6;
        }
        std::this_thread::sleep_for(std::chrono::seconds(poll_seconds));
    }
}
