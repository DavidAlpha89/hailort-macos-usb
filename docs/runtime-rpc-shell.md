# UGen300 交互式 Runtime RPC Shell

这个实验功能给已明确启用的 UGen300 Runtime 增加一个真实 PTY Shell。它不是 ADB，也不是
SSH：终端输入和输出通过现有 Hailo USB HRPC 通道传输，设备端启动 `/bin/sh -i`。

## 能做什么

- 保持会话状态：`cd`、`export`、shell 变量不会在每条命令后丢失。
- 支持 PTY：`tty`、终端尺寸、方向键和 Ctrl-C 等终端控制字符可传到设备端。
- 同一 CLI 仍支持 `-c` 单命令模式，便于脚本自动化。
- Shell 以 Runtime 中 `hailort_server` 的身份运行；官方镜像中该身份是 `root`。

## 直接使用

设备必须已经加载带 `HAILO_ENABLE_DEBUG_RPC_SHELL=ON` 的实验 Runtime。本机默认从被 Git
忽略的 `runtime-lab/debug-shell.token` 读取令牌：

```bash
scripts/macos/debug_shell.sh
```

退出：

```text
exit
```

单命令模式：

```bash
scripts/macos/debug_shell.sh -c 'id; uname -a; cat /etc/os-release'
```

HRPC 单个消息体上限是 2048 字节。交互模式会把连续输出自动拆成不超过 1536 字节的响应；
请求 protobuf 缓冲区是 256 字节，因此交互输入也会自动拆成 128 字节的小片。`-c` 单命令
本身最多 128 字节、输出超过 1536 字节会标记为截断；长命令、大目录和日志应使用交互模式。

也可以直接调用 CLI：

```bash
build-macos-usb/hailort/hailortcli/hailortcli \
  fw-control debug shell \
  --token-file runtime-lab/debug-shell.token
```

## Runtime 构建约束

该能力默认关闭，普通 `hailort_server` 不注册 Shell 的 HRPC action。设备端构建至少需要：

```bash
cmake -S . -B build-runtime \
  -DCMAKE_BUILD_TYPE=Release \
  -DHAILO_BUILD_HAILORT_SERVER=ON \
  -DHAILO_BUILD_GENAI_SERVER=ON \
  -DHAILO_ENABLE_DEBUG_RPC_SHELL=ON

cmake --build build-runtime --target hailort_server --parallel
```

在你有权使用的官方 Runtime ext4 副本中替换配套的 `hailort_server` 和
`libhailort.so.5.3.0`，并写入设备端令牌：

```text
/etc/hailort-debug-shell.token  root:root 0600
```

令牌建议使用 32 字节随机数的十六进制形式：

```bash
mkdir -p runtime-lab
umask 077
openssl rand -hex 32 > runtime-lab/debug-shell.token
```

不要把令牌、修改后的 ext4、qcow2、厂商 Runtime 或 HEF 提交到 Git。本仓库的
`runtime-lab/` 已整体忽略。

## ABI 检查

设备原始 Hailo 5.3 Runtime 的 glibc 上限是 `GLIBC_2.34`，原始 `libstdc++` 最高提供
`GLIBCXX_3.4.29`。把 ARM64 Linux 构建产物放进 Runtime 前必须检查：

```bash
strings hailort_server | grep -oE 'GLIBC_[0-9.]+' | sort -Vu | tail -1
strings hailort_server | grep -oE 'GLIBCXX_[0-9.]+' | sort -Vu | tail -1
readelf -d hailort_server | grep NEEDED
```

如果构建产物要求 `GLIBCXX_3.4.30`，可以改用旧版构建 sysroot；也可以给实验 server 配置
私有 `libstdc++.so.6`，但它自身的 glibc 需求仍不得超过设备值。不要直接覆盖系统
`libstdc++`，否则可能破坏 Runtime 中其他程序。

## 已验证行为

在 ASUS UGen300、Hailo-10H、HailoRT 5.3.0 上已验证：

- 同一会话中从 `/` 切换到 `/tmp` 后工作目录保持；环境变量保持。
- `tty` 返回 `/dev/pts/0`，`stty size` 返回同步后的终端行列数。
- Ctrl-C 可以中断远端 `sleep 30`，随后 Shell 继续可用。
- `exit 0` 正常关闭会话，之后可以立即重新进入。
- 单命令模式、设备 identify、YOLOv8n HEF 推理没有回归。

## 边界

- 这是 root Shell，具备修改当前内存 Runtime 的完整能力。
- 当前实现只维护一个活动 PTY；新会话会替换遗留会话。
- Runtime 是 Loader 上传到设备内存的。只要不执行厂商 Flash 更新流程，拔电或 reboot 后
  会回到 Loader，不会把这个 Shell 固化到设备 Flash。
- 官方 Runtime 没有注册这些 HRPC action；仅更新 macOS CLI 不会凭空获得 Shell。
