# geodata

这是一个手工维护的产品、网站及集合规则库。源文件放在 `data/`，由单文件 C++ 编译器 `build.cpp` 直接编码为未压缩的 Protobuf 文件 `geodata.dat`。

GitHub Releases 提供编译完成的 `geodata.dat`。仓库只提交可审查的数据源和 C++ 编译器，避免二进制产物进入 Git 历史。

项目刻意保持简单：只保留一个 `data` 目录、一个 `build.cpp` 和编译产物，不引入 Protobuf、压缩库、脚本运行时或额外源码目录。

## 当前约定

- 一个文件就是一个同名产品、网站或集合，例如 `data/youtube`、`data/google`、`data/category-video`。
- 允许使用 geosite 风格的 `include:`，编译时递归展开、按属性筛选、去重并移除被父域覆盖的普通子域。
- 产品和网站是基础数据；集合应优先通过 `include:` 组合基础数据，不要复制同一批规则。
- 允许明确属于产品的官方 CIDR，例如 Telegram 官方公布的网段。
- 不使用 ASN 生成规则，也不能把整个 ASN 推断为某个产品。
- 不收录来源不明、由 DNS 临时解析得到或仅凭观察得到的单个 IP。
- IP 数据按来源可信度判断，不按前缀长度判断。产品官方接口明确发布的 IPv4 `/32` 和 IPv6 `/128` 应完整保留；例如 `222.167.198.21` 这类没有可信来源的单 IP 不属于收录范围。
- 云厂商和 CDN 地址只能进入 `aws`、`google-cloud`、`azure`、`cloudflare`、`fastly` 等基础设施集合，不能据此推断其租户产品。
- 文件名就是产品或集合名称，数据文件不设置 `@company`、`@product` 等元数据头。
- 最终只发布未压缩的 `geodata.dat`；不再生成 XZ 或其他外层压缩格式。

## 数据格式

空行和 `#` 注释会被忽略。裸域名等价于 `domain:`。

```text
# 匹配域名本身及其所有子域
domain:example.com
example.net

# 仅匹配完整域名
full:api.example.com

# 关键词和正则表达式
keyword:example
regexp:^.+\.example\.org$

# 仅限有权威来源的产品网段
ip:149.154.160.0/20
ip6:2001:b28:f23d::/48

# 引用另一个产品或集合
include:google
include:youtube @cn
include:youtube @-cn
```

规则末尾可以保留 V2Fly 属性，例如 `@cn`。`include:目标 @属性` 只引入具有该属性的规则，`@-属性` 排除具有该属性的规则。属性只参与构建时筛选，不写入最终 `geodata.dat`。

## 上游策略

### 1. 基础主干

