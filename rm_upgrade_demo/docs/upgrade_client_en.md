# Using the `upgrade_client` library

`upgrade_client` is a small C++11 library that wraps the controller's
firmware-upgrade interface behind a clean API. The implementation is hidden
behind a pimpl, so **you only include one header** (`upgrade_client.h`) and link
one library.

- Header: `include/upgrade_client.h`
- Library: `libupgrade_client.a` (static) or `libupgrade_client.so` /
  `upgrade_client.dll` (shared)
- Everything lives in namespace `upgrade`.

---

## 1. Adding it to your build

The client connects to the controller.

### CMake — link the prebuilt library

```cmake
add_executable(myapp main.cpp)

target_include_directories(myapp PRIVATE /path/to/rm_upgrade/include)
target_link_libraries(myapp PRIVATE /path/to/libupgrade_client.a Threads::Threads)
```

The static archive does not carry its dependencies, so link `Threads` as well.
See `rm_upgrade_demo/` for a complete, minimal consumer project.

### Manual / other build systems

Add `include/` to the include path, compile/link against the library, and link
`-lpthread` (Linux). No extra headers are needed by your code.

---

## 2. Quick start — the full upgrade flow

```cpp
#include "upgrade_client.h"
#include <iostream>

int main() {
    upgrade::UpgradeClient client("192.168.1.100");   // 192.168.1.100
    client.setTimeouts(/*connect=*/10, /*transfer=*/300);

    // 1) Upload the package (multipart: name + content).
    if (!client.uploadUpgradeFile("firmware_pkg.tar")) {
        std::cerr << "upload failed\n";
        return 1;
    }

    // 2) Wait until the controller has extracted the package.
    upgrade::ExtractInfo ext;
    if (!client.waitForExtractList(ext, /*timeout_s=*/120)) {
        std::cerr << "extract failed: " << client.lastError() << "\n";
        return 1;
    }

    // 3) Select what to upgrade, then start. (Here: everything.)
    for (auto& it : ext.list) it.check = true;
    if (!client.startUpgrade(ext.list)) {
        std::cerr << "startUpgrade failed\n";
        return 1;
    }

    // 4) Poll progress until every selected item reaches 100%.
    for (;;) {
        upgrade::RateInfo rate;
        if (!client.getUpgradeRate(rate)) {
            std::cerr << "poll error: " << client.lastError() << "\n";
            continue;   // transient; retry
        }
        bool done = true;
        for (const auto& it : rate.list) {
            std::cout << it.name << ": " << it.rate << "%\n";
            if (it.rate < 100) done = false;
        }
        if (done) break;
    }
    std::cout << "upgrade complete\n";

    // 5) Reboot the controller so the new program takes effect.
    client.sysReboot();
}
```

---

## 3. API reference

### Version

```cpp
const char* upgrade::version();   // e.g. "v1.0.0"
```

### Construction & configuration

```cpp
upgrade::UpgradeClient client("host");   // targets host
```

`UpgradeClient` is non-copyable. Apply configuration **before the first call**:

| Method | Purpose |
|--------|---------|
| `setTimeouts(connect_s, transfer_s)` | Connect timeout and read/write timeout, in seconds (defaults 10 / 300). |

### Endpoints

| Method | Returns | Notes |
|--------|---------|-------|
| `uploadUpgradeFile(file_path, stored_name = "")` | `CallResult` | Uploads the package. `stored_name` is the name saved on the controller (defaults to the file's basename). |
| `getExtractList(ExtractInfo& out)` | `bool` | One query. `true` = round-trip ok; check `out.ready` for whether extraction finished. |
| `waitForExtractList(out, timeout_s = 120, poll_ms = 1000)` | `bool` | Polls `getExtractList` until `out.ready` or timeout. |
| `startUpgrade(std::vector<FileItem>)` | `CallResult` | Each item's `check` decides whether it is upgraded. |
| `startUpgrade(std::vector<std::pair<std::string,bool>>)` | `CallResult` | Convenience overload: `(name, check)` pairs. |
| `getUpgradeRate(RateInfo& out)` | `bool` | Current per-item progress/status; poll in a loop. |
| `clearUpgrade()` | `CallResult` | Clears upgrade state on the controller. |
| `sysReboot()` | `CallResult` | Reboots the controller (`POST /robot/sysReboot`). Call once the upgrade completes so the new program takes effect. |
| `lastError()` | `const std::string&` | Last transport/parse error for the `bool`-returning calls. |

### Data structures

```cpp
struct FileItem {
    std::string name;    // item name
    std::string type;    // item type (server-defined string)
    int         rate;    // progress 0..100        (getUpgradeRate)
    int         status;  // per-item status         (getUpgradeRate)
    bool        check;   // selected / installable  (set before startUpgrade)
};

struct ExtractInfo {
    int                   code;   // server code
    std::string           msg;
    std::vector<FileItem> list;   // extracted items (name/type/check populated)
    bool                  ready;  // true once extraction has finished
};

struct RateInfo {
    int                   code;
    int                   state;  // overall state (server-defined enum)
    std::string           msg;
    std::vector<FileItem> list;   // per-item rate/status populated
};

struct CallResult {
    bool        ok;           // transport ok AND status == 200
    int         http_status;  // response status code
    std::string body;         // raw response body
    std::string error;        // populated on failure
    explicit operator bool() const;   // true iff ok
};
```

---

## 4. Error handling

Two conventions, depending on the return type:

- **`CallResult`** (upload / start / clear): test it directly — `operator bool`
  is `true` only when the transport succeeded **and** the status is 200. On
  failure, read `.error` (and `.http_status` / `.body` for detail).

  ```cpp
  auto r = client.uploadUpgradeFile("pkg.tar");
  if (!r) std::cerr << r.error << " (status " << r.http_status << ")\n";
  ```

- **`bool`** (extract / rate): `false` means the request or JSON parse
  failed — call `lastError()` for the reason. For `getExtractList`, a `true`
  return with `ready == false` simply means "extraction not finished yet",
  which is why `waitForExtractList` keeps polling.

---

## 5. Notes & assumptions

- **Endpoint is fixed**: the constructor always targets `<host>
  is not part of the public API.
- **Completion** is detected when every selected item reports `rate >= 100`; the
  numeric meaning of the top-level `state` enum is not defined by the interface.
- `getExtractList` returns an empty/`null` body until extraction finishes; the
  library reports this as `ready == false` (not an error).
- The `type` field is read as a string, since the controller may send it as
  either a number or a string.
- Upgrade package: Supports firmware upgrades for controllers and joints (files ending in [.realman]) and end-effector controllers (filename must be tool.bin).
