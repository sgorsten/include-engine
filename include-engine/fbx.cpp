#include "fbx.h"
#include <optional>
#include <sstream>
#include <zlib.h>

#include <iostream>

namespace fbx
{
    ///////////////////////////////
    // Binary file format reader //
    ///////////////////////////////

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

    //////////////////////////////
    // ASCII file format reader //
    //////////////////////////////

    template<class T> std::optional<T> parse_number(const std::string & s)
    {
        std::istringstream in(s);
        T number;
        if(!(in >> number)) return std::nullopt;
        in.get();
        if(!in.eof()) return std::nullopt;
        return number;
    }

    void skip_whitespace(FILE * f)
    {
        while(true)
        {
            if(feof(f)) return;
            int ch = fgetc(f);
            if(isspace(ch)) continue;
            if(ch == ';')
            {
                while(!feof(f) && ch != '\n') ch = fgetc(f);
                continue;
            }
            ungetc(ch, f);
            return;
        }
    }

    std::string parse_key(FILE * f)
    {
        std::string s;
        while(true)
        {
            if(feof(f)) throw std::runtime_error("missing ':' after "+s);
            int ch = fgetc(f);
            if(ch == ':') return s;
            s += ch;
        }
    }

    std::string parse_token(FILE * f)
    {
        std::string s;
        while(true)
        {
            int ch = fgetc(f);
            if(isspace(ch) || ch == ',') 
            {
                ungetc(ch, f);
                return s;
            }
            s.push_back(ch);
        }
    }

    std::optional<fbx::property> parse_property(FILE * f)
    {
        skip_whitespace(f);
        int ch = fgetc(f);

        // Boolean (TODO: Fix ambiguity)
        if(ch == 'F') return boolean{0};
        if(ch == 'T') return boolean{1};        

        // Number
        if(isdigit(ch) || ch == '-')
        {
            ungetc(ch, f);
            auto s = parse_token(f);
            if(auto n = parse_number<int64_t>(s)) return *n;
            if(auto d = parse_number<double>(s)) return *d;
            throw std::runtime_error("not a number: "+s);
        }

        // String
        if(ch == '"')
        {
            std::string s;
            while(true)
            {
                ch = fgetc(f);
                if(ch == '"') return s;
                s += ch;
            }
        }

        // Array
        if(ch == '*')
        {
            auto s = parse_token(f);
            auto len = parse_number<size_t>(s);
            if(!len) throw std::runtime_error("invalid array length: "+s);
            skip_whitespace(f);
            if(fgetc(f) != '{') throw std::runtime_error("missing array contents");
            skip_whitespace(f);
            if(parse_key(f) != "a") throw std::runtime_error("missing array contents");
            std::vector<double> contents(*len);
            for(size_t i=0; i<contents.size(); ++i)
            {
                skip_whitespace(f);
                if(auto d = parse_number<double>(parse_token(f))) contents[i] = *d;
                else throw std::runtime_error("not a number: "+s);
                skip_whitespace(f);
                int ch = fgetc(f);
                if(i+1 < len && ch != ',') throw std::runtime_error("missing ,");
                if(i+1 == len && ch != '}') throw std::runtime_error("missing }");            
            }
            return contents;
        }

        // Not a property
        ungetc(ch, f);
        return std::nullopt;
    }

    fbx::node parse_node(FILE * f)
    {
        skip_whitespace(f);
        fbx::node node;
        node.name = parse_key(f);
        while(true)
        {
            if(auto prop = parse_property(f))
            {
                node.properties.push_back(*prop);

                skip_whitespace(f);
                int ch = fgetc(f);
                if(ch == ',') continue;
                ungetc(ch, f);
            }
            break;
        }

        skip_whitespace(f);
        int ch = fgetc(f);
        if(ch == '{')
        {
            while(true)
            {
                skip_whitespace(f);
                int ch = fgetc(f);
                if(ch == '}') 
                {
                    break;
                }
                ungetc(ch, f);
                node.children.push_back(parse_node(f));
            }
        }
        else ungetc(ch, f);
        return node;
    }

    document load_ascii(FILE * f)
    {
        document doc {};
        while(true) 
        {
            skip_whitespace(f);
            if(feof(f)) break;
            doc.nodes.push_back(parse_node(f));
        }
        return doc;
    }

    ///////////
    // Other //
    ///////////

    struct property_printer
    {
        std::ostream & out;

        void operator() (const std::string & string) { out << '"' << string << '"'; }
        template<class T> void operator() (const T & scalar) { out << scalar; }    
        template<class T> void operator() (const std::vector<T> & array) { out << typeid(T).name() << '[' << array.size() << ']'; }
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

