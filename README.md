# TEN VAD AX650 端到端推理 Demo

本仓库整理了 TEN VAD 在爱芯元智 AX650 平台上的端到端推理代码。Demo 从 PCM16 单声道 16 kHz WAV 文件读取音频，完成特征提取、AX Engine 模型推理，并输出带 VAD 结果的双声道 WAV 文件。

## 目录结构

```text
github/
├── README.md                 # 本说明
├── CMakeLists.txt            # 主机/交叉编译配置
├── aarch64-linux.ini         # AArch64 交叉编译工具链配置
├── axmodel/                  # AX650 模型
│   ├── ten-vad-ax650.axmodel # 源码默认加载的文件 (需要从下载)
├── examples/
│   └── example.c             # 端到端 WAV 推理示例
├── include/
│   └── ten_vad.h             # TEN VAD C API
├── src/                      # VAD、STFT、音高估计及 AX Engine 实现
├── testaudio/
│   └── testset-audio-01.wav  # 端到端推理示例输入音频
├── LICENSE
└── NOTICES
```

`axmodel/ten-vad-ax650.axmodel` 是源码默认加载的 AX650 模型；不包含在本仓库中，部署使用时可以从 HuggingFace 模型仓库下载 [TEN VAD AX650 模型](https://huggingface.co/AXERA-TECH/ten-vad/blob/main/models/axmodel/ten-vad-ax650.axmodel)，并将模型放入 `axmodel/` 目录，可以按照下面脚本下载。确保源码加载的 `ten-vad-ax650.axmodel` 文件存在。

```bash
mkdir axmodel
curl -L https://huggingface.co/AXERA-TECH/ten-vad/resolve/main/models/axmodel/ten-vad-ax650.axmodel  -o ./axmodel/ten-vad-ax650.axmodel
``  


## 编译

编译需要 CMake 3.16 或更高版本、AArch64 交叉编译器，以及 AX650 SDK（需包含 `include/` 头文件和 `lib/libax_engine.so`、`libax_interpreter.so`、`libax_sys.so`）。在本目录执行：

```bash
cmake -S . -B build-aarch64 \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/aarch64-linux.ini" \
  -DTEN_VAD_SDK_ROOT=/path/to/ax650/board_sdk \
  -DCMAKE_INSTALL_PREFIX="$PWD/install"
cmake --build build-aarch64 -j
cmake --install build-aarch64
```

安装结果包括：

- `install/example/ten_vad_example`
- `install/aarch64-lib/lib/libten_vad.so`
- `install/aarch64-lib/include/ten_vad.h`

## 端侧运行

将 `install/`、`axmodel/`和待测试的 WAV 文件 `testaudio\`复制到 AX650 开发板。例如：

```bash
scp -r install axmodel testaudio user@<ax650-ip>:/root/username/ten-vad-ax650/
```
在开发板上进入部署根目录运行 Demo，确保当前目录下存在 `axmodel/ten-vad-ax650.axmodel`：

```bash
cd /root/username/ten-vad-ax650
export LD_LIBRARY_PATH="$PWD/install/aarch64-lib/lib:/soc/lib:/opt/lib"
install/example/ten_vad_example testaudio/testset-audio-01.wav output-stereo.wav
```

命令格式为：
```text
ten_vad_example input.wav output-stereo.wav
```

输入必须是 16 kHz、16-bit、单声道 PCM WAV。示例使用 256 个采样点（16 ms）作为帧长，默认语音阈值为 `0.5`。  
输出output-stereo.wav 是16 kHz 双声道 WAV：左声道是 VAD 标志（非语音为 0、语音为 32767(1.0)），右声道是原始音频。可以在Audacity 或Audition 等软件中图形化查看话段识别结果。
