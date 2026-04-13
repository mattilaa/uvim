#pragma once
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

class GitIgnore
{
public:
    struct Pattern
    {
        std::string pattern;
        bool negation;      // Pattern starts with !
        bool directoryOnly; // Pattern ends with /
        bool anchored;      // Pattern contains / (except trailing)
        std::regex regex;
        fs::path baseDir;
    };

    GitIgnore() = default;

    // Load .gitignore from a directory
    bool load(const fs::path& directory)
    {
        patterns.clear();

        fs::path gitignorePath = directory / ".gitignore";
        std::ifstream file(gitignorePath);
        if(!file.is_open())
            return false;

        std::string line;
        while(std::getline(file, line))
        {
            addPattern(line, directory);
        }

        return true;
    }

    // Load from multiple directories (walking up to find .gitignore files)
    void loadRecursive(const fs::path& directory,
                       const fs::path& stopAt = fs::path())
    {
        patterns.clear();

        // Always ignore .git directory
        addPattern(".git/", directory);

        std::error_code ec;
        fs::path current = fs::absolute(directory, ec);
        if(ec || current.empty())
        {
            ec.clear();
            current = directory;
        }
        current = current.lexically_normal();
        fs::path stop = stopAt;
        if(!stop.empty())
        {
            stop = fs::absolute(stop, ec);
            if(ec || stop.empty())
            {
                ec.clear();
                stop = stopAt;
            }
            stop = stop.lexically_normal();
        }
        std::vector<fs::path> gitignoreFiles;

        // Collect .gitignore files from current upwards, optionally stopping
        // at a workspace boundary.
        while(!current.empty())
        {
            fs::path gitignorePath = current / ".gitignore";
            if(fs::exists(gitignorePath, ec))
            {
                gitignoreFiles.push_back(gitignorePath);
            }
            ec.clear();

            if(!stop.empty() && current == stop)
                break;

            fs::path parent = current.parent_path();
            if(parent == current)
                break;
            current = parent;
        }

        // Load in reverse order (root first, so more specific patterns
        // override)
        for(auto it = gitignoreFiles.rbegin(); it != gitignoreFiles.rend();
            ++it)
        {
            std::ifstream file(*it);
            if(file.is_open())
            {
                std::string line;
                while(std::getline(file, line))
                {
                    addPattern(line, it->parent_path());
                }
            }
        }
    }

    // Check if a path should be ignored
    bool isIgnored(const fs::path& path, bool isDirectory = false) const
    {
        if(patterns.empty())
            return false;

        bool ignored = false;

        for(const auto& part : path)
        {
            if(part == ".git")
                return true;
        }

        for(const auto& pattern : patterns)
        {
            if(pattern.directoryOnly && !isDirectory)
                continue;

            std::error_code ec;
            fs::path relativePath = fs::relative(path, pattern.baseDir, ec);
            if(ec)
                relativePath = path;

            std::string pathStr = relativePath.generic_string();
            if(isDirectory && !pathStr.empty() && pathStr.back() != '/')
            {
                pathStr += '/';
            }

            if(matchPattern(pattern, pathStr, relativePath.filename().string()))
            {
                ignored = !pattern.negation;
            }
        }

        return ignored;
    }

    bool empty() const
    {
        return patterns.empty();
    }

private:
    std::vector<Pattern> patterns;

    void addPattern(const std::string& line, const fs::path& dir)
    {
        std::string trimmed = trim(line);

        // Skip empty lines and comments
        if(trimmed.empty() || trimmed[0] == '#')
            return;

        Pattern p;
        p.negation = false;
        p.directoryOnly = false;
        p.anchored = false;

        std::string pat = trimmed;

        // Handle negation
        if(!pat.empty() && pat[0] == '!')
        {
            p.negation = true;
            pat = pat.substr(1);
        }

        // Handle directory-only patterns
        if(!pat.empty() && pat.back() == '/')
        {
            p.directoryOnly = true;
            pat.pop_back();
        }

        // Check if pattern is anchored (contains / except at end)
        p.anchored = (pat.find('/') != std::string::npos);

        // Handle leading /
        if(!pat.empty() && pat[0] == '/')
        {
            pat = pat.substr(1);
            p.anchored = true;
        }

        p.pattern = pat;
        p.regex = globToRegex(pat, p.anchored);
        p.baseDir = dir;

        patterns.push_back(std::move(p));
    }

    std::regex globToRegex(const std::string& glob, bool anchored) const
    {
        std::string regex;

        if(anchored)
        {
            regex = "^";
        }
        else
        {
            regex = "(^|/)";
        }

        for(size_t i = 0; i < glob.size(); ++i)
        {
            char c = glob[i];

            switch(c)
            {
            case '*':
                if(i + 1 < glob.size() && glob[i + 1] == '*')
                {
                    // ** matches everything including /
                    if(i + 2 < glob.size() && glob[i + 2] == '/')
                    {
                        regex += "(.*/)?";
                        i += 2;
                    }
                    else
                    {
                        regex += ".*";
                        i += 1;
                    }
                }
                else
                {
                    // * matches everything except /
                    regex += "[^/]*";
                }
                break;

            case '?':
                regex += "[^/]";
                break;

            case '[':
            {
                regex += '[';
                ++i;
                if(i < glob.size() && (glob[i] == '!' || glob[i] == '^'))
                {
                    regex += '^';
                    ++i;
                }
                while(i < glob.size() && glob[i] != ']')
                {
                    if(glob[i] == '\\' && i + 1 < glob.size())
                    {
                        regex += '\\';
                        regex += glob[++i];
                    }
                    else
                    {
                        regex += glob[i];
                    }
                    ++i;
                }
                regex += ']';
                break;
            }

            case '.':
            case '(':
            case ')':
            case '+':
            case '|':
            case '^':
            case '$':
            case '{':
            case '}':
            case '\\':
                regex += '\\';
                regex += c;
                break;

            default:
                regex += c;
                break;
            }
        }

        regex += "(/.*)?$";

        try
        {
            return std::regex(regex,
                              std::regex::ECMAScript | std::regex::icase);
        }
        catch(const std::regex_error&)
        {
            // Fallback to a never-matching regex
            return std::regex("^$");
        }
    }

    bool matchPattern(const Pattern& pattern, const std::string& fullPath,
                      const std::string& filename) const
    {
        // For non-anchored patterns, also try matching just the filename
        if(!pattern.anchored)
        {
            if(std::regex_search(filename, pattern.regex))
                return true;
        }

        return std::regex_search(fullPath, pattern.regex);
    }

    static std::string trim(const std::string& str)
    {
        size_t first = str.find_first_not_of(" \t\r\n");
        if(first == std::string::npos)
            return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, last - first + 1);
    }
};
