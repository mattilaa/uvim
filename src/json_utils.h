#pragma once

#include <yyjson.h>

#include <climits>
#include <cstdint>
#include <cstdlib>
#include <istream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace json_utils
{
enum JsonType
{
    kNullType,
    kFalseType,
    kTrueType,
    kObjectType,
    kArrayType,
    kStringType,
    kNumberType,
};

using SizeType = std::size_t;

struct Alloc
{};

class Name
{
public:
    Name() = default;
    explicit Name(const char* text) : text_(text ? text : "") {}
    explicit Name(std::string_view text) : text_(text) {}

    bool IsString() const { return true; }
    const char* GetString() const { return text_.c_str(); }
    SizeType GetStringLength() const { return text_.size(); }
    bool equals(const char* key) const { return text_ == (key ? key : ""); }

private:
    std::string text_;
};

struct Member;

class Value
{
public:
    enum class Type
    {
        Null,
        Bool,
        Int,
        Uint,
        Double,
        String,
        Object,
        Array,
    };

    using Array = std::vector<Value>;
    using Object = std::vector<Member>;
    using iterator = Object::iterator;
    using const_iterator = Object::const_iterator;

    Value();
    explicit Value(JsonType type);
    explicit Value(const char* text);
    explicit Value(std::string_view text);
    explicit Value(bool value);
    explicit Value(int value);
    explicit Value(unsigned value);
    explicit Value(std::int64_t value);
    explicit Value(std::uint64_t value);
    explicit Value(double value);
    Value(const char* text, SizeType len, Alloc&);
    Value(const Value& other);
    Value(Value&& other) noexcept = default;
    Value& operator=(const Value& other);
    Value& operator=(Value&& other) noexcept = default;

    bool IsNull() const;
    bool IsBool() const;
    bool IsInt() const;
    bool IsUint() const;
    bool IsInt64() const;
    bool IsUint64() const;
    bool IsDouble() const;
    bool IsString() const;
    bool IsObject() const;
    bool IsArray() const;

    bool GetBool() const;
    int GetInt() const;
    unsigned GetUint() const;
    std::int64_t GetInt64() const;
    std::uint64_t GetUint64() const;
    double GetDouble() const;
    const char* GetString() const;
    SizeType GetStringLength() const;

    void SetNull();
    void SetObject();
    void SetArray();
    void SetString(const char* text, SizeType len, Alloc&);

    Array& GetArray();
    const Array& GetArray() const;
    Value& operator[](SizeType index);
    const Value& operator[](SizeType index) const;
    SizeType Size() const;
    bool Empty() const;

    iterator MemberBegin();
    iterator MemberEnd();
    const_iterator MemberBegin() const;
    const_iterator MemberEnd() const;
    iterator FindMember(const char* key);
    const_iterator FindMember(const char* key) const;

    void AddMember(const char* key, Value value, Alloc&);
    void AddMember(std::string_view key, Value value, Alloc&);
    void AddMember(const char* key, const char* value, Alloc&);
    void AddMember(const char* key, std::string_view value, Alloc&);
    void AddMember(const char* key, bool value, Alloc&);
    void AddMember(const char* key, int value, Alloc&);
    void AddMember(const char* key, unsigned value, Alloc&);
    void AddMember(const char* key, std::int64_t value, Alloc&);
    void AddMember(const char* key, std::uint64_t value, Alloc&);
    void AddMember(const char* key, double value, Alloc&);
    void PushBack(Value value, Alloc&);
    void PushBack(const char* value, Alloc&);
    void PushBack(std::string_view value, Alloc&);
    void PushBack(bool value, Alloc&);
    void PushBack(int value, Alloc&);
    void PushBack(unsigned value, Alloc&);
    void PushBack(std::int64_t value, Alloc&);
    void PushBack(std::uint64_t value, Alloc&);
    void PushBack(double value, Alloc&);
    void CopyFrom(const Value& other, Alloc&);

    Type type() const;

private:
    struct Data;

    Data& data();
    const Data& data() const;
    void clear();
    void ensure_object();
    void ensure_array();
    void set_type(JsonType type);

    std::shared_ptr<Data> data_;
};

struct Member
{
    Name name;
    Value value;
};

struct Value::Data
{
    Type type = Type::Null;
    bool boolValue = false;
    std::int64_t intValue = 0;
    std::uint64_t uintValue = 0;
    double doubleValue = 0.0;
    std::string stringValue;
    Array arrayValue;
    Object objectValue;
};

inline Value::Value() : data_(std::make_shared<Data>()) {}

inline Value::Value(JsonType type) : Value() { set_type(type); }

inline Value::Value(const char* text) : Value(std::string_view(text ? text : ""))
{}

inline Value::Value(std::string_view text) : Value()
{
    data().type = Type::String;
    data().stringValue.assign(text.data(), text.size());
}

inline Value::Value(bool value) : Value()
{
    data().type = Type::Bool;
    data().boolValue = value;
}

inline Value::Value(int value) : Value()
{
    data().type = Type::Int;
    data().intValue = value;
}

inline Value::Value(unsigned value) : Value()
{
    data().type = Type::Uint;
    data().uintValue = value;
}

inline Value::Value(std::int64_t value) : Value()
{
    data().type = Type::Int;
    data().intValue = value;
}

inline Value::Value(std::uint64_t value) : Value()
{
    data().type = Type::Uint;
    data().uintValue = value;
}

inline Value::Value(double value) : Value()
{
    data().type = Type::Double;
    data().doubleValue = value;
}

inline Value::Value(const char* text, SizeType len, Alloc&) : Value()
{
    data().type = Type::String;
    data().stringValue.assign(text ? text : "", len);
}

inline Value::Value(const Value& other)
    : data_(std::make_shared<Data>(other.data()))
{}

inline Value& Value::operator=(const Value& other)
{
    if(this != &other)
        data_ = std::make_shared<Data>(other.data());
    return *this;
}

inline Value::Data& Value::data() { return *data_; }
inline const Value::Data& Value::data() const { return *data_; }

inline bool Value::IsNull() const { return data().type == Type::Null; }
inline bool Value::IsBool() const { return data().type == Type::Bool; }
inline bool Value::IsInt() const
{
    if(data().type == Type::Int)
        return data().intValue >= INT32_MIN && data().intValue <= INT32_MAX;
    return data().type == Type::Uint && data().uintValue <= INT32_MAX;
}
inline bool Value::IsUint() const
{
    if(data().type == Type::Uint)
        return data().uintValue <= UINT32_MAX;
    return data().type == Type::Int && data().intValue >= 0 &&
           static_cast<std::uint64_t>(data().intValue) <= UINT32_MAX;
}
inline bool Value::IsInt64() const
{
    return data().type == Type::Int ||
           (data().type == Type::Uint &&
            data().uintValue <= static_cast<std::uint64_t>(INT64_MAX));
}
inline bool Value::IsUint64() const
{
    return data().type == Type::Uint ||
           (data().type == Type::Int && data().intValue >= 0);
}
inline bool Value::IsDouble() const
{
    return data().type == Type::Double || data().type == Type::Int ||
           data().type == Type::Uint;
}
inline bool Value::IsString() const { return data().type == Type::String; }
inline bool Value::IsObject() const { return data().type == Type::Object; }
inline bool Value::IsArray() const { return data().type == Type::Array; }

inline bool Value::GetBool() const { return data().boolValue; }
inline int Value::GetInt() const
{
    if(data().type == Type::Uint)
        return static_cast<int>(data().uintValue);
    return static_cast<int>(data().intValue);
}
inline unsigned Value::GetUint() const
{
    if(data().type == Type::Int)
        return static_cast<unsigned>(data().intValue);
    return static_cast<unsigned>(data().uintValue);
}
inline std::int64_t Value::GetInt64() const
{
    if(data().type == Type::Uint)
        return static_cast<std::int64_t>(data().uintValue);
    return data().intValue;
}
inline std::uint64_t Value::GetUint64() const
{
    if(data().type == Type::Int)
        return static_cast<std::uint64_t>(data().intValue);
    return data().uintValue;
}
inline double Value::GetDouble() const
{
    if(data().type == Type::Int)
        return static_cast<double>(data().intValue);
    if(data().type == Type::Uint)
        return static_cast<double>(data().uintValue);
    return data().doubleValue;
}
inline const char* Value::GetString() const { return data().stringValue.c_str(); }
inline SizeType Value::GetStringLength() const
{
    return data().stringValue.size();
}

inline void Value::clear()
{
    data().boolValue = false;
    data().intValue = 0;
    data().uintValue = 0;
    data().doubleValue = 0.0;
    data().stringValue.clear();
    data().arrayValue.clear();
    data().objectValue.clear();
}

inline void Value::SetNull()
{
    clear();
    data().type = Type::Null;
}

inline void Value::SetObject()
{
    clear();
    data().type = Type::Object;
}

inline void Value::SetArray()
{
    clear();
    data().type = Type::Array;
}

inline void Value::SetString(const char* text, SizeType len, Alloc&)
{
    clear();
    data().type = Type::String;
    data().stringValue.assign(text ? text : "", len);
}

inline Value::Array& Value::GetArray() { return data().arrayValue; }
inline const Value::Array& Value::GetArray() const { return data().arrayValue; }
inline Value& Value::operator[](SizeType index)
{
    return data().arrayValue[index];
}
inline const Value& Value::operator[](SizeType index) const
{
    return data().arrayValue[index];
}
inline SizeType Value::Size() const
{
    if(data().type == Type::Array)
        return data().arrayValue.size();
    if(data().type == Type::Object)
        return data().objectValue.size();
    return 0;
}
inline bool Value::Empty() const { return Size() == 0; }

inline Value::iterator Value::MemberBegin() { return data().objectValue.begin(); }
inline Value::iterator Value::MemberEnd() { return data().objectValue.end(); }
inline Value::const_iterator Value::MemberBegin() const
{
    return data().objectValue.begin();
}
inline Value::const_iterator Value::MemberEnd() const
{
    return data().objectValue.end();
}
inline Value::iterator Value::FindMember(const char* key)
{
    for(auto it = data().objectValue.begin(); it != data().objectValue.end(); ++it)
    {
        if(it->name.equals(key))
            return it;
    }
    return data().objectValue.end();
}
inline Value::const_iterator Value::FindMember(const char* key) const
{
    for(auto it = data().objectValue.begin(); it != data().objectValue.end(); ++it)
    {
        if(it->name.equals(key))
            return it;
    }
    return data().objectValue.end();
}

inline void Value::ensure_object()
{
    if(data().type != Type::Object)
        SetObject();
}

inline void Value::ensure_array()
{
    if(data().type != Type::Array)
        SetArray();
}

inline void Value::AddMember(const char* key, Value value, Alloc&)
{
    ensure_object();
    data().objectValue.push_back(Member{Name(key), std::move(value)});
}

inline void Value::AddMember(std::string_view key, Value value, Alloc&)
{
    ensure_object();
    data().objectValue.push_back(Member{Name(key), std::move(value)});
}

inline void Value::AddMember(const char* key, const char* value, Alloc& alloc)
{
    AddMember(key, Value(value), alloc);
}

inline void Value::AddMember(const char* key, std::string_view value,
                             Alloc& alloc)
{
    AddMember(key, Value(value), alloc);
}

inline void Value::AddMember(const char* key, bool value, Alloc& alloc)
{
    AddMember(key, Value(value), alloc);
}

inline void Value::AddMember(const char* key, int value, Alloc& alloc)
{
    AddMember(key, Value(value), alloc);
}

inline void Value::AddMember(const char* key, unsigned value, Alloc& alloc)
{
    AddMember(key, Value(value), alloc);
}

inline void Value::AddMember(const char* key, std::int64_t value, Alloc& alloc)
{
    AddMember(key, Value(value), alloc);
}

inline void Value::AddMember(const char* key, std::uint64_t value, Alloc& alloc)
{
    AddMember(key, Value(value), alloc);
}

inline void Value::AddMember(const char* key, double value, Alloc& alloc)
{
    AddMember(key, Value(value), alloc);
}

inline void Value::PushBack(Value value, Alloc&)
{
    ensure_array();
    data().arrayValue.push_back(std::move(value));
}

inline void Value::PushBack(const char* value, Alloc& alloc)
{
    PushBack(Value(value), alloc);
}

inline void Value::PushBack(std::string_view value, Alloc& alloc)
{
    PushBack(Value(value), alloc);
}

inline void Value::PushBack(bool value, Alloc& alloc)
{
    PushBack(Value(value), alloc);
}

inline void Value::PushBack(int value, Alloc& alloc)
{
    PushBack(Value(value), alloc);
}

inline void Value::PushBack(unsigned value, Alloc& alloc)
{
    PushBack(Value(value), alloc);
}

inline void Value::PushBack(std::int64_t value, Alloc& alloc)
{
    PushBack(Value(value), alloc);
}

inline void Value::PushBack(std::uint64_t value, Alloc& alloc)
{
    PushBack(Value(value), alloc);
}

inline void Value::PushBack(double value, Alloc& alloc)
{
    PushBack(Value(value), alloc);
}

inline void Value::CopyFrom(const Value& other, Alloc&) { *this = other; }
inline Value::Type Value::type() const { return data().type; }

inline void Value::set_type(JsonType type)
{
    switch(type)
    {
    case kObjectType:
        SetObject();
        break;
    case kArrayType:
        SetArray();
        break;
    case kStringType:
        clear();
        data().type = Type::String;
        break;
    case kTrueType:
        clear();
        data().type = Type::Bool;
        data().boolValue = true;
        break;
    case kFalseType:
        clear();
        data().type = Type::Bool;
        data().boolValue = false;
        break;
    case kNumberType:
        clear();
        data().type = Type::Int;
        break;
    case kNullType:
    default:
        SetNull();
        break;
    }
}

class Document : public Value
{
public:
    Document() = default;
    explicit Document(JsonType type) : Value(type) {}

    Alloc& GetAllocator() { return alloc_; }
    const Alloc& GetAllocator() const { return alloc_; }

private:
    Alloc alloc_;
};

inline Value convert(yyjson_val* value)
{
    if(!value || yyjson_is_null(value))
        return Value();
    if(yyjson_is_bool(value))
        return Value(yyjson_get_bool(value));
    if(yyjson_is_sint(value))
        return Value(static_cast<std::int64_t>(yyjson_get_sint(value)));
    if(yyjson_is_uint(value))
        return Value(static_cast<std::uint64_t>(yyjson_get_uint(value)));
    if(yyjson_is_real(value))
        return Value(yyjson_get_real(value));
    if(yyjson_is_str(value))
        return Value(std::string_view(yyjson_get_str(value),
                                      yyjson_get_len(value)));
    if(yyjson_is_arr(value))
    {
        Value out(kArrayType);
        Alloc alloc;
        yyjson_val* item = nullptr;
        yyjson_arr_iter iter;
        yyjson_arr_iter_init(value, &iter);
        while((item = yyjson_arr_iter_next(&iter)))
            out.PushBack(convert(item), alloc);
        return out;
    }
    if(yyjson_is_obj(value))
    {
        Value out(kObjectType);
        Alloc alloc;
        yyjson_val* key = nullptr;
        yyjson_obj_iter iter;
        yyjson_obj_iter_init(value, &iter);
        while((key = yyjson_obj_iter_next(&iter)))
        {
            yyjson_val* val = yyjson_obj_iter_get_val(key);
            std::string_view name(yyjson_get_str(key), yyjson_get_len(key));
            out.AddMember(name, convert(val), alloc);
        }
        return out;
    }
    return Value();
}

inline bool parse(Document& doc, std::string_view text)
{
    std::string buffer(text);
    yyjson_read_err err{};
    yyjson_doc* parsed =
        yyjson_read_opts(buffer.data(), buffer.size(), 0, nullptr, &err);
    if(!parsed)
        return false;
    doc.CopyFrom(convert(yyjson_doc_get_root(parsed)), doc.GetAllocator());
    yyjson_doc_free(parsed);
    return true;
}

inline bool parse(Document& doc, std::istream& input)
{
    std::ostringstream ss;
    ss << input.rdbuf();
    return parse(doc, ss.str());
}

inline void append_escaped(std::string& out, std::string_view text)
{
    out.push_back('"');
    for(unsigned char ch : text)
    {
        switch(ch)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if(ch < 0x20)
            {
                constexpr char hex[] = "0123456789abcdef";
                out += "\\u00";
                out.push_back(hex[ch >> 4]);
                out.push_back(hex[ch & 0x0f]);
            }
            else
                out.push_back(static_cast<char>(ch));
            break;
        }
    }
    out.push_back('"');
}

