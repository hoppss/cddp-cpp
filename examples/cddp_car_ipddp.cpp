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

#include <iostream>
#include <vector>
#include <filesystem>
#include <cmath>
#include <map>
#include <string>
#include "cddp.hpp"
#include "matplot/matplot.h"

using namespace matplot;
namespace fs = std::filesystem;

namespace cddp
{

    class CarParkingObjective : public NonlinearObjective
    {
    public:
        CarParkingObjective(const Eigen::VectorXd &goal_state, double timestep)
            : NonlinearObjective(timestep), reference_state_(goal_state)
        {
            // Control cost coefficients: cu = 1e-2*[1 .01]
            cu_ = Eigen::Vector2d(1e-2, 1e-4);

            // Final cost coefficients: cf = [.1 .1 1 .3]
            cf_ = Eigen::Vector4d(0.1, 0.1, 1.0, 0.3);

            // Smoothness scales for final cost: pf = [.01 .01 .01 1]
            pf_ = Eigen::Vector4d(0.01, 0.01, 0.01, 1.0);

            // Running cost coefficients: cx = 1e-3*[1 1]
            cx_ = Eigen::Vector2d(1e-3, 1e-3);

            // Smoothness scales for running cost: px = [.1 .1]
            px_ = Eigen::Vector2d(0.1, 0.1);
        }

        double running_cost(const Eigen::VectorXd &state,
                            const Eigen::VectorXd &control,
                            int index) const override
        {
            // Control cost: lu = cu*u.^2
            double lu = cu_.dot(control.array().square().matrix());

            // Running cost on distance from origin: lx = cx*sabs(x(1:2,:),px)
            Eigen::VectorXd xy_state = state.head(2);
            double lx = cx_.dot(sabs(xy_state, px_));

            return lu + lx;
        }

        double terminal_cost(const Eigen::VectorXd &final_state) const override
        {
            // Final state cost: llf = cf*sabs(x(:,final),pf);
            return cf_.dot(sabs(final_state, pf_)) + running_cost(final_state, Eigen::VectorXd::Zero(2), 0);
        }

    private:
        // Helper function for smooth absolute value (pseudo-Huber)
        Eigen::VectorXd sabs(const Eigen::VectorXd &x, const Eigen::VectorXd &p) const
        {
            return ((x.array().square() / p.array().square() + 1.0).sqrt() * p.array() - p.array()).matrix();
        }

        Eigen::VectorXd reference_state_;
        Eigen::Vector2d cu_; // Control cost coefficients
        Eigen::Vector4d cf_; // Final cost coefficients
        Eigen::Vector4d pf_; // Smoothness scales for final cost
        Eigen::Vector2d cx_; // Running cost coefficients
        Eigen::Vector2d px_; // Smoothness scales for running cost
    };
} // namespace cddp

void plotCarBox(const Eigen::VectorXd &state, const Eigen::VectorXd &control,
                double length, double width, const std::string &color,
                axes_handle ax)
{
    double x = state(0);
    double y = state(1);
    double theta = state(2);
    double steering = control(1);

    // Compute the car's four corners (and close the polygon)
    std::vector<double> car_x(5), car_y(5);

    // Front right
    car_x[0] = x + length / 2 * cos(theta) - width / 2 * sin(theta);
    car_y[0] = y + length / 2 * sin(theta) + width / 2 * cos(theta);

    // Front left
    car_x[1] = x + length / 2 * cos(theta) + width / 2 * sin(theta);
    car_y[1] = y + length / 2 * sin(theta) - width / 2 * cos(theta);

    // Rear left
    car_x[2] = x - length / 2 * cos(theta) + width / 2 * sin(theta);
    car_y[2] = y - length / 2 * sin(theta) - width / 2 * cos(theta);

    // Rear right
    car_x[3] = x - length / 2 * cos(theta) - width / 2 * sin(theta);
    car_y[3] = y - length / 2 * sin(theta) + width / 2 * cos(theta);

    // Close polygon
    car_x[4] = car_x[0];
    car_y[4] = car_y[0];

    // Plot car body as a polygon line.
    plot(ax, car_x, car_y, color + "-");

    // Plot base point (center of rear axle) as a red circle.
    plot(ax, std::vector<double>{x}, std::vector<double>{y}, "ro");

    // Compute steering direction
    double front_x = x + length / 2 * cos(theta);
    double front_y = y + length / 2 * sin(theta);
    double steering_length = width / 2;
    double steering_angle = theta + steering;
    double steering_end_x = front_x + steering_length * cos(steering_angle);
    double steering_end_y = front_y + steering_length * sin(steering_angle);

    std::vector<double> steer_x = {front_x, steering_end_x};
    std::vector<double> steer_y = {front_y, steering_end_y};
    plot(ax, steer_x, steer_y, "g-");
}


