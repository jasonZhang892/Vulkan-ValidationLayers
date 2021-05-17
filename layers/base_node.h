/* Copyright (c) 2015-2021 The Khronos Group Inc.
 * Copyright (c) 2015-2021 Valve Corporation
 * Copyright (c) 2015-2021 LunarG, Inc.
 * Copyright (C) 2015-2021 Google Inc.
 * Modifications Copyright (C) 2020 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Author: Courtney Goeltzenleuchter <courtneygo@google.com>
 * Author: Tobin Ehlis <tobine@google.com>
 * Author: Chris Forbes <chrisf@ijw.co.nz>
 * Author: Mark Lobodzinski <mark@lunarg.com>
 * Author: Dave Houlton <daveh@lunarg.com>
 * Author: John Zulauf <jzulauf@lunarg.com>
 * Author: Tobias Hector <tobias.hector@amd.com>
 * Author: Jeremy Gebben <jeremyg@lunarg.com>
 */
#pragma once

#include "vulkan/vulkan.h"
#include "vk_object_types.h"
#include "vk_layer_data.h"

#include <atomic>

struct CMD_BUFFER_STATE;

class BASE_NODE {
  public:
    using BindingsType = layer_data::unordered_set<BASE_NODE*>;

    template <typename Handle>
    BASE_NODE(Handle h, VulkanObjectType t) : handle_(h, t, this), destroyed_(false) {}

    virtual ~BASE_NODE() { Destroy(); }

    virtual void Destroy() {
        Invalidate();
        destroyed_ = true;
    }

    bool Destroyed() const { return destroyed_; }

    const VulkanTypedHandle &Handle() const { return handle_; }

    virtual bool InUse() const {
        bool result = false;
        for (auto& node: parent_nodes_) {
            result |= node->InUse();
            if (result) {
                break;
            }
        }
        return result;
    }

    virtual bool AddParent(BASE_NODE *parent_node) {
        auto result = parent_nodes_.emplace(parent_node);
        return result.second;
    }

    virtual void RemoveParent(BASE_NODE *parent_node) {
        parent_nodes_.erase(parent_node);
    }

    void Invalidate(bool unlink = true) {
        LogObjectList invalid_handles(handle_);
        for (auto& node: parent_nodes_) {
            node->NotifyInvalidate(invalid_handles, unlink);
        }
        if (unlink) {
            parent_nodes_.clear();
        }
    }
  protected:
    virtual void NotifyInvalidate(const LogObjectList& invalid_handles, bool unlink) {
        if (parent_nodes_.size() == 0) {
            return;
        }
        LogObjectList up_handles = invalid_handles;
        up_handles.object_list.emplace_back(handle_);
        for (auto& node: parent_nodes_) {
            node->NotifyInvalidate(up_handles, unlink);
        }
        if (unlink) {
            parent_nodes_.clear();
        }
    }

    VulkanTypedHandle handle_;

    // Set to true when the API-level object is destroyed, but this object may
    // hang around until its shared_ptr refcount goes to zero.
    bool destroyed_;

    // Set of immediate parent nodes for this object. For an in-use object, the
    // parent nodes should form a tree with the root being a command buffer.
    BindingsType parent_nodes_;
};

class REFCOUNTED_NODE : public BASE_NODE {
private:
    // Track if command buffer is in-flight
    std::atomic_int in_use_;

public:
    template <typename Handle>
    REFCOUNTED_NODE(Handle h, VulkanObjectType t) : BASE_NODE(h, t), in_use_(0) { }

    void BeginUse() { in_use_.fetch_add(1); }

    void EndUse() { in_use_.fetch_sub(1); }

    void ResetUse() { in_use_.store(0); }

    bool InUse() const override { return (in_use_.load() > 0) || BASE_NODE::InUse(); }
};
