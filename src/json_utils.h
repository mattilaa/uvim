#pragma once

#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <rapidjson/prettywriter.h>
#include <istream>
#include <string>
#include <string_view>

namespace json_utils
{
using Document = rapidjson::Document;
using Value = rapidjson::Value;
using Alloc = rapidjson::Document::AllocatorType;

inline bool parse(Document& doc, std::string_view text)
{
    doc.Parse(text.data(), text.size());
    return !doc.HasParseError();
}

inline bool parse(Document& doc, std::istream& input)
{
    rapidjson::IStreamWrapper isw(input);
    doc.ParseStream(isw);
    return !doc.HasParseError();
}

inline std::string stringify(const Value& v)
{
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> wr(sb);
    v.Accept(wr);
    return std::string(sb.GetString(), sb.GetSize());
}

inline std::string stringify_pretty(const Value& v, unsigned indent = 2)
{
    rapidjson::StringBuffer sb;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> wr(sb);
    wr.SetIndent(' ', indent);
    v.Accept(wr);
    return std::string(sb.GetString(), sb.GetSize());
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
    return Value(s.data(), static_cast<rapidjson::SizeType>(s.size()), a);
}

inline Value make_object()
{
    return Value(rapidjson::kObjectType);
}

inline Value make_array()
{
    return Value(rapidjson::kArrayType);
}

inline void add_member(Value& obj, const char* key, Value&& val, Alloc& a)
{
    obj.AddMember(rapidjson::StringRef(key), val, a);
}
} // namespace json_utils
