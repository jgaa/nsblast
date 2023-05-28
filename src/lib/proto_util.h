#pragma once

#include <ranges>
#include <optional>
#include "proto/nsblast.pb.h"

namespace nsblast::lib {

#define PB_GET(obj, prop, defval) \
    (obj.has_##prop() ? obj.prop() : defval)

template <typename T>
concept ProtoList = requires(T container) {
    requires std::ranges::range<T>;
};

template <typename T>
concept ProtoMessage = std::is_base_of<google::protobuf::Message, T>::value;

template <ProtoList T, typename nameT, typename messageT = typename T::value_type>
std::optional<messageT>  getFromList(const T& list, const nameT& name) {
    for(const auto& item : list) {
        if (item.has_name() && compareCaseInsensitive(item.name(), name)) {
            return item;
        }
    }

    return {};
}

template <ProtoList T, typename nameT, typename messageT = typename T::value_type, typename fnT>
std::optional<messageT>  getFromList(const T& list, const fnT& fn) {
    for(const auto& item : list) {
        if (fn(item)) {
            return item;
        }
    }

    return {};
}

template <ProtoList T, typename nameT, typename messageT = typename T::value_type>
void removeFromList(T *list, const nameT& name) {
    for(auto it = list->begin(); it != list->end(); ++it) {
        if (it->has_name() && compareCaseInsensitive(it->name(), name)) {
            list->erase(it);
            return;
        }
    }
}

} // ns
