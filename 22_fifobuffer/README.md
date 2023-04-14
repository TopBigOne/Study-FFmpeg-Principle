### AVFifoBuffer : 环形buffer 内存管理器

### AVFifoBuffer 的主要api
1. **av_fifo_alloc**()，申请一个 AVFifoBuffer 实例，可以设置初始的内存容量大小，我设置为了 10个 MyData 大小。现在这个内存管理器可以写进去 10 个MyData。

2. **av_fifo_grow**()，**扩展** AVFifoBuffer 内存容量的大小，我扩展了 5 个，现在可以存储 15 个 MyData。

3. av_fifo_size()，AVFifoBuffer 里面**有多少数据可以读**。

4. av_fifo_space()，AVFifoBuffer 里面**还有多少内存空间可以写。**

5. av_fifo_generic_write()，往 AVFifoBuffer 里面**写内存数据。**

6. av_fifo_generic_read()，往 AVFifoBuffer 里面**读内存数据。**

7. **av_fifo_freep**()，释放 AVFifoBuffer 实例。


