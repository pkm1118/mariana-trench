/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <iterator>

#include <mariana-trench/JsonValidation.h>
#include <mariana-trench/Options.h>
#include <mariana-trench/constraints/MethodConstraints.h>
#include <mariana-trench/shim-generator/ShimGenerator.h>

namespace marianatrench {

JsonShimGenerator::JsonShimGenerator(
    std::unique_ptr<AllOfMethodConstraint> constraint,
    ShimTemplate shim_template)
    : constraint_(std::move(constraint)),
      shim_template_(std::move(shim_template)) {}

std::optional<Shim> JsonShimGenerator::visit_method(
    const Method* method) const {
  if (constraint_->satisfy(method)) {
    LOG(5,
        "Method `{}{}` satisfies all constraints in shim model generator",
        method->is_static() ? "(static) " : "",
        method->show());
    return shim_template_.instantiate(method);
  }

  return std::nullopt;
}

MethodToShimMap JsonShimGenerator::emit_method_shims(
    const Methods& methods,
    const MethodMappings& method_mappings) {
  MethodToShimMap method_shims;

  MethodHashedSet filtered_methods = constraint_->may_satisfy(method_mappings);
  if (filtered_methods.is_bottom()) {
    return method_shims;
  }

  if (!filtered_methods.is_top()) {
    return visit_methods(
        filtered_methods.elements().begin(), filtered_methods.elements().end());
  }

  return visit_methods(methods.begin(), methods.end());
}

} // namespace marianatrench
