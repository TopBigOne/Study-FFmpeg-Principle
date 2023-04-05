### 丢弃音频流
* 核心代码：
```c++
fmt_ctx->streams[1]->discard = AVDISCARD_ALL;
```