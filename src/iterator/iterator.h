#pragma once
#include <string>

class Iterator {
public:
    virtual ~Iterator() = default;

    virtual bool valid() const = 0;
    virtual void next() = 0;

    virtual std::string key() const = 0;
    virtual std::string value() const = 0;

    virtual void seek(const std::string& target) = 0;
    
    // Prefix 优化接口
    virtual void seek_to_first() {
        seek("");
    }
    
    virtual void seek_with_prefix(const std::string& prefix) {
        seek(prefix);
    }
};
