#include <arpa/inet.h>
#include <math.h>
#include <string.h>
#include <cstdint>
#include <iostream>
#include <vector>

bool format_udp_content(
    uint8_t data_type,            // Data type (0-3)
    std::vector<char>& content,   // Content as a char vector
    std::string& out_type_str,    // Output: Type as string ("INT", etc.)
    std::string& out_value_str);  // Output: Formatted value as string