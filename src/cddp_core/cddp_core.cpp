/*
 Copyright 2024 Tomo Sasaki

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

      https://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
*/

#include "cddp_core/cddp_core.hpp"      // For CDDP class declaration
#include "cddp_core/alddp_solver.hpp"   // For AlddpSolver
#include "cddp_core/asddp_solver.hpp"   // For ASDDPSolver
#include "cddp_core/clddp_solver.hpp"   // For CLDDPSolver
#include "cddp_core/ipddp_solver.hpp"   // For IPDDPSolver
#include "cddp_core/logddp_solver.hpp"  // For LogDDPSolver
#include "cddp_core/msipddp_solver.hpp" // For MSIPDDPSolver
#include "cddp_core/options.hpp"        // For CDDPOptions structure
#include <cmath>                        // For std::min, std::max
#include <functional>
#include <iomanip> // For std::setw
#include <iostream>
#include <map>
#include <array>

namespace cddp {

// Static registry definition
std::map<std::string, std::function<std::unique_ptr<ISolverAlgorithm>()>>
    CDDP::external_solver_registry_;

// Constructor
CDDP::CDDP(const Eigen::VectorXd &initial_state,
           const Eigen::VectorXd &reference_state, int horizon, double timestep,
           std::unique_ptr<DynamicalSystem> system,
           std::unique_ptr<Objective> objective, const CDDPOptions &options)
    : initial_state_(initial_state), reference_state_(reference_state),
      horizon_(horizon), timestep_(timestep), system_(std::move(system)),
      objective_(std::move(objective)), options_(options),
      initialized_(false), // Will be set true by initializeProblemIfNecessary
                           // or by strategies
      cost_(0.0), merit_function_(0.0), inf_pr_(0.0), inf_du_(0.0),
      inf_comp_(0.0),
      alpha_pr_(
          options.line_search.initial_step_size), // Initialize from options
      alpha_du_(0.0), regularization_(options.regularization.initial_value),
      terminal_regularization_(options.regularization.initial_value),
      total_dual_dim_(0) {

  if (objective_ && !reference_state.isZero() &&
      reference_state.size() >
          0) { // Check if reference_state is valid before setting
    objective_->setReferenceState(reference_state_);
  }
  // Basic alpha sequence for line search
  alphas_.clear();
  double current_alpha = options_.line_search.initial_step_size;
  for (int i = 0; i < options_.line_search.max_iterations; ++i) {
    alphas_.push_back(current_alpha);
    current_alpha *= options_.line_search.step_reduction_factor;
    if (current_alpha < options_.line_search.min_step_size &&
        i < options_.line_search.max_iterations - 1) {
      alphas_.push_back(
          options_.line_search.min_step_size); // Ensure min_step_size is tried
      break;
    }
  }
  if (alphas_
          .empty()) { // Ensure at least one alpha if max_iterations is 0 or 1
    alphas_.push_back(options_.line_search.initial_step_size);
  }
}
// --- Setters ---
void CDDP::setDynamicalSystem(std::unique_ptr<DynamicalSystem> system) {
  system_ = std::move(system);
  initialized_ = false; // Dimensions might change
}

void CDDP::setInitialState(const Eigen::VectorXd &initial_state) {
  initial_state_ = initial_state;
  if (X_.empty() || X_[0].size() != initial_state.size()) {
    // If X_ is not compatible, it will be handled by
    // initializeProblemIfNecessary
  } else {
    X_[0] = initial_state_;
  }
}

void CDDP::setReferenceState(const Eigen::VectorXd &reference_state) {
  reference_state_ = reference_state;
  if (objective_) {
    objective_->setReferenceState(reference_state_);
  }
  reference_states_.clear(); // Clear trajectory if single ref state is set
  reference_states_.push_back(
      reference_state_); // For consistency if getReferenceStates is used
}

void CDDP::setReferenceStates(
    const std::vector<Eigen::VectorXd> &reference_states) {
  reference_states_ = reference_states;
  if (objective_) {
    objective_->setReferenceStates(reference_states_);
  }
  if (!reference_states_.empty()) {
    reference_state_ =
        reference_states_
            .back(); // Update single reference state to the final one
  }
}

void CDDP::setHorizon(int horizon) {
  horizon_ = horizon;
  initialized_ = false; // Trajectory sizes will change
}

void CDDP::setTimestep(double timestep) { timestep_ = timestep; }

void CDDP::setOptions(const CDDPOptions &options) {
  options_ = options;
  // Re-initialize alpha sequence if line search options changed
  alphas_.clear();
  double current_alpha = options_.line_search.initial_step_size;
  for (int i = 0; i < options_.line_search.max_iterations; ++i) {
    alphas_.push_back(current_alpha);
    current_alpha *= options_.line_search.step_reduction_factor;
    if (current_alpha < options_.line_search.min_step_size &&
        i < options_.line_search.max_iterations - 1) {
      alphas_.push_back(options_.line_search.min_step_size);
      break;
    }
  }
  if (alphas_.empty()) {
    alphas_.push_back(options_.line_search.initial_step_size);
  }
  alpha_pr_ = options_.line_search.initial_step_size;
}

void CDDP::setObjective(std::unique_ptr<Objective> objective) {
  objective_ = std::move(objective);
  if (objective_ && !reference_state_.isZero() &&
      reference_state_.size() > 0) { // Check if reference_state is valid
    objective_->setReferenceState(reference_state_);
  }
  if (objective_ && !reference_states_.empty()) {
    objective_->setReferenceStates(reference_states_);
  }
}

void CDDP::setInitialTrajectory(const std::vector<Eigen::VectorXd> &X,
                                const std::vector<Eigen::VectorXd> &U) {
  if (X.size() != static_cast<size_t>(horizon_ + 1) ||
      U.size() != static_cast<size_t>(horizon_)) {
    // Or throw error, or just warn and let initializeProblemIfNecessary handle
    // it
    std::cerr << "Warning: Provided initial trajectory dimensions do not match "
                 "horizon."
              << std::endl;
  }
  X_ = X;
  U_ = U;
  if (!X_.empty()) { // Ensure initial state is consistent
    initial_state_ = X_[0];
  }
}

// Placeholder Getters for dimensions (assuming system_ is valid)
int CDDP::getStateDim() const {
  if (!system_)
    throw std::runtime_error("Dynamical system not set.");
  return system_->getStateDim();
}
int CDDP::getControlDim() const {
  if (!system_)
    throw std::runtime_error("Dynamical system not set.");
  return system_->getControlDim();
}
int CDDP::getTotalDualDim() const { return total_dual_dim_; }

void CDDP::addPathConstraint(std::string constraint_name,
                             std::unique_ptr<Constraint> constraint) {
  if (!constraint) {
    throw std::runtime_error("Cannot add null constraint.");
  }

  // Get dual dimension BEFORE moving the constraint
  int dual_dim = constraint->getDualDim();

  path_constraint_set_[constraint_name] = std::move(constraint);

  // Increment total dual dimension
  total_dual_dim_ += dual_dim;

  initialized_ = false; // Constraint set changed, need to reinitialize
}

bool CDDP::removePathConstraint(const std::string &constraint_name) {
  auto it = path_constraint_set_.find(constraint_name);
  if (it != path_constraint_set_.end()) {
    // Decrement total dual dimension
    total_dual_dim_ -= it->second->getDualDim();

    // Remove the constraint from the set
    path_constraint_set_.erase(it);

    // Mark as needing reinitialization since constraint set changed
    initialized_ = false;

    return true; // Successfully removed
  }

  return false; // Constraint not found
}

void CDDP::addTerminalConstraint(std::string constraint_name,
                                 std::unique_ptr<Constraint> constraint) {
  if (!constraint) {
    throw std::runtime_error("Cannot add null constraint.");
  }

  // Get dual dimension BEFORE moving the constraint
  int dual_dim = constraint->getDualDim();

  terminal_constraint_set_[constraint_name] = std::move(constraint);

  // Increment total dual dimension
  total_dual_dim_ += dual_dim;

  initialized_ = false; // Constraint set changed, need to reinitialize
}

bool CDDP::removeTerminalConstraint(const std::string &constraint_name) {
  auto it = terminal_constraint_set_.find(constraint_name);
  if (it != terminal_constraint_set_.end()) {
    // Decrement total dual dimension
    total_dual_dim_ -= it->second->getDualDim();

    // Remove the constraint from the set
    terminal_constraint_set_.erase(it);

    // Mark as needing reinitialization since constraint set changed
    initialized_ = false;

    return true; // Successfully removed
  }

  return false; // Constraint not found
}

namespace {
std::string solverTypeToString(SolverType solver_type) {
  switch (solver_type) {
  case SolverType::CLDDP:
    return "CLDDP";
  case SolverType::ASDDP:
    return "ASDDP";
  case SolverType::LogDDP:
    return "LogDDP";
  case SolverType::IPDDP:
    return "IPDDP";
  case SolverType::MSIPDDP:
    return "MSIPDDP";
  case SolverType::ALDDP:
    return "ALDDP";
  default:
    return "CLDDP"; // Default fallback
  }
}
} // namespace

CDDPSolution CDDP::solve(SolverType solver_type) {
  return solve(solverTypeToString(solver_type));
}

// Virtual factory method
std::unique_ptr<ISolverAlgorithm>
CDDP::createSolver(const std::string &solver_type) {
  // First check external registry
  auto it = external_solver_registry_.find(solver_type);
  if (it != external_solver_registry_.end()) {
    return it->second(); // Call the factory function
  }

  // Fall back to built-in solvers
  if (solver_type == "CLCDDP" || solver_type == "CLDDP") {
    return std::make_unique<CLDDPSolver>();
  } else if (solver_type == "ASDDP") {
    return std::make_unique<ASDDPSolver>();
  } else if (solver_type == "LogDDP" || solver_type == "LOGDDP") {
    return std::make_unique<LogDDPSolver>();
  } else if (solver_type == "IPDDP") {
    return std::make_unique<IPDDPSolver>();
  } else if (solver_type == "MSIPDDP") {
    return std::make_unique<MSIPDDPSolver>();
  } else if (solver_type == "ALDDP") {
    return std::make_unique<AlddpSolver>();
  }

  return nullptr; // Solver not found
}

CDDPSolution CDDP::solve(const std::string &solver_type) {
  // This is where strategy selection and invocation will happen.

  initializeProblemIfNecessary(); // Ensure X_, U_ are sized etc.

  // Strategy selection and instantiation
  solver_ = createSolver(solver_type);

  if (!solver_) {
    // Solver not found - return error solution
    CDDPSolution solution;
    solution["solver_name"] = solver_type;
    solution["status_message"] =
        std::string("UnknownSolver - No solver registered for '") +
        solver_type + "'";
    solution["iterations_completed"] = 0;
    solution["solve_time_ms"] = 0.0;
    solution["final_objective"] = 0.0;
    solution["final_step_length"] = 1.0;

    // Add empty trajectories
    solution["time_points"] = std::vector<double>();
    solution["state_trajectory"] = std::vector<Eigen::VectorXd>();
    solution["control_trajectory"] = std::vector<Eigen::VectorXd>();

    if (options_.verbose) {
      std::cout << "Solver type '" << solver_type
                << "' not found. Available solvers: ";
      auto available = getRegisteredSolvers();
      for (const auto &name : available) {
        std::cout << name << " ";
      }
      std::cout << "CLDDP ASDDP LogDDP IPDDP MSIPDDP ALDDP" << std::endl;
    }

    return solution;
  }

  // Use the strategy to solve the problem
  solver_->initialize(*this);
  return solver_->solve(*this);
}

void CDDP::initializeProblemIfNecessary() {
  if (initialized_) {
    return; // Already initialized
  }

  if (!system_) {
    throw std::runtime_error("Dynamical system must be set before solving.");
  }
  if (!objective_) {
    throw std::runtime_error("Objective function must be set before solving.");
  }

  int state_dim = system_->getStateDim();
  int control_dim = system_->getControlDim();

  // For warm start: preserve existing trajectories if they have compatible
  // dimensions
  bool preserve_trajectories =
      options_.warm_start && !X_.empty() && !U_.empty() &&
      static_cast<int>(X_.size()) == horizon_ + 1 &&
      static_cast<int>(U_.size()) == horizon_ && X_[0].size() == state_dim &&
      U_[0].size() == control_dim;

  // Initialize state trajectory
  if (X_.empty() || static_cast<int>(X_.size()) != horizon_ + 1) {
    if (preserve_trajectories) {
      // Warm start: resize existing trajectory carefully
      if (options_.verbose) {
        std::cout << "CDDP: Warm start detected - preserving existing state "
                     "trajectory"
                  << std::endl;
      }
      // Keep existing X_ but ensure correct size
      X_.resize(horizon_ + 1);
    } else {
      // Cold start: initialize with zeros
      X_.clear();
      X_.resize(horizon_ + 1);
      for (int k = 0; k <= horizon_; ++k) {
        X_[k] = Eigen::VectorXd::Zero(state_dim);
      }
    }
  }

  // Ensure initial state is set correctly (always required)
  X_[0] = initial_state_;

  // Initialize control trajectory
  if (U_.empty() || static_cast<int>(U_.size()) != horizon_) {
    if (preserve_trajectories) {
      // Warm start: resize existing trajectory carefully
      if (options_.verbose) {
        std::cout << "CDDP: Warm start detected - preserving existing control "
                     "trajectory"
                  << std::endl;
      }
      // Keep existing U_ but ensure correct size
      U_.resize(horizon_);
    } else {
      // Cold start: initialize with zeros
      U_.clear();
      U_.resize(horizon_);
      for (int k = 0; k < horizon_; ++k) {
        U_[k] = Eigen::VectorXd::Zero(control_dim);
      }
    }
  }

  // Initialize cost and merit function
  cost_ = std::numeric_limits<double>::infinity();
  merit_function_ = std::numeric_limits<double>::infinity();
  inf_pr_ = std::numeric_limits<double>::infinity();
  inf_du_ = std::numeric_limits<double>::infinity();
  inf_comp_ = std::numeric_limits<double>::infinity();
  regularization_ = options_.regularization.initial_value;
  terminal_regularization_ = options_.regularization.initial_value;

  initialized_ = true;
}

void CDDP::increaseRegularization() {
  regularization_ *= options_.regularization.update_factor;

  // Clamp to maximum value
  regularization_ =
      std::min(regularization_, options_.regularization.max_value);
}

void CDDP::decreaseRegularization() {
  regularization_ /= options_.regularization.update_factor;

  // Clamp to minimum value
  regularization_ =
      std::max(regularization_, options_.regularization.min_value);
}

bool CDDP::isRegularizationLimitReached() const {
  return regularization_ >= options_.regularization.max_value;
}

void CDDP::increaseTerminalRegularization() {
  terminal_regularization_ *= options_.regularization.update_factor;

  // Clamp to maximum value
  terminal_regularization_ =
      std::min(terminal_regularization_, options_.regularization.max_value);
}

void CDDP::decreaseTerminalRegularization() {
  terminal_regularization_ /= options_.regularization.update_factor;

  // Clamp to minimum value
  terminal_regularization_ =
      std::max(terminal_regularization_, options_.regularization.min_value);
}

bool CDDP::isTerminalRegularizationLimitReached() const {
  return terminal_regularization_ >= options_.regularization.max_value;
}

bool CDDP::isKKTToleranceSatisfied() const {
  return (inf_pr_ <= options_.tolerance && inf_du_ <= options_.tolerance);
}
namespace ansi {
  inline std::string rgb(unsigned r, unsigned g, unsigned b) {
      return "\033[38;2;" + std::to_string(r) + ';' + std::to_string(g) + ';' + std::to_string(b) + 'm';
  }
  constexpr const char* reset() { return "\033[0m"; }
  constexpr const char* bold()   { return "\033[1m"; }
  constexpr const char* italic() { return "\033[3m"; }
  constexpr const char* dim()    { return "\033[2m"; }
}

/* 6-row block fonts ─ trimmed (no trailing blanks) */
static constexpr std::array<const char*, 6> C{
  " ██████╗",
  "██╔════╝",
  "██║     ",
  "██║     ",
  "╚██████╗",
  " ╚═════╝"
};
static constexpr std::array<const char*, 6> D{
  "██████╗ ",
  "██╔══██╗",
  "██║  ██║",
  "██║  ██║",
  "██████╔╝",
  "╚═════╝ "
};
static constexpr std::array<const char*, 6> P{
  "██████╗ ",
  "██╔══██╗",
  "██████╔╝",
  "██╔═══╝ ",
  "██║     ",
  "╚═╝     "
};
static constexpr std::array<const char*, 6> I{
  "██╗",
  "██║",
  "██║",
  "██║",
  "██║",
  "╚═╝"
};
static constexpr std::array<const char*, 6> N{
  "███╗   ██╗",
  "████╗  ██║",
  "██╔██╗ ██║",
  "██║╚██╗██║",
  "██║ ╚████║",
  "╚═╝  ╚═══╝"
};

void CDDP::printSolverInfo()
{
  std::cout << '\n';
  constexpr auto letterSep  = "";      // now zero-width
  constexpr auto groupSep   = " ";     // keep single-space gap between groups

  for (std::size_t row = 0; row < 6; ++row) {
      // gradient: dark-blue → gold
      std::cout << ansi::rgb( 10,  61,  98) << C[row] << letterSep;
      std::cout << ansi::rgb( 40,  80, 105) << D[row] << letterSep;
      std::cout << ansi::rgb( 70,  99, 112) << D[row] << letterSep;
      std::cout << ansi::rgb(100, 118, 119) << P[row] << groupSep;

      std::cout << ansi::rgb(130, 137, 126) << I[row] << letterSep << N[row] << groupSep;

      std::cout << ansi::rgb(160, 156, 133) << C[row] << letterSep;
      std::cout << ansi::rgb(180, 166, 128) << P[row] << letterSep;
      std::cout << ansi::rgb(196, 177, 123) << P[row] << '\n';
  }

  std::cout << ansi::reset();
  std::cout << '\n'
            << ansi::bold() << "Constrained Differential Dynamic Programming" << ansi::reset() << '\n'
            << ansi::rgb(196,177,123) << ansi::dim() << ansi::italic()
            << "Author: Tomo Sasaki (@astomodynamics)" << ansi::reset() << "\n\n";
}
// Helper function to print SolverSpecificBarrierOptions
void print_solver_specific_barrier_options(
    const SolverSpecificBarrierOptions &barrier_opts,
    const std::string &prefix = "  ") {
  std::cout << prefix << "Barrier Mu Initial: " << std::setw(10)
            << barrier_opts.mu_initial << "\n";
  std::cout << prefix << "Barrier Mu Min Value: " << std::setw(10)
            << barrier_opts.mu_min_value << "\n";
  std::cout << prefix << "Barrier Mu Update Factor: " << std::setw(10)
            << barrier_opts.mu_update_factor << "\n";
  std::cout << prefix << "Barrier Mu Update Power: " << std::setw(10)
            << barrier_opts.mu_update_power << "\n";
  std::cout << prefix << "Min Fraction to Boundary: " << std::setw(10)
            << barrier_opts.min_fraction_to_boundary << "\n";
}

// Helper function to print SolverSpecificFilterOptions
void print_solver_specific_filter_options(
    const SolverSpecificFilterOptions &filter_opts,
    const std::string &prefix = "  ") {
  std::cout << prefix << "Filter Merit Accept Thresh: " << std::setw(10)
            << filter_opts.merit_acceptance_threshold << "\n";
  std::cout << prefix << "Filter Violation Accept Thresh: " << std::setw(10)
            << filter_opts.violation_acceptance_threshold << "\n";
  std::cout << prefix << "Filter Max Violation Thresh: " << std::setw(10)
            << filter_opts.max_violation_threshold << "\n";
  std::cout << prefix << "Filter Min Violation for Armijo: " << std::setw(10)
            << filter_opts.min_violation_for_armijo_check << "\n";
  std::cout << prefix << "Filter Armijo Constant: " << std::setw(10)
            << filter_opts.armijo_constant << "\n";
}

void CDDP::printOptions(const CDDPOptions &options) {
  std::cout << "\n========================================\n";
  std::cout << "           CDDP Options Overview\n";
  std::cout << "========================================\n";

  std::cout << "--- General Solver Configuration ---\n";
  std::cout << "  KKT/Optimality Tolerance: " << std::setw(10)
            << options.tolerance << "\n";
  std::cout << "  Cost Change Tolerance: " << std::setw(10)
            << options.acceptable_tolerance << "\n";
  std::cout << "  Max Iterations: " << std::setw(10) << options.max_iterations
            << "\n";
  std::cout << "  Max CPU Time (s): " << std::setw(10) << options.max_cpu_time
            << "\n";
  std::cout << "  Verbose Output: " << std::setw(10)
            << (options.verbose ? "Yes" : "No") << "\n";
  std::cout << "  Debug Mode: " << std::setw(10)
            << (options.debug ? "Yes" : "No") << "\n";
  std::cout << "  Print Solver Header: " << std::setw(10)
            << (options.print_solver_header ? "Yes" : "No") << "\n";
  std::cout << "  Use iLQR Approximations: " << std::setw(10)
            << (options.use_ilqr ? "Yes" : "No") << "\n";
  std::cout << "  Enable Parallel Computation: " << std::setw(10)
            << (options.enable_parallel ? "Yes" : "No") << "\n";
  std::cout << "  Number of Threads: " << std::setw(10) << options.num_threads
            << "\n";
  std::cout << "  Return Iteration Info: " << std::setw(10)
            << (options.return_iteration_info ? "Yes" : "No") << "\n";

  std::cout << "\n--- Line Search Options ---\n";
  std::cout << "  Max Iterations: " << std::setw(10)
            << options.line_search.max_iterations << "\n";
  std::cout << "  Initial Step Size: " << std::setw(10)
            << options.line_search.initial_step_size << "\n";
  std::cout << "  Min Step Size: " << std::setw(10)
            << options.line_search.min_step_size << "\n";
  std::cout << "  Step Reduction Factor: " << std::setw(10)
            << options.line_search.step_reduction_factor << "\n";

  std::cout << "\n--- Regularization Options ---\n";
  std::cout << "  Initial Value: " << std::setw(10)
            << options.regularization.initial_value << "\n";
  std::cout << "  Update Factor: " << std::setw(10)
            << options.regularization.update_factor << "\n";
  std::cout << "  Max Value: " << std::setw(10)
            << options.regularization.max_value << "\n";
  std::cout << "  Min Value: " << std::setw(10)
            << options.regularization.min_value << "\n";
  std::cout << "  Step Initial Value: " << std::setw(10)
            << options.regularization.step_initial_value << "\n";

  std::cout << "\n--- BoxQP Options ---\n";
  std::cout << "  Max Iterations: " << std::setw(10)
            << options.box_qp.max_iterations << "\n";
  std::cout << "  Min Gradient Norm: " << std::setw(10)
            << options.box_qp.min_gradient_norm << "\n";
  std::cout << "  Min Relative Improvement: " << std::setw(10)
            << options.box_qp.min_relative_improvement << "\n";
  std::cout << "  Step Decrease Factor: " << std::setw(10)
            << options.box_qp.step_decrease_factor << "\n";
  std::cout << "  Min Step Size: " << std::setw(10)
            << options.box_qp.min_step_size << "\n";
  std::cout << "  Armijo Constant: " << std::setw(10)
            << options.box_qp.armijo_constant << "\n";
  std::cout << "  Verbose: " << std::setw(10)
            << (options.box_qp.verbose ? "Yes" : "No") << "\n";

  std::cout << "\n--- Log-Barrier Method Options ---\n";
  std::cout << "  Use Relaxed Log-Barrier Penalty: "
            << (options.log_barrier.use_relaxed_log_barrier_penalty ? "Yes"
                                                                    : "No")
            << "\n";
  std::cout << "  Relaxed Log-Barrier Delta: " << std::setw(10)
            << options.log_barrier.relaxed_log_barrier_delta << "\n";
  std::cout << "  Termination Scaling Max Factor: " << std::setw(10)
            << options.termination_scaling_max_factor << "\n";
  std::cout << "  Barrier Parameters (for Log-Barrier):\n";
  print_solver_specific_barrier_options(options.log_barrier.barrier, "    ");
  std::cout << "  Filter Parameters (for Log-Barrier):\n";
  print_solver_specific_filter_options(options.filter, "    ");

  std::cout << "\n--- IPDDP Algorithm Options ---\n";
  std::cout << "  Dual Variable Init Scale: " << std::setw(10)
            << options.ipddp.dual_var_init_scale << "\n";
  std::cout << "  Slack Variable Init Scale: " << std::setw(10)
            << options.ipddp.slack_var_init_scale << "\n";
  std::cout << "  Termination Scaling Max Factor: " << std::setw(10)
            << options.termination_scaling_max_factor << "\n";
  std::cout << "  Barrier Parameters (for IPDDP):\n";
  print_solver_specific_barrier_options(options.ipddp.barrier, "    ");
  std::cout << "  Filter Parameters (for IPDDP):\n";
  print_solver_specific_filter_options(options.filter, "    ");

  std::cout << "\n--- MSIPDDP Algorithm Options ---\n";
  std::cout << "  Dual Variable Init Scale: " << std::setw(10)
            << options.msipddp.dual_var_init_scale << "\n";
  std::cout << "  Slack Variable Init Scale: " << std::setw(10)
            << options.msipddp.slack_var_init_scale << "\n";
  std::cout << "  Costate Variable Init Scale: " << std::setw(10)
            << options.msipddp.costate_var_init_scale << "\n";
  std::cout << "  Segment Length: " << std::setw(10)
            << options.msipddp.segment_length << "\n";
  std::cout << "  Rollout Type: " << std::setw(10)
            << options.msipddp.rollout_type << "\n";
  std::cout << "  Use Controlled Rollout: " << std::setw(10)
            << (options.msipddp.use_controlled_rollout ? "Yes" : "No") << "\n";
  std::cout << "  Termination Scaling Max Factor: " << std::setw(10)
            << options.termination_scaling_max_factor << "\n";
  std::cout << "  Barrier Parameters (for MSIPDDP):\n";
  print_solver_specific_barrier_options(options.msipddp.barrier, "    ");
  std::cout << "  Filter Parameters (for MSIPDDP):\n";
  print_solver_specific_filter_options(options.filter, "    ");

  std::cout << "========================================\n\n";
}

// Static methods for solver registration
void CDDP::registerSolver(
    const std::string &solver_name,
    std::function<std::unique_ptr<ISolverAlgorithm>()> factory) {
  external_solver_registry_[solver_name] = factory;
}

bool CDDP::isSolverRegistered(const std::string &solver_name) {
  return external_solver_registry_.find(solver_name) !=
         external_solver_registry_.end();
}

std::vector<std::string> CDDP::getRegisteredSolvers() {
  std::vector<std::string> names;
  for (const auto &pair : external_solver_registry_) {
    names.push_back(pair.first);
  }
  return names;
}

} // namespace cddp
