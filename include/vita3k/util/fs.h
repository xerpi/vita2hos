// Vita3K emulator project
// Copyright (C) 2021 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>

#include "string_utils.h"

namespace fs = std::filesystem;

class Root
{
    fs::path base_path;
    fs::path pref_path;

  public:
    void set_base_path(const fs::path &p)
    {
        base_path = p;
    }

    fs::path get_base_path() const
    {
        return base_path;
    }

    std::string get_base_path_string() const
    {
        return base_path.string();
    }

    void set_pref_path(const fs::path &p)
    {
        pref_path = p;
    }

    fs::path get_pref_path() const
    {
        return pref_path;
    }

    std::string get_pref_path_string() const
    {
        return pref_path.string();
    }
}; // class root

namespace fs_utils
{

/**
 * \brief  Construct a file name (optionally with an extension) to be placed in a Vita3K directory.
 * \param  base_path   The main output path for the file.
 * \param  folder_path The sub-directory/sub-directories to output to.
 * \param  file_name   The name of the file.
 * \param  extension   The extension of the file (optional)
 * \return A complete Boost.Filesystem file path normalized.
 */
static inline fs::path construct_file_name(const fs::path &base_path, const fs::path &folder_path,
                                           const fs::path &file_name, const fs::path &extension)
{
    fs::path full_file_path{ base_path / folder_path / file_name };
    if (!extension.empty())
        full_file_path.replace_extension(extension);

    return full_file_path;
}

static inline std::string path_to_utf8(const fs::path &path)
{
    if constexpr (sizeof(fs::path::value_type) == sizeof(wchar_t)) {
        return string_utils::wide_to_utf(path.wstring());
    } else {
        return path.string();
    }
}

static inline fs::path utf8_to_path(const std::string &str)
{
    if constexpr (sizeof(fs::path::value_type) == sizeof(wchar_t)) {
        return fs::path{ string_utils::utf_to_wide(str) };
    } else {
        return fs::path{ str };
    }
}

static inline fs::path path_concat(const fs::path &path1, const fs::path &path2)
{
    return fs::path{ path1.native() + path2.native() };
}

static inline void dump_data(const fs::path &path, const void *data, const std::streamsize size)
{
    std::ofstream of{ path, std::ofstream::binary };
    if (!of.fail()) {
        of.write(static_cast<const char *>(data), size);
        of.close();
    }
}

template <typename T> static inline bool read_data(const fs::path &path, std::vector<T> &data)
{
    data.clear();
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }

    // Get the size of the file
    std::streamsize size = file.tellg();
    if (size <= 0) {
        return false;
    }

    // Resize the vector to fit the file content
    data.resize(size);

    // Go back to the beginning of the file and read the content
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char *>(data.data()), size)) {
        return false;
    }
    return true;
}

static inline bool read_data(const fs::path &path, std::vector<uint8_t> &data)
{
    return read_data<uint8_t>(path, data);
}

static inline bool read_data(const fs::path &path, std::vector<int8_t> &data)
{
    return read_data<int8_t>(path, data);
}

static inline bool read_data(const fs::path &path, std::vector<char> &data)
{
    return read_data<char>(path, data);
}

} // namespace fs_utils
