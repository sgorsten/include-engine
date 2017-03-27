#include "../include-engine/fbx.h"
#include <iostream>

struct property_printer
{
    void operator() (const uint8_t & boolean) { std::cout << (boolean & 1 ? "true" : "false"); }
    void operator() (const std::string & string) { std::cout << '"' << string << '"'; }
    template<class T> void operator() (const std::vector<T> & array) { std::cout << typeid(T).name() << '[' << array.size() << ']'; }
    template<class T> void operator() (const T & scalar) { std::cout << scalar; }
};

void print_node(int indent, const fbx::node & node)
{
    for(int i=0; i<indent; ++i) std::cout << "  ";
    std::cout << node.name;
    for(auto & prop : node.properties)
    {
        std::cout << ' ';
        std::visit(property_printer{}, prop);
    }
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