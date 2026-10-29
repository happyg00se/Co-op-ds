#include "dscommon/config.hpp"

#include <fstream>
#include <system_error>

namespace dscommon::config {

std::optional<json> load(const std::filesystem::path& file, std::string* error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open " + file.string();
        return std::nullopt;
    }
    try {
        return json::parse(in, nullptr, true, true);  // allow comments
    } catch (const std::exception& e) {
        if (error) *error = std::string("json parse error in ") + file.string() + ": " + e.what();
        return std::nullopt;
    }
}

bool save(const std::filesystem::path& file, const json& j, std::string* error) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    const auto tmp = file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) *error = "cannot write " + tmp;
            return false;
        }
        out << j.dump(2) << '\n';
    }
    std::filesystem::rename(tmp, file, ec);
    if (ec) {
        // Windows: rename over an existing file can fail; fall back to copy.
        std::filesystem::copy_file(tmp, file, std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove(tmp);
        if (ec) {
            if (error) *error = "cannot replace " + file.string() + ": " + ec.message();
            return false;
        }
    }
    return true;
}

void mergeDefaults(json& target, const json& defaults) {
    if (!target.is_object() || !defaults.is_object()) {
        if (target.is_null()) target = defaults;
        return;
    }
    for (auto it = defaults.begin(); it != defaults.end(); ++it) {
        if (!target.contains(it.key())) target[it.key()] = it.value();
        else if (it.value().is_object() && target[it.key()].is_object()) mergeDefaults(target[it.key()], it.value());
    }
}

json loadOrCreate(const std::filesystem::path& file, const json& defaults) {
    auto loaded = load(file);
    if (!loaded) {
        save(file, defaults);
        return defaults;
    }
    json j = std::move(*loaded);
    mergeDefaults(j, defaults);
    return j;
}

}  // namespace dscommon::config
