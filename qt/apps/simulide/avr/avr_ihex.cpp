/*
 * Intel HEX File Loader Implementation
 */

#include "avr_ihex.h"

int ihex_parse_line(const char *line, uint8_t *buffer, uint32_t buffer_size,
                    uint32_t *base_addr, uint32_t *max_addr) {
    // Skip leading whitespace
    while (*line == ' ' || *line == '\t') line++;
    
    // Must start with ':'
    if (*line != ':') return -1;
    line++;
    
    // Parse byte count
    int count = hex_byte(line);
    if (count < 0) return -1;
    line += 2;
    
    // Parse address (16-bit)
    int addr_hi = hex_byte(line);
    if (addr_hi < 0) return -1;
    line += 2;
    int addr_lo = hex_byte(line);
    if (addr_lo < 0) return -1;
    line += 2;
    uint32_t addr = (addr_hi << 8) | addr_lo;
    
    // Parse record type
    int type = hex_byte(line);
    if (type < 0) return -1;
    line += 2;
    
    // Calculate full address
    uint32_t full_addr = *base_addr + addr;
    
    // Parse data bytes and calculate checksum
    uint8_t checksum = count + addr_hi + addr_lo + type;
    uint8_t data[256];
    
    for (int i = 0; i < count; i++) {
        int byte = hex_byte(line);
        if (byte < 0) return -1;
        line += 2;
        data[i] = byte;
        checksum += byte;
    }
    
    // Parse and verify checksum
    int file_checksum = hex_byte(line);
    if (file_checksum < 0) return -1;
    
    checksum += file_checksum;
    if (checksum != 0) {
        // Checksum error - but continue anyway (some files have bad checksums)
        // return -1;
    }
    
    // Process based on record type
    switch (type) {
    case 0x00:  // Data record
        if (full_addr + count > buffer_size) {
            return -1;  // Buffer overflow
        }
        memcpy(buffer + full_addr, data, count);
        if (full_addr + count > *max_addr) {
            *max_addr = full_addr + count;
        }
        break;
        
    case 0x01:  // End of file
        return 1;  // Signal EOF
        
    case 0x02:  // Extended segment address
        if (count >= 2) {
            *base_addr = ((data[0] << 8) | data[1]) << 4;
        }
        break;
        
    case 0x03:  // Start segment address (CS:IP) - ignore
        break;
        
    case 0x04:  // Extended linear address
        if (count >= 2) {
            *base_addr = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16);
        }
        break;
        
    case 0x05:  // Start linear address (EIP) - ignore
        break;
        
    default:
        // Unknown record type - ignore
        break;
    }
    
    return 0;
}

int ihex_load_string(const char *hex_data, uint8_t *buffer, uint32_t buffer_size, uint32_t *start_addr) {
    uint32_t base_addr = 0;
    uint32_t max_addr = 0;
    
    // Clear buffer
    memset(buffer, 0xFF, buffer_size);
    
    const char *line = hex_data;
    char line_buf[1024];
    
    while (*line) {
        // Find end of line
        const char *eol = strchr(line, '\n');
        if (!eol) eol = line + strlen(line);
        
        int line_len = eol - line;
        if (line_len > 0 && line_len < (int)sizeof(line_buf)) {
            memcpy(line_buf, line, line_len);
            line_buf[line_len] = '\0';
            
            // Remove trailing \r if present
            if (line_len > 0 && line_buf[line_len - 1] == '\r') {
                line_buf[line_len - 1] = '\0';
            }
            
            int result = ihex_parse_line(line_buf, buffer, buffer_size, &base_addr, &max_addr);
            if (result == 1) {
                // EOF record
                break;
            } else if (result < 0) {
                // Parse error - continue anyway
            }
        }
        
        // Move to next line
        line = (*eol == '\n') ? eol + 1 : eol;
    }
    
    if (start_addr) *start_addr = 0;
    return max_addr;
}

int ihex_load_file(const char *filename, uint8_t *buffer, uint32_t buffer_size, uint32_t *start_addr) {
    FILE *f = fopen(filename, "r");
    if (!f) return -1;
    
    // Read entire file
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (file_size <= 0 || file_size > 1024 * 1024) {
        fclose(f);
        return -1;
    }
    
    char *file_data = (char *)malloc(file_size + 1);
    if (!file_data) {
        fclose(f);
        return -1;
    }
    
    size_t read = fread(file_data, 1, file_size, f);
    file_data[read] = '\0';
    fclose(f);
    
    int result = ihex_load_string(file_data, buffer, buffer_size, start_addr);
    free(file_data);
    
    return result;
}
