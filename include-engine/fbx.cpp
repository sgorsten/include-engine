#include "fbx.h"
#include <optional>
#include <sstream>
#include <set>
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

    struct model_transform
    {
        enum class rotation_order { xyz, xzy, yzx, yxz, zxy, zyx, spheric_xyz };
        rotation_order rot_order {rotation_order::xyz};
        float3 translation, rotation_offset, rotation_pivot; // Translation vectors
        float3 pre_rotation, rotation, post_rotation; // Euler angles in radians
        float3 scaling_offset, scaling_pivot; // Translation vectors
        float3 scaling; // Scaling factors

        model_transform() {};

        model_transform(const ast::node & node)
        {
            auto & prop70 = find(node.children, "Properties70"); // TODO: Is this version dependant?
            for(auto & p : prop70.children)
            {
                if(p.name != "P") continue;
                auto & prop_name = p.properties[0].get_string();
                if(prop_name == "RotationOffset") rotation_offset = read_vector3d_property(p);
                if(prop_name == "RotationPivot") rotation_pivot = read_vector3d_property(p);
                if(prop_name == "ScalingOffset") scaling_offset = read_vector3d_property(p);
                if(prop_name == "ScalingPivot") scaling_pivot = read_vector3d_property(p);
                if(prop_name == "RotationOrder") rot_order = static_cast<rotation_order>(p.properties[4].get<int32_t>()); // Note: Just a guess, need to see one of these in the wild
                if(prop_name == "PreRotation") pre_rotation = read_vector3d_property(p);
                if(prop_name == "PostRotation") post_rotation = read_vector3d_property(p);
                if(prop_name == "Lcl Translation") translation = read_vector3d_property(p);
                if(prop_name == "Lcl Rotation") rotation = read_vector3d_property(p);
                if(prop_name == "Lcl Scaling") scaling = read_vector3d_property(p);
            }
        }

        float4 quat_from_euler(float3 angles) const
        {
            angles *= deg_to_rad<float>;
            const float4 x = rotation_quat(float3{1,0,0}, angles.x), y = rotation_quat(float3{0,1,0}, angles.y), z = rotation_quat(float3{0,0,1}, angles.z);
            switch(rot_order)
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

        mesh::bone_keyframe get_keyframe() const        
        {
            // Derived from http://help.autodesk.com/view/FBX/2017/ENU/?guid=__files_GUID_10CDD63C_79C1_4F2D_BB28_AD2BE65A02ED_htm
            // LocalToParentTransform = T * Roff * Rp * Rpre * R * Rpost^-1 * Rp^-1 * Soff * Sp * S * Sp^-1 
            const float3 translation_before_scaling = -scaling_pivot;
            const float3 translation_after_scaling_and_before_rotation = -rotation_pivot + scaling_offset + scaling_pivot;
            const float3 translation_after_rotation = translation + rotation_offset + rotation_pivot;
            const float4 total_rotation = qmul(quat_from_euler(pre_rotation), quat_from_euler(rotation), qconj(quat_from_euler(post_rotation)));
            const float3 total_translation = translation_after_rotation + qrot(total_rotation, translation_after_scaling_and_before_rotation + scaling * translation_before_scaling);
            return {total_translation, total_rotation, scaling};
        }
    };

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
        auto get_children(std::string_view type) const { std::vector<const object *> objects; for(auto & c : children) if(c.obj->get_type() == type) objects.push_back(c.obj); return objects; }
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

    void add_bone_weight(mesh::vertex & v, uint32_t index, float weight)
    {
        if(weight > v.bone_weights[3])
        {
            v.bone_indices[3] = index;
            v.bone_weights[3] = weight;
        }
        for(int i=3; i>0; --i) if(v.bone_weights[i] > v.bone_weights[i-1])
        {
            std::swap(v.bone_indices[i-1], v.bone_indices[i]);
            std::swap(v.bone_weights[i-1], v.bone_weights[i]);
        }
    }

    std::vector<mesh> load_meshes(const ast::document & doc)
    {
        const auto objects = index(doc);
              
        // Obtain skeletal meshes
        std::vector<mesh> meshes;
        for(auto & obj : objects)
        {
            if(obj.get_type() != "Geometry") continue;            

            mesh geom;

            // Obtain vertices
            auto & vertices_node = find(obj.node->children, "Vertices");
            if(vertices_node.properties.size() != 1) throw std::runtime_error("malformed Vertices");
            auto & vertices_array = vertices_node.properties[0];
            std::vector<mesh::vertex> geom_vertices;
            for(size_t i=0; i<vertices_array.size(); i+=3) geom_vertices.push_back({{vertices_array.get<float>(i), vertices_array.get<float>(i+1), vertices_array.get<float>(i+2)}});

            // Obtain bone weights and indices
            auto * skin = obj.get_first_child("Deformer");
            if(skin && skin->get_subtype() == "Skin")
            {
                std::vector<const object *> bone_models;
                for(auto & cluster : skin->children)
                {
                    if(cluster.obj->get_type() != "Deformer" || cluster.obj->get_subtype() != "Cluster") continue;
                    auto * model = cluster.obj->get_first_child("Model");
                    if(!model) throw std::runtime_error("No Model affiliated with Cluster");

                    // Factor in bone weights for this bone
                    const auto & indices = find(cluster.obj->node->children, "Indexes").properties[0];
                    const auto & weights = find(cluster.obj->node->children, "Weights").properties[0];
                    if(indices.size() != weights.size()) throw std::runtime_error("Length of Indexes array does not match length of Weights array");
                    for(size_t i=0; i<indices.size(); ++i)
                    {
                        add_bone_weight(geom_vertices[indices.get<size_t>(i)], bone_models.size(), weights.get<float>(i));
                    }

                    // Obtain initial pose
                    bone_models.push_back(model);
                    model_transform m {*model->node};
                    mesh::bone bone {model->get_name()};
                    bone.initial_pose = m.get_keyframe();                    

                    // Obtain model-to-bone matrix
                    const auto & transform = find(cluster.obj->node->children, "Transform").properties[0];
                    if(transform.size() != 16) throw std::runtime_error("Length of Transform array is not 16");
                    for(int j=0; j<4; ++j) for(int i=0; i<4; ++i) bone.model_to_bone_matrix[j][i] = transform.get<float>(j*4+i);
                    geom.bones.push_back(bone);
                }

                // Renormalize weights
                for(auto & v : geom_vertices) v.bone_weights /= sum(v.bone_weights);

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

                            mesh::bone b;
                            b.name = parent->get_name();
                            model_transform m{*parent->node};
                            b.initial_pose = m.get_keyframe();
                            b.model_to_bone_matrix = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
                            geom.bones.push_back(b);
                        }
                    }
                }

                // Get animations
                for(auto & stack : objects)
                {
                    if(stack.get_type() != "AnimationStack") continue;
                    auto * layer = stack.get_first_child("AnimationLayer");
                    if(!layer) continue;

                    mesh::animation a;
                    a.name = stack.get_name();

                    // Generate transformation state for each bone
                    std::map<const object *, model_transform> model_transforms;
                    for(auto * model : bone_models) model_transforms[model] = model_transform(*model->node);

                    // Obtain all animation curves
                    struct curve_segment { int64_t key0, key1; float value0, value1; };
                    struct curve_state { float * target; std::vector<curve_segment> segments; size_t current; };
                    std::vector<curve_state> curves;
                    std::set<int64_t> keys;
                    for(auto * curve_node : layer->get_children("AnimationCurveNode"))
                    {
                        // Determine which property of a Model object this node is targeting
                        float3 * model_property = nullptr;
                        for(auto & p : curve_node->parents) if(p.obj->get_type() == "Model" && p.prop)
                        {
                            if(*p.prop == "Lcl Translation") model_property = &model_transforms[p.obj].translation;
                            if(*p.prop == "Lcl Rotation") model_property = &model_transforms[p.obj].rotation;
                            if(*p.prop == "Lcl Scaling") model_property = &model_transforms[p.obj].scaling;
                        }
                        if(!model_property) continue;

                        // For each AnimationCurve that is a child of this node
                        for(auto & c : curve_node->children) if(c.obj->get_type() == "AnimationCurve" && c.prop)
                        {
                            // Determine which channel this curve is targeting
                            curve_state cs {};
                            if(*c.prop == "d|X") cs.target = &model_property->x;
                            if(*c.prop == "d|Y") cs.target = &model_property->y;
                            if(*c.prop == "d|Z") cs.target = &model_property->z;
                            if(!cs.target) continue;

                            // Read the keyframes and aggregate the total keyframes in use in this stack
                            const auto & key_time = find(c.obj->node->children, "KeyTime").properties[0];
                            const auto & key_value = find(c.obj->node->children, "KeyValueFloat").properties[0];
                            if(key_time.size() != key_value.size()) throw std::runtime_error("Length of KeyTime array does not match length of KeyValueFloat array");
                            if(key_time.size() == 0) throw std::runtime_error("KeyTime/KeyValueFloat arrays are empty");

                            // Create curve segments
                            keys.insert(key_time.get<int64_t>(0));
                            cs.segments.push_back({std::numeric_limits<int64_t>::min(), key_time.get<int64_t>(0), key_value.get<float>(0), key_value.get<float>(0)});
                            for(size_t i=1; i<key_time.size(); ++i) 
                            {
                                const int64_t key0 = key_time.get<int64_t>(i-1), key1 = key_time.get<int64_t>(i);
                                keys.insert(key1);
                                cs.segments.push_back({key0, key1, key_value.get<float>(i-1), key_value.get<float>(i)});
                            }
                            size_t last = key_time.size()-1;
                            cs.segments.push_back({key_time.get<int64_t>(last), std::numeric_limits<int64_t>::max(), key_value.get<float>(last), key_value.get<float>(last)});
                            curves.push_back(std::move(cs));
                        }
                    }

                    // Determine the state of each model at each keyframe
                    for(auto key : keys)
                    {   
                        // Interpolate between keyframes
                        for(auto & curve : curves)
                        {
                            while(key > curve.segments[curve.current].key1) ++curve.current;
                            const auto & seg = curve.segments[curve.current];
                            if(seg.value0 == seg.value1) *curve.target = seg.value0;
                            else 
                            {
                                // TODO: Apply nonlinear mappings
                                float t = (float)(key - seg.key0)/(seg.key1 - seg.key0);
                                *curve.target = seg.value0*(1-t) + seg.value1*t;
                            }
                        }

                        // Compute local matrices
                        mesh::keyframe anim_kf {key};
                        for(auto * model : bone_models) anim_kf.local_transforms.push_back({model_transforms[model].get_keyframe()});
                        a.keyframes.push_back(anim_kf);
                    }

                    geom.animations.push_back(a);
                }
            }

            std::vector<std::vector<uint3>> material_triangles;

            auto layer_material = find(obj.node->children, "LayerElementMaterial");
            auto mapping_information_type = find(layer_material.children, "MappingInformationType").properties[0].get_string();
            auto reference_information_type = find(layer_material.children, "ReferenceInformationType").properties[0].get_string();
            if(mapping_information_type != "ByPolygon" || reference_information_type != "IndexToDirect") throw std::runtime_error("Unsupported LayerElementMaterial mapping");
            auto & materials = find(layer_material.children, "Materials").properties[0];
            size_t polygon_index = 0;

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
                geom.vertices.push_back(geom_vertices[i]);

                // Generate triangles if necessary
                if(end_of_polygon)
                {
                    auto material_index = materials.get<size_t>(polygon_index);
                    if(material_index >= material_triangles.size()) material_triangles.resize(material_index+1);

                    for(size_t j=polygon_start+2; j<geom.vertices.size(); ++j)
                    {
                        material_triangles[material_index].push_back(uint3{linalg::vec<size_t,3>{polygon_start, j-1, j}});
                    }
                    ++polygon_index;
                    polygon_start = geom.vertices.size();
                }
            }

            // Obtain normals and UVs
            decode_layer(geom.vertices, &mesh::vertex::normal, find(obj.node->children, "LayerElementNormal"), "Normals");
            decode_layer(geom.vertices, &mesh::vertex::texcoord, find(obj.node->children, "LayerElementUV"), "UV");
            for(auto & v : geom.vertices) v.texcoord.y = 1 - v.texcoord.y;

            for(auto & tris : material_triangles)
            {
                geom.materials.push_back({geom.triangles.size(), tris.size()});
                geom.triangles.insert(end(geom.triangles), begin(tris), end(tris));
            }

            meshes.push_back(geom);
        }
        
        return meshes;
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
