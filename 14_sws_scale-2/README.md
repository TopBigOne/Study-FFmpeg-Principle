* av_frame_get_buffer() 函数来申请 buffers 的内存。如下：

```c++
ret = av_frame_get_buffer(result_frame, 1);
```

### 重点：你必须指定像素格式，宽高，它才知道要申请多少内存。

-------

### 把内存映射到数组里面

* av_image_fill_arrays();

### 进行转换

* sws_scale()

