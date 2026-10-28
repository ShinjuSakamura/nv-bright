# nv-bright：Legion Y7000X IAX11

项目目录：`~/nv-bright`。

基于 [msteele/nv-bright](https://github.com/msteele/nv-bright) 的本机适配版，上游基线为 `c1263e9fbaffd738a100e2b3c5823c7c50c6fbaa`。使用原项目的 NVIDIA RM/AUX 接口，控制 EDO EF25QBA63.E OLED 面板的绝对亮度。

本机已验证：独显模式下设置 250 尼特，屏幕明显变亮；30 秒测试结束后，原模式与原目标值成功恢复并读回核对。系统亮度键与滑块调节已获用户确认。服务已启用并运行，已观察到本次开机自动启动成功；休眠恢复效果待验证。

## 运行

当前由系统服务在后台运行，使用系统滑块或亮度键调节。需要前台运行时，在本机终端手动执行：

```bash
cd ~/nv-bright
sudo systemctl stop nv-bright.service
sudo ./nv-bright-wrapper
```

程序持续读取 `/sys/class/backlight/nvidia_0/brightness`，将亮度滑块映射到 5–350 尼特。启动脚本按自身位置查找同目录的可执行程序。前台运行时保持终端开启，Ctrl+C 停止程序；随后执行 `sudo systemctl start nv-bright.service` 恢复后台控制。

`nv-bright-session` 通过系统 D-Bus 订阅本机 `seat0` 图形会话的解锁通知，解锁后等待 500 毫秒，再通知 `watch` 按当前滑块数值恢复一次亮度。监听器使用系统已有的 Python 3、D-Bus 和 GLib 组件，以事件循环等待通知。锁屏、远程会话、非活动会话和其他座席的通知不会触发恢复；再次锁屏会取消尚未执行的恢复。

`watch` 在启动、亮度数值变化或收到恢复通知时写入面板，平时没有周期性的面板寄存器核对。屏幕关闭时暂停亮度读写，尚未完成的设置会等待屏幕点亮。NVIDIA 接口暂时不可用时重试，只有写入并读回核对成功才记录为已设置。事件恢复检查见 `verification/watch-recovery.json`；本机闲置锁屏后的解锁恢复效果待验证。

手动查看面板状态或单次设置亮度时，先停止后台服务，操作结束后恢复：

```bash
sudo systemctl stop nv-bright.service
sudo ./nv-bright
sudo ./nv-bright lum 250000
sudo systemctl start nv-bright.service
```

`lum` 参数单位为毫尼特；250000 对应 250 尼特。程序接受的单次设置上限为 400000，启动脚本当前配置的上限为 350 尼特。

## 开机自启

服务已安装并启用，随附的 `nv-bright.service` 使用本机项目路径。需要重新安装服务时，由用户在本机终端手动执行：

```bash
sudo install -m 644 ~/nv-bright/nv-bright.service /etc/systemd/system/nv-bright.service
sudo systemctl daemon-reload
sudo systemctl enable --now nv-bright.service
```

启用服务前，用 Ctrl+C 结束前台的 nv-bright，保持单个控制进程。启用后可关闭终端。

状态与日志：

```bash
systemctl status nv-bright.service --no-pager
journalctl -u nv-bright.service -b --no-pager
```

停止并取消自启：

```bash
sudo systemctl disable --now nv-bright.service
```

## 编译与验证资料

使用已有的 GCC 和 make 编译：

```bash
make
```

`iax11.patch` 包含相对上游的程序、启动配置、构建文件和运行说明补丁；Git 历史保留在 `.git/`。`verification/` 保存实机日志、只读面板探测和模拟检查记录。`sha256sum -c SHA256SUMS` 可核对交付文件。

适配内容包括 NVIDIA AUX 长度编码、绝对亮度模式启用及读回检查、亮度范围和参数校验。适配状态见 `verification/适配判定.md`；亮度模式的实机定位结果见 `verification/亮度模式定位.md`。
