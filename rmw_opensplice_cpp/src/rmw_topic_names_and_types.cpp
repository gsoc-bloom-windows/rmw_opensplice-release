// Copyright 2014-2015 Open Source Robotics Foundation, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <map>
#include <set>
#include <string>

#include "rcutils/allocator.h"
#include "rcutils/logging_macros.h"
#include "rcutils/strdup.h"
#include "rmw/allocators.h"
#include "rmw/error_handling.h"
#include "rmw/names_and_types.h"
#include "rmw/get_topic_names_and_types.h"
#include "rmw/rmw.h"
#include "rmw/sanity_checks.h"
#include "rmw/types.h"
#include "rmw/convert_rcutils_ret_to_rmw_ret.h"

#include "rosidl_typesupport_opensplice_cpp/misc.hpp"
#include "identifier.hpp"
#include "types.hpp"

// The extern "C" here enforces that overloading is not used.
extern "C"
{
rmw_ret_t
rmw_get_topic_names_and_types(
  const rmw_node_t * node,
  rcutils_allocator_t * allocator,
  bool no_demangle,
  rmw_names_and_types_t * topic_names_and_types)
{
  if (!node) {
    RMW_SET_ERROR_MSG("node handle is null");
    return RMW_RET_ERROR;
  }
  if (node->implementation_identifier != opensplice_cpp_identifier) {
    RMW_SET_ERROR_MSG("node handle is not from this rmw implementation");
    return RMW_RET_ERROR;
  }
  rmw_ret_t rmw_ret;
  rmw_ret = rmw_names_and_types_check_zero(topic_names_and_types);
  if (rmw_ret != RMW_RET_OK) {
    return rmw_ret;
  }

  auto node_info = static_cast<OpenSpliceStaticNodeInfo *>(node->data);
  if (!node_info) {
    RMW_SET_ERROR_MSG("node info handle is null");
    return RMW_RET_ERROR;
  }
  if (!node_info->publisher_listener) {
    RMW_SET_ERROR_MSG("publisher listener handle is null");
    return RMW_RET_ERROR;
  }
  if (!node_info->subscriber_listener) {
    RMW_SET_ERROR_MSG("subscriber listener handle is null");
    return RMW_RET_ERROR;
  }

  // combine publisher and subscriber information
  std::map<std::string, std::set<std::string>> topics;
  node_info->publisher_listener->fill_topic_names_and_types(no_demangle, topics);
  node_info->subscriber_listener->fill_topic_names_and_types(no_demangle, topics);

  // Copy data to results handle
  if (topics.size() > 0) {
    // Setup string array to store names
    rmw_ret_t rmw_ret = rmw_names_and_types_init(topic_names_and_types, topics.size(), allocator);
    if (rmw_ret != RMW_RET_OK) {
      return rmw_ret;
    }
    // Setup cleanup function, in case of failure below
    auto fail_cleanup = [&topic_names_and_types]() {
        rmw_ret_t rmw_ret = rmw_names_and_types_fini(topic_names_and_types);
        if (rmw_ret != RMW_RET_OK) {
          RCUTILS_LOG_ERROR("error during report of error: %s", rmw_get_error_string_safe())
        }
      };
    // Setup demangling functions based on no_demangle option
    auto demangle_topic = demangle_if_ros_topic;
    auto demangle_type = demangle_if_ros_type;
    if (no_demangle) {
      auto noop = [](const std::string & in) {
          return in;
        };
      demangle_topic = noop;
      demangle_type = noop;
    }
    // For each topic, store the name, initialize the string array for types, and store all types
    size_t index = 0;
    for (const auto & topic_n_types : topics) {
      // Duplicate and store the topic_name
      char * topic_name = rcutils_strdup(demangle_topic(topic_n_types.first).c_str(), *allocator);
      if (!topic_name) {
        RMW_SET_ERROR_MSG_ALLOC("failed to allocate memory for topic name", *allocator);
        fail_cleanup();
        return RMW_RET_BAD_ALLOC;
      }
      topic_names_and_types->names.data[index] = topic_name;
      // Setup storage for types
      {
        rcutils_ret_t rcutils_ret = rcutils_string_array_init(
          &topic_names_and_types->types[index],
          topic_n_types.second.size(),
          allocator);
        if (rcutils_ret != RCUTILS_RET_OK) {
          RMW_SET_ERROR_MSG(rcutils_get_error_string_safe())
          fail_cleanup();
          return rmw_convert_rcutils_ret_to_rmw_ret(rcutils_ret);
        }
      }
      // Duplicate and store each type for the topic
      size_t type_index = 0;
      for (const auto & type : topic_n_types.second) {
        char * type_name = rcutils_strdup(demangle_type(type).c_str(), *allocator);
        if (!type_name) {
          RMW_SET_ERROR_MSG_ALLOC("failed to allocate memory for type name", *allocator)
          fail_cleanup();
          return RMW_RET_BAD_ALLOC;
        }
        topic_names_and_types->types[index].data[type_index] = type_name;
        ++type_index;
      }  // for each type
      ++index;
    }  // for each topic
  }
  return RMW_RET_OK;
}
}  // extern "C"
