/*
 * Copyright (c) 2017 github.com/cryptocode
 *
 * MIT License (see github.com/cryptocode/cxxprops/LICENSE)
 */

#ifndef CXXPROPS_PROPERTIES_H
#define CXXPROPS_PROPERTIES_H

#include <istream>
#include <vector>
#include <unordered_map>
#include <memory>
#include <iostream>
#include <sstream>
#include <utility>
#include <algorithm>
#include <numeric>

namespace cxxprops
{
/**
 * Parses and renders property files. Comments, formatting and property order
 * are preserved, with new properties and comments appended.
 *
 * Pretty-formatting the output is also supported.
 *
 * There are some differences between cxxprops and Java property files:
 *
 *  - Input is utf8 input instead of latin1
 *  - Only '=' is used for property assignment, not ':'. This allows
 *    colons in keys, such as "host:name = 0.0.0.0"
 *  - Support for property templates to reduce duplication
 *  - Prefix blocks with arbitrary nesting is supported; e.g server.log.level = debug, etc
 *    is the same as:
 *
 *      server
 *      {
 *          port = 1234
 *          log.level = debug
 *      }
 *
 * See also http://docs.oracle.com/javase/8/docs/api/java/util/Properties.html
 */
class Properties
{
public:

    /**
     * Parse the input stream
     *
     * @param stream Input stream, such as a std::ifstream
     */
    inline void parse(std::istream& stream)
    {
        // Resolve template variables
        std::stringstream is = preprocess(stream);
        std::string line;
        std::string prefix = "";

        while (getline(is, line))
        {
            lines.emplace_back(line);
            Line& lineEntry = lines.back();

            if (isComment(line))
            {
                lineEntry.linetype = LineType::Comment;
            }
            else if (isEmptyLine(line))
            {
                lineEntry.linetype = LineType::Empty;
            }
            else if (isBlockStart(line))
            {
                lineEntry.linetype = LineType::BlockStart;
                if (!prefix.empty())
                    prefixStack.push_back(prefix);
            }
            else if (isBlockEnd(line))
            {
                lineEntry.linetype = LineType::BlockEnd;
                if (!prefixStack.empty())
                    prefixStack.pop_back();
            }
            else
            {
                lineEntry.linetype = LineType::Property;

                std::string trimmedStr = line;
                std::string::size_type assignPos = trimmedStr.find_first_of("=");

                std::string key;
                std::string value;

                // Lines without = are considered keys with empty values. They may also start a prefix block.
                if (assignPos == std::string::npos)
                {
                    prefix = key = trim(trimmedStr.substr(0, assignPos),
                                        lineEntry.beforeKey, lineEntry.afterKey);
                    value = "";
                    lineEntry.lacksAssignment = true;
                }
                else
                {
                    prefix = "";
                    key = trim(trimmedStr.substr(0, assignPos), lineEntry.beforeKey, lineEntry.afterKey);
                    value = unescape(trim(trimmedStr.substr(assignPos + 1),
                                          lineEntry.beforeValue, lineEntry.afterValue));
                }

                // To avoid having to reparse the line, associate the key of a line with the Line entry
                lineEntry.bareKey = key;

                auto prop = std::make_unique<Prop>();

                lineEntry.key = prop->key = prependPrefix(key);
                prop->value = value;

                if (isMultiLine(value))
                {
                    // Remove the backslash
                    prop->value.pop_back();

                    // Trim at the end and unquote
                    prop->value = trimright(prop->value);
                    prop->value = unquote(prop->value);

                    while (getline(is, line))
                    {
                        this->lines.emplace_back(line);
                        this->lines.back().linetype = LineType::MultilineValue;

                        std::string theline = trim(line);
                        if (isMultiLine(theline))
                        {
                            theline.pop_back();
                            if (endswith(theline, '"') || endswith(theline, '\\'))
                            {
                                theline = unquote(trimright(theline));
                            }

                            prop->value += theline;
                        }
                        else
                        {
                            prop->value += unquote(theline);
                            break;
                        }
                    }
                }
                else
                {
                    prop->value = unquote(prop->value);
                }

                props.insert(std::make_pair(prop->key, std::move(prop)));
            }
        }
    }

