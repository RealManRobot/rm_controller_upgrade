# **使用 upgrade\_client 库**

upgrade\_client 是一个小型 C++11 库，它将控制器的固件升级接口封装在一个简洁的 API 后面。其底层实现采用了 Pimpl（指向实现的指针）模式进行隐藏，因此**您只需要引入一个头文件**（upgrade\_client.h）并链接一个库文件即可。

* 头文件：include/upgrade\_client.h  
* 库文件：libupgrade\_client.a（静态库）或 libupgrade\_client.so / upgrade\_client.dll（动态链接库）  
* 所有内容都位于命名空间 upgrade 中。

## **1\. 将其添加到您的构建中**

客户端会连接到控制器。

### **CMake — 链接预编译库**

add\_executable(myapp main.cpp)

target\_include\_directories(myapp PRIVATE /path/to/rm\_upgrade/include)  
target\_link\_libraries(myapp PRIVATE /path/to/libupgrade\_client.a Threads::Threads)

由于静态库不携带其依赖项，因此需要同时链接 Threads。有关完整、极简的消费方项目示例，请参阅 rm\_upgrade\_demo/。

### **手动构建 / 其他构建系统**

将 include/ 添加到包含路径中，对库进行编译/链接，并在 Linux 下链接 \-lpthread。您的代码不需要包含任何额外的头文件。

## **2\. 快速入门 — 完整升级流程**

\#include "upgrade\_client.h"  
\#include \<iostream\>

int main() {  
    upgrade::UpgradeClient client("192.168.1.100");   // 目标地址为 192.168.1.100
    client.setTimeouts(/\*connect=\*/10, /\*transfer=\*/300);

    // 1\) 上传升级包（multipart 格式：包含名称和内容）。  
    if (\!client.uploadUpgradeFile("firmware\_pkg.tar")) {  
        std::cerr \<\< "上传失败\\n";  
        return 1;  
    }

    // 2\) 等待控制器完成升级包的解压。  
    upgrade::ExtractInfo ext;  
    if (\!client.waitForExtractList(ext, /\*timeout\_s=\*/120)) {  
        std::cerr \<\< "解压失败: " \<\< client.lastError() \<\< "\\n";  
        return 1;  
    }

    // 3\) 选择需要升级的单项，然后启动升级。（此处选择升级全部。）  
    for (auto& it : ext.list) it.check \= true;  
    if (\!client.startUpgrade(ext.list)) {  
        std::cerr \<\< "startUpgrade 失败\\n";  
        return 1;  
    }

    // 4\) 轮询升级进度，直到所有勾选的单项进度均达到 100%。  
    for (;;) {  
        upgrade::RateInfo rate;  
        if (\!client.getUpgradeRate(rate)) {  
            std::cerr \<\< "轮询错误: " \<\< client.lastError() \<\< "\\n";  
            continue;   // 属于瞬态错误；重试  
        }  
        bool done \= true;  
        for (const auto& it : rate.list) {  
            std::cout \<\< it.name \<\< ": " \<\< it.rate \<\< "%\\n";  
            if (it.rate \< 100\) done \= false;  
        }  
        if (done) break;  
    }  
    std::cout \<\< "升级完成\\n";

    // 5\) 重启控制器，使新程序生效。  
    client.sysReboot();  
}

## **3\. API 参考**

### **版本**

const char\* upgrade::version();   // 例如 "v1.0.0"

### **构造与配置**

upgrade::UpgradeClient client("host");   // 目标地址为 host

UpgradeClient 是不可复制的。请**在调用任何接口之前**应用相关配置：

| 方法 | 用途 |
| :---- | :---- |
| setTimeouts(connect\_s, transfer\_s) | 设置连接超时和读写超时，单位为秒（默认值为 10 / 300）。 |

### **接口方法**

