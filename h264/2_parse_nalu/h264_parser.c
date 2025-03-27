//
// Created by dev on 2025/3/26.
//

#include "h264_parser.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

// 初始化比特流
void bs_init(Bitstream* bs, const uint8_t* data, uint32_t size) {
    bs->data = data;
    bs->size_bytes = size;
    bs->curr_byte = 0;
    bs->curr_bit = 0;
    bs->bits_read = 0;
}

// 读取n个比特 (MSB-first)
// 修改后的比特读取函数（h264_parser.c）
uint32_t read_bits(Bitstream* bs, uint8_t n) {
    // 参数校验（替代assert，更友好的错误处理）
    if (n == 0 || n > 32) {
        fprintf(stderr, "Invalid bit count: %d (must be 1-32)\n", n);
        return 0;
    }

    // 检查剩余比特是否足够
    uint32_t remaining_bits = (bs->size_bytes - bs->curr_byte) * 8 - bs->curr_bit;
    if (n > remaining_bits) {
        fprintf(stderr, "Requested %d bits but only %d available\n", n, remaining_bits);
        return 0;
    }

    uint32_t val = 0;
    for (uint8_t i = 0; i < n; i++) {
        // 从当前字节提取比特（MSB-first）
        uint8_t bit = (bs->data[bs->curr_byte] >> (7 - bs->curr_bit)) & 0x1;
        val = (val << 1) | bit;

        // 更新位置
        bs->curr_bit++;
        if (bs->curr_bit >= 8) {
            bs->curr_byte++;
            bs->curr_bit = 0;
        }
    }
    return val;
}


// 移除防竞争字节 (0x000003 -> 0x000000)
static uint8_t *remove_emulation_prevention(const uint8_t *data, uint32_t size, uint32_t *out_size) {
    uint8_t  *clean = malloc(size);
    uint32_t j      = 0;

    for (uint32_t i = 0; i < size; i++) {
        if (i >= 2 && data[i] == 0x03 && data[i - 1] == 0x00 && data[i - 2] == 0x00) {
            continue; // 跳过防竞争字节
        }
        clean[j++] = data[i];
    }

    *out_size = j;
    return realloc(clean, j);
}

// 解析NALU
NaluUnit *parse_nalu(const uint8_t *data, uint32_t size) {
    if (size < 4) return NULL;

    // 检测起始码 (0x000001 或 0x00000001)
    uint32_t start_offset    = 0;
    uint8_t  start_code_size = 3;
    if (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x01) {
        start_code_size = 4;
        start_offset    = 4;
    } else if (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01) {
        start_offset = 3;
    } else {
        return NULL; // 无效起始码
    }

    // 解析NAL Header
    uint8_t  nal_header = data[start_offset];
    NaluUnit *nalu      = calloc(1, sizeof(NaluUnit));
    nalu->forbidden_bit   = (nal_header >> 7) & 0x1;
    nalu->nal_ref_idc     = (nal_header >> 5) & 0x3;
    nalu->nal_unit_type   = nal_header & 0x1F;
    nalu->start_code_size = start_code_size;

    // 提取RBSP (去除防竞争字节)
    uint32_t rbsp_size = size - start_offset - 1;
    nalu->rbsp_data = remove_emulation_prevention(data + start_offset + 1, rbsp_size, &nalu->rbsp_size);

    return nalu;
}

// 指数哥伦布解码
static uint32_t decode_uev(Bitstream *bs) {
    int leading_zeros = 0;
    while (read_bits(bs, 1) == 0 && leading_zeros < 32) {
        leading_zeros++;
    }
    return (1 << leading_zeros) - 1 + read_bits(bs, leading_zeros);
}

// 获取剩余可用比特数
uint32_t bs_remaining_bits(const Bitstream* bs) {
    return (bs->size_bytes - bs->curr_byte) * 8 - bs->curr_bit;
}

// 安全读取UEV（指数哥伦布编码）
uint32_t safe_decode_uev(Bitstream* bs) {
    if (bs_remaining_bits(bs) < 1) return 0;

    int leading_zeros = 0;
    while (read_bits(bs, 1) == 0 && leading_zeros < 32) {
        leading_zeros++;
        if (bs_remaining_bits(bs) < 1) break;
    }

    if (leading_zeros >= 32 || bs_remaining_bits(bs) < leading_zeros) {
        return 0; // 错误处理
    }
    return (1 << leading_zeros) - 1 + read_bits(bs, leading_zeros);
}


// 解析SPS
SeqParameterSet *parse_sps(const uint8_t *rbsp_data, uint32_t rbsp_size) {
    Bitstream bs;
    bs_init(&bs, rbsp_data, rbsp_size);

    SeqParameterSet *sps = calloc(1, sizeof(SeqParameterSet));
    sps->profile_idc          = read_bits(&bs, 8);
    if (bs_remaining_bits(&bs) < 16) { // 检查剩余比特
        fprintf(stderr, "Incomplete SPS data\n");
        return NULL;
    }


    sps->constraint_flags     = read_bits(&bs, 8);
    sps->level_idc            = read_bits(&bs, 8);
    sps->seq_parameter_set_id = decode_uev(&bs);

    // 处理Chroma格式
    if (sps->profile_idc >= 100) { // High Profile
        sps->chroma_format_idc = decode_uev(&bs);
        if (sps->chroma_format_idc == 3) {
            read_bits(&bs, 1); // separate_colour_plane_flag
        }
        sps->bit_depth_luma   = decode_uev(&bs) + 8;
        sps->bit_depth_chroma = decode_uev(&bs) + 8;
        read_bits(&bs, 1); // qpprime_y_zero_transform_bypass_flag
    }

    // 解析分辨率
    uint32_t width_mbs  = decode_uev(&bs) + 1;
    uint32_t height_mbs = decode_uev(&bs) + 1;
    sps->width  = width_mbs * 16;
    sps->height = height_mbs * 16;

    // 跳过后续字段 (实际项目需完整实现)
    return sps;
}

