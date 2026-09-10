//
// yaml-cpp shim implementation - the reader/parser half.  See yaml.h for the
// API contract and why this exists instead of the real yaml-cpp.
//
// The parser handles exactly the YAML subset CityBuilderEngine's data uses:
//   * indentation-based block mappings and block sequences
//   * flow mappings { k: v, ... } and flow sequences [ v, ... ], nested
//   * "- key: value" sequence items whose map continues on following lines
//   * plain/quoted scalars, inline `# comments`
// It does not attempt anchors, aliases, tags, multi-document streams, block
// scalars (| >) or multi-line flow collections - none appear in config.yaml,
// the map .yaml files or the per-building manifest.yaml files.
//
// Strategy: preprocess the text into a flat list of (indent, text, seqMarker)
// lines - every "- x" becomes a bare marker line plus a content line indented
// to the column x starts at - then recursive-descent over that list by indent.
// Normalizing the dash away up front is what makes "- file: a\n  position: b"
// fall out as an ordinary block map at the deeper indent.
//
#include <yaml-cpp/yaml.h>

#include <fstream>

namespace YAML {
namespace {

struct Line
{
    int indent = 0;
    std::string text;
    bool seqMarker = false;   // a bare "-" introducing a sequence element
};

NodeDataPtr newNode(NodeType type)
{
    auto data = std::make_shared<NodeData>();
    data->type = type;
    return data;
}

NodeDataPtr newScalar(const std::string& value)
{
    auto data = newNode(NodeType::Scalar);
    data->scalar = value;
    return data;
}

std::string trim(const std::string& s)
{
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && (s[begin] == ' ' || s[begin] == '\t' || s[begin] == '\r')) {
        ++begin;
    }
    while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r')) {
        --end;
    }
    return s.substr(begin, end - begin);
}

std::string unquote(const std::string& s)
{
    if (s.size() >= 2) {
        char front = s[0];
        char back = s[s.size() - 1];
        if ((front == '"' && back == '"') || (front == '\'' && back == '\'')) {
            return s.substr(1, s.size() - 2);
        }
    }
    return s;
}

// Drops a trailing `# comment`, honouring quotes so a '#' inside a quoted
// scalar survives.  A '#' only starts a comment at the beginning of the
// content or after whitespace.
std::string stripComment(const std::string& line)
{
    bool inSingle = false;
    bool inDouble = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '\'' && !inDouble) {
            inSingle = !inSingle;
        }
        else if (c == '"' && !inSingle) {
            inDouble = !inDouble;
        }
        else if (c == '#' && !inSingle && !inDouble) {
            if (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t') {
                return line.substr(0, i);
            }
        }
    }
    return line;
}

std::vector<Line> preprocess(const std::string& content)
{
    std::vector<Line> lines;
    std::size_t start = 0;
    while (start <= content.size()) {
        std::size_t nl = content.find('\n', start);
        std::string raw = (nl == std::string::npos) ? content.substr(start)
                                                    : content.substr(start, nl - start);
        if (nl == std::string::npos) {
            start = content.size() + 1;
        }
        else {
            start = nl + 1;
        }

        // Indentation = run of leading spaces (the data uses spaces only).
        int indent = 0;
        while (static_cast<std::size_t>(indent) < raw.size() && raw[indent] == ' ') {
            ++indent;
        }
        std::string body = trim(stripComment(raw.substr(indent)));
        if (body.empty()) {
            continue;   // blank or comment-only line
        }

        if (body[0] == '-' && (body.size() == 1 || body[1] == ' ' || body[1] == '\t')) {
            Line marker;
            marker.indent = indent;
            marker.text.clear();
            marker.seqMarker = true;
            lines.push_back(marker);

            std::size_t j = 1;
            while (j < body.size() && (body[j] == ' ' || body[j] == '\t')) {
                ++j;
            }
            std::string rest = body.substr(j);
            if (!rest.empty()) {
                Line item;
                item.indent = indent + static_cast<int>(j);   // column the content starts at
                item.text = rest;
                item.seqMarker = false;
                lines.push_back(item);
            }
        }
        else {
            Line line;
            line.indent = indent;
            line.text = body;
            line.seqMarker = false;
            lines.push_back(line);
        }
    }
    return lines;
}

// ---- flow collections ------------------------------------------------------

void skipSpaces(const std::string& s, std::size_t& i)
{
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
        ++i;
    }
}

NodeDataPtr parseFlow(const std::string& s, std::size_t& i);

NodeDataPtr parseFlowScalar(const std::string& s, std::size_t& i)
{
    std::size_t start = i;
    while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']') {
        ++i;
    }
    return newScalar(unquote(trim(s.substr(start, i - start))));
}

