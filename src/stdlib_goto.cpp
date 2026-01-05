#include "stdlib_goto.h"
#include <unordered_map>

namespace stdlib_goto
{

std::string headerForSymbol(const std::string& symbol)
{
    static const std::unordered_map<std::string, const char*> stdHeaders = {
        // Strings
        {"string", "string"},
        {"string_view", "string_view"},
        {"u8string", "string"},
        {"u16string", "string"},
        {"u32string", "string"},
        {"string_literals", "string"},
        {"string_view_literals", "string_view"},

        // Containers
        {"vector", "vector"},
        {"list", "list"},
        {"forward_list", "forward_list"},
        {"map", "map"},
        {"multimap", "map"},
        {"unordered_map", "unordered_map"},
        {"unordered_multimap", "unordered_map"},
        {"set", "set"},
        {"multiset", "set"},
        {"unordered_set", "unordered_set"},
        {"unordered_multiset", "unordered_set"},
        {"deque", "deque"},
        {"queue", "queue"},
        {"priority_queue", "queue"},
        {"stack", "stack"},
        {"array", "array"},
        {"span", "span"},
        {"bitset", "bitset"},

        // Utilities / misc
        {"pair", "utility"},
        {"make_pair", "utility"},
        {"tuple", "tuple"},
        {"make_tuple", "tuple"},
        {"apply", "tuple"},
        {"optional", "optional"},
        {"variant", "variant"},
        {"any", "any"},
        {"expected", "expected"},
        {"move", "utility"},
        {"forward", "utility"},
        {"swap", "utility"},
        {"exchange", "utility"},
        {"index_sequence", "utility"},
        {"make_index_sequence", "utility"},
        {"integer_sequence", "utility"},

        // Memory
        {"unique_ptr", "memory"},
        {"shared_ptr", "memory"},
        {"weak_ptr", "memory"},
        {"make_unique", "memory"},
        {"make_shared", "memory"},
        {"allocator", "memory"},
        {"polymorphic_allocator", "memory_resource"},
        {"memory_resource", "memory_resource"},
        {"pmr", "memory_resource"},

        // I/O
        {"cout", "iostream"},
        {"cin", "iostream"},
        {"cerr", "iostream"},
        {"clog", "iostream"},
        {"istream", "istream"},
        {"ostream", "ostream"},
        {"fstream", "fstream"},
        {"stringstream", "sstream"},
        {"istringstream", "sstream"},
        {"ostringstream", "sstream"},

        // Algorithms
        {"sort", "algorithm"},
        {"stable_sort", "algorithm"},
        {"partial_sort", "algorithm"},
        {"find", "algorithm"},
        {"find_if", "algorithm"},
        {"find_if_not", "algorithm"},
        {"count", "algorithm"},
        {"count_if", "algorithm"},
        {"copy", "algorithm"},
        {"copy_if", "algorithm"},
        {"transform", "algorithm"},
        {"for_each", "algorithm"},
        {"lower_bound", "algorithm"},
        {"upper_bound", "algorithm"},
        {"binary_search", "algorithm"},
        {"min", "algorithm"},
        {"max", "algorithm"},
        {"minmax", "algorithm"},
        {"clamp", "algorithm"},

        // Functional
        {"function", "functional"},
        {"bind", "functional"},
        {"invoke", "functional"},
        {"reference_wrapper", "functional"},

        // Type traits
        {"is_same", "type_traits"},
        {"is_base_of", "type_traits"},
        {"is_convertible", "type_traits"},
        {"enable_if", "type_traits"},
        {"conditional", "type_traits"},
        {"remove_reference", "type_traits"},
        {"remove_cv", "type_traits"},
        {"decay", "type_traits"},

        // Numbers / utilities
        {"size_t", "cstddef"},
        {"nullptr_t", "cstddef"},
        {"byte", "cstddef"},
        {"from_chars", "charconv"},
        {"to_chars", "charconv"},
        {"error_code", "system_error"},
        {"system_error", "system_error"},
        {"errc", "system_error"},

        // Chrono / threading
        {"chrono", "chrono"},
        {"duration", "chrono"},
        {"time_point", "chrono"},
        {"thread", "thread"},
        {"jthread", "thread"},
        {"mutex", "mutex"},
        {"scoped_lock", "mutex"},
        {"unique_lock", "mutex"},
        {"shared_mutex", "shared_mutex"},
        {"shared_lock", "shared_mutex"},
        {"future", "future"},
        {"promise", "future"},
        {"packaged_task", "future"},
        {"stop_token", "stop_token"},
        {"stop_source", "stop_token"},
        {"stop_callback", "stop_token"},
        {"latch", "latch"},
        {"barrier", "barrier"},
        {"semaphore", "semaphore"},
        {"counting_semaphore", "semaphore"},
        {"binary_semaphore", "semaphore"},

        // Filesystem
        {"filesystem", "filesystem"},
        {"path", "filesystem"},
        {"directory_entry", "filesystem"},
        {"directory_iterator", "filesystem"},
        {"recursive_directory_iterator", "filesystem"},
        {"filesystem_error", "filesystem"},
        {"current_path", "filesystem"},

        // Ranges
        {"ranges", "ranges"},
        {"views", "ranges"},
        {"iota_view", "ranges"},
        {"filter_view", "ranges"},
        {"transform_view", "ranges"},
        {"take_view", "ranges"},
        {"drop_view", "ranges"},
        {"take_while_view", "ranges"},
        {"drop_while_view", "ranges"},
        {"join_view", "ranges"},
        {"split_view", "ranges"},
        {"zip_view", "ranges"},
        {"chunk_view", "ranges"},
        {"slide_view", "ranges"},
        {"adjacent_view", "ranges"},
        {"filter", "ranges"},
        {"transform", "ranges"},
        {"take", "ranges"},
        {"drop", "ranges"},
        {"take_while", "ranges"},
        {"drop_while", "ranges"},
        {"join", "ranges"},
        {"split", "ranges"},
        {"zip", "ranges"},
        {"chunk", "ranges"},
        {"slide", "ranges"},
        {"adjacent", "ranges"},
        {"iota", "ranges"},
        {"to", "ranges"},

        // Formatting / source location / bit
        {"format", "format"},
        {"vformat", "format"},
        {"format_to", "format"},
        {"format_to_n", "format"},
        {"formatted_size", "format"},
        {"formatter", "format"},
        {"source_location", "source_location"},
        {"bit_cast", "bit"},
        {"bit_ceil", "bit"},
        {"bit_floor", "bit"},
        {"rotl", "bit"},
        {"rotr", "bit"},
        {"bit_width", "bit"},
        {"print", "print"},
        {"println", "print"},

        // Numeric
        {"accumulate", "numeric"},
        {"reduce", "numeric"},
        {"transform_reduce", "numeric"},
        {"transform_inclusive_scan", "numeric"},
        {"transform_exclusive_scan", "numeric"},
        {"inclusive_scan", "numeric"},
        {"exclusive_scan", "numeric"},
        {"gcd", "numeric"},
        {"lcm", "numeric"},

        // Complex
        {"complex", "complex"},
        {"complex_literals", "complex"},

        // Strings / chars
        {"char_traits", "string"},
        {"basic_string", "string"},
        {"basic_string_view", "string_view"},
        {"from_chars_result", "charconv"},
        {"to_chars_result", "charconv"},

        // Optional / variant / expected helpers
        {"nullopt", "optional"},
        {"monostate", "variant"},
        {"unexpected", "expected"},

        // Chrono
        {"steady_clock", "chrono"},
        {"system_clock", "chrono"},
        {"high_resolution_clock", "chrono"},
        {"hours", "chrono"},
        {"minutes", "chrono"},
        {"seconds", "chrono"},
        {"milliseconds", "chrono"},
        {"microseconds", "chrono"},
        {"nanoseconds", "chrono"},
        {"chrono_literals", "chrono"},

        // Execution
        {"execution", "execution"},
        {"sequenced_policy", "execution"},
        {"parallel_policy", "execution"},
        {"parallel_unsequenced_policy", "execution"},

        // PMR resources
        {"monotonic_buffer_resource", "memory_resource"},
        {"unsynchronized_pool_resource", "memory_resource"},
        {"synchronized_pool_resource", "memory_resource"},

        // C++23+
        {"mdspan", "mdspan"},
        {"extents", "mdspan"},
        {"dextents", "mdspan"},
        {"expected", "expected"},
        {"flat_map", "flat_map"},
        {"flat_set", "flat_set"},
    };

    auto it = stdHeaders.find(symbol);
    if(it == stdHeaders.end())
        return {};
    return it->second;
}

} // namespace stdlib_goto
