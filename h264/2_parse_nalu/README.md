#如何解析NALU

### 关键点总结
分层解析：先分离NALU，再按类型解析SPS/PPS/Slice

比特流处理：严格按H.264的MSB-first顺序读取比特

内存安全：所有动态分配的内存均有释放

标准兼容：遵循ITU-T H.264 Annex B规范