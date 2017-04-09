#ifndef FBX_H
#define FBX_H

#include "data-types.h"
#include <string>
#include <variant>
#include <optional>

namespace fbx
{
    // Abstract syntax tree of an FBX file, maps to both binary and textual representations
    namespace ast
    {
        struct boolean 
        { 
            uint8_t byte; 
            explicit operator bool() const { return static_cast<bool>(byte & 1); } 
        };

        using property_variant = std::variant
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

        class property
        {
            property_variant contents;

            struct size_visitor
            {
                template<class T> size_t operator() (const std::vector<T> & v) { return v.size(); }
                size_t operator() (...) { return 1; }
            };

            template<class U> struct element_visitor
            {
                size_t index;
                template<class T> U operator() (const std::vector<T> & v) { return operator()(v[index]); }
                template<class T> U operator() (const T & n) { return static_cast<U>(n); }
                U operator() (const std::string & s) { return {}; }
                U operator() (const boolean & b) { return b ? U{1} : U{0}; }
            };
        public:
            property(property_variant && contents) : contents{move(contents)} {}
        
            size_t size() const { return std::visit(size_visitor{}, contents); }
            template<class U> U get(size_t i=0) const { return std::visit(element_visitor<U>{i}, contents); }
            const std::string & get_string() const { return std::get<std::string>(contents); }
            void print(std::ostream & out) const;
        };

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

    /////////////////////
    // FBX Scene Graph //
    /////////////////////
    
    std::vector<mesh> load_meshes(const ast::document & doc);
}

std::ostream & operator << (std::ostream & out, const fbx::ast::boolean & b);
std::ostream & operator << (std::ostream & out, const fbx::ast::property & p);
std::ostream & operator << (std::ostream & out, const fbx::ast::node & n);

#endif
