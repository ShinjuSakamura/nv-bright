# nv-bright：Legion Y7000X IAX11

项目目录：`~/nv-bright`。

基于 [msteele/nv-bright](https://github.com/msteele/nv-bright) 的本机适配版，上游基线为 `c1263e9fbaffd738a100e2b3c5823c7c50c6fbaa`。使用原项目的 NVIDIA RM/AUX 接口，控制 EDO EF25QBA63.E OLED 面板的绝对亮度。

本机已验证：独显模式下设置 250 尼特，屏幕明显变亮；30 秒测试结束后，原模式与原目标值成功恢复并读回核对。系统亮度键与滑块调节已获用户确认。服务已启用并运行；实际重启和休眠恢复效果待验证。

## 运行

当前由系统服务在后台运行，使用系统滑块或亮度键调节。需要前台运行时，在本机终端手动执行：

```bash
cd ~/nv-bright
sudo systemctl stop nv-bright.service
sudo ./nv-bright-wrapper
```

程序持续读取 `/sys/class/backlight/nvidia_0/brightness`，将亮度滑块映射到 5–250 尼特。启动脚本按自身位置查找同目录的可执行程序。前台运行时保持终端开启，Ctrl+C 停止程序；随后执行 `sudo systemctl start nv-bright.service` 恢复后台控制。

手动查看面板状态或单次设置亮度时，先停止后台服务，操作结束后恢复：

```bash
sudo systemctl stop nv-bright.service
sudo ./nv-bright
sudo ./nv-bright lum 250000
sudo systemctl start nv-bright.service
```

`lum` 参数单位为毫尼特；250000 对应 250 尼特。程序接受的单次设置上限为 400000，启动脚本采用已实测的 250 尼特上限。

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

`iax11.patch` 包含相对上游的当前修改；Git 历史保留在 `.git/`。`verification/` 保存实机日志、只读面板探测和模拟检查记录。`sha256sum -c SHA256SUMS` 可核对交付文件。

适配内容包括 NVIDIA AUX 长度编码、绝对亮度模式启用及读回检查、亮度范围和参数校验。适配状态见 `verification/适配判定.md`；亮度模式的实机定位结果见 `verification/亮度模式定位.md`。