[V2Fly/domain-list-community](https://github.com/v2fly/domain-list-community) 是域名规则的主要上游。同步时保留其产品文件、网站文件、集合文件、`include` 和属性语义，不把所有引用提前复制成文本；展开由编译器完成。

当前 V2Fly 基线提交为 `3b7fbb1ee566a4dc4ee4c91fe5ca8c4ea7cecfa9`（2026-07-17）。后续同步完成后，应在本节更新为新的实际提交号。

### 2. 社区补充

[MetaCubeX/meta-rules-dat](https://github.com/MetaCubeX/meta-rules-dat) 只能作为候选补充和差异参考，不能整体导入其 `geosite.dat`、`geoip.dat` 或宽泛策略集合。

当前检查的 MetaCubeX `meta` 分支提交为 `a83b5f6c1192f83011074220dd17767a30525910`。本次从可追溯来源接收了 `biliintl`、`tracker` 和 OneDrive 增量；其余属性物化文件由本项目构建器自行处理。

接收 MetaCubeX 规则必须同时满足：

1. 是明确的产品、网站或有意义的集合；
2. 能追溯到独立原始来源、产品官方资料或人工可验证的域名；
3. 原始来源能够说明规则为何属于对应产品或网站；
4. 内容属于产品、网站或可复用集合，而不是客户端路由策略；
5. IP 部分采用产品官方发布的稳定 CIDR。

无法确认来源时直接跳过，不因规则量大而导入。

### 3. 产品官方数据

纯 IP 或域名补充优先使用产品官方机器可读数据：

- Telegram CIDR：<https://core.telegram.org/resources/cidr.txt>
- GitHub Meta API：<https://api.github.com/meta>
- Google 全局地址：<https://www.gstatic.com/ipranges/goog.json>
- Google Cloud 地址：<https://www.gstatic.com/ipranges/cloud.json>
- AWS 地址：<https://ip-ranges.amazonaws.com/ip-ranges.json>
- Azure Service Tags：<https://www.microsoft.com/en-us/download/details.aspx?id=56519>
- Cloudflare 地址：<https://api.cloudflare.com/client/v4/ips>
- Fastly 地址：<https://api.fastly.com/public-ip-list>

并非所有官方地址都应导入。只有能够明确归到当前文件名的 CIDR 才能加入；官方发布的单地址 CIDR同样属于有效数据。共享云和 CDN 数据必须保持为基础设施集合。

当前已同步的官方数据组包括：

- `telegram`；
- `google`、`google-cloud`；
- `github`、`github-actions`、`github-codespaces`、`github-copilot`、`github-pages`；
- `aws`、`cloudfront`、`route53`；
- `azure`、`cloudflare`、`fastly`。

每个数据文件的 `# BEGIN OFFICIAL CIDR` 数据块记录实际来源、同步日期和源文件 SHA-256。再次同步时应替换整个数据块，不要在旧网段后面不断追加。

### 当前构建快照

2026-07-17 完成一次完整同步和汇总：

```text
V2Fly products plus local groups: 1526
Expanded rules: 191411
Official IP/CIDR source rules: 28002
geodata.dat bytes: 3536116
geodata.dat SHA-256: 52512F4802F81DF7F4C86887CAF97280CD86769E03FA5BEEEE38620467ED8083
```

其中包含 157 条由官方源明确发布的 IPv4 `/32` 或 IPv6 `/128`。后续同步后应更新本快照，便于新会话识别异常增减。

同步 V2Fly 时必须保留或根据官方源重新生成这些补充，不能被上游文件覆盖丢失。

## 同步和汇总流程

每次更新按下面顺序执行，避免上游互相污染。

### 第一步：记录现状

```powershell
git status --short
Get-FileHash .\geodata.dat -Algorithm SHA256
.\build.exe
```

如果工作区已有用户修改，先辨认并保留，不要使用 `git reset --hard` 或覆盖整个 `data`。

### 第二步：同步 V2Fly 主干

1. 在仓库外的临时目录获取 `v2fly/domain-list-community` 最新提交。
2. 记录准确提交号，不使用模糊的“最新版”描述。
3. 将其 `data/` 与本仓库 `data/` 按文件比较。
4. 接收上游新增、修改和删除，但保护本 README 列出的官方 IP 补充与本地人工规则。
5. 对删除文件先检查是否仍被本地 `include:` 引用。
6. 同步完成后更新 README 中的 V2Fly 提交号。

不要从编译后的 `geosite.dat` 反向覆盖文本；应直接同步可审查的上游 `data` 文件。

### 第三步：检查 MetaCubeX 差异

1. 只查找 V2Fly 中没有的产品、网站或集合。
2. 查明每一项的原始来源；仅标注 MetaCubeX 仓库本身不算完成溯源。
3. 只提取产品、网站和能够用 `include:` 表达的集合；路由策略、国家地址和广告聚合不属于这一层数据。
4. 与现有父域、完整域名和引用关系比较，避免重复。
5. 合格规则转换为本项目文本格式后，放入对应 `data/产品名`；集合使用 `include:`。

### 第四步：同步官方 CIDR

1. 下载官方源并记录获取日期及上游内容哈希。
2. 只选择明确产品范围；不要导入共享基础设施中的租户猜测。
3. 将网络地址规范化；官方源中的 `/32`、`/128` 按原样保留。
4. 同一产品的重叠网段保留覆盖范围合理的最小集合。
5. 域名和 CIDR 放在同一个产品文件中，例如 `data/telegram`。

### 第五步：构建和审计

Windows/MinGW：

```powershell
g++ -std=c++20 -O2 -s .\build.cpp -lws2_32 -o .\build.exe
.\build.exe
Get-FileHash .\geodata.dat -Algorithm SHA256
```

Linux：

```sh
g++ -std=c++20 -O2 -s build.cpp -o build
./build
sha256sum geodata.dat
```

构建必须成功报告产品数、展开后的规则数和输出字节数。随后至少执行这些检查：

```powershell
# 列出单地址 CIDR，逐项确认都位于可信来源标记的数据块内
rg -n '^ip:[^#\s]+/32(?:\s|$)|^ip6:[^#\s]+/128(?:\s|$)' .\data

# 筛查不属于当前格式的元数据头；正常结果应为空
rg -n '^@' .\data

# 检查所有 IP 规则，逐项确认来源
rg -n '^ip6?:' .\data
```

元数据头检查应无输出；单地址 CIDR 检查允许有输出，但必须能追溯到对应文件中的官方来源。还要确认：

- 没有缺失的 `include`；
- 没有循环引用；
- 没有非法域名、CIDR 或规则类型；
- `geodata.dat` 是本次重新构建的未压缩 Protobuf；
- 产品数或规则数发生异常大幅变化时，先检查差异，不能直接接受。

## 二进制格式

`build.cpp` 不依赖 Protobuf 运行库，而是直接写入兼容的 wire format：

```proto
message Database { repeated Product products = 1; }
message Product  { string name = 1; repeated Rule rules = 2; }
message Rule     { Type type = 1; bytes value = 2; uint32 prefix = 3; }
```

域名、完整域名、关键词和正则以文本写入 `value`。IPv4/IPv6 以 4/16 字节网络序写入 `value`，前缀长度写入 `prefix`。类型编号以 `build.cpp` 中的 `Type` 枚举为准。

## 新会话接手清单

新会话开始维护时，先阅读本 README 和 `build.cpp`，然后遵守以下边界：

1. 不改变“一个 `data` 目录 + 单文件 C++ 编译器 + `geodata.dat`”结构；
2. V2Fly 是主干，MetaCubeX 只是逐项溯源后的补充；
3. 上游范围是 V2Fly 主干、逐项溯源的 MetaCubeX 补充和产品官方数据；
4. 保留 `include`，不要在文本数据中预展开；
5. 只接收明确产品/网站、合理集合和官方产品 CIDR；
6. IP 采用产品官方 CIDR，不用 ASN、临时解析 IP和来源不明单 IP；官方 `/32`、`/128` 完整保留；
7. 同步前保护本地官方 IP 补充，同步后重新编译并报告产品数、规则数、文件大小和 SHA-256；
8. 所有新规则都应说明来源，无法追溯就不收录。

## 发布

每次完成同步、审计和确定性构建后发布新版本：

1. 提交 `README.md`、`build.cpp` 和 `data/`；
2. 不提交本地 `build.exe`、`build` 和 `geodata.dat`；
3. 版本号使用 `vYYYY.MM.DD`，同一天再次发布时追加 `.2`、`.3`；
4. Release 附件上传未压缩的 `geodata.dat`；
5. Release 说明记录产品数、规则数、字节数、SHA-256 和上游基线。

## 许可证

本仓库按 GPL-3.0 发布。各上游数据仍保留其原有许可证和归属，具体来源记录在本 README 及相应数据文件的注释中。
