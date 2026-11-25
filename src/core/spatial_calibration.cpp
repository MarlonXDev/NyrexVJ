#include "core/spatial_calibration.hpp"

#include "core/spatial_math.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace nvj {

namespace {

using spatial::V3;

constexpr size_t kMaxParameters = 7;

struct Problem {
    std::vector<CalibrationPoint> points;
    SpatialDevice base;
    bool solveFov = false;
    size_t count() const { return solveFov ? 7u : 6u; }
};

SpatialDevice deviceFrom(const Problem& problem, const std::array<double, kMaxParameters>& p) {
    SpatialDevice device = problem.base;
    device.pose.position = Vec3f{static_cast<float>(p[0]), static_cast<float>(p[1]), static_cast<float>(p[2])};
    device.pose.yaw = static_cast<float>(p[3]);
    device.pose.pitch = static_cast<float>(p[4]);
    device.pose.roll = static_cast<float>(p[5]);
    if (problem.solveFov) {
        device.lens.fovY = static_cast<float>(p[6]);
    }
    return device;
}

void residuals(const Problem& problem, const std::array<double, kMaxParameters>& p, std::vector<double>& out) {
    const double yaw = p[3];
    const double pitch = std::min(89.9, std::max(-89.9, p[4]));
    const double roll = p[5];
    const spatial::Mat3 rotation = spatial::rotationFromAngles(yaw, pitch, roll);
    const double fov = problem.solveFov ? std::min(120.0, std::max(1.0, p[6])) : static_cast<double>(problem.base.lens.fovY);
    const spatial::LensTerms terms = spatial::lensTermsWithFov(problem.base.lens, fov);
    const V3 position{p[0], p[1], p[2]};
    out.clear();
    for (const CalibrationPoint& point : problem.points) {
        const spatial::Clip c = spatial::toClip(terms, rotation, position, spatial::toV3(point.world));
        if (c.w <= 1e-6) {
            const double penalty = 5.0 + (1e-6 - c.w);
            out.push_back(penalty);
            out.push_back(penalty);
            continue;
        }
        const double x = 0.5 + 0.5 * c.x / c.w;
        const double y = 0.5 - 0.5 * c.y / c.w;
        out.push_back(x - static_cast<double>(point.outputX));
        out.push_back(y - static_cast<double>(point.outputY));
    }
}

double costOf(const std::vector<double>& r) {
    double sum = 0.0;
    for (double v : r) {
        sum += v * v;
    }
    return 0.5 * sum;
}

bool solveLinear(std::array<std::array<double, kMaxParameters + 1>, kMaxParameters>& a, size_t n, std::array<double, kMaxParameters>& x) {
    for (size_t col = 0; col < n; ++col) {
        size_t pivot = col;
        for (size_t row = col + 1; row < n; ++row) {
            if (std::fabs(a[row][col]) > std::fabs(a[pivot][col])) {
                pivot = row;
            }
        }
        if (std::fabs(a[pivot][col]) < 1e-300) {
            return false;
        }
        std::swap(a[pivot], a[col]);
        for (size_t row = col + 1; row < n; ++row) {
            const double f = a[row][col] / a[col][col];
            for (size_t k = col; k <= n; ++k) {
                a[row][k] -= f * a[col][k];
            }
        }
    }
    for (size_t i = n; i-- > 0;) {
        double sum = a[i][n];
        for (size_t k = i + 1; k < n; ++k) {
            sum -= a[i][k] * x[k];
        }
        x[i] = sum / a[i][i];
    }
    return true;
}

struct Run {
    std::array<double, kMaxParameters> p{};
    double cost = 1e300;
    int iterations = 0;
};

Run levenberg(const Problem& problem, std::array<double, kMaxParameters> start, int maxIterations) {
    Run run;
    run.p = start;
    const size_t n = problem.count();
    std::vector<double> r;
    residuals(problem, run.p, r);
    run.cost = costOf(r);
    double lambda = 1e-3;
    const double steps[kMaxParameters] = {1e-6, 1e-6, 1e-6, 1e-6, 1e-6, 1e-6, 1e-6};
    for (int iteration = 0; iteration < maxIterations; ++iteration) {
        run.iterations = iteration + 1;
        const size_t m = r.size();
        std::vector<double> jacobian(m * n, 0.0);
        std::vector<double> plus;
        std::vector<double> minus;
        for (size_t k = 0; k < n; ++k) {
            std::array<double, kMaxParameters> up = run.p;
            std::array<double, kMaxParameters> down = run.p;
            up[k] += steps[k];
            down[k] -= steps[k];
            residuals(problem, up, plus);
            residuals(problem, down, minus);
            for (size_t i = 0; i < m; ++i) {
                jacobian[i * n + k] = (plus[i] - minus[i]) / (2.0 * steps[k]);
            }
        }
        std::array<std::array<double, kMaxParameters + 1>, kMaxParameters> normal{};
        std::array<double, kMaxParameters> gradient{};
        for (size_t a = 0; a < n; ++a) {
            for (size_t b = 0; b < n; ++b) {
                double sum = 0.0;
                for (size_t i = 0; i < m; ++i) {
                    sum += jacobian[i * n + a] * jacobian[i * n + b];
                }
                normal[a][b] = sum;
            }
            double g = 0.0;
            for (size_t i = 0; i < m; ++i) {
                g += jacobian[i * n + a] * r[i];
            }
            gradient[a] = g;
        }
        bool accepted = false;
        double stepSize = 0.0;
        for (int attempt = 0; attempt < 12 && !accepted; ++attempt) {
            std::array<std::array<double, kMaxParameters + 1>, kMaxParameters> system = normal;
            for (size_t a = 0; a < n; ++a) {
                system[a][a] += lambda * std::max(normal[a][a], 1e-12);
                system[a][n] = -gradient[a];
            }
            std::array<double, kMaxParameters> delta{};
            if (!solveLinear(system, n, delta)) {
                lambda *= 10.0;
                continue;
            }
            std::array<double, kMaxParameters> candidate = run.p;
            stepSize = 0.0;
            for (size_t a = 0; a < n; ++a) {
                candidate[a] += delta[a];
                stepSize += delta[a] * delta[a];
            }
            candidate[4] = std::min(89.9, std::max(-89.9, candidate[4]));
            if (problem.solveFov) {
                candidate[6] = std::min(120.0, std::max(1.0, candidate[6]));
            }
            std::vector<double> next;
            residuals(problem, candidate, next);
            const double nextCost = costOf(next);
            if (nextCost < run.cost) {
                const double drop = run.cost - nextCost;
                run.p = candidate;
                run.cost = nextCost;
                r = next;
                lambda = std::max(lambda * 0.3, 1e-14);
                accepted = true;
                if (drop < 1e-22 || std::sqrt(stepSize) < 1e-11) {
                    return run;
                }
            } else {
                lambda *= 5.0;
            }
        }
        if (!accepted || run.cost < 1e-24) {
            break;
        }
    }
    return run;
}

bool collinear(const std::vector<CalibrationPoint>& points) {
    if (points.size() < 3) {
        return true;
    }
    const V3 origin = spatial::toV3(points[0].world);
    size_t far = 0;
    double farDistance = 0.0;
    for (size_t i = 1; i < points.size(); ++i) {
        const double d = spatial::length(spatial::toV3(points[i].world) - origin);
        if (d > farDistance) {
            farDistance = d;
            far = i;
        }
    }
    if (farDistance < 1e-6) {
        return true;
    }
    const V3 axis = spatial::toV3(points[far].world) - origin;
    double best = 0.0;
    for (size_t i = 1; i < points.size(); ++i) {
        best = std::max(best, spatial::length(spatial::cross(spatial::toV3(points[i].world) - origin, axis)));
    }
    return best < 1e-6 * farDistance * farDistance;
}

std::array<double, kMaxParameters> parametersOf(const SpatialDevice& device) {
    std::array<double, kMaxParameters> p{};
    p[0] = static_cast<double>(device.pose.position.x);
    p[1] = static_cast<double>(device.pose.position.y);
    p[2] = static_cast<double>(device.pose.position.z);
    p[3] = static_cast<double>(device.pose.yaw);
    p[4] = static_cast<double>(device.pose.pitch);
    p[5] = static_cast<double>(device.pose.roll);
    p[6] = static_cast<double>(device.lens.fovY);
    return p;
}

std::array<double, kMaxParameters> lookAtParameters(const V3& position, const V3& target, double fov) {
    const SpatialPose pose = poseLookAt(spatial::toVec3f(position), spatial::toVec3f(target), 0.0f);
    std::array<double, kMaxParameters> p{};
    p[0] = position.x;
    p[1] = position.y;
    p[2] = position.z;
    p[3] = static_cast<double>(pose.yaw);
    p[4] = static_cast<double>(pose.pitch);
    p[5] = 0.0;
    p[6] = fov;
    return p;
}

}

