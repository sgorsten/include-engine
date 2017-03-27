#include "fbx.h"
#include <optional>
#include <sstream>
#include <zlib.h>

namespace fbx
{
    template<class T> T read(std::istream & in, const char * desc)
    {
        T value;
        if(in.read(reinterpret_cast<char *>(&value), sizeof(T))) return value;
        std::ostringstream ss;
        ss << "failed to read " << desc;
        throw std::runtime_error(ss.str());
    }

    template<class T> T read_scalar(std::istream & in)
    {
        return read<T>(in, typeid(T).name());
    }

    template<class T> std::vector<T> read_array(std::istream & in)
    {
        const auto array_length = read<uint32_t>(in, "array_length");
        const auto encoding = read<uint32_t>(in, "encoding");
        const auto compressed_length = read<uint32_t>(in, "compressed_length");

        std::vector<T> elements(array_length);
        if(encoding == 0)
        {
            if(!in.read(reinterpret_cast<char *>(elements.data()), sizeof(T)*elements.size())) throw std::runtime_error("failed to read array data");
            return elements;
        }

        if(encoding == 1)
        {
            std::vector<Byte> compressed(compressed_length);
            if(!in.read(reinterpret_cast<char *>(compressed.data()), compressed.size())) throw std::runtime_error("failed to read compressed array data");
            
            z_stream strm {};
            strm.next_in = compressed.data();
            strm.avail_in = compressed.size();
            strm.next_out = reinterpret_cast<Bytef *>(elements.data());
            strm.avail_out = elements.size() * sizeof(T);
            if(inflateInit(&strm) != Z_OK) throw std::runtime_error("inflateInit(...) failed");
            if(inflate(&strm, Z_NO_FLUSH) == Z_STREAM_ERROR) throw std::runtime_error("inflate(...) failed");
            if(inflateEnd(&strm) != Z_OK) throw std::runtime_error("inflateEnd(...) failed");
            return elements;
        }

        throw std::runtime_error("unknown array encoding");
    }

    property read_property(std::istream & in)
    {
        const auto type = read<uint8_t>(in, "type");
        if(type == 'S')
        {
            const auto length = read<uint32_t>(in, "length");
            std::string string(length, ' ');
            if(!in.read(&string[0], length)) throw std::runtime_error("failed to read string");
            return string;
        }
        else if(type == 'R')
        {
            const auto length = read<uint32_t>(in, "length");
            std::vector<uint8_t> raw(length);
            if(!in.read(reinterpret_cast<char *>(raw.data()), length)) throw std::runtime_error("failed to read raw data");
            return raw;
        }
        else if(type == 'C') return read_scalar<boolean>(in);
        else if(type == 'Y') return read_scalar<int16_t>(in);
        else if(type == 'I') return read_scalar<int32_t>(in);
        else if(type == 'L') return read_scalar<int64_t>(in);
        else if(type == 'F') return read_scalar<float>(in);
        else if(type == 'D') return read_scalar<double>(in);
        else if(type == 'b') return read_array<boolean>(in);
        else if(type == 'y') return read_array<int16_t>(in);
        else if(type == 'i') return read_array<int32_t>(in);
        else if(type == 'l') return read_array<int64_t>(in);
        else if(type == 'f') return read_array<float>(in);
        else if(type == 'd') return read_array<double>(in);
        else 
        {
            std::ostringstream ss;
            ss << "unknown property type '" << type << '\'';
            throw std::runtime_error(ss.str());
        }
    }

    std::vector<node> read_node_list(std::istream & in);

    std::optional<node> read_node(std::istream & in)
    {
        // Read node header
        const auto end_offset           = read<uint32_t>(in, "end_offset");
        const auto num_properties       = read<uint32_t>(in, "num_properties");
        const auto property_list_len    = read<uint32_t>(in, "property_list_len");
        const auto name_len             = read<uint8_t>(in, "name_len");

        // If all header entries are zero, this is a null node (used to terminate lists of child nodes)
        if(end_offset == 0 && num_properties == 0 && property_list_len == 0 && name_len == 0) return std::nullopt;

        // Read name
        node node;
        node.name.resize(name_len);
        
        if(!in.read(&node.name[0], name_len)) throw std::runtime_error("failed to read name");
       
        // Read property list
        const uint32_t property_list_start = in.tellg();
        for(uint32_t i=0; i<num_properties; ++i)
        {
            node.properties.push_back(read_property(in));
        }
        if(static_cast<uint32_t>(in.tellg()) != property_list_start + property_list_len) throw std::runtime_error("malformed property list");   

        // Read child nodes
        if(static_cast<uint32_t>(in.tellg()) != end_offset)
        {
            node.children = read_node_list(in);
            if(static_cast<uint32_t>(in.tellg()) != end_offset) throw std::runtime_error("malformed children list");           
        }

        return node;
    }

    std::vector<node> read_node_list(std::istream & in)
    {
        std::vector<node> nodes;
        while(true)
        {
            auto n = read_node(in);
            if(n) nodes.push_back(*std::move(n));
            else return nodes;
        }
    }

    document load(std::istream & in)
    {
        in.seekg(0, std::istream::end);
        uint32_t file_len = in.tellg();
        in.seekg(0, std::istream::beg);

        char header[23] {};
        if(!in.read(header, sizeof(header))) throw std::runtime_error("failed to read header");
        if(strcmp("Kaydara FBX Binary  ", header)) throw std::runtime_error("not an FBX Binary file");

        return {read<uint32_t>(in, "version"), read_node_list(in)};
    }

    struct property_printer
    {
        std::ostream & out;

        void operator() (const std::string & string) { out << '"' << string << '"'; }
        template<class T> void operator() (const T & scalar) { out << scalar; }    
        template<class T> void operator() (const std::vector<T> & array) 
        { 
            out << '[';
            for(size_t i=0; i<array.size(); ++i) out << (i?",":"") << array[i];
            out << ']';
        }    
    };

    void print(std::ostream & out, int indent, const fbx::node & node)
    {
        if(indent) out << '\n';
        for(int i=0; i<indent; ++i) out << "  ";
        out << node.name;
        for(auto & prop : node.properties) out << ' ' << prop;
        if(!node.children.empty())
        {
            out << ':';
            for(auto & child : node.children) print(out, indent + 1, child);
        }
    }
}

std::ostream & operator << (std::ostream & out, const fbx::boolean & b) { return out << (b ? "true" : "false"); }
std::ostream & operator << (std::ostream & out, const fbx::property & p) { std::visit(fbx::property_printer{out}, p); return out; }
std::ostream & operator << (std::ostream & out, const fbx::node & n) { fbx::print(out, 0, n); return out; }