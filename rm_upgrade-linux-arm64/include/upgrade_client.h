#ifndef UPGRADE_CLIENT_H
#define UPGRADE_CLIENT_H

// upgrade_client.h
//
// A small library that encapsulates the controller's HTTPS upgrade interface:
//
//   POST /update/uploadUpgradeFile   uploadUpgradeFile()
//   POST /update/getExtractList      getExtractList()      / waitForExtractList()
//   POST /update/startUpgrade        startUpgrade()
//   POST /update/getUpgradeRate      getUpgradeRate()
//   POST /update/clearUpgrade        clearUpgrade()
//   POST /robot/sysReboot            sysReboot()
//
// The implementation (cpp-httplib + nlohmann/json + OpenSSL) is hidden behind a
// pimpl, so users of this header don't need any of those dependencies.

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace upgrade {

// Returns the upgrade_client library version string, e.g. "v1.0.0".
const char* version();

// One item as reported by the controller. Not every field is populated by every
// endpoint: getExtractList fills name/type/check; getUpgradeRate additionally
// fills rate/status.
struct FileItem {
    std::string name;            // file / item name
    std::string type;            // item type (server-defined, string form)
    int         rate   = 0;      // progress 0..100 (getUpgradeRate)
    int         status = 0;      // per-item status (getUpgradeRate)
    bool        check  = false;  // selected / installable flag
};

// Parsed /update/getExtractList payload.
struct ExtractInfo {
    int                   code = -1;
    std::string           msg;
    std::vector<FileItem> list;
    bool                  ready = false;  // true once extraction has finished
};

// Parsed /update/getUpgradeRate payload.
struct RateInfo {
    int                   code  = -1;
    int                   state = -1;     // overall state (server-defined enum)
    std::string           msg;
    std::vector<FileItem> list;
};

// Outcome for endpoints that just return HTTP 200 / 404.
struct CallResult {
    bool        ok          = false;  // transport ok AND http status == 200
    int         http_status = 0;
    std::string body;
    std::string error;                // populated on failure
    explicit operator bool() const { return ok; }
};

class UpgradeClient {
public:
    // Targets the controller at http://host:8090. Port (8090) and scheme
    // (HTTP) are fixed and intentionally not part of the creation interface.
    explicit UpgradeClient(std::string host);
    ~UpgradeClient();

    UpgradeClient(const UpgradeClient&)            = delete;
    UpgradeClient& operator=(const UpgradeClient&) = delete;

    // --- TLS / timeout configuration (apply before the first call) ----------

    // Verify the server certificate. Default: false, because controllers
    // typically present a self-signed certificate.
    void setVerifyCertificate(bool verify);
    // CA bundle to use when verification is enabled.
    void setCACertPath(const std::string& path);
    // Connect timeout and read+write timeout, in seconds.
    void setTimeouts(int connect_seconds, int transfer_seconds);

    // --- 1. Upload --------------------------------------------------------
    // Uploads the upgrade package. `stored_name` is sent as the "name" field
    // (what the server saves); defaults to the basename of file_path.
    CallResult uploadUpgradeFile(const std::string& file_path,
                                 const std::string& stored_name = "");

    // --- 2. Extract list --------------------------------------------------
    // Single query. Returns true on a successful HTTP round-trip; check
    // out.ready to know whether extraction has finished and the list is valid.
    bool getExtractList(ExtractInfo& out);
    // Polls getExtractList until out.ready is true or the timeout elapses.
    bool waitForExtractList(ExtractInfo& out,
                            int timeout_seconds  = 120,
                            int poll_interval_ms = 1000);

    // --- 3. Start ---------------------------------------------------------
    // Starts the upgrade. Each item's `check` decides whether it is upgraded.
    CallResult startUpgrade(const std::vector<FileItem>& items);
    // Convenience overload: (name, check) pairs.
    CallResult startUpgrade(
        const std::vector<std::pair<std::string, bool>>& items);

    // --- 4. Progress ------------------------------------------------------
    bool getUpgradeRate(RateInfo& out);

    // --- 5. Clear ---------------------------------------------------------
    CallResult clearUpgrade();

    // --- 6. Reboot --------------------------------------------------------
    // Reboots the controller (POST /robot/sysReboot). Typically called once
    // the upgrade has completed so the new program takes effect.
    CallResult sysReboot();

    // Last transport/parse error for the bool-returning calls.
    const std::string& lastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace upgrade

#endif  // UPGRADE_CLIENT_H
