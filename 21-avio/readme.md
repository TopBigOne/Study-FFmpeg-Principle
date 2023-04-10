### 自定义AVIO
* 从内存中读取AVPacket
### 流程如下
* 先用了 readFile() 函数读取本地的 mp4 文件到内存，来模拟内存场景。然后
* 调 **avio_alloc_context**() 自定义输入跟 seek 函数

### avio_alloc_context()
```c++
AVIOContext *avio_alloc_context(
                  unsigned char *buffer,
                  int buffer_size,
                  int write_flag,
                  void *opaque,
                  int (*read_packet)(void *opaque, uint8_t *buf, int buf_size),
                  int (*write_packet)(void *opaque, uint8_t *buf, int buf_size),
                  int64_t (*seek)(void *opaque, int64_t offset, int whence));

```
* 参数如下：
1. unsigned char *buffer，这是一个指针，指向一块 **av_malloc()** 申请的内存。这是我们的自定义函数跟 FFmpeg 的 API 函数沟通的桥梁。

    * 当 **write_flag** 为 **0** 时，由 自定义的回调函数 向 buffer 填充数据， **FFmpeg API 函数**取走数据。

    * 当 **write_flag** 为 **1** 时，由 FFmpeg API 函数 向 buffer 填充数据，**自定义的回调函数** 取走数据。

> 补充，我测试的时候，这里的 buffer 指针跟 回调函数 里面的 buf 指针，好像不是同一块内存，FFmpeg 注释说，buffer 可能会被替换成另一块内存。

2，int buffer_size，buffer 内存的大小，**通常设置为 4kb 大小即可**，对于一些有固定块大小的格式，例如 TS 格式，TS流的包结构是固定长度188字节的，所以你需要设置为 188 字节大小。如果这个值设置得不对，性能会下降得比较厉害，但是不会报错。

又例如，如果输入数据是 yuv，你最好把 buffer_size 设置成一帧 yuv 的大小，这样上层处理起来更加方便。

3，int write_flag ，write_flag 可以是 0 或者 1，**作用是标记 buffer 内存的用途。**

4，void *opaque，传递给我们自定义函数用的。

5，int (*read_packet)(...)，输入函数的指针。

6，int (*write_packet)(...)，输出函数的指针。

7，int (*seek)(...)，seek 函数的指针。


### IO 模式
* URL-IO模式
1. 调用 avio_open()或avio_open2()
2. 形如 ： avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
* 内存IO模式 
1. 调用 avio_alloc_context()分配AVIOContext，然后为pb赋值
2. avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, NULL);
3. ofmt_ctx->pb=avio_out;



### 参考
* [FFmpeg内存IO模式(内存区作输入或输出)](https://www.cnblogs.com/leisure_chn/p/10318145.html)





