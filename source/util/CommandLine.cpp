//------------------------------------------------------------------------------
// CommandLine.cpp
// Command line argument parsing support.
//
// File is under the MIT license; see LICENSE for details.
//------------------------------------------------------------------------------
#include "slang/util/CommandLine.h"

#include "../text/CharInfo.h"
#include <filesystem>
#include <fmt/format.h>

#include "slang/util/SmallVector.h"

namespace fs = std::filesystem;

namespace slang {

void CommandLine::add(string_view name, optional<bool>& value, string_view desc) {
    addInternal(name, &value, desc, {});
}
void CommandLine::add(string_view name, optional<int32_t>& value, string_view desc,
                      string_view valueName) {
    addInternal(name, &value, desc, valueName);
}
void CommandLine::add(string_view name, optional<uint32_t>& value, string_view desc,
                      string_view valueName) {
    addInternal(name, &value, desc, valueName);
}
void CommandLine::add(string_view name, optional<int64_t>& value, string_view desc,
                      string_view valueName) {
    addInternal(name, &value, desc, valueName);
}
void CommandLine::add(string_view name, optional<uint64_t>& value, string_view desc,
                      string_view valueName) {
    addInternal(name, &value, desc, valueName);
}

void CommandLine::add(string_view name, optional<double>& value, string_view desc,
                      string_view valueName) {
    addInternal(name, &value, desc, valueName);
}
void CommandLine::add(string_view name, optional<std::string>& value, string_view desc,
                      string_view valueName) {
    addInternal(name, &value, desc, valueName);
}

void CommandLine::add(string_view name, std::vector<bool>& value, string_view desc) {
    addInternal(name, &value, desc, {});
}
void CommandLine::add(string_view name, std::vector<int32_t>& value, string_view desc,
                      string_view valueName) {
    addInternal(name, &value, desc, valueName);
}
void CommandLine::add(string_view name, std::vector<uint32_t>& value, string_view desc,
                      string_view valueName) {
    addInternal(name, &value, desc, valueName);
}
void CommandLine::add(string_view name, std::vector<int64_t>& value, string_view desc,
                      string_view valueName) {
    addInternal(name, &value, desc, valueName);
}
void CommandLine::add(string_view name, std::vector<uint64_t>& value, string_view desc,
                      string_view valueName) {
    addInternal(name, &value, desc, valueName);
}
void CommandLine::add(string_view name, std::vector<double>& value, string_view desc,
                      string_view valueName) {
    addInternal(name, &value, desc, valueName);
}
void CommandLine::add(string_view name, std::vector<std::string>& value, string_view desc,
                      string_view valueName) {
    addInternal(name, &value, desc, valueName);
}

void CommandLine::addInternal(string_view name, OptionStorage storage, string_view desc,
                              string_view valueName) {
    auto option = std::make_shared<Option>();
    option->desc = desc;
    option->valueName = valueName;
    option->storage = std::move(storage);

    if (name.empty())
        throw std::invalid_argument("Name cannot be empty");

    while (!name.empty()) {
        size_t index = name.find_first_of(',');
        string_view curr = name;
        if (index != string_view::npos) {
            curr = name.substr(0, index);
            name = name.substr(index + 1);
        }

        if (curr.empty() || curr[0] != '-')
            throw std::invalid_argument("Names must begin with '-' or '--'");

        curr = name.substr(1);
        if (curr.empty())
            throw std::invalid_argument("Names must begin with '-' or '--'");
        if (curr[0] == '-')
            curr = curr.substr(1);

        if (!optionMap.try_emplace(std::string(curr), option).second)
            throw std::invalid_argument(fmt::format("Argument with name '{}' already exists", curr));
    }
}

bool CommandLine::parse(int argc, const char* const argv[]) {
    SmallVectorSized<string_view, 8> args(argc);
    for (int i = 0; i < argc; i++)
        args.append(argv[i]);

    return parse(args);
}

bool CommandLine::parse(int argc, const wchar_t* const argv[]) {
    SmallVectorSized<std::string, 8> storage(argc);
    SmallVectorSized<string_view, 8> args(argc);
    for (int i = 0; i < argc; i++) {
        storage.append(narrow(argv[i]));
        args.append(storage.back());
    }

    return parse(args);
}

bool CommandLine::parse(string_view argList) {
    SmallVectorSized<std::string, 8> storage;
    std::string current;
    auto end = argList.data() + argList.size();

    for (const char* ptr = argList.data(); ptr != end; ptr++) {
        // Whitespace breaks up arguments.
        if (isWhitespace(*ptr)) {
            // Ignore empty arguments.
            if (!current.empty()) {
                storage.emplace(std::move(current));
                current.clear();
            }
            continue;
        }

        // Escape character preserves the value of the next character.
        if (*ptr == '\\') {
            if (++ptr != end)
                current += *ptr;
            continue;
        }

        // Single quotes consume all characters until the next single quote.
        if (*ptr == '\'') {
            while (++ptr != end && *ptr != '\'')
                current += *ptr;
            continue;
        }

        // Double quotes consume all characters except escaped characters.
        if (*ptr == '"') {
            while (++ptr != end && *ptr != '"') {
                // Only backslashes and quotes can be escaped.
                if (*ptr == '\\' && ptr + 1 != end && (ptr[1] == '\\' || ptr[1] == '"'))
                    ++ptr;
                current += *ptr;
            }
            continue;
        }

        // Otherwise we just have a normal character.
        current += *ptr;
    }

    if (!current.empty())
        storage.emplace(std::move(current));

    SmallVectorSized<string_view, 8> args(storage.size());
    for (auto& arg : storage)
        args.append(arg);

    return parse(args);
}

bool CommandLine::parse(span<const string_view> args) {
    if (args.empty())
        throw std::runtime_error("Expected at least one argument");

    programName = fs::path(args[0]).filename().string();

    SmallVectorSized<string_view, 8> positionalArgs;
    bool doubleDash = false;
    for (auto it = args.begin(); it != args.end(); it++) {
        // Skip completely empty arguments.
        string_view arg = *it;
        if (arg.empty())
            continue;

        // This is a positional argument if:
        // - Doesn't start with '-'
        // - Is exactly '-'
        // - Or we've seen a double dash already
        if (arg[0] != '-' || arg.length() == 1 || doubleDash) {
            positionalArgs.append(arg);
            continue;
        }

        // Double dash indicates that all further arguments are positional.
        if (arg == "--"sv) {
            doubleDash = true;
            continue;
        }

        // Get the raw name without leading dashes.
        bool longName = false;
        string_view name = arg.substr(1);
        if (name[0] == '-') {
            longName = true;
            name = name.substr(1);
        }

        string_view value;
        auto option = findOption(name, value);

        // If we didn't find the option and there was only a single dash,
        // maybe this was actually a group of single-char options or a prefixed value.
        if (!option && !longName)
            option = tryGroupOrPrefix(name, value);

        // If we still didn't find it, that's an error.
        if (!option) {
            // Try to find something close to give a better error message.
            auto error = fmt::format("{}: unknown command line argument '{}'"sv, programName, name);
            auto nearest = findNearestMatch(name);
            if (!nearest.empty())
                error += fmt::format(", did you mean '{}'?"sv, nearest);

            errors.emplace_back(std::move(error));
            continue;
        }

        // Otherwise, we found what we wanted.
        option->set(value);
    }

    if (positional) {
        for (auto arg : positionalArgs)
            positional->set(arg);
    }
    else if (!positionalArgs.empty()) {
        errors.emplace_back(
            fmt::format("{}: positional arguments are not allowed (see e.g. '{}')"sv, programName,
                        positionalArgs[0]));
    }

    return errors.empty();
}

CommandLine::Option* CommandLine::findOption(string_view arg, string_view& value) const {
    if (arg.empty())
        return nullptr;

    // If there is an equals sign, strip off the value.
    size_t equalsIndex = arg.find_first_of('=');
    if (equalsIndex != string_view::npos) {
        value = arg.substr(equalsIndex + 1);
        arg = arg.substr(0, equalsIndex);
    }

    // TODO: change once we have heterogeneous lookup from C++20
    auto it = optionMap.find(std::string(arg));
    if (it == optionMap.end())
        return nullptr;

    return it->second.get();
}

CommandLine::Option* CommandLine::tryGroupOrPrefix(string_view& , string_view& ) {
    // TODO:
    return nullptr;
}

string_view CommandLine::findNearestMatch(string_view arg) const {
    size_t equalsIndex = arg.find_first_of('=');
    if (equalsIndex != string_view::npos)
        arg = arg.substr(0, equalsIndex);

    string_view bestName;
    int bestDistance = INT_MAX;

    for (auto& [key, value] : optionMap) {
        int dist = editDistance(key, arg, /* allowReplacements */ true, bestDistance);
        if (dist < bestDistance) {
            bestName = key;
            bestDistance = dist;
        }
    }

    return bestName;
}

void CommandLine::Option::set(string_view ) {
    // TODO:
}

} // namespace slang