| 方法 | 返回值 | 备注 |
| :---- | :---- | :---- |
| uploadUpgradeFile(file\_path, stored\_name \= "") | CallResult | 上传升级包。stored\_name 是保存在控制器上的文件名（默认使用文件的 basename）。 |
| getExtractList(ExtractInfo& out) | bool | 单次查询。返回 true 表示请求往返正常；需检查 out.ready 以确认解压是否完成。 |
| waitForExtractList(out, timeout\_s \= 120, poll\_ms \= 1000\) | bool | 轮询调用 getExtractList，直到 out.ready 为 true 或超时。 |
| startUpgrade(std::vector\<FileItem\>) | CallResult | 每个单项的 check 字段决定了其是否执行升级。 |
| startUpgrade(std::vector\<std::pair\<std::string,bool\>\>) | CallResult | 便捷重载版本：传入 (名称, 是否勾选) 的键值对。 |
| getUpgradeRate(RateInfo& out) | bool | 获取当前各单项的进度/状态；需在循环中轮询。 |
| clearUpgrade() | CallResult | 清除控制器上的升级状态。 |
| sysReboot() | CallResult | 重启控制器（POST /robot/sysReboot）。通常在升级完成后调用，使新程序生效。 |
| lastError() | const std::string& | 获取返回值为 bool 的接口在发生传输或解析失败时的最后一条错误信息。 |

### **数据结构**

struct FileItem {  
    std::string name;    // 单项名称  
    std::string type;    // 单项类型（服务端定义的字符串）  
    int         rate;    // 进度 0..100             (getUpgradeRate)  
    int         status;  // 单项状态                (getUpgradeRate)  
    bool        check;   // 是否选中 / 可安装       (在 startUpgrade 之前设置)  
};

struct ExtractInfo {  
    int                   code;   // 服务端状态码  
    std::string           msg;  
    std::vector\<FileItem\> list;   // 已解压的单项列表（已填充 name/type/check）  
    bool                  ready;  // 当解压彻底完成时为 true  
};

struct RateInfo {  
    int                   code;  
    int                   state;  // 整体状态（服务端定义的枚举）  
    std::string           msg;  
    std::vector\<FileItem\> list;   // 各单项的进度/状态列表（已填充 rate/status）  
};

struct CallResult {  
    bool        ok;           // 传输正常 且 HTTP 状态码 \== 200  
    int         http\_status;  // 响应的状态码  
    std::string body;         // 原始响应体  
    std::string error;        // 失败时填充错误信息  
    explicit operator bool() const;   // 当且仅当 ok 为 true 时返回 true  
};

## **4\. 错误处理**

根据返回类型的不同，有两种错误处理约定：

* **CallResult**（上传 / 启动 / 清除）：直接对结果进行布尔测试 —— 只有在传输成功**且**状态码为 200 时，operator bool 才会返回 true。如果失败，可以通过 .error 读取错误信息（通过 .http\_status / .body 查看详情）。  
  auto r \= client.uploadUpgradeFile("pkg.tar");  
  if (\!r) std::cerr \<\< r.error \<\< " (status " \<\< r.http\_status \<\< ")\\n";

* **bool**（解压 / 进度率）：返回 false 意味着请求失败或 JSON 解析失败 —— 此时可调用 lastError() 获取原因。对于 getExtractList，如果返回 true 但 ready \== false，仅表示“解压尚未完成”，这也是为什么 waitForExtractList 会继续保持轮询的原因。

## **5\. 注意事项与假设**

* **端口固定**：构造函数始终指向 \<host\>。该端口不属于公共 API 的一部分。  
* **完成度判定**：当所有选中的单项报告的进度均达到 rate \>= 100 时，即视为升级完成；顶层 state 枚举的具体数字含义未在接口中定义。  
* 在解压完成之前，getExtractList 会返回一个空/null 的响应体；库会将此情况报告为 ready \== false（不视作错误）。  
* type 字段会作为字符串读取，因为控制器发送该字段时可能是数字也可能是字符串。
* 升级包：支持[.realman]结尾的控制器版本、关节版本升级，支持末端控制器版本升级（文件名只能为tool.bin）