    /**
     * Expands template variables.
     *
     * @note This method could be merged with parse(...) so that even
     *       template expansions are pretty printed
     * @param is Input stream
     * @return Stream with expanded variables
     */
    inline std::stringstream preprocess(std::istream& is)
    {
        std::stringstream os;
        std::string line;
        std::unordered_map<std::string, std::vector<std::string>> vars;

        while (getline(is, line))
        {
            if (isTemplateStart(line))
            {
                auto trimmed = trim(line);
                if (trimmed.size() < 3)
                    throw std::runtime_error("Invalid template definition syntax");

                // Extract template variable name
                std::string varname = trimmed.substr(1,trimmed.size()-2);
                std::vector<std::string> templatelines;

                bool endedOK = false;
                while (getline(is, line))
                {
                    if (isTemplateEnd(line))
                    {
                        endedOK = true;
                        break;
                    }
                    else
                        templatelines.push_back(line);
                }

                if (!endedOK)
                    throw std::runtime_error("Missing closing tag in template definition");

                vars[varname] = templatelines;
            }
            else if (isTemplateVariable(line))
            {
                // Expand template variable
                auto trimmed = trim(line);
                if (trimmed.size() < 3)
                    throw std::runtime_error("Invalid template variable syntax");

                std::string varname = trimmed.substr(1,trimmed.size()-2);

                auto match = vars.find(varname);
                if (match == vars.end())
                    throw std::runtime_error(std::string("Template variable is not defined: ") + varname);

                for (auto templateline : match->second)
                    os << templateline << std::endl;
            }
            else
            {
                os << line << std::endl;
            }
        }

        return os;
    }

    /**
     * If a key occurs inside a prefix block, prepend the prefix.
     *
     * @param key Key to prepend (original is not changed)
     * @return Key with prefix, or the original if no prefix is currently active
     */
    std::string prependPrefix(const std::string& key)
    {
        if (!prefixStack.empty())
            return join(prefixStack, ".", true) + key;
        else
            return key;
    }

    /**
     * @return true if the key exists
     */
    bool hasKey(const std::string& key) const
    {
        return props.find(key) != props.end();
    }

    /**
     * Get a property value. The returned value will be trimmed.
     *
     * Updating the value will preserve whitespaces.
     *
     * @param key Property key
     * @return Property value, or an empty string if the key doesn't exists.
     */
    inline std::string get(const std::string& key) const
    {
        std::string res = "";

        auto match = props.find(key);
        if (match != props.end())
            res = match->second->value;

        return res;
    }

    inline std::string get(std::string key, std::string defaultValue) const
    {
        return hasKey(key) ? get(key) : defaultValue;
    }

    /**
     * Returns true if the value is "true", "1" or "yes"
     *
     * @param key Property key
     * @param defaultValue Default value if the key doesn't exist
     * @return Property value, or an empty string if the key doesn't exists.
     */
    inline bool getBool(const std::string& key, bool defaultValue)
    {
        if (hasKey(key))
        {
            std::string val = get(key);
            return (val == "true" || val == "1" || val == "yes");
        }
        else
            return defaultValue;
    }

    /**
     * Update the property value. If the key already exists, attempt to
     * maintain as much whitespace information as possible (this work is only
     * done if text() is called; this method simply updates the property value)
     *
     * @param key Property key
     * @param value New property value
     * @return The old key, if any
     */
    inline std::string put(const std::string& key, const std::string& value)
    {
        std::string old = "";

        auto match = props.find(key);
        if (match != props.end())
        {
            auto prop = match->second.get();
            prop->modified = true;

            old = prop->value;
            prop->value = value;
        }
        else
        {
            this->lines.emplace_back(key + " = " + value);
            Line& lineEntry = lines.back();
            lineEntry.key = lineEntry.bareKey = key;
            lineEntry.linetype = LineType::Property;

            auto prop = std::make_unique<Prop>();
            prop->modified = true;
            prop->key = key;
            prop->value = value;

            props.insert(std::make_pair(key, std::move(prop)));
        }

        return old;
    }

