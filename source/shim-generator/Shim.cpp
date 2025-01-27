/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <algorithm>
#include <optional>

#include <Show.h>

#include <mariana-trench/Access.h>
#include <mariana-trench/Log.h>
#include <mariana-trench/Methods.h>
#include <mariana-trench/shim-generator/Shim.h>

namespace marianatrench {
namespace {

DexType* MT_NULLABLE
get_parameter_type(const Method* method, ParameterPosition position) {
  if (position >= method->number_of_parameters()) {
    ERROR(
        1,
        "Parameter mapping for shim_target `{}` contains a port on parameter {} but the method only has {} parameters.",
        method->show(),
        position,
        method->number_of_parameters());
    return nullptr;
  }

  if (!method->is_static() && position == 0) {
    // Include "this" as argument 0
    return method->get_class();
  }

  return method->get_proto()->get_args()->at(
      position - method->first_parameter_index());
}

} // namespace

ShimMethod::ShimMethod(const Method* method) : method_(method) {
  ShimParameterPosition index = 0;

  if (!method_->is_static()) {
    // Include "this" as argument 0
    types_to_position_.emplace(method->get_class(), index++);
  }

  const auto* dex_arguments = method_->get_proto()->get_args();
  if (!dex_arguments) {
    return;
  }

  for (auto* dex_argument : *dex_arguments) {
    types_to_position_.emplace(dex_argument, index++);
  }
}

const Method* ShimMethod::method() const {
  return method_;
}

DexType* MT_NULLABLE
ShimMethod::parameter_type(ShimParameterPosition argument) const {
  return method_->parameter_type(argument);
}

std::optional<ShimParameterPosition> ShimMethod::type_position(
    const DexType* dex_type) const {
  auto found = types_to_position_.find(dex_type);
  if (found == types_to_position_.end()) {
    return std::nullopt;
  }

  LOG(5,
      "Found dex type {} in shim parameter position: {}",
      found->first->str(),
      found->second);

  return found->second;
}

ShimParameterMapping::ShimParameterMapping() {}

bool ShimParameterMapping::empty() const {
  return map_.empty();
}

bool ShimParameterMapping::contains(ParameterPosition position) const {
  return map_.count(position) > 0;
}

std::optional<ShimParameterPosition> ShimParameterMapping::at(
    ParameterPosition parameter_position) const {
  auto found = map_.find(parameter_position);
  if (found == map_.end()) {
    return std::nullopt;
  }

  return found->second;
}

void ShimParameterMapping::insert(
    ParameterPosition parameter_position,
    ShimParameterPosition shim_parameter_position) {
  map_.insert_or_assign(parameter_position, shim_parameter_position);
}

ShimParameterMapping ShimParameterMapping::from_json(const Json::Value& value) {
  ShimParameterMapping parameter_mapping;
  if (value.isNull()) {
    return parameter_mapping;
  }

  JsonValidation::validate_object(value);

  for (auto item = value.begin(); item != value.end(); ++item) {
    auto parameter_argument = JsonValidation::string(item.key());
    auto shim_argument = JsonValidation::string(*item);
    parameter_mapping.insert(
        Root::from_json(parameter_argument).parameter_position(),
        Root::from_json(shim_argument).parameter_position());
  }

  return parameter_mapping;
}

ShimParameterMapping ShimParameterMapping::infer(
    const Method* shim_target_callee,
    const ShimMethod& shim_method) {
  ShimParameterMapping parameter_mapping;

  if (!shim_target_callee->is_static()) {
    // Include "this" as argument 0
    auto receiver_position =
        shim_method.type_position(shim_target_callee->get_class());
    if (!receiver_position) {
      return parameter_mapping;
    }
    parameter_mapping.insert(0, *receiver_position);
  }

  const auto* dex_arguments = shim_target_callee->get_proto()->get_args();
  if (!dex_arguments) {
    return parameter_mapping;
  }

  auto first_parameter_position = shim_target_callee->first_parameter_index();
  for (std::size_t position = 0; position < dex_arguments->size(); position++) {
    if (auto shim_position =
            shim_method.type_position(dex_arguments->at(position))) {
      parameter_mapping.insert(
          static_cast<ParameterPosition>(position + first_parameter_position),
          *shim_position);
    }
  }

  return parameter_mapping;
}

ShimParameterMapping ShimParameterMapping::instantiate(
    const Method* shim_target_callee,
    const ShimMethod& shim_method) const {
  if (map_.empty()) {
    return infer(shim_target_callee, shim_method);
  }

  ShimParameterMapping parameter_mapping;
  for (const auto& [position, shim_position] : map_) {
    auto callee_type = get_parameter_type(shim_target_callee, position);
    if (callee_type == nullptr) {
      continue;
    }

    auto shim_type = shim_method.parameter_type(shim_position);
    if (callee_type != shim_type) {
      ERROR(
          1,
          "Parameter mapping type mismatch for shim_target `{}` for parameter {}. Expected: {} but got {}.",
          shim_target_callee->show(),
          position,
          show(callee_type),
          show(shim_type));
      continue;
    }

    parameter_mapping.insert(position, shim_position);
  }

  return parameter_mapping;
}

ShimTarget::ShimTarget(
    const Method* method,
    ShimParameterMapping parameter_mapping)
    : call_target_(method), parameter_mapping_(std::move(parameter_mapping)) {}

std::optional<ShimTarget> ShimTarget::from_json(
    const Json::Value& value,
    Context& context) {
  if (!value.isMember("signature")) {
    ERROR(1, "Invalid shim definition. callee must have `signature`");
    return std::nullopt;
  }

  auto signature = JsonValidation::string(value, "signature");
  auto* call_target = context.methods->get(signature);

  if (call_target == nullptr) {
    WARNING(1, "Provided method signature not found: `{}`", signature);
    return std::nullopt;
  }

  auto parameters_map = ShimParameterMapping::from_json(
      JsonValidation::null_or_object(value, "parameters_map"));

  return ShimTarget(call_target, std::move(parameters_map));
}

std::optional<ShimTarget> ShimTarget::instantiate(
    const ShimMethod& shim_method) const {
  auto parameter_mapping =
      parameter_mapping_.instantiate(call_target_, shim_method);

  // Require non-static methods to have a receiver.
  if (!call_target_->is_static() && !parameter_mapping.at(0)) {
    WARNING(
        1,
        "Shim method `{}` missing `this` argument required for non-static shim callee `{}`.",
        shim_method.method()->show(),
        show(call_target_));
    return std::nullopt;
  }

  return ShimTarget(call_target_, std::move(parameter_mapping));
}

std::optional<Register> ShimTarget::receiver_register(
    const IRInstruction* instruction) const {
  if (call_target_->is_static()) {
    return std::nullopt;
  }

  auto receiver_position = parameter_mapping_.at(0);
  if (!receiver_position) {
    return std::nullopt;
  }

  mt_assert(*receiver_position < instruction->srcs_size());

  return instruction->src(*receiver_position);
}

std::unordered_map<ParameterPosition, Register> ShimTarget::parameter_registers(
    const IRInstruction* instruction) const {
  std::unordered_map<ParameterPosition, Register> parameter_registers;

  for (ParameterPosition position = 0;
       position < call_target_->number_of_parameters();
       ++position) {
    if (auto shim_position = parameter_mapping_.at(position)) {
      parameter_registers.emplace(position, instruction->src(*shim_position));
    }
  }

  return parameter_registers;
}

Shim::Shim(const Method* method, std::vector<ShimTarget> targets)
    : method_(method), targets_(std::move(targets)) {}

std::ostream& operator<<(std::ostream& out, const ShimParameterMapping& map) {
  out << "parameters_map={";
  for (const auto& [parameter, shim_parameter] : map.map_) {
    out << " Argument(" << parameter << "):";
    out << " Argument(" << shim_parameter << "),";
  }
  return out << " }";
}

std::ostream& operator<<(std::ostream& out, const ShimTarget& shim_target) {
  out << "ShimTarget(method=`";
  if (shim_target.call_target_ != nullptr) {
    out << shim_target.call_target_->show();
  }
  out << "`";
  out << ", " << shim_target.parameter_mapping_;
  return out << ")";
}

std::ostream& operator<<(std::ostream& out, const Shim& shim) {
  out << "ShimTarget(method=`";
  if (auto* method = shim.method()) {
    out << method->show();
  }
  out << "`";
  if (!shim.empty()) {
    out << ",\n  targets=[\n";
    for (const auto& target : shim.targets()) {
      out << "    " << target << ",\n";
    }
    out << "  ]";
  }
  return out << ")";
}

} // namespace marianatrench
