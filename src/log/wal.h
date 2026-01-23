#pragma once
#include <fstream>
#include <string>
#include <functional>
#include <iostream>
#include <sstream>
#ifdef __has_include
#    if __has_include(<filesystem>)
#        include <filesystem>
         namespace fs = std::filesystem;
#    elif __has_include(<experimental/filesystem>)
#        include <experimental/filesystem>
         namespace fs = std::experimental::filesystem;
#    else
#        error "No filesystem support"
#    endif
#endif

class WAL {
public:
    explicit WAL(const std::string& filename);
    void log_put(const std::string& key, const std::string& value);
    void log_del(const std::string& key);

    void replay(
        const std::function<void(const std::string&, const std::string&)>& on_put,
        const std::function<void(const std::string&)>& on_del
    );
    const std::string& get_filename() const { return filename_; }

private:
    std::ofstream file_;
    std::string filename_;
};
