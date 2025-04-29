
#include "common.h"

bool format_udp_content(
    uint8_t data_type,           // Data type (0-3)
    std::vector<char>& content,  // Content as a char vector
    std::string& out_type_str,   // Output: Type as string ("INT", etc.)
    std::string& out_value_str)  // Output: Formatted value as string
{
    const char* content_ptr = content.data();
    size_t content_len = content.size();

    switch (data_type) {
        case 0: {  // INT
            out_type_str = "INT";
            if (content_len < 5) {  // 1 sign + 4 uint32_t
                // std::cerr << "Warning: UDP INT content too short ("
                //           << content_len << " bytes).\n";
                return false;
            }
            // Read the sign (first byte)
            char sign_char = content_ptr[0];
            uint8_t sign = (sign_char == 1) ? 1 : 0;

            // Read the absolute value (4 bytes, index 1-4)
            uint32_t num_net;
            memcpy(&num_net, content_ptr + 1, sizeof(num_net));
            uint32_t num_host = ntohl(num_net);

            long long val = (sign == 1) ? -static_cast<long long>(num_host)
                                        : static_cast<long long>(num_host);
            out_value_str = std::to_string(val);
            return true;
        }
        case 1: {  // SHORT_REAL
            out_type_str = "SHORT_REAL";
            if (content_len < 2) {  // 2 bytes for uint16_t
                // std::cerr << "Warning: UDP SHORT_REAL content too short ("
                //           << content_len << " bytes).\n";
                return false;
            }
            uint16_t num_net;
            memcpy(&num_net, content_ptr, sizeof(num_net));
            uint16_t num_host = ntohs(num_net);
            double val = static_cast<double>(num_host) / 100.0;

            // We only need 2 decimal places
            char float_buf[40];
            snprintf(float_buf, sizeof(float_buf), "%.2f", val);
            out_value_str = float_buf;
            return true;
        }
        case 2: {  // FLOAT
            out_type_str = "FLOAT";
            if (content_len < 6) {  // 1 sign + 4 bytes for uint32_t + 1 power
                // std::cerr << "Warning: UDP FLOAT content too short ("
                //           << content_len << " bytes).\n";
                return false;
            }
            // Reading the sign (1st byte, index 0)
            char sign_char = content_ptr[0];
            uint8_t sign = (sign_char == 1) ? 1 : 0;

            // Reading the absolute value (4 bytes, index 1-4)
            uint32_t num_abs_val_net;
            memcpy(&num_abs_val_net, content_ptr + 1, sizeof(num_abs_val_net));
            uint32_t num_abs_val_host = ntohl(num_abs_val_net);

            // Reading the negative power of 10 (byte 6, index 5)
            uint8_t power_neg = static_cast<uint8_t>(content_ptr[5]);

            double val = static_cast<double>(num_abs_val_host);
            if (power_neg > 0) {
                val /= std::pow(10.0, power_neg);
            }
            if (sign == 1) {
                val = -val;
            }
            out_value_str = std::to_string(val);
            return true;
        }
        case 3: {  // STRING
            out_type_str = "STRING";
            out_value_str.assign(content.begin(), content.end());
            return true;
        }
        default:
            out_type_str = "INVALID_TYPE";
            out_value_str = "Unknown Data Type";
            // std::cerr << "Error: Invalid UDP data type encountered: "
            //           << static_cast<int>(data_type) << "\n";
            return false;
    }
}