inline void stringify_into(const Value& value, std::string& out,
                           unsigned indent, unsigned depth, bool pretty)
{
    auto newline_indent = [&]()
    {
        if(pretty)
        {
            out.push_back('\n');
            out.append(depth * indent, ' ');
        }
    };

    switch(value.type())
    {
    case Value::Type::Null:
        out += "null";
        break;
    case Value::Type::Bool:
        out += value.GetBool() ? "true" : "false";
        break;
    case Value::Type::Int:
        out += std::to_string(value.GetInt64());
        break;
    case Value::Type::Uint:
        out += std::to_string(value.GetUint64());
        break;
    case Value::Type::Double:
        out += std::to_string(value.GetDouble());
        break;
    case Value::Type::String:
        append_escaped(out, std::string_view(value.GetString(),
                                             value.GetStringLength()));
        break;
    case Value::Type::Array: {
        out.push_back('[');
        const auto& arr = value.GetArray();
        for(std::size_t i = 0; i < arr.size(); ++i)
        {
            if(i > 0)
                out.push_back(',');
            if(pretty)
            {
                out.push_back('\n');
                out.append((depth + 1) * indent, ' ');
            }
            stringify_into(arr[i], out, indent, depth + 1, pretty);
        }
        if(pretty && !arr.empty())
            newline_indent();
        out.push_back(']');
        break;
    }
    case Value::Type::Object: {
        out.push_back('{');
        bool first = true;
        for(auto it = value.MemberBegin(); it != value.MemberEnd(); ++it)
        {
            if(!first)
                out.push_back(',');
            first = false;
            if(pretty)
            {
                out.push_back('\n');
                out.append((depth + 1) * indent, ' ');
            }
            append_escaped(out, std::string_view(it->name.GetString(),
                                                 it->name.GetStringLength()));
            out.push_back(':');
            if(pretty)
                out.push_back(' ');
            stringify_into(it->value, out, indent, depth + 1, pretty);
        }
        if(pretty && value.MemberBegin() != value.MemberEnd())
            newline_indent();
        out.push_back('}');
        break;
    }
    }
}