    /**
     * Remove a property if it exists.
     *
     * @param key Property key
     */
    inline void remove(const std::string& key)
    {
        auto match = props.find(key);
        if (match != props.end())
            props.erase(match);
    }

    /**
     * Append an empty line
     */
    inline void putEmptyLine()
    {
        lines.emplace_back("");
        lines.back().linetype = LineType::Empty;
    }

    /**
     * Append a comment
     *
     * @param comment Comment. If the leading # or ! is missing, it will be added
     */
    inline void putComment(const std::string& comment)
    {
        std::string line = trim(comment);

        if (!line.empty())
        {
            if (line[0] != '#' && line[0] != '!')
                line.insert(0, "# ");

            lines.emplace_back(line);
            lines.back().linetype = LineType::Comment;
        }
    }

    /**
     * @return All keys
     */
    inline std::vector<std::string> keys()
    {
        std::vector<std::string> keys(props.size());

        std::transform(props.begin(), props.end(), keys.begin(),
                  [](auto& pair) { return pair.first; });

        return keys;
    }

    /**
     * @return All values
     */
    inline std::vector<std::string> values()
    {
        std::vector<std::string> values(props.size());

        std::transform(props.begin(), props.end(), values.begin(),
                       [](auto& pair) { return pair.second.get()->value; });

        return values;
    }

    /**
     * Renders the properties as a string, including any updates. This can be
     * written to the property file.
     *
     * The original property formatting is kept intact, unless prettyPrint is set.
     *
     * Pretty printing removes unnecessary white spaces, and multiple blank lines
     * in a row are collapsed into a single blank line.
     *
     * @param prettyPrint If true, the output is pretty printed.
     * @return Properties as text
     */
    inline std::string text(bool prettyPrint=false)
    {
        std::ostringstream ss;

        Line* prev = nullptr;
        size_t idx = 0;
        int prefixDepth = 0;

        for (auto& entry : lines)
        {
            switch (entry.linetype)
            {
                case LineType::Empty:
                {
                    // A subtle point is that \r is implicitly preserved for \r\n line endings,
                    // because \r occurs before \n
                    if (!prettyPrint || !(prev && prev->linetype == LineType::Empty))
                        ss << "\n";

                    break;
                }
                case LineType::Comment:
                {
                    ss << (prettyPrint ? trim(entry.line) : entry.line);
                    ss << "\n";

                    break;
                }
                case LineType::Property:
                {
                    // The property may have been removed
                    auto match = props.find(entry.key);
                    if (match != props.end())
                    {
                        if (prettyPrint)
                        {
                            // Indent prefix blocks
                            if (prefixDepth > 0)
                                ss << std::string(prefixDepth*4, ' ');

                            ss << entry.bareKey;

                            if (!match->second->value.empty())
                                ss << " = " << escape(match->second->value);
                        }
                        else
                        {
                            // Inject whitespaces before and after key, value
                            ss << entry.beforeKey << entry.bareKey << entry.afterKey;

                            if (!entry.lacksAssignment || match->second->modified)
                            {
                                ss << "=" << entry.beforeValue << escape(match->second->value) << entry.afterValue;
                            }
                        }

                        ss << "\n";
                    }

                    break;
                }
                case LineType::BlockStart:
                {
                    ss << std::string(prefixDepth*4, ' ') << "{" << '\n';
                    prefixDepth++;

                    break;
                }
                case LineType::BlockEnd:
                {
                    prefixDepth--;
                    ss << std::string(prefixDepth*4, ' ') << "}" << '\n';

                    break;
                }
                default:
                    break;
            }

            prev = &entry;
            idx++;
        }

        return ss.str();
    }

private:

