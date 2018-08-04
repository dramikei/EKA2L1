#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include <common/log.h>

namespace eka2l1 {
    enum class handle_owner_type {
        kernel,
        process,
        thread
    };

    struct handle_info {
        bool free;

        int org;

        handle_owner_type owner;
        uint64_t owner_id;
    };

    template <unsigned int max_handles = 512>
    class handle_table {
        std::array<handle_info, max_handles> handles;

    public:
        handle_table() {
            handle_info init_info;
            init_info.free = true;
            init_info.org = -1;

            std::fill(handles.begin(), handles.end(), init_info);
        }

        uint64_t get_owner_id(int handle) {
            if (handle <= 0 || handle > max_handles) {
                return 0;
            }

            return handles[handle - 1].owner_id;
        }

        int new_handle(handle_owner_type owner, uint64_t owner_id) {
            auto &frhandle = std::find_if(handles.begin(), handles.end(),
                [](const auto &handle) -> bool {
                    return handle.free;
                });

            if (frhandle == handles.end()) {
                return -1;
            }

            frhandle->free = false;
            frhandle->org = frhandle - handles.begin() + 1;
            frhandle->owner = owner;
            frhandle->owner_id = owner_id;

            return frhandle->org;
        }

        int get_real_handle_id(int handle) {
            if (handle < 1 || handle > max_handles) {
                return -1;
            }

            return handles[handle - 1].org;
        }

        virtual bool free_handle(int handle) {
            if (handle > max_handles) {
                return false;
            }

            handles[handle - 1].free = true;

            return true;
        }

        bool handle_closeable(int handle) {
            if (handle > max_handles) {
                return false;
            }

            return handles[handle - 1].closeable;
        }

        void handle_set_closeable(int handle, bool closeable) {
            if (handle > max_handles) {
                return;
            }

            handles[handle - 1].closeable = closeable;
        }

        void free_all_handle(int owner_id) {
            for (size_t i = 0; i < max_handles; i++) {
                if (!handles[i].free && handles[i].owner_id == owner_id) {
                    free_handle(i + 1);
                }
            }
        }

    };
}