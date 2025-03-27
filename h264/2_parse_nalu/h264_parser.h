//
// Created by dev on 2025/3/26.
//
#ifndef H264_PARSER_H
#define H264_PARSER_H

#include <stdint.h>
#include <stdbool.h>

// NAL Unit类型 (H.264标准 Table 7-1)
typedef enum {
    NALU_TYPE_SLICE    = 1,  // 非IDR片
    NALU_TYPE_DPA      = 2,  // 数据分区A
    NALU_TYPE_DPB      = 3,  // 数据分区B
    NALU_TYPE_DPC      = 4,  // 数据分区C
    NALU_TYPE_IDR      = 5,  // IDR片
    NALU_TYPE_SEI      = 6,  // 补充增强信息
    NALU_TYPE_SPS      = 7,  // 序列参数集
    NALU_TYPE_PPS      = 8,  // 图像参数集
    NALU_TYPE_AUD      = 9,  // 访问单元分隔符
    NALU_TYPE_EOSEQ    = 10, // 序列结束
    NALU_TYPE_EOSTREAM = 11, // 流结束
    NALU_TYPE_FILLER   = 12  // 填充数据
} NaluType;

// 比特流读取器
typedef struct {
    const uint8_t *data;    // 数据指针
    uint32_t size_bytes;    // 总字节数
    uint32_t curr_byte;     // 当前字节位置
    uint8_t  curr_bit;       // 当前比特位置 (0=MSB)
    uint32_t bits_read;     // 已读取比特数 (调试用)
} Bitstream;

// SPS参数结构
typedef struct {
    uint8_t  profile_idc;
    uint8_t  constraint_flags;
    uint8_t  level_idc;
    uint32_t seq_parameter_set_id;
    uint32_t chroma_format_idc;
    uint32_t bit_depth_luma;
    uint32_t bit_depth_chroma;
    uint32_t width, height;
    uint32_t log2_max_frame_num;
    // ... 其他字段见H.264标准 Annex E
} SeqParameterSet;

// NAL Unit结构
typedef struct {
    uint8_t  start_code_size; // 起始码长度 (3或4)
    uint8_t  forbidden_bit;
    uint8_t  nal_ref_idc;
    NaluType nal_unit_type;
    uint8_t *rbsp_data;     // 已去除防竞争字节
    uint32_t rbsp_size;
} NaluUnit;

// 初始化比特流读取器
void bs_init(Bitstream *bs, const uint8_t *data, uint32_t size);

// 读取n个比特 (1-32)
uint32_t read_bits(Bitstream *bs, uint8_t n);

// 解析NALU
NaluUnit *parse_nalu(const uint8_t *data, uint32_t size);

// 解析SPS
SeqParameterSet *parse_sps(const uint8_t *rbsp_data, uint32_t rbsp_size);

#endif