    enum LineType
    {
        Property, Comment, Empty, MultilineValue,
        BlockStart, BlockEnd,
        TemplateStart, TemplateLine, TemplateEnd
    };

    /** Represents a line in the input, with enough information to preserve formatting */
    struct Line
    {
        Line(std::string str) : line(str)
        {}

        /** The line exactly as it occurs in the file */
        std::string line;

        /** Type of line */
        LineType linetype = LineType::Property;

        /** The key in this line, when type is LineType::Property. This may include a prefix. */
        std::string key = "";

        /** The non-prefixed line, as it appears in the file; kept for rendering. */
        std::string bareKey = "";

        /* Format preservation */

        std::string beforeKey = "";
        std::string afterKey = " ";
        std::string beforeValue = " ";
        std::string afterValue = "";

        /**
         * The line may contain only a key and lack "=value"
         * This fact must be recorded for doing unformatted output
         */
        bool lacksAssignment = false;
    };

    /** Internal property representation */
    struct Prop
    {
        /** Trimmed key */
        std::string key;

        /** Trimmed value */
        std::string value;

        /**
         * If true, a call to put(...) has modified, or added, this property
         * since the last call to parse. This affects how text(true) renders the output
         * for multi-line values. If not modified, a multi line property is kept as-is,
         * otherwise the property is written on a single line (unless \ markers are
         * included in the property)
         */
        bool modified = false;
    };

    /**
     * Destructive replace
     *
     * @param str String where the replace will take place
     * @param from What to replace
     * @param to Replacement string
     * @return The updated string (same as str)
     */
    inline std::string& replaceAll(std::string& str, const std::string& from, const std::string& to)
    {
        size_t start_pos = 0;

        while ((start_pos = str.find(from, start_pos)) != std::string::npos)
        {
            str.replace(start_pos, from.length(), to);
            start_pos += to.length();
        }

        return str;
    }

    /**
     * Prepends '\' to leading whitespaces, per Java property spec
     *
     * It also replaces newlines with \<nl>
     */
    inline std::string escape(const std::string& str)
    {
        std::string::size_type s = str.find_first_not_of(WS);
        if (s != std::string::npos)
        {
            std::ostringstream ss;

            for (int i = 0; i < s; i++)
                ss << '\\' << str[i];

            auto substr = str.substr(s);
            ss << replaceAll(substr, "\n", "\\\n    ");

            return ss.str();
        }

        return str;
    }

    /**
     * Remove escaping '\' before white spaces
     */
    inline std::string unescape(const std::string& str)
    {
        if (str.size() > 1 && str[0] == '\\')
        {
            std::ostringstream ss;

            int i = 0;
            for (; i < str.size()-1; i+=2)
            {
                if (str[i] == '\\')
                    ss << str[i + 1];
                else
                    break;
            }

            ss << str.substr(i);
            return ss.str();
        }
        else
        {
            return str;
        }
    }

    /**
     * Removes 'single' or "double" quotes around the string. The input must be trimmed.
     */
    inline std::string unquote(const std::string& str)
    {
        std::string res = str;

        if (str.length() > 2 && ((str[0] == '\'' && str[str.length()-1] == '\'')
                                 || (str[0] == '"' && str[str.length()-1] == '"')))
        {
            res = str.substr(1, str.length()-2);
        }

        return res;
    }


    inline std::string trimright(const std::string& str)
    {
        std::string ignored;
        return trimright(str, ignored);
    }

    inline std::string trimright(const std::string& str, std::string& trimmed)
    {
        std::string::size_type s = str.find_last_not_of(WS);
        if (s == std::string::npos)
        {
            return "";
        }

        trimmed = str.substr(s+1);
        return str.substr(0, s+1);
    }

