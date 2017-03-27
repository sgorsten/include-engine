#ifndef FBX_H
#define FBX_H

#include <string>
#include <vector>
#include <variant>

namespace fbx
{
    enum byte : uint8_t {};

    using property = std::variant<
        uint8_t,               // type C: 1 byte boolean value (0 or 1)
        int16_t,               // type Y: 2 byte signed integer
        int32_t,               // type I: 4 byte signed integer
        int64_t,               // type L: 8 byte signed integer
        float,                 // type F: 4 byte floating point
        double,                // type D: 8 byte floating point
        std::vector<uint8_t>,  // type b: array of 1 byte boolean values (0 or 1)
        std::vector<int16_t>,  // type y: array of 2 byte signed integers
        std::vector<int32_t>,  // type i: array of 4 byte signed integers
        std::vector<int64_t>,  // type l: array of 8 byte signed integers
        std::vector<float>,    // type f: array of 4 byte floating points
        std::vector<double>,   // type d: array of 8 byte floating points
        std::string,           // type S: string
        std::vector<byte>      // type R: raw binary data
    >;

    struct node
    {
        std::string name;
        std::vector<property> properties;
        std::vector<node> children;
    };

    struct document
    {
        uint32_t version;
        std::vector<node> nodes;
    };

    document load(const char * path);
}

#endif
