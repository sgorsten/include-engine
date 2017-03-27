#include "../include-engine/fbx.h"
#include <iostream>

struct property_printer
{
    std::ostream & out;

    void operator() (const uint8_t & boolean) { out << (boolean & 1 ? "true" : "false"); }
    void operator() (const std::string & string) { out << '"' << string << '"'; }
    void operator() (const std::vector<fbx::byte> & raw) { out << "byte[" << raw.size() << "]"; }
    void operator() (const std::vector<uint8_t> & array) 
    { 
        out << '[';
        for(size_t i=0; i<array.size(); ++i) out << (i?",":"") << (array[i] & 1 ? "true" : "false");
        out << ']';
    }
    template<class T> void operator() (const T & scalar) { out << scalar; }    
    template<class T> void operator() (const std::vector<T> & array) 
    { 
        out << '[';
        for(size_t i=0; i<array.size(); ++i) out << (i?",":"") << array[i];
        out << ']';
    }    
};
std::ostream & operator << (std::ostream & out, const fbx::property & prop) { std::visit(property_printer{out}, prop); return out; }

void print_node(int indent, const fbx::node & node)
{
    for(int i=0; i<indent; ++i) std::cout << "  ";
    std::cout << node.name;
    for(auto & prop : node.properties) std::cout << ' ' << prop;
    std::cout << ':' << std::endl;
    for(auto & child : node.children) print_node(indent + 1, child);
}

int main() try
{
    const auto doc = fbx::load("test.fbx");
    std::cout << "FBX Version " << doc.version << std::endl;
    for(auto & node : doc.nodes) print_node(0, node);   
    return EXIT_SUCCESS;
}
catch(const std::exception & e)
{
    std::cerr << "\n" << e.what() << std::endl;
    return EXIT_FAILURE;
}