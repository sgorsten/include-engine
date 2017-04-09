#include "fbx.h"
#include <optional>
#include <sstream>
#include <map>
#include <zlib.h>

#include <iostream>

namespace fbx
{
    namespace ast
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
                strm.avail_in = static_cast<uInt>(compressed.size());
                strm.next_out = reinterpret_cast<Bytef *>(elements.data());
                strm.avail_out = static_cast<uInt>(elements.size() * sizeof(T));
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
                return {string};
            }
            else if(type == 'R')
            {
                const auto length = read<uint32_t>(in, "length");
                std::vector<uint8_t> raw(length);
                if(!in.read(reinterpret_cast<char *>(raw.data()), length)) throw std::runtime_error("failed to read raw data");
                return {raw};
            }
            else if(type == 'C') return {read_scalar<boolean>(in)};
            else if(type == 'Y') return {read_scalar<int16_t>(in)};
            else if(type == 'I') return {read_scalar<int32_t>(in)};
            else if(type == 'L') return {read_scalar<int64_t>(in)};
            else if(type == 'F') return {read_scalar<float>(in)};
            else if(type == 'D') return {read_scalar<double>(in)};
            else if(type == 'b') return {read_array<boolean>(in)};
            else if(type == 'y') return {read_array<int16_t>(in)};
            else if(type == 'i') return {read_array<int32_t>(in)};
            else if(type == 'l') return {read_array<int64_t>(in)};
            else if(type == 'f') return {read_array<float>(in)};
            else if(type == 'd') return {read_array<double>(in)};
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

        //////////////////////////////
        // ASCII file format reader //
        //////////////////////////////

        template<class T> std::optional<T> parse_number(const std::string & s)
        {
            std::istringstream in(s);
            T number;
            if(!(in >> number)) return std::nullopt;
            in.get();
            if(!!in.good()) return std::nullopt;
            return number;
        }

        void skip_whitespace(std::istream & in)
        {
            while(true)
            {
                if(!in.good()) return;
                int ch = in.get();
                if(isspace(ch)) continue;
                if(ch == ';')
                {
                    while(!!in.good() && ch != '\n') ch = in.get();
                    continue;
                }
                in.unget();
                return;
            }
        }

        std::string parse_key(std::istream & in)
        {
            std::string s;
            while(true)
            {
                if(!in.good()) throw std::runtime_error("missing ':' after "+s);
                int ch = in.get();
                if(ch == ':') return s;
                s += ch;
            }
        }

        std::string parse_token(std::istream & in)
        {
            std::string s;
            while(true)
            {
                int ch = in.get();
                if(isspace(ch) || ch == ',') 
                {
                    in.unget();
                    return s;
                }
                s.push_back(ch);
            }
        }

        std::optional<property> parse_property(std::istream & in)
        {
            skip_whitespace(in);
            int ch = in.get();

            // Boolean
            if(ch == 'F' && (isspace(in.peek()) || in.peek() == ',')) return property{boolean{0}};
            if(ch == 'T' && (isspace(in.peek()) || in.peek() == ',')) return property{boolean{1}};

            // Number
            if(isdigit(ch) || ch == '-')
            {
                in.unget();
                auto s = parse_token(in);
                if(auto n = parse_number<int64_t>(s)) return property{*n};
                if(auto d = parse_number<double>(s)) return property{*d};
                throw std::runtime_error("not a number: "+s);
            }

            // String
            if(ch == '"')
            {
                std::string s;
                while(true)
                {
                    ch = in.get();
                    if(ch == '"') return property{s};
                    s += ch;
                }
            }

            // Array
            if(ch == '*')
            {
                auto s = parse_token(in);
                auto len = parse_number<size_t>(s);
                if(!len) throw std::runtime_error("invalid array length: "+s);
                skip_whitespace(in);
                if(in.get() != '{') throw std::runtime_error("missing array contents");
                skip_whitespace(in);
                if(parse_key(in) != "a") throw std::runtime_error("missing array contents");
                std::vector<double> contents(*len);
                for(size_t i=0; i<contents.size(); ++i)
                {
                    skip_whitespace(in);
                    if(auto d = parse_number<double>(parse_token(in))) contents[i] = *d;
                    else throw std::runtime_error("not a number: "+s);
                    skip_whitespace(in);
                    int ch = in.get();
                    if(i+1 < len && ch != ',') throw std::runtime_error("missing ,");
                    if(i+1 == len && ch != '}') throw std::runtime_error("missing }");            
                }
                return property{contents};
            }

            // Not a property
            in.unget();
            return std::nullopt;
        }

        node parse_node(std::istream & in)
        {
            skip_whitespace(in);
            node node;
            node.name = parse_key(in);
            while(true)
            {
                if(auto prop = parse_property(in))
                {
                    node.properties.push_back(*prop);

                    skip_whitespace(in);
                    int ch = in.get();
                    if(ch == ',') continue;
                    in.unget();
                }
                break;
            }

            skip_whitespace(in);
            int ch = in.get();
            if(ch == '{')
            {
                while(true)
                {
                    skip_whitespace(in);
                    int ch = in.get();
                    if(ch == '}') 
                    {
                        break;
                    }
                    in.unget();
                    node.children.push_back(parse_node(in));
                }
            }
            else in.unget();
            return node;
        }
    
        document load(std::istream & in)
        {
            // Try reading file as FBX binary
            char header[23] {};
            if(in.read(header, sizeof(header)) && strcmp("Kaydara FBX Binary  ", header) == 0)
            {
                return {read<uint32_t>(in, "version"), read_node_list(in)};
            }

            // Try reading file as FBX ascii
            in.seekg(0);
            document doc {};
            while(true) 
            {
                skip_whitespace(in);
                if(!in.good()) break;
                doc.nodes.push_back(parse_node(in));
            }
            return doc;
        }
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

    void print(std::ostream & out, int indent, const ast::node & node)
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

    template<class T, int M> void decode_attribute(linalg::vec<T,M> & attribute, const ast::property & array, size_t index) 
    {
        for(int j=0; j<M; ++j) attribute[j] = array.get<T>(index*M+j);
    }

    template<class T> constexpr T rad_to_deg = static_cast<T>(57.295779513082320876798154814105);
    template<class T> constexpr T deg_to_rad = static_cast<T>(0.0174532925199432957692369076848);

    const ast::node & find(const std::vector<ast::node> & nodes, std::string_view name)
    {
        for(auto & n : nodes) if(n.name == name) return n;
        throw std::runtime_error("missing node " + std::string(name));
    }

    template<class V, class T, int M> void decode_layer(std::vector<V> & vertices, linalg::vec<T,M> V::*attribute, const ast::node & node, std::string_view array_name) 
    {
        auto & array = find(node.children, array_name).properties[0];
        auto mapping_information_type = find(node.children, "MappingInformationType").properties[0].get_string();
        auto reference_information_type = find(node.children, "ReferenceInformationType").properties[0].get_string();
        if(mapping_information_type == "ByPolygonVertex") 
        {
            if(reference_information_type == "Direct")
            {
                for(size_t i=0; i<vertices.size(); ++i) decode_attribute(vertices[i].*attribute, array, i);
            }
            else if(reference_information_type == "IndexToDirect")
            {
                auto & index_array = find(node.children, std::string(array_name) + "Index").properties[0];
                for(size_t i=0; i<vertices.size(); ++i) decode_attribute(vertices[i].*attribute, array, index_array.get<size_t>(i));
            }
            else throw std::runtime_error("unsupported ReferenceInformationType: " + reference_information_type);    
        }
        else throw std::runtime_error("unsupported MappingInformationType: " + mapping_information_type);
        
    }

    float3 read_vector3d_property(const ast::node & prop)
    {
        return {prop.properties[4].get<float>(), prop.properties[5].get<float>(), prop.properties[6].get<float>()};
    }

    model::model(const ast::node & node)
    {
        id = node.properties[0].get<int64_t>();

        auto & prop70 = find(node.children, "Properties70"); // TODO: Is this version dependant?
        for(auto & p : prop70.children)
        {
            if(p.name != "P") continue;
            auto & prop_name = p.properties[0].get_string();
            if(prop_name == "RotationOffset") rotation_offset = read_vector3d_property(p);
            if(prop_name == "RotationPivot") rotation_pivot = read_vector3d_property(p);
            if(prop_name == "ScalingOffset") scaling_offset = read_vector3d_property(p);
            if(prop_name == "ScalingPivot") scaling_pivot = read_vector3d_property(p);
            if(prop_name == "RotationOrder") rotation_order = static_cast<fbx::rotation_order>(p.properties[4].get<int32_t>()); // Note: Just a guess, need to see one of these in the wild
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

    struct object
    {
        struct connection { const object * obj; std::optional<std::string> prop; };

        const ast::node * node;
        std::vector<connection> parents; // Objects which we were attached to via an OO or OP connection
        std::vector<connection> children; // Objects which were attached to us via an OO or OP connection
        
        int64_t get_id() const { return node->properties[0].get<int64_t>(); }
        const std::string & get_name() const { return node->properties[1].get_string(); }
        const std::string & get_type() const { return node->name; }
        const std::string & get_subtype() const { return node->properties[2].get_string(); }        

        const object * get_first_parent(std::string_view type) const { for(auto & c : parents) if(c.obj->get_type() == type) return c.obj; return nullptr; }
        const object * get_first_child(std::string_view type) const { for(auto & c : children) if(c.obj->get_type() == type) return c.obj; return nullptr; }
        auto get_children(std::string_view type) { std::vector<const object *> objects; for(auto & c : children) if(c.obj->get_type() == type) objects.push_back(c.obj); return objects; }
    };
    std::vector<object> index(const ast::document & doc)
    {
        // Obtain list of objects
        std::vector<object> objects;
        for(auto & node : find(doc.nodes, "Objects").children) objects.push_back({&node});

        // Capture all connections between objects
        auto find_object_by_id = [&objects](int64_t id) -> object & 
        { 
            for(auto & obj : objects) if(obj.get_id() == id) return obj; 
            throw std::runtime_error("invalid object ID"); 
        };
        for(auto & n : find(doc.nodes, "Connections").children)
        {
            const uint64_t from = n.properties[1].get<int64_t>(), to = n.properties[2].get<int64_t>();
            if(!to) continue;
            if(n.properties[0].get_string() == "OO")
            {
                find_object_by_id(to).children.push_back({&find_object_by_id(from), std::nullopt});
                find_object_by_id(from).parents.push_back({&find_object_by_id(to), std::nullopt});
            }
            if(n.properties[0].get_string() == "OP")
            {
                find_object_by_id(to).children.push_back({&find_object_by_id(from), n.properties[3].get_string()});
                find_object_by_id(from).parents.push_back({&find_object_by_id(to), n.properties[3].get_string()});
            }
        }

        // NOTE: We are relying on move semantics (or copy ellision) to ensure that the addresses of individual object structs do not change
        return objects;
    }

    void add_bone_weight(geometry::bone_weights & w, uint32_t index, float weight)
    {
        if(weight > w.weights[3])
        {
            w.indices[3] = index;
            w.weights[3] = weight;
        }
        for(int i=3; i>0; --i) if(w.weights[i] > w.weights[i-1])
        {
            std::swap(w.indices[i-1], w.indices[i]);
            std::swap(w.weights[i-1], w.weights[i]);
        }
    }

    document load(const ast::document & doc)
    {
        const auto objects = index(doc);

        // Load the unit scale factor
        float unit_scale_factor = 1.0f;
        for(auto & prop : find(find(doc.nodes, "GlobalSettings").children, "Properties70").children)
        {
            if(prop.name == "P" && prop.properties[0].get_string() == "UnitScaleFactor") unit_scale_factor = prop.properties[4].get<float>();
        }
               
        // Obtain skeletal meshes
        std::vector<geometry> geometries;
        for(auto & obj : objects)
        {
            if(obj.get_type() != "Geometry") continue;            

            fbx::geometry geom;
            geom.id = obj.get_id();

            // Obtain vertices
            auto & vertices_node = find(obj.node->children, "Vertices");
            if(vertices_node.properties.size() != 1) throw std::runtime_error("malformed Vertices");
            auto & vertices_array = vertices_node.properties[0];
            std::vector<float3> vertex_positions;
            for(size_t i=0; i<vertices_array.size(); i+=3) vertex_positions.push_back(float3{vertices_array.get<float>(i), vertices_array.get<float>(i+1), vertices_array.get<float>(i+2)} * unit_scale_factor);

            // Obtain bone weights and indices
            std::vector<geometry::bone_weights> vertex_position_bone_weights;
            auto * skin = obj.get_first_child("Deformer");
            if(skin && skin->get_subtype() == "Skin")
            {
                vertex_position_bone_weights.resize(vertex_positions.size());
                std::vector<const object *> bone_models;
                for(auto & cluster : skin->children)
                {
                    if(cluster.obj->get_type() != "Deformer" || cluster.obj->get_subtype() != "Cluster") continue;

                    auto * model = cluster.obj->get_first_child("Model");
                    if(!model) throw std::runtime_error("No Model affiliated with Cluster");

                    uint32_t bone_index = bone_models.size();
                    bone_models.push_back(model);                    

                    const auto & indices = find(cluster.obj->node->children, "Indexes").properties[0];
                    const auto & weights = find(cluster.obj->node->children, "Weights").properties[0];
                    const auto & transform = find(cluster.obj->node->children, "Transform").properties[0];
                    const auto & transform_link = find(cluster.obj->node->children, "TransformLink").properties[0];
                    
                    if(indices.size() != weights.size()) throw std::runtime_error("Length of Indexes array does not match length of Weights array");
                    for(size_t i=0; i<indices.size(); ++i)
                    {
                        add_bone_weight(vertex_position_bone_weights[indices.get<size_t>(i)], bone_index, weights.get<float>(i));
                    }

                    fbx::model m {*model->node};
                    bone bone {model->get_name()};
                    bone.initial_pose = m.get_model_matrix();                    

                    if(transform.size() != 16) throw std::runtime_error("Length of Transform array is not 16");
                    if(transform_link.size() != 16) throw std::runtime_error("Length of TransformLink array is not 16");
                    for(int j=0; j<4; ++j)
                    {
                        for(int i=0; i<4; ++i)
                        {
                            // TODO: Verify column major storage in FBX files
                            bone.transform[j][i] = transform.get<float>(j*4+i);
                            bone.transform_link[j][i] = transform_link.get<float>(j*4+i);
                        }
                    }
                    geom.bones.push_back(bone);
                }

                // Renormalize weights
                for(auto & w : vertex_position_bone_weights) w.weights /= sum(w.weights);

                // Make connections
                for(size_t i=0; i<bone_models.size(); ++i)
                {
                    if(auto parent = bone_models[i]->get_first_parent("Model"))
                    {
                        for(size_t j=0; j<bone_models.size(); ++j)
                        {
                            if(bone_models[j] == parent)
                            {
                                geom.bones[i].parent_index = j;
                                break;
                            }
                        }
                        if(!geom.bones[i].parent_index) 
                        {
                            geom.bones[i].parent_index = bone_models.size();
                            bone_models.push_back(parent);

                            bone b;
                            b.name = parent->get_name();
                            fbx::model m{*parent->node};
                            b.initial_pose = m.get_model_matrix();
                            geom.bones.push_back(b);
                        }
                    }
                }
            }

            // Obtain polygons
            auto & indices_node = find(obj.node->children, "PolygonVertexIndex");
            if(indices_node.properties.size() != 1) throw std::runtime_error("malformed PolygonVertexIndex");

            size_t polygon_start = 0;
            for(size_t j=0, n=indices_node.properties[0].size(); j<n; ++j)
            {
                auto i = indices_node.properties[0].get<int32_t>(j);

                // Detect end-of-polygon, indicated by a negative index
                const bool end_of_polygon = i < 0;
                if(end_of_polygon) i = ~i;

                // Store a polygon vertex
                geom.vertices.push_back({vertex_positions[i]});
                if(!vertex_position_bone_weights.empty()) geom.weights.push_back(vertex_position_bone_weights[i]);

                // Generate triangles if necessary
                if(end_of_polygon)
                {
                    for(size_t j=polygon_start+2; j<geom.vertices.size(); ++j)
                    {
                        geom.triangles.push_back(uint3{linalg::vec<size_t,3>{polygon_start, j-1, j}});
                    }
                    polygon_start = geom.vertices.size();
                }
            }

            // Obtain normals
            decode_layer(geom.vertices, &geometry::vertex::normal, find(obj.node->children, "LayerElementNormal"), "Normals");
            decode_layer(geom.vertices, &geometry::vertex::texcoord, find(obj.node->children, "LayerElementUV"), "UV");

            geometries.push_back(geom);
        }

        // Obtain full set of animation curves, indexed by the AnimationCurve index
        /*struct keyframe { int64_t key; float value; };
        std::vector<std::vector<keyframe>> curves(objects.size());
        for(auto & obj : objects)
        {
            if(obj.get_type() != "AnimationCurve") continue;
            
            auto & keyframes = curves[obj.index];
            const auto & key_time = find(obj.node->children, "KeyTime").properties[0];
            const auto & key_value = find(obj.node->children, "KeyValueFloat").properties[0];
            if(key_time.size() != key_value.size()) throw std::runtime_error("Length of KeyTime array does not match length of KeyValueFloat array");
            for(size_t i=0; i<key_time.size(); ++i) keyframes.push_back({key_time.get<int64_t>(i), key_value.get<float>(i)});
        }*/

        // For each stack, save an animation
        /*for(auto & stack : objects)
        {
            if(stack.get_type() != "AnimationStack") continue;
            if(stack.object_children.size() != 1 || stack.object_children[0]->get_type() != "AnimationLayer") throw std::runtime_error("We only support single AnimationLayer AnimationStacks at the moment");
            auto & layer = *stack.object_children[0];

            std::vector<std::optional<float3>> evaluated_values(objects.size());
            for(auto * curve_node : layer.object_children)
            {
                if(curve_node->get_type() != "AnimationCurveNode") continue;
                auto dx = curve_node->property_children.find("d|X");
                auto dy = curve_node->property_children.find("d|Y");
                auto dz = curve_node->property_children.find("d|Z");
            }
        }*/

        // Load all objects
        document d;
        /*for(auto & obj : objects)
        {
            if(obj.get_type() == "AnimationStack")
            {
                std::cout << "AnimationStack " << obj.get_id() << " is named " << obj.get_name() << ":" << std::endl;
                for(auto * layer : obj.object_children)
                {
                    if(layer->get_type() != "AnimationLayer") continue;
                    std::cout << "  AnimationLayer " << layer->get_id() << " is named " << layer->get_name() << ":" << std::endl;
                    for(auto * curve_node : layer->object_children)
                    {
                        if(curve_node->get_type() != "AnimationCurveNode") continue;
                        std::cout << "    AnimationCurveNode " << curve_node->get_id() << " has curves for";
                        for(auto & p : curve_node->property_children)
                        {
                            for(auto * curve : p.second)
                            {
                                if(curve->get_type() != "AnimationCurve") continue;
                                std::cout << " " << p.first;
                            }
                        }
                        std::cout << std::endl;
                    }
                }
            }
        }*/
        
        std::map<int64_t, std::vector<animation_keyframe>> animation_curves;
        for(auto & n : find(doc.nodes, "Objects").children)
        {
            if(n.name == "Model")
            {
                d.models.push_back(fbx::model{n});
            }
            if(n.name == "AnimationCurve")
            {
                auto & keyframes = animation_curves[n.properties[0].get<int64_t>()];
                const auto & key_time = find(n.children, "KeyTime").properties[0];
                const auto & key_value = find(n.children, "KeyValueFloat").properties[0];
                if(key_time.size() != key_value.size()) throw std::runtime_error("Length of KeyTime array does not match length of KeyValueFloat array");
                for(size_t i=0; i<key_time.size(); ++i) keyframes.push_back({key_time.get<int64_t>(i), key_value.get<float>(i)});
            }
        }

        for(auto & g : geometries)
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
            auto a = n.properties[1].get<int64_t>(), b = n.properties[2].get<int64_t>();

            // Object-to-property connection
            if(n.properties[0].get_string() == "OP")
            {
                auto & prop = n.properties[3].get_string();
                /*for(auto & c : d.curve_nodes)
                {
                    if(c.id == b)
                    {
                        auto it = animation_curves.find(a);
                        if(it == end(animation_curves)) throw std::runtime_error("no object with ID " + a);
                    
                        if(prop == "d|X") c.x = std::move(it->second);
                        else if(prop == "d|Y") c.y = std::move(it->second);
                        else if(prop == "d|Z") c.z = std::move(it->second);
                        else throw std::runtime_error("unknown property " + prop);
                        animation_curves.erase(it);
                    }
                }*/
            }
            else if(n.properties[0].get_string() == "OO") // Object-to-object connection
            {
                for(auto & m : d.models)
                {
                    if(m.id == b)
                    {
                        for(auto & g : geometries)
                        {
                            if(g.id == a)
                            {
                                m.geoms.push_back(g);
                            }
                        }
                    }
                }
            }
            else throw std::runtime_error("Unknown connection type " + n.properties[0].get_string());
        }

        return d;
    }
}

struct fbx_property_printer
{
    std::ostream & out;

    void operator() (const std::string & string) { out << '"' << string << '"'; }
    template<class T> void operator() (const T & scalar) { out << scalar; }    
    template<class T> void operator() (const std::vector<T> & array) { out << typeid(T).name() << '[' << array.size() << ']'; }
};
void fbx::ast::property::print(std::ostream & out) const
{
    std::visit(fbx_property_printer{out}, contents);
}

std::ostream & operator << (std::ostream & out, const fbx::ast::boolean & b) { return out << (b ? "true" : "false"); }
std::ostream & operator << (std::ostream & out, const fbx::ast::property & p) { p.print(out); return out; }
std::ostream & operator << (std::ostream & out, const fbx::ast::node & n) { fbx::print(out, 0, n); return out; }
