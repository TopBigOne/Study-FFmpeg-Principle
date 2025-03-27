#include <stdio.h>

#include <stdlib.h>
#include <string.h>
#include <stdint.h>


#include <stdint.h>
#include <stdbool.h>
#include "h264_parser.h"


#include "h264_parser.h"
#include <stdio.h>

void analyze_h264_file(const char* filename) {
    FILE* fp = fopen(filename, "rb");
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t* data = malloc(size);
    fread(data, 1, size, fp);
    fclose(fp);

    uint32_t pos = 0;
    while (pos < size - 4) {
        // 查找NALU起始码
        if (data[pos] == 0x00 && data[pos+1] == 0x00 &&
            (data[pos+2] == 0x01 || (data[pos+2] == 0x00 && data[pos+3] == 0x01))) {

            uint32_t start_code_len = (data[pos+2] == 0x01) ? 3 : 4;
            uint32_t nalu_start = pos + start_code_len;
            uint32_t nalu_end = nalu_start;

            // 查找下一个起始码
            while (nalu_end < size - 3) {
                if (data[nalu_end] == 0x00 && data[nalu_end+1] == 0x00 &&
                    (data[nalu_end+2] == 0x01 || (data[nalu_end+2] == 0x00 && data[nalu_end+3] == 0x01))) {
                    break;
                }
                nalu_end++;
            }

            // 解析NALU
            NaluUnit* nalu = parse_nalu(data + pos, nalu_end - pos);
            if (nalu) {
                printf("NALU Type: %d, Size: %d\n", nalu->nal_unit_type, nalu->rbsp_size);

                if (nalu->nal_unit_type == NALU_TYPE_SPS) {
                    SeqParameterSet* sps = parse_sps(nalu->rbsp_data, nalu->rbsp_size);
                    printf("  Resolution: %dx%d\n", sps->width, sps->height);
                    free(sps);
                }

                free(nalu->rbsp_data);
                free(nalu);
            }

            pos = nalu_end;
        } else {
            pos++;
        }
    }

    free(data);
}

int main() {
    analyze_h264_file("/Users/dev/Desktop/yuv_data/告五人_愛人錯過.h264");
    return 0;
}
