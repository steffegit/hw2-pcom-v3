#pragma once

#include <set>
#include <string>

struct Client {
    std::string id;
    std::set<std::string> subscriptions;
};