    template<class T, int M> void decode_attribute(linalg::vec<T,M> & attribute, const std::vector<double> & array, size_t index) 
    {
        for(int j=0; j<M; ++j) attribute[j] = static_cast<T>(array[index*M+j]);
    }

    template<class T> constexpr T rad_to_deg = static_cast<T>(57.295779513082320876798154814105);
    template<class T> constexpr T deg_to_rad = static_cast<T>(0.0174532925199432957692369076848);

    const fbx::node & find(const std::vector<fbx::node> & nodes, std::string_view name)
    {
        for(auto & n : nodes) if(n.name == name) return n;
        throw std::runtime_error("missing node " + std::string(name));
    }

    struct vector_size_visitor
    {
        template<class T> size_t operator() (const std::vector<T> & v) { return v.size(); }
        size_t operator() (...) { return 0; }
    };

    template<class U> struct vector_element_visitor
    {
        size_t index;
        template<class T> U operator() (const std::vector<T> & v) { return static_cast<U>(v[index]); }
        U operator() (const std::vector<boolean> & v) { return static_cast<U>(v[index] ? 1 : 0); }
        U operator() (int32_t n) { return static_cast<U>(n); }
        U operator() (int64_t n) { return static_cast<U>(n); }
        U operator() (double n) { return static_cast<U>(n); }
        U operator() (...) { return {}; }
    };

    size_t get_vector_size(const property & p) { return std::visit(vector_size_visitor{}, p); }
    template<class U> U get_vector_element(const property & p, size_t i) { return std::visit(vector_element_visitor<U>{i}, p); }
    template<class U> U get_property(const property & p) { return std::visit(vector_element_visitor<U>{0}, p); }

    template<class V, class T, int M> void decode_layer(std::vector<V> & vertices, linalg::vec<T,M> V::*attribute, const fbx::node & node, std::string_view array_name) 
    {
        auto & array = std::get<std::vector<double>>(find(node.children, array_name).properties[0]);
        auto mapping_information_type = std::get<std::string>(find(node.children, "MappingInformationType").properties[0]);
        auto reference_information_type = std::get<std::string>(find(node.children, "ReferenceInformationType").properties[0]);
        if(mapping_information_type == "ByPolygonVertex") 
        {
            if(reference_information_type == "Direct")
            {
                for(size_t i=0; i<vertices.size(); ++i) decode_attribute(vertices[i].*attribute, array, i);
            }
            else if(reference_information_type == "IndexToDirect")
            {
                auto & index_array = find(node.children, std::string(array_name) + "Index").properties[0];
                for(size_t i=0; i<vertices.size(); ++i) decode_attribute(vertices[i].*attribute, array, get_vector_element<size_t>(index_array, i));
            }
            else throw std::runtime_error("unsupported ReferenceInformationType: " + reference_information_type);    
        }
        else throw std::runtime_error("unsupported MappingInformationType: " + mapping_information_type);
        
    }

    geometry::geometry(const fbx::node & node)
    {
        id = std::get<int64_t>(node.properties[0]);

        // Obtain vertices
        auto & vertices_node = find(node.children, "Vertices");
        if(vertices_node.properties.size() != 1) throw std::runtime_error("malformed Vertices");
        auto & vertices_array = std::get<std::vector<double>>(vertices_node.properties[0]);
        std::vector<float3> vertex_positions;
        for(size_t i=0; i<vertices_array.size(); i+=3) vertex_positions.push_back(float3{double3{vertices_array[i], vertices_array[i+1], vertices_array[i+2]}});

        // Obtain polygons
        auto & indices_node = find(node.children, "PolygonVertexIndex");
        if(indices_node.properties.size() != 1) throw std::runtime_error("malformed PolygonVertexIndex");

        size_t polygon_start = 0;
        for(size_t j=0, n=get_vector_size(indices_node.properties[0]); j<n; ++j)
        {
            auto i = get_vector_element<int32_t>(indices_node.properties[0], j);

            // Detect end-of-polygon, indicated by a negative index
            const bool end_of_polygon = i < 0;
            if(end_of_polygon) i = ~i;

            // Store a polygon vertex
            vertices.push_back({vertex_positions[i]});

            // Generate triangles if necessary
            if(end_of_polygon)
            {
                for(size_t j=polygon_start+2; j<vertices.size(); ++j)
                {
                    triangles.push_back(uint3{linalg::vec<size_t,3>{polygon_start, j-1, j}});
                }
                polygon_start = vertices.size();
            }
        }

        // Obtain normals
        decode_layer(vertices, &vertex::normal, find(node.children, "LayerElementNormal"), "Normals");
        decode_layer(vertices, &vertex::texcoord, find(node.children, "LayerElementUV"), "UV");
    }

    float3 read_vector3d_property(const fbx::node & prop)
    {
        return float3{double3{get_property<double>(prop.properties[4]), get_property<double>(prop.properties[5]), get_property<double>(prop.properties[6])}};
    }