    inline std::string trimleft(const std::string& str, std::string& trimmed)
    {
        std::string::size_type s = str.find_first_not_of(WS);
        if (s == std::string::npos)
        {
            return "";
        }

        trimmed = str.substr(0,s);
        return str.substr(s);
    }

    inline std::string trim(const std::string& str)
    {
        std::string l,r;
        return trim(str,l,r);
    }

    /**
     * Returns the trimmed string, along with what was trimmed off in output
     * parameters; this is used to preserve formatting.
     */
    inline std::string trim(const std::string& str, std::string& trimmedLeft, std::string& trimmedRight)
    {
        return trimright(trimleft(str, trimmedLeft), trimmedRight);
    }

    /**
     * Check if str ends with ch. This function will ignore trailing whitespaces.
     *
     * @param str String to search
     * @param ch Check if this character is the last non-whitespace character
     * @return True if str ends with ch, ignoring whitespaces
     */
    inline bool endswith(const std::string& str, char ch)
    {
        // This is utf8-safe, since we are scanning backwards and only looking for 7 bit chars.
        std::string::size_type pos = str.find_last_not_of(WS);
        return (pos == std::string::npos) ? false : str[pos] == ch;
    }

    /**
     * Joins a vector of string using the supplied separator.
     *
     * @param vs Vector of strings. If empty, an empty string is returned.
     * @param sep Separator
     * @param append If true, the separator will be appended to the final string
     * @return String joined by separator
     */
    inline std::string join(std::vector<std::string>& vs, std::string sep, bool append=false)
    {
        if (!vs.empty())
        {
            std::string str = std::accumulate(std::next(vs.begin()), vs.end(), vs[0],
                                            [&](std::string a, std::string b)
                                            { return a + sep + b; });

            if (append && !str.empty())
                str += sep;

            return str;
        }

        return "";
    }

    inline bool isTemplateVariable(const std::string& str)
    {
        std::string::size_type pos = str.find_first_not_of(WS);
        return (pos == std::string::npos) ? false : str[pos] == '%';
    }

    inline bool isTemplateStart(const std::string& str)
    {
        std::string::size_type pos = str.find_first_not_of(WS);
        return (pos == std::string::npos) ? false : str[pos] == '<';
    }

    inline bool isTemplateEnd(const std::string& str)
    {
        std::string::size_type pos = str.find_first_not_of(WS);
        return (pos == std::string::npos) ? false : str[pos] == '<' &&  pos+1 < str.size() && str[pos+1] == '/';
    }

    /**
     * A left-trimmed line starting with # or ! is a comment
     */
    inline bool isComment(const std::string& str)
    {
        std::string::size_type pos = str.find_first_not_of(WS);
        return (pos == std::string::npos) ? false : str[pos] == '#' || str[pos] == '!';
    }

    /**
     * A trimmed line containing only {
     */
    inline bool isBlockStart(const std::string& str)
    {
        std::string::size_type pos = str.find_first_not_of(WS);
        return (pos == std::string::npos) ? false : str[pos] == '{';
    }

    /**
     * A trimmed line containing only }
     */
    inline bool isBlockEnd(const std::string& str)
    {
        std::string::size_type pos = str.find_first_not_of(WS);
        return (pos == std::string::npos) ? false : str[pos] == '}';
    }
    /**
     * A right-trimmed line ending with \ is considered a multiline property.
     */
    inline bool isMultiLine(const std::string& str)
    {
        return endswith(str, '\\');
    }

    /**
     * An empty line, or a line consisting only of whitespace
     */
    inline bool isEmptyLine(const std::string& str)
    {
        return std::all_of(str.begin(), str.end(), isspace);
    }

    std::unordered_map<std::string, std::unique_ptr<Prop>> props;
    std::vector<Line> lines;
    std::vector<std::string> prefixStack;
    const std::string WS = " \n\r\t\v\f";
};

} // namespace

#endif //CXXPROPS_PROPERTIES_H
