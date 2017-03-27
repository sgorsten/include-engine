#include "fbx.h"
#include <sstream>
#include <iostream>

namespace fbx
{
    template<class T> T read(FILE * f, const char * desc)
    {
        T value;
        if(fread(&value, sizeof(value), 1, f) == 1) return value;
        std::ostringstream ss;
        ss << "failed to read " << desc;
        throw std::runtime_error(ss.str());
    }

    template<class T> T read_scalar(FILE * f)
    {
        return read<T>(f, typeid(T).name());
    }

    template<class T> std::vector<T> read_array(FILE * f)
    {
        const auto array_length = read<uint32_t>(f, "array_length");
        const auto encoding = read<uint32_t>(f, "encoding");
        const auto compressed_length = read<uint32_t>(f, "compressed_length");

        std::vector<T> elements(array_length);
        switch(encoding)
        {
        case 0: 
            fread(elements.data(), sizeof(T), elements.size(), f); 
            break;
        case 1:
            // TODO: Handle compressed arrays
            std::cout << "Warning: Compressed array of " << typeid(T).name() << std::endl;
            fseek(f, compressed_length, SEEK_CUR); 
            break;
        default: 
            throw std::runtime_error("unknown encoding");
        }    
        return elements;
    }

    property read_property(FILE * f)
    {
        const auto type = read<uint8_t>(f, "type");
        if(type == 'C') return read_scalar<uint8_t>(f);
        else if(type == 'Y') return read_scalar<int16_t>(f);
        else if(type == 'I') return read_scalar<int32_t>(f);
        else if(type == 'L') return read_scalar<int64_t>(f);
        else if(type == 'F') return read_scalar<float>(f);
        else if(type == 'D') return read_scalar<double>(f);
        else if(type == 'b') return read_array<uint8_t>(f);
        else if(type == 'y') return read_array<int16_t>(f);
        else if(type == 'i') return read_array<int32_t>(f);
        else if(type == 'l') return read_array<int64_t>(f);
        else if(type == 'f') return read_array<float>(f);
        else if(type == 'd') return read_array<double>(f);
        else if(type == 'S')
        {
            const auto length = read<uint32_t>(f, "length");
            std::string string(length, ' ');
            fread(&string[0], 1, length, f);
            return string;
        }
        else if(type == 'R')
        {
            const auto length = read<uint32_t>(f, "length");
            std::vector<byte> raw(length);
            fread(raw.data(), 1, length, f);
            return raw;
        }
        else 
        {
            std::ostringstream ss;
            ss << "unknown property type " << type;
            throw std::runtime_error(ss.str());
        }
    }

    node read_node(FILE * f)
    {
        // Read node header
        const auto end_offset           = read<uint32_t>(f, "end_offset");
        const auto num_properties       = read<uint32_t>(f, "num_properties");
        const auto property_list_len    = read<uint32_t>(f, "property_list_len");
        const auto name_len             = read<uint8_t>(f, "name_len");

        // Read name
        node node;
        node.name.resize(name_len);
        if(fread(&node.name[0], 1, name_len, f) != name_len) throw std::runtime_error("failed to read name");
       
        // Read property list
        auto property_list_start = ftell(f);
        for(uint32_t i=0; i<num_properties; ++i)
        {
            node.properties.push_back(read_property(f));
        }
        if(ftell(f) != property_list_start + property_list_len) throw std::runtime_error("malformed property list");   

        // Read child nodes
        while(ftell(f) < end_offset)
        {
            node.children.push_back(read_node(f));
            if(node.children.back().name.empty() && node.children.back().properties.empty() && node.children.back().children.empty())
            {
                node.children.pop_back();
                break;
            }
        }
        //if(ftell(f) != end_offset) throw std::runtime_error("malformed children list");   

        return node;
    }

    document load(const char * path)
    {
        FILE * f = fopen(path, "rb");
        fseek(f, 0, SEEK_END);
        const long file_len = ftell(f);
        fseek(f, 0, SEEK_SET);
        char header[27] {};
        fread(header, 1, sizeof(header), f);
        if(strcmp("Kaydara FBX Binary  ", header)) throw std::runtime_error("not an FBX Binary file");

        document doc;
        doc.version = reinterpret_cast<const uint32_t &>(header[23]);
        while(ftell(f) < file_len)
        {
            doc.nodes.push_back(read_node(f));
            if(doc.nodes.back().name.empty() && doc.nodes.back().properties.empty() && doc.nodes.back().children.empty())
            {
                doc.nodes.pop_back();
                break;
            }
        }
        fclose(f);

        return doc;
    }
}