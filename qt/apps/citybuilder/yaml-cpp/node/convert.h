//
// Forwarding header - see node.h.  Everything the shim provides lives in
// yaml.h; this exists so CBE's `#include <yaml-cpp/node/convert.h>` (in
// src/global/yamlLibraryEnhancement.hpp, which specializes YAML::convert for
// QString/QPoint/QSize) resolves against the shim.
//
#ifndef EWOKOS_YAML_CPP_NODE_CONVERT_HPP
#define EWOKOS_YAML_CPP_NODE_CONVERT_HPP

#include "../yaml.h"

#endif // EWOKOS_YAML_CPP_NODE_CONVERT_HPP