const char* calibrationStatusName(CalibrationStatus status) {
    switch (status) {
        case CalibrationStatus::Ok:
            return "ok";
        case CalibrationStatus::TooFewPoints:
            return "too few points";
        case CalibrationStatus::Collinear:
            return "points on one line";
        case CalibrationStatus::NotConverged:
            return "not converged";
    }
    return "unknown";
}

double calibrationError(const std::vector<CalibrationPoint>& points, const SpatialDevice& device, std::vector<float>* perPoint) {
    if (perPoint) {
        perPoint->clear();
    }
    double sum = 0.0;
    int count = 0;
    for (const CalibrationPoint& point : points) {
        if (!point.enabled) {
            continue;
        }
        const ProjectedPoint p = projectPoint(device, point.world);
        double error = 5.0;
        if (p.inFront) {
            const double dx = static_cast<double>(p.x) - static_cast<double>(point.outputX);
            const double dy = static_cast<double>(p.y) - static_cast<double>(point.outputY);
            error = std::sqrt(dx * dx + dy * dy);
        }
        if (perPoint) {
            perPoint->push_back(static_cast<float>(error));
        }
        sum += error * error;
        ++count;
    }
    return count > 0 ? std::sqrt(sum / static_cast<double>(count)) : 0.0;
}

CalibrationResult solveDevice(const std::vector<CalibrationPoint>& points, const SpatialDevice& initial, const CalibrationOptions& options) {
    CalibrationResult result;
    result.device = initial;
    normalizeDevice(result.device);
    Problem problem;
    problem.base = result.device;
    problem.solveFov = options.solveFov;
    for (const CalibrationPoint& point : points) {
        if (point.enabled) {
            problem.points.push_back(point);
        }
    }
    result.usedPoints = static_cast<int>(problem.points.size());
    const int needed = options.solveFov ? kMinCalibrationPointsWithFov : kMinCalibrationPoints;
    if (result.usedPoints < needed) {
        result.status = CalibrationStatus::TooFewPoints;
        result.message = "At least " + std::to_string(needed) + " points are needed" + (options.solveFov ? " when the field of view is solved too" : "") + ", " + std::to_string(result.usedPoints) + " are switched on";
        calibrationError(points, result.device, &result.errors);
        return result;
    }
    if (collinear(problem.points)) {
        result.status = CalibrationStatus::Collinear;
        result.message = "The points lie on one line; spread them over the surface";
        calibrationError(points, result.device, &result.errors);
        return result;
    }
    const int maxIterations = std::clamp(options.maxIterations, 10, 1000);
    Run best = levenberg(problem, parametersOf(result.device), maxIterations);
    result.starts = 1;
    result.iterations = best.iterations;
    const double toleranceCost = 0.5 * options.tolerance * options.tolerance * static_cast<double>(problem.points.size());
    if (best.cost > toleranceCost && options.multiStart) {
        V3 centre;
        for (const CalibrationPoint& point : problem.points) {
            centre = centre + spatial::toV3(point.world);
        }
        centre = centre * (1.0 / static_cast<double>(problem.points.size()));
        double radius = 1.0;
        for (const CalibrationPoint& point : problem.points) {
            radius = std::max(radius, spatial::length(spatial::toV3(point.world) - centre));
        }
        std::vector<V3> directions;
        for (int x = -1; x <= 1; ++x) {
            for (int y = -1; y <= 1; ++y) {
                for (int z = -1; z <= 1; ++z) {
                    if (x != 0 || y != 0 || z != 0) {
                        directions.push_back(spatial::normalized(V3{static_cast<double>(x), static_cast<double>(y), static_cast<double>(z)}));
                    }
                }
            }
        }
        const double distances[3] = {2.0, 4.0, 8.0};
        for (const V3& direction : directions) {
            for (double factor : distances) {
                const V3 position = centre + direction * (radius * factor);
                Run run = levenberg(problem, lookAtParameters(position, centre, static_cast<double>(result.device.lens.fovY)), 60);
                ++result.starts;
                if (run.cost < best.cost) {
                    best = run;
                }
                if (best.cost <= toleranceCost) {
                    break;
                }
            }
            if (best.cost <= toleranceCost) {
                break;
            }
        }
        best = levenberg(problem, best.p, maxIterations);
        result.iterations += best.iterations;
    }
    SpatialDevice solved = deviceFrom(problem, best.p);
    normalizeDevice(solved);
    result.device = solved;
    result.rms = calibrationError(points, solved, &result.errors);
    result.rmsPixels = result.rms * kCalibrationPixelWidth;
    for (float e : result.errors) {
        result.maxError = std::max(result.maxError, static_cast<double>(e));
    }
    if (result.rms <= options.tolerance) {
        result.status = CalibrationStatus::Ok;
        result.message = "Solved: the average error is " + std::to_string(result.rmsPixels) + " pixels at 1920 wide";
    } else {
        result.status = CalibrationStatus::NotConverged;
        result.message = "No pose fits these points (average error " + std::to_string(result.rmsPixels) + " pixels at 1920 wide); check that the points match the surface positions";
    }
    return result;
}

}
