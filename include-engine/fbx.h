#ifndef FBX_H
#define FBX_H

#include <string>
#include <vector>
#include <variant>

namespace fbx
{
    struct boolean 
    { 
        uint8_t byte; 
        explicit operator bool() const { return static_cast<bool>(byte & 1); } 
    };

    using property = std::variant
    <
        boolean,               // type 'C'
        int16_t,               // type 'Y'
        int32_t,               // type 'I'
        int64_t,               // type 'L'
        float,                 // type 'F'
        double,                // type 'D'
        std::vector<boolean>,  // type 'b'
        std::vector<int16_t>,  // type 'y'
        std::vector<int32_t>,  // type 'i'
        std::vector<int64_t>,  // type 'l'
        std::vector<float>,    // type 'f'
        std::vector<double>,   // type 'd'
        std::string,           // type 'S'
        std::vector<uint8_t>   // type 'R'
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

    document load(std::istream & in);
}

std::ostream & operator << (std::ostream & out, const fbx::boolean & b);
std::ostream & operator << (std::ostream & out, const fbx::property & p);
std::ostream & operator << (std::ostream & out, const fbx::node & n);

#endif
