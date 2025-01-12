package(default_visibility = [
    "//software/ai:__subpackages__",
    "//software/simulated_tests:__subpackages__",
])

cc_library(
    name = "enlsvg_path_planner",
    srcs = ["enlsvg_path_planner.cpp"],
    hdrs = ["enlsvg_path_planner.h"],
    deps = [
        "//extlibs/enlsvg",
        "//software/ai/navigator/obstacle",
        "//software/ai/navigator/obstacle:robot_navigation_obstacle_factory",
        "//software/geom:linear_spline2d",
        "//software/logger",
    ],
)

cc_library(
    name = "global_path_planner_factory",
    srcs = ["global_path_planner_factory.cpp"],
    hdrs = ["global_path_planner_factory.h"],
    deps = [
        ":enlsvg_path_planner",
        "//software/ai/hl/stp/tactic",
        "//software/ai/motion_constraint:motion_constraint_set_builder",
        "//software/geom:linear_spline2d",
        "//software/geom:point",
    ],
)

cc_library(
    name = "kinematic_constraints",
    hdrs = ["kinematic_constraints.h"],
)

cc_library(
    name = "trajectory",
    hdrs = ["trajectory.hpp"],
)

cc_library(
    name = "trajectory_2d",
    hdrs = ["trajectory_2d.h"],
    deps = [
        ":trajectory",
        "//software/geom:rectangle",
    ],
)

cc_library(
    name = "bang_bang_trajectory_1d",
    srcs = ["bang_bang_trajectory_1d.cpp"],
    hdrs = ["bang_bang_trajectory_1d.h"],
    deps = [
        ":trajectory",
        "//software/geom/algorithms:is_in_range",
        "//software/logger",
    ],
)

cc_library(
    name = "bang_bang_trajectory_1d_angular",
    srcs = ["bang_bang_trajectory_1d_angular.cpp"],
    hdrs = ["bang_bang_trajectory_1d_angular.h"],
    deps = [
        ":bang_bang_trajectory_1d",
        "//software/geom:angle",
        "//software/geom:angular_acceleration",
        "//software/geom:angular_velocity",
    ],
)

cc_library(
    name = "bang_bang_trajectory_2d",
    srcs = ["bang_bang_trajectory_2d.cpp"],
    hdrs = ["bang_bang_trajectory_2d.h"],
    deps = [
        ":bang_bang_trajectory_1d",
        ":trajectory_2d",
        "//software/geom:point",
        "//software/geom:rectangle",
    ],
)

cc_library(
    name = "trajectory_path_node",
    hdrs = ["trajectory_path_node.h"],
    deps = [
        ":bang_bang_trajectory_2d",
        "//software/geom:point",
    ],
)

cc_library(
    name = "trajectory_path",
    srcs = ["trajectory_path.cpp"],
    hdrs = ["trajectory_path.h"],
    deps = [
        ":trajectory_path_node",
        "//software/ai/navigator/path_planner:kinematic_constraints",
        "//software/logger",
    ],
)

cc_test(
    name = "bang_bang_trajectory_1d_test",
    srcs = ["bang_bang_trajectory_1d_test.cpp"],
    deps = [
        ":bang_bang_trajectory_1d",
        "//shared/test_util:tbots_gtest_main",
        "//software/test_util",
    ],
)

cc_test(
    name = "bang_bang_trajectory_2d_test",
    srcs = ["bang_bang_trajectory_2d_test.cpp"],
    deps = [
        ":bang_bang_trajectory_2d",
        "//shared/test_util:tbots_gtest_main",
        "//software/test_util",
    ],
)

cc_test(
    name = "enlsvg_path_planner_test",
    srcs = ["enlsvg_path_planner_test.cpp"],
    deps = [
        ":enlsvg_path_planner",
        "//shared/test_util:tbots_gtest_main",
        "//software/ai/navigator/obstacle:robot_navigation_obstacle_factory",
        "//software/test_util",
    ],
)

cc_test(
    name = "global_path_planner_factory_test",
    srcs = ["global_path_planner_factory_test.cpp"],
    deps = [
        ":global_path_planner_factory",
        "//shared/test_util:tbots_gtest_main",
        "//software/test_util",
        "//software/world",
    ],
)