    model::model(const fbx::node & node)
    {
        id = std::get<int64_t>(node.properties[0]);

        auto & prop70 = find(node.children, "Properties70"); // TODO: Is this version dependant?
        for(auto & p : prop70.children)
        {
            if(p.name != "P") continue;
            auto & prop_name = std::get<std::string>(p.properties[0]);
            if(prop_name == "RotationOffset") rotation_offset = read_vector3d_property(p);
            if(prop_name == "RotationPivot") rotation_pivot = read_vector3d_property(p);
            if(prop_name == "ScalingOffset") scaling_offset = read_vector3d_property(p);
            if(prop_name == "ScalingPivot") scaling_pivot = read_vector3d_property(p);
            if(prop_name == "RotationOrder") rotation_order = static_cast<fbx::rotation_order>(std::get<int32_t>(p.properties[4])); // Note: Just a guess, need to see one of these in the wild
            if(prop_name == "PreRotation") pre_rotation = read_vector3d_property(p) * deg_to_rad<float>;
            if(prop_name == "PostRotation") post_rotation = read_vector3d_property(p) * deg_to_rad<float>;
            if(prop_name == "Lcl Translation") translation = read_vector3d_property(p);
            if(prop_name == "Lcl Rotation") rotation = read_vector3d_property(p) * deg_to_rad<float>;
            if(prop_name == "Lcl Scaling") scaling = read_vector3d_property(p);
        }
    }

    float4 quat_from_euler(rotation_order order, const float3 & angles)
    {
        const float4 x = rotation_quat(float3{1,0,0}, angles.x), y = rotation_quat(float3{0,1,0}, angles.y), z = rotation_quat(float3{0,0,1}, angles.z);
        switch(order)
        {
        case rotation_order::xyz: return qmul(z, y, x);
        case rotation_order::xzy: return qmul(y, z, x);
        case rotation_order::yzx: return qmul(x, z, y);
        case rotation_order::yxz: return qmul(z, x, y);
        case rotation_order::zxy: return qmul(y, x, z);
        case rotation_order::zyx: return qmul(x, y, z);
        case rotation_order::spheric_xyz: throw std::runtime_error("spheric_xyz rotation order not yet supported");
        default: throw std::runtime_error("bad rotation_order");
        }
    }

    float4x4 model::get_model_matrix() const
    {
        // Derived from http://help.autodesk.com/view/FBX/2017/ENU/?guid=__files_GUID_10CDD63C_79C1_4F2D_BB28_AD2BE65A02ED_htm
        // LocalToParentTransform = T * Roff * Rp * Rpre * R * Rpost^-1 * Rp^-1 * Soff * Sp * S * Sp -1 
        return mul
        (
            translation_matrix(translation + rotation_offset + rotation_pivot),
            rotation_matrix(qmul
            (
                quat_from_euler(rotation_order, pre_rotation), 
                quat_from_euler(rotation_order, rotation), 
                qconj(quat_from_euler(rotation_order, post_rotation))
            )), 
            translation_matrix(-rotation_pivot + scaling_offset + scaling_pivot),
            scaling_matrix(scaling), 
            translation_matrix(-scaling_pivot)
        );        
    }

    std::vector<model> load_models(const fbx::document & doc)
    {
        // Load all objects
        std::vector<model> models;
        std::vector<geometry> geoms;
        for(auto & n : find(doc.nodes, "Objects").children)
        {
            if(n.name == "Model")
            {
                models.push_back(fbx::model{n});
            }
            if(n.name == "Geometry")
            {
                geoms.push_back(fbx::geometry{n});
            }
        }

        for(auto & g : geoms)
        {
            for(auto & v : g.vertices)
            {
                v.texcoord.y = 1 - v.texcoord.y;
            }
        }

        // Connect objects
        for(auto & n : find(doc.nodes, "Connections").children)
        {
            if(n.name != "C") continue;
            if(std::get<std::string>(n.properties[0]) != "OO") continue; // Object-to-object connection
            auto a = std::get<int64_t>(n.properties[1]), b = std::get<int64_t>(n.properties[2]);
            for(auto & m : models)
            {
                if(m.id == b)
                {
                    for(auto & g : geoms)
                    {
                        if(g.id == a)
                        {
                            m.geoms.push_back(g);
                        }
                    }
                }
            }
        }

        return models;
    }
}

std::ostream & operator << (std::ostream & out, const fbx::boolean & b) { return out << (b ? "true" : "false"); }
std::ostream & operator << (std::ostream & out, const fbx::property & p) { std::visit(fbx::property_printer{out}, p); return out; }
std::ostream & operator << (std::ostream & out, const fbx::node & n) { fbx::print(out, 0, n); return out; }