NodeDataPtr parseFlow(const std::string& s, std::size_t& i)
{
    skipSpaces(s, i);
    if (i >= s.size()) {
        return newNode(NodeType::Null);
    }

    if (s[i] == '{') {
        ++i;
        auto map = newNode(NodeType::Map);
        skipSpaces(s, i);
        while (i < s.size() && s[i] != '}') {
            std::size_t keyStart = i;
            while (i < s.size() && s[i] != ':' && s[i] != ',' && s[i] != '}') {
                ++i;
            }
            std::string key = trim(s.substr(keyStart, i - keyStart));
            if (i < s.size() && s[i] == ':') {
                ++i;
            }
            skipSpaces(s, i);
            NodeDataPtr value = parseFlow(s, i);
            map->map.push_back({key, value});
            skipSpaces(s, i);
            if (i < s.size() && s[i] == ',') {
                ++i;
            }
            skipSpaces(s, i);
        }
        if (i < s.size() && s[i] == '}') {
            ++i;
        }
        return map;
    }

    if (s[i] == '[') {
        ++i;
        auto seq = newNode(NodeType::Sequence);
        skipSpaces(s, i);
        while (i < s.size() && s[i] != ']') {
            seq->seq.push_back(parseFlow(s, i));
            skipSpaces(s, i);
            if (i < s.size() && s[i] == ',') {
                ++i;
            }
            skipSpaces(s, i);
        }
        if (i < s.size() && s[i] == ']') {
            ++i;
        }
        return seq;
    }

    return parseFlowScalar(s, i);
}

NodeDataPtr parseInline(const std::string& text)
{
    std::string value = trim(text);
    if (value.empty()) {
        return newNode(NodeType::Null);
    }
    if (value[0] == '{' || value[0] == '[') {
        std::size_t i = 0;
        return parseFlow(value, i);
    }
    return newScalar(unquote(value));
}

// ---- block structure -------------------------------------------------------

// True when `text` is a `key: ...` / `key:` line: it holds a ':' that is not
// inside a flow collection or quotes and is followed by a space or ends the
// line.  Returns the separator position through `colon`.
bool hasMapKey(const std::string& text, std::size_t& colon)
{
    if (!text.empty() && (text[0] == '{' || text[0] == '[')) {
        return false;
    }
    int depth = 0;
    bool inSingle = false;
    bool inDouble = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '\'' && !inDouble) {
            inSingle = !inSingle;
        }
        else if (c == '"' && !inSingle) {
            inDouble = !inDouble;
        }
        else if (!inSingle && !inDouble) {
            if (c == '{' || c == '[') {
                ++depth;
            }
            else if (c == '}' || c == ']') {
                --depth;
            }
            else if (c == ':' && depth == 0) {
                if (i + 1 == text.size() || text[i + 1] == ' ' || text[i + 1] == '\t') {
                    colon = i;
                    return true;
                }
            }
        }
    }
    return false;
}

NodeDataPtr parseBlock(const std::vector<Line>& lines, std::size_t& pos);

NodeDataPtr parseMap(const std::vector<Line>& lines, std::size_t& pos, int indent)
{
    auto map = newNode(NodeType::Map);
    while (pos < lines.size() && !lines[pos].seqMarker && lines[pos].indent == indent) {
        const std::string text = lines[pos].text;
        std::size_t colon = 0;
        ++pos;
        if (!hasMapKey(text, colon)) {
            continue;   // stray non-key line at this level; skip to stay terminating
        }
        std::string key = trim(text.substr(0, colon));
        std::string value = trim(text.substr(colon + 1));

        if (value.empty()) {
            // Nested block (deeper-indented) or a null value.
            if (pos < lines.size() &&
                (lines[pos].indent > indent ||
                 (lines[pos].seqMarker && lines[pos].indent >= indent))) {
                map->map.push_back({key, parseBlock(lines, pos)});
            }
            else {
                map->map.push_back({key, newNode(NodeType::Null)});
            }
        }
        else {
            map->map.push_back({key, parseInline(value)});
        }
    }
    return map;
}

NodeDataPtr parseSeq(const std::vector<Line>& lines, std::size_t& pos, int indent)
{
    auto seq = newNode(NodeType::Sequence);
    while (pos < lines.size() && lines[pos].seqMarker && lines[pos].indent == indent) {
        ++pos;   // consume the "-" marker
        if (pos < lines.size() && lines[pos].indent > indent) {
            seq->seq.push_back(parseBlock(lines, pos));
        }
        else {
            seq->seq.push_back(newNode(NodeType::Null));
        }
    }
    return seq;
}

NodeDataPtr parseBlock(const std::vector<Line>& lines, std::size_t& pos)
{
    if (pos >= lines.size()) {
        return newNode(NodeType::Null);
    }
    if (lines[pos].seqMarker) {
        return parseSeq(lines, pos, lines[pos].indent);
    }
    std::size_t colon = 0;
    if (hasMapKey(lines[pos].text, colon)) {
        return parseMap(lines, pos, lines[pos].indent);
    }
    // A lone scalar / flow value occupying a single line.
    NodeDataPtr value = parseInline(lines[pos].text);
    ++pos;
    return value;
}

} // namespace

Node Load(const std::string& content)
{
    std::vector<Line> lines = preprocess(content);
    if (lines.empty()) {
        return Node();
    }
    std::size_t pos = 0;
    return Node(parseBlock(lines, pos));
}

Node LoadFile(const std::string& path)
{
    std::ifstream file(path.c_str());
    if (!file.is_open()) {
        return Node();   // undefined; CBE reports missing files itself
    }

    // EWOK_STL's <fstream> has no std::getline (and there is no <istream>),
    // only get()/read(); read the whole file a char at a time and let Load()
    // do the line splitting.  get() returns int and yields -1 at EOF, which no
    // real char equals, so the loop bound is unambiguous.
    std::string content;
    for (int ch = file.get(); ch != -1; ch = file.get()) {
        content.push_back(static_cast<char>(ch));
    }
    file.close();

    return Load(content);
}

} // namespace YAML
