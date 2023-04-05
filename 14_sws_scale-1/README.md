### sws_scale() 是 libswscale 库里面一个非常常用的函数，它的功能如下：

1. 对图像的大小进行缩放。

2. 转换图像格式跟颜色空间，例如把 YUYV422 转成 RGB24 。

3. 转换像素格式的存储布局，例如把 YUYV422 转成 YUV420P ，YUYV422 是 packed 的布局，YUV 3 个分量是一起存储在 data[0] 里面的。而 YUV420P 是 planner 的布局，YUV 分别存储在 data[0] ~ data[2]。

> sws_scale() 转换到不同的颜色空间的时候，例如 yuv 转 rgb，或者 rgb 转 yuv，通常是有损失的，推荐阅读《RGB与YUV相互转换》

### 先讲解 sws_scale_1 项目的重点代码，如下：
```c++
int sws_flags = SWS_BICUBIC;
AVFrame* result_frame = av_frame_alloc();
//定义 AVFrame 的格式，宽高。
result_frame->format = AV_PIX_FMT_BGRA;
result_frame->width = 200;
result_frame->height = 100;
```

```c++
ret = av_frame_get_buffer(result_frame, 1);
```
* 重点：你必须指定像素格式，宽高，它才知道要申请多少内存。最后一个参数是对齐参数，用的是 1 字节对齐，

### sws_getCachedContext() 函数的定义如下
```c++
struct SwsContext *sws_getCachedContext(struct SwsContext *context,
int srcW, int srcH, enum AVPixelFormat srcFormat,
int dstW, int dstH, enum AVPixelFormat dstFormat,
int flags, SwsFilter *srcFilter,
SwsFilter *dstFilter, const double *param);
```
* sws_getCachedContext() 函数的名字之所以带 Cached，是因为如果 context 是 NULL，sws_getCachedContext() 函数内部就会申请 sws 实例的内存。如果 context 已经有内存了，就会复用，不会重新申请内存。
* 第 2 ~ 8 个参数就是 原始图像的信息 跟 目标图像的信息，宽高，像素格式。
* flags 参数是指使用哪种转换算法，一共有很多种算法，定义在 libswscale/swscale.h
```c++
/* values for the flags, the stuff on the command line is different */
#define SWS_FAST_BILINEAR     1
#define SWS_BILINEAR          2
#define SWS_BICUBIC           4
#define SWS_X                 8
#define SWS_POINT          0x10
#define SWS_AREA           0x20
#define SWS_BICUBLIN       0x40
```
* 不过比较常用的算法就是 SWS_BICUBIC，ffplay 播放器也是用的这个算法。

* sws_getCachedContext() 函数最后面 3 个参数很少用，直接填 NULL 就行。
### sws_scale() 函数的定义如下：
```c++
int sws_scale(struct SwsContext *c, const uint8_t *const srcSlice[],const int srcStride[], int srcSliceY, int srcSliceH,uint8_t *const dst[], const int dstStride[]);
```
* 比较重要的是 **srcSliceY** 参数，这个应该是像素内存的偏移位，偏移多少才是真正的像素数据

-----
* 有些场景，是不需要编码的，不需要编码，就不需要申请 AVFrame。有些场景只需要 sws_scale() 转换之后的内存，把内存丢给播放器播放，丢给网络，或者丢给另一个系统处理。

下面就介绍一下另一种申请图像内存的方式，流程如下：
```c++
//根据像素格式，宽高，确定内存的大小。
int buf_size = av_image_get_buffer_size(result_format, result_width, result_height, 1);
//申请内存
uint8_t* buffer = (uint8_t *)av_malloc(buf_size);
//把内存 映射 到数组，因为有些像素布局可能是 planner
av_image_fill_arrays(pixels, pitch, buffer, result_format, result_width, result_height, 1);
```


### 补充一个知识点：
* 图像相关的API函数在 libavutil/imgutil.h 能找到，
* AVFrame 相关的函数在 libavutil/frame.h 里面。
* AVFrame 是一个管理图像数据的结构体，
* AVFrame 里面有指针指向 **真正的图像内存数据**。



