//
// yaml-cpp shim - a minimal, exception-free re-implementation of the exact
// slice of the yaml-cpp 0.6 API that CityBuilderEngine touches, so the engine
// can be built on EwokOS without vendoring yaml-cpp itself.
//
// Why a shim and not the real library:
//   * The whole EwokOS Qt environment (Qt itself included) is compiled
//     -fno-exceptions -fno-rtti with a freestanding EWOK_STL.  yaml-cpp throws
//     (BadConversion, ParserException, InvalidNode) all over its read path and
//     cannot be built here.  Neither can the CBE code that catches/throws.
//   * CBE only ever *reads* YAML: config.yaml, the per-map .yaml files and the
//     per-building manifest.yaml files.  It never emits, so the writer half of
//     yaml-cpp is dead weight.
//
// The API surface reproduced here is exactly what a grep of CBE turns up:
//   YAML::LoadFile / YAML::Load
//   Node: operator[](const char* / std::string / QString / int), as<T>(),
//         IsMap/IsScalar/IsSequence/IsDefined/IsNull, Scalar(), size(),
//         begin()/end(), contextual operator bool, and .first/.second on the
//         elements yielded when iterating a map.
//   convert<T>: the primary template plus specializations for the arithmetic
//         and std::string types; CBE specializes it further for QString,
//         QPoint and QSize in src/global/yamlLibraryEnhancement.hpp, which is
//         why the primary template has to be visible from a <yaml-cpp/...>
//         include exactly as upstream's is.
//
// Semantics deliberately differ from yaml-cpp in the one place exceptions used
// to matter: a missing key, a wrong-typed read or an as<T>() on a non-scalar
// yields an *undefined* Node / a default-constructed T instead of throwing.
// CBE already guards every required key with `if (!node[key]) ...`, so an
// undefined Node is exactly the "not there" signal it expects.
//
// Layout note: Node holds its parsed data behind a shared_ptr, so a Node is a
// cheap handle and a subtree stays alive for as long as any handle to it does.
// That makes CBE's habit of binding `node[key]` temporaries to `const Node&`
// members (ModelReader) safe here in a way it is not with upstream's arena.
//
#ifndef EWOKOS_YAML_CPP_SHIM_HPP
#define EWOKOS_YAML_CPP_SHIM_HPP

#include <QtCore/QString>

#include <cstdlib>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace YAML {

// ---- the parsed tree -------------------------------------------------------

enum class NodeType { Undefined, Null, Scalar, Sequence, Map };

struct NodeData
{
    NodeType type = NodeType::Undefined;
    std::string scalar;                                     // Scalar
    std::vector<std::shared_ptr<struct NodeData>> seq;      // Sequence
    // Map, kept as an ordered vector rather than std::map so that iteration
    // follows document order - CBE builds its control panel and building
    // tables by walking the config in the order it was written.
    std::vector<std::pair<std::string, std::shared_ptr<struct NodeData>>> map;
};

using NodeDataPtr = std::shared_ptr<NodeData>;

class Node;
class EntryRef;

// ---- iterator --------------------------------------------------------------
//
// One iterator type serves both maps and sequences.  Dereferencing a sequence
// yields the element Node; dereferencing a map yields an entry Node whose
// .first / .second address the key and the value (see EntryRef).  It holds the
// parent data by shared_ptr, so it stays valid independently of the Node it
// was obtained from.
class NodeIterator
{
    public:
        NodeIterator() = default;
        NodeIterator(NodeDataPtr parent, std::size_t index) :
            parent(std::move(parent)),
            index(index)
        {
        }

        inline Node operator*() const;

        NodeIterator& operator++() { ++index; return *this; }

        bool operator==(const NodeIterator& other) const { return index == other.index; }
        bool operator!=(const NodeIterator& other) const { return index != other.index; }

    private:
        NodeDataPtr parent;
        std::size_t index = 0;
};

// ---- EntryRef --------------------------------------------------------------
//
// The type of Node::first / Node::second.  It has to be distinct from Node:
// a Node exposes two of these by value, and a Node containing Nodes would be
// a recursive (incomplete, infinitely-sized) type.  EntryRef carries the same
// read API CBE actually uses on a map entry's key/value - as<T>(), iteration,
// and conversion to a full Node - over the same shared data.
class EntryRef
{
    public:
        EntryRef() = default;
        explicit EntryRef(NodeDataPtr data) : d(std::move(data)) {}

        template <typename T> inline T as() const;

        inline operator Node() const;