int main() {
    // Problem parameters
    const int state_dim = 4;     // [x, y, theta, v]
    const int control_dim = 2;   // [wheel_angle, acceleration]
    const int horizon = 500;
    const double timestep = 0.03;
    const std::string integration_type = "euler";

    // Create a Car instance with given parameters
    double wheelbase = 2.0;
    std::unique_ptr<cddp::DynamicalSystem> system =
        std::make_unique<cddp::Car>(timestep, wheelbase, integration_type);

    // Define initial and goal states
    Eigen::VectorXd initial_state(state_dim);
    initial_state << 1.0, 1.0, 1.5 * M_PI, 0.0;
    Eigen::VectorXd goal_state(state_dim);
    goal_state << 0.0, 0.0, 0.0, 0.0;  // Desired parking state

    // Create the nonlinear objective for car parking
    auto objective = std::make_unique<cddp::CarParkingObjective>(goal_state, timestep);

    // Set solver options
    cddp::CDDPOptions options;
    options.max_iterations = 600;
    options.verbose = true;  
    options.tolerance = 1e-7;
    options.acceptable_tolerance = 1e-4;
    options.regularization.initial_value = 1e-7;
    options.debug = false;
    options.use_ilqr = true;
    options.enable_parallel = false;
    options.num_threads = 1;
    options.msipddp.barrier.mu_initial = 1e-0;
    options.msipddp.dual_var_init_scale = 1e-1;
    options.msipddp.slack_var_init_scale = 1e-2;
    options.msipddp.segment_length = horizon / 100;
    options.msipddp.rollout_type = "nonlinear";

    // Create CDDP solver for the car model with new API
    cddp::CDDP cddp_solver(initial_state, goal_state, horizon, timestep,
                          std::move(system), std::move(objective), options);

    // Define control constraints
    Eigen::VectorXd control_lower_bound(control_dim);
    control_lower_bound << -0.5, -2.0;  // [steering_angle, acceleration]
    Eigen::VectorXd control_upper_bound(control_dim);
    control_upper_bound << 0.5, 2.0;
    cddp_solver.addPathConstraint("ControlConstraint", std::make_unique<cddp::ControlConstraint>(control_upper_bound));

    // Initialize the trajectory with zero controls
    std::vector<Eigen::VectorXd> X(horizon + 1, Eigen::VectorXd::Zero(state_dim));
    std::vector<Eigen::VectorXd> U(horizon, Eigen::VectorXd::Zero(control_dim));
    X[0] = initial_state;
    for (int i = 0; i < horizon; ++i) {
        U[i](0) = 0.01;
        U[i](1) = 0.01;
        X[i + 1] = cddp_solver.getSystem().getDiscreteDynamics(X[i], U[i], i * timestep);
    }
    cddp_solver.setInitialTrajectory(X, U);

    // Solve the problem using MSIPDDP
    cddp::CDDPSolution solution = cddp_solver.solve(cddp::SolverType::MSIPDDP);

    // Extract solution trajectories
    auto X_sol = std::any_cast<std::vector<Eigen::VectorXd>>(solution.at("state_trajectory"));
    auto U_sol = std::any_cast<std::vector<Eigen::VectorXd>>(solution.at("control_trajectory"));
    auto t_sol = std::any_cast<std::vector<double>>(solution.at("time_points"));

    // Prepare trajectory data for plotting
    std::vector<double> x_hist, y_hist;
    for (const auto& state : X_sol) {
        x_hist.push_back(state(0));
        y_hist.push_back(state(1));
    }
    // Car dimensions.
    double car_length = 2.1;
    double car_width = 0.9;

    // Create a figure and get current axes.
    auto fig = figure(true);
    auto ax = fig->current_axes();

    Eigen::VectorXd empty_control = Eigen::VectorXd::Zero(2);

    // Create directory for saving plots
    const std::string plotDirectory = "../results/tests";
    if (!fs::exists(plotDirectory))
    {
        fs::create_directory(plotDirectory);
    }

    // Create a directory for frame images.
    (void) std::system("mkdir -p frames");

    // Animation loop: update plot for each time step and save frame.
    for (size_t i = 0; i < X_sol.size(); ++i)
    {
        // Skip every 10th frame for smoother animation.
        if (i % 10 == 0)
        {
            // Clear previous content.
            cla(ax);
            hold(ax, true);

            // Plot the full trajectory.
            plot(ax, x_hist, y_hist, "b-");

            // Plot goal configuration.
            plotCarBox(goal_state, empty_control, car_length, car_width, "r", ax);

            // Plot current car state.
            if (i < U_sol.size())
                plotCarBox(X_sol[i], U_sol[i], car_length, car_width, "k", ax);
            else
                plotCarBox(X_sol[i], empty_control, car_length, car_width, "k", ax);

            // Set grid and axis limits.
            grid(ax, true);
            xlim(ax, {-4, 4});
            ylim(ax, {-4, 4});

            // Update drawing.
            fig->draw();

            // Save the frame to a PNG file.
            std::string frame_filename = plotDirectory + "/frame_" + std::to_string(i) + ".png";
            fig->save(frame_filename);
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
        }
    }

    // Combine all saved frames into a GIF using ImageMagick's convert tool.
    std::string command = "convert -delay 15 " + plotDirectory + "/frame_*.png " + plotDirectory + "/car_parking_ipddp.gif";
    std::system(command.c_str());

    std::string cleanup_command = "rm " + plotDirectory + "/frame_*.png";
    std::system(cleanup_command.c_str());

    std::cout << "Animation saved as car_parking_ipddp.gif" << std::endl;

    return 0;
}