inline std::string stringify(const Value& value)
{
    std::string out;
    stringify_into(value, out, 2, 0, false);
    return out;
}

inline std::string stringify_pretty(const Value& value, unsigned indent = 2)
{
    std::string out;
    stringify_into(value, out, indent, 0, true);
    return out;
}

inline const Value* find(const Value& obj, const char* key)
{
    if(!obj.IsObject())
        return nullptr;
    auto it = obj.FindMember(key);
    if(it == obj.MemberEnd())
        return nullptr;
    return &it->value;
}

inline bool has(const Value& obj, const char* key)
{
    return find(obj, key) != nullptr;
}

inline std::string get_string(const Value& obj, const char* key,
                              std::string_view def = {})
{
    auto v = find(obj, key);
    if(v && v->IsString())
        return std::string(v->GetString(), v->GetStringLength());
    return std::string(def);
}

inline int get_int(const Value& obj, const char* key, int def = 0)
{
    auto v = find(obj, key);
    if(v && v->IsInt())
        return v->GetInt();
    if(v && v->IsUint())
        return static_cast<int>(v->GetUint());
    if(v && v->IsInt64())
        return static_cast<int>(v->GetInt64());
    if(v && v->IsUint64())
        return static_cast<int>(v->GetUint64());
    if(v && v->IsDouble())
        return static_cast<int>(v->GetDouble());
    return def;
}

inline bool get_bool(const Value& obj, const char* key, bool def = false)
{
    auto v = find(obj, key);
    if(v && v->IsBool())
        return v->GetBool();
    if(v && v->IsInt())
        return v->GetInt() != 0;
    return def;
}

inline Value make_string(std::string_view s, Alloc& a)
{
    return Value(s.data(), static_cast<SizeType>(s.size()), a);
}

inline Value make_object()
{
    return Value(kObjectType);
}

inline Value make_array()
{
    return Value(kArrayType);
}

inline void add_member(Value& obj, const char* key, Value&& val, Alloc& a)
{
    obj.AddMember(key, std::move(val), a);
}
} // namespace json_utils