        inline NodeIterator begin() const;
        inline NodeIterator end() const;

        inline Node operator[](const char* key) const;
        inline Node operator[](const std::string& key) const;
        inline Node operator[](const QString& key) const;
        inline Node operator[](int index) const;

        inline bool IsDefined() const;
        inline bool IsNull() const;
        inline bool IsScalar() const;
        inline bool IsSequence() const;
        inline bool IsMap() const;
        inline explicit operator bool() const;
        inline std::string Scalar() const;
        inline std::size_t size() const;

    private:
        NodeDataPtr d;

        friend class Node;
};

// ---- Node ------------------------------------------------------------------

class Node
{
    public:
        Node() = default;                                   // undefined
        explicit Node(NodeDataPtr data) : d(std::move(data)) {}
        // Scalar node from a string - the one piece of the *write* API CBE's
        // convert<QString>::encode references; harmless to keep.
        explicit Node(const std::string& scalar);

        template <typename T> inline T as() const;

        Node operator[](const char* key) const { return (*this)[std::string(key)]; }
        Node operator[](const std::string& key) const;
        Node operator[](const QString& key) const { return (*this)[key.toStdString()]; }
        Node operator[](int index) const;

        bool IsDefined() const { return static_cast<bool>(d); }
        bool IsNull() const { return d && d->type == NodeType::Null; }
        bool IsScalar() const { return d && d->type == NodeType::Scalar; }
        bool IsSequence() const { return d && d->type == NodeType::Sequence; }
        bool IsMap() const { return d && d->type == NodeType::Map; }

        // Non-explicit on purpose: ModelReader::has() does `return node[key];`
        // with a bool return type, which is copy-initialization and will not
        // consider an explicit conversion function.
        operator bool() const { return IsDefined(); }

        std::string Scalar() const { return (d && d->type == NodeType::Scalar) ? d->scalar : std::string(); }

        std::size_t size() const
        {
            if (!d) {
                return 0;
            }
            if (d->type == NodeType::Map) {
                return d->map.size();
            }
            if (d->type == NodeType::Sequence) {
                return d->seq.size();
            }
            return 0;
        }

        NodeIterator begin() const;
        NodeIterator end() const;

        // Only ever populated on the entry Node a map iterator dereferences to.
        EntryRef first;
        EntryRef second;

    private:
        // Builds a map-entry Node carrying its key/value handles; used by the
        // iterator, hence the friendship.  The init list follows declaration
        // order (first, second, then d) to keep -Wreorder quiet; the three are
        // independent so the order is immaterial.
        Node(NodeDataPtr data, EntryRef key, EntryRef value) :
            first(std::move(key)),
            second(std::move(value)),
            d(std::move(data))
        {
        }

        NodeDataPtr d;

        friend class NodeIterator;
};

// ---- convert<T> ------------------------------------------------------------
//
// as<T>() dispatches here, exactly like yaml-cpp, so that CBE's own
// specializations for QString/QPoint/QSize (src/global/yamlLibraryEnhancement.hpp)
// slot in unchanged.  The primary template resolves to "leave rhs alone,
// report failure" - every type CBE reads has an explicit specialization below
// or in that header.

template <typename T>
struct convert
{
    static bool decode(const Node& node, T& rhs) { (void)node; (void)rhs; return false; }
};

template <>
struct convert<int>
{
    static bool decode(const Node& node, int& rhs)
    {
        if (!node.IsScalar()) { return false; }
        rhs = static_cast<int>(strtol(node.Scalar().c_str(), nullptr, 10));
        return true;
    }
};

template <>
struct convert<long>
{
    static bool decode(const Node& node, long& rhs)
    {
        if (!node.IsScalar()) { return false; }
        rhs = strtol(node.Scalar().c_str(), nullptr, 10);
        return true;
    }
};

template <>
struct convert<long long>
{
    static bool decode(const Node& node, long long& rhs)
    {
        if (!node.IsScalar()) { return false; }
        rhs = strtoll(node.Scalar().c_str(), nullptr, 10);
        return true;
    }
};

template <>
struct convert<unsigned>
{
    static bool decode(const Node& node, unsigned& rhs)
    {
        if (!node.IsScalar()) { return false; }
        rhs = static_cast<unsigned>(strtoul(node.Scalar().c_str(), nullptr, 10));
        return true;
    }
};

template <>
struct convert<float>
{
    static bool decode(const Node& node, float& rhs)
    {
        if (!node.IsScalar()) { return false; }
        rhs = static_cast<float>(strtod(node.Scalar().c_str(), nullptr));
        return true;
    }
};

