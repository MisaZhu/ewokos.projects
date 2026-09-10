//
// Forwarding header.  CBE includes <yaml-cpp/node/node.h> and
// <yaml-cpp/node/convert.h> at different points; the shim keeps everything in
// one place, so both resolve here.  The include guard in yaml.h makes the
// double inclusion a no-op.
//
#ifndef EWOKOS_YAML_CPP_NODE_NODE_HPP
#define EWOKOS_YAML_CPP_NODE_NODE_HPP

#include "../yaml.h"

#endif // EWOKOS_YAML_CPP_NODE_NODE_HPP