template <>
struct convert<double>
{
    static bool decode(const Node& node, double& rhs)
    {
        if (!node.IsScalar()) { return false; }
        rhs = strtod(node.Scalar().c_str(), nullptr);
        return true;
    }
};

template <>
struct convert<bool>
{
    static bool decode(const Node& node, bool& rhs)
    {
        if (!node.IsScalar()) { return false; }
        std::string s(node.Scalar());
        rhs = (s == "true" || s == "True" || s == "TRUE" || s == "yes" || s == "Yes" ||
               s == "on" || s == "On" || s == "1");
        return true;
    }
};

template <>
struct convert<std::string>
{
    static bool decode(const Node& node, std::string& rhs)
    {
        if (!node.IsScalar()) { return false; }
        rhs = node.Scalar();
        return true;
    }
};

// ---- deferred inline definitions ------------------------------------------

template <typename T>
T Node::as() const
{
    T value = T();
    convert<T>::decode(*this, value);
    return value;
}

inline Node Node::operator[](const std::string& key) const
{
    if (d && d->type == NodeType::Map) {
        for (const auto& kv : d->map) {
            if (kv.first == key) {
                return Node(kv.second);
            }
        }
    }
    return Node();
}

inline Node Node::operator[](int index) const
{
    if (d && d->type == NodeType::Sequence && index >= 0 &&
        static_cast<std::size_t>(index) < d->seq.size()) {
        return Node(d->seq[static_cast<std::size_t>(index)]);
    }
    return Node();
}

inline NodeIterator Node::begin() const
{
    if (!d || (d->type != NodeType::Map && d->type != NodeType::Sequence)) {
        return NodeIterator();
    }
    return NodeIterator(d, 0);
}

inline NodeIterator Node::end() const
{
    if (!d || (d->type != NodeType::Map && d->type != NodeType::Sequence)) {
        return NodeIterator();
    }
    return NodeIterator(d, d->type == NodeType::Map ? d->map.size() : d->seq.size());
}

inline Node::Node(const std::string& scalar)
{
    d = std::make_shared<NodeData>();
    d->type = NodeType::Scalar;
    d->scalar = scalar;
}

inline Node NodeIterator::operator*() const
{
    if (!parent) {
        return Node();
    }
    if (parent->type == NodeType::Map && index < parent->map.size()) {
        const auto& kv = parent->map[index];
        auto keyData = std::make_shared<NodeData>();
        keyData->type = NodeType::Scalar;
        keyData->scalar = kv.first;
        // The entry Node's own data is the value; first/second give CBE the
        // key and value handles it reads through.
        return Node(kv.second, EntryRef(keyData), EntryRef(kv.second));
    }
    if (parent->type == NodeType::Sequence && index < parent->seq.size()) {
        return Node(parent->seq[index]);
    }
    return Node();
}

template <typename T>
T EntryRef::as() const
{
    return Node(d).as<T>();
}

inline EntryRef::operator Node() const
{
    return Node(d);
}

inline NodeIterator EntryRef::begin() const { return Node(d).begin(); }
inline NodeIterator EntryRef::end() const { return Node(d).end(); }

inline Node EntryRef::operator[](const char* key) const { return Node(d)[key]; }
inline Node EntryRef::operator[](const std::string& key) const { return Node(d)[key]; }
inline Node EntryRef::operator[](const QString& key) const { return Node(d)[key]; }
inline Node EntryRef::operator[](int index) const { return Node(d)[index]; }

inline bool EntryRef::IsDefined() const { return Node(d).IsDefined(); }
inline bool EntryRef::IsNull() const { return Node(d).IsNull(); }
inline bool EntryRef::IsScalar() const { return Node(d).IsScalar(); }
inline bool EntryRef::IsSequence() const { return Node(d).IsSequence(); }
inline bool EntryRef::IsMap() const { return Node(d).IsMap(); }
inline EntryRef::operator bool() const { return Node(d).IsDefined(); }
inline std::string EntryRef::Scalar() const { return Node(d).Scalar(); }
inline std::size_t EntryRef::size() const { return Node(d).size(); }

// ---- loading ---------------------------------------------------------------
//
// Defined in yaml.cpp.  LoadFile reads the whole file (an unreadable path
// yields an undefined Node rather than throwing FileNotFoundException); Load
// parses an in-memory document.
Node LoadFile(const std::string& path);
Node Load(const std::string& content);

} // namespace YAML

#endif // EWOKOS_YAML_CPP_SHIM_HPP
