#include "software/simulated_tests/simulated_er_force_sim_test_fixture.h"

#include <cstdlib>
#include <experimental/filesystem>

#include "proto/message_translation/ssl_wrapper.h"
#include "shared/2021_robot_constants.h"
#include "software/logger/logger.h"
#include "software/test_util/test_util.h"
#include "shared/test_util/test_util.h"
#include "proto/message_translation/er_force_world.h"

SimulatedErForceSimTestFixture::SimulatedErForceSimTestFixture()
    : mutable_thunderbots_config(std::make_shared<ThunderbotsConfig>()),
      thunderbots_config(
          std::const_pointer_cast<const ThunderbotsConfig>(mutable_thunderbots_config)),
      sensor_fusion(thunderbots_config->getSensorFusionConfig()),
      should_log_replay(false),
      run_simulation_in_realtime(false)
{
}

void SimulatedErForceSimTestFixture::SetUp()
{
    LoggerSingleton::initializeLogger(TbotsGtestMain::logging_dir);

    mutable_thunderbots_config->getMutableAiControlConfig()->getMutableRunAi()->setValue(
        !TbotsGtestMain::stop_ai_on_start);

    // The simulated test abstracts and maintains the invariant that the friendly team
    // is always the yellow team
    mutable_thunderbots_config->getMutableSensorFusionConfig()
        ->getMutableOverrideGameControllerDefendingSide()
        ->setValue(true);
    mutable_thunderbots_config->getMutableSensorFusionConfig()
        ->getMutableDefendingPositiveSide()
        ->setValue(false);

    // Experimentally determined restitution value
    mutable_thunderbots_config->getMutableSimulatorConfig()
        ->getMutableBallRestitution()
        ->setValue(0.8);
    // Measured these values from fig. 9 on page 8 of
    // https://ssl.robocup.org/wp-content/uploads/2020/03/2020_ETDP_ZJUNlict.pdf
    mutable_thunderbots_config->getMutableSimulatorConfig()
        ->getMutableSlidingFrictionAcceleration()
        ->setValue(6.9);
    mutable_thunderbots_config->getMutableSimulatorConfig()
        ->getMutableRollingFrictionAcceleration()
        ->setValue(0.5);

    // The simulated test abstracts and maintains the invariant that the friendly team
    // is always defending the "negative" side of the field. This is so that the
    // coordinates given when setting up tests is from the perspective of the friendly
    // team
    mutable_thunderbots_config->getMutableSensorFusionConfig()
        ->getMutableFriendlyColorYellow()
        ->setValue(true);
    if (TbotsGtestMain::enable_visualizer)
    {
        enableVisualizer();
    }
    setupReplayLogging();

    // Reset tick duration trackers
    total_tick_duration = 0.0;
    // all tick times should be greater than 0
    max_tick_duration = 0.0;
    // all tick times should be less than the max value of a double
    min_tick_duration = std::numeric_limits<double>::max();
    tick_count        = 0;
}

void SimulatedErForceSimTestFixture::enableVisualizer()
{
    full_system_gui = std::make_shared<ThreadedFullSystemGUI>(mutable_thunderbots_config);
    run_simulation_in_realtime = true;
}

void SimulatedErForceSimTestFixture::setupReplayLogging()
{
    // get the name of the current test to name the replay output directory
    auto test_name = ::testing::UnitTest::GetInstance()->current_test_info()->name();

    namespace fs = std::experimental::filesystem;
    static constexpr auto SIMULATED_TEST_OUTPUT_DIR_SUFFIX = "simulated_test_outputs";

    const char *test_outputs_dir_or_null = std::getenv("TEST_UNDECLARED_OUTPUTS_DIR");
    if (!test_outputs_dir_or_null)
    {
        // we're not running with the Bazel test env vars set, don't set up replay logging
        return;
    }

    fs::path bazel_test_outputs_dir(test_outputs_dir_or_null);
    fs::path out_dir =
        bazel_test_outputs_dir / SIMULATED_TEST_OUTPUT_DIR_SUFFIX / test_name;
    fs::create_directories(out_dir);

    LOG(INFO) << "Logging " << test_name << " replay to " << out_dir;

    fs::path sensorproto_out_dir = out_dir / "ErForceSimulator_SensorProto";
    fs::path ssl_wrapper_out_dir = out_dir / "SensorFusion_SSL_WrapperPacket";

    simulator_sensorproto_logger =
        std::make_shared<ProtoLogger<SensorProto>>(sensorproto_out_dir);
    sensorfusion_wrapper_logger =
        std::make_shared<ProtoLogger<SSLProto::SSL_WrapperPacket>>(ssl_wrapper_out_dir);
    should_log_replay = true;
}

bool SimulatedErForceSimTestFixture::validateAndCheckCompletion(
    std::vector<TerminatingFunctionValidator> &terminating_function_validators,
    std::vector<NonTerminatingFunctionValidator> &non_terminating_function_validators)
{
    for (auto &function_validator : non_terminating_function_validators)
    {
        auto error_message = function_validator.executeAndCheckForFailures();
        if (error_message)
        {
            ADD_FAILURE() << error_message.value();
        }
    }

    bool validation_successful = std::all_of(
        terminating_function_validators.begin(), terminating_function_validators.end(),
        [](TerminatingFunctionValidator &fv) { return fv.executeAndCheckForSuccess(); });

    return terminating_function_validators.empty() ? false : validation_successful;
}

void SimulatedErForceSimTestFixture::updateSensorFusion(
    std::shared_ptr<ErForceSimulator> simulator)
{
    auto ssl_wrapper_packets = simulator->getSSLWrapperPackets();

    for (const auto &packet : ssl_wrapper_packets)
    {
        auto sensor_msg                        = SensorProto();
        *(sensor_msg.mutable_ssl_vision_msg()) = packet;

        sensor_fusion.processSensorProto(sensor_msg);
        if (should_log_replay)
        {
            simulator_sensorproto_logger->onValueReceived(sensor_msg);
            auto world_or_null = sensor_fusion.getWorld();

            if (world_or_null)
            {
                auto filtered_ssl_wrapper = *createSSLWrapperPacket(
                    *sensor_fusion.getWorld(), TeamColour::YELLOW);
                sensorfusion_wrapper_logger->onValueReceived(filtered_ssl_wrapper);
            }
        }
    }
}

void SimulatedErForceSimTestFixture::sleep(
    const std::chrono::steady_clock::time_point &wall_start_time,
    const Duration &desired_wall_tick_time)
{
    auto wall_time_now = std::chrono::steady_clock::now();
    auto current_tick_wall_time_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(wall_time_now -
                                                              wall_start_time);
    auto ms_to_sleep = std::chrono::milliseconds(
                           static_cast<int>(desired_wall_tick_time.toMilliseconds())) -
                       current_tick_wall_time_duration;
    if (ms_to_sleep > std::chrono::milliseconds(0))
    {
        std::this_thread::sleep_for(ms_to_sleep);
    }
}

void SimulatedErForceSimTestFixture::runTest(
    const Field &field, const BallState &ball,
    const std::vector<RobotStateWithId> &friendly_robots,
    const std::vector<RobotStateWithId> &enemy_robots,
    const std::vector<ValidationFunction> &terminating_validation_functions,
    const std::vector<ValidationFunction> &non_terminating_validation_functions,
    const Duration &timeout)
{
    std::shared_ptr<ErForceSimulator> simulator(std::make_shared<ErForceSimulator>(
        FieldType::DIV_A, create2015RobotConstants(), create2015WheelConstants(),
        thunderbots_config->getSimulatorConfig()));
    simulator->setBallState(ball);
    simulator->addYellowRobots(friendly_robots);
    simulator->addBlueRobots(enemy_robots);

    updateSensorFusion(simulator);
    std::shared_ptr<World> world;
    if (auto world_opt = sensor_fusion.getWorld())
    {
        world = std::make_shared<World>(world_opt.value());
    }
    else
    {
        FAIL() << "Invalid initial world state";
    }

    for (const auto &validation_function : terminating_validation_functions)
    {
        terminating_function_validators.emplace_back(
            TerminatingFunctionValidator(validation_function, world));
    }

    for (const auto &validation_function : non_terminating_validation_functions)
    {
        non_terminating_function_validators.emplace_back(
            NonTerminatingFunctionValidator(validation_function, world));
    }

    const Timestamp timeout_time = simulator->getTimestamp() + timeout;
    const Duration simulation_time_step =
        Duration::fromSeconds(1.0 / SIMULATED_CAMERA_FPS);


    double speed_factor         = 1 / (TbotsGtestMain::test_speed);
    const Duration ai_time_step = Duration::fromSeconds(
        simulation_time_step.toSeconds() * CAMERA_FRAMES_PER_AI_TICK * speed_factor);

    // declare difference (velocity, position) variables 
    double ball_displacement;
    double ball_velocity_diff;
    double sum_ball_displacement;
    double sum_ball_velocity;
    std::vector<double> robots_displacement;
    std::vector<double> robots_velocity_diff;
    std::vector<double> sum_robots_displacement;
    std::vector<double> sum_robots_velocity;

    // declare struct for ball_displacement, velocity_diff; array of struct for robots fields
    struct AggregateFunctions ball_displacement_stats;
    struct AggregateFunctions ball_velocity_stats;
    std::vector<AggregateFunctions> robots_displacement_stats;
    std::vector<AggregateFunctions> robots_velocity_stats;

    // Tick one frame to aid with visualization
    bool validation_functions_done =
        tickTest(simulation_time_step, ai_time_step, world, simulator, ball_displacement, ball_velocity_diff, robots_displacement, robots_velocity_diff);

    // Initialize Values 
    ball_displacement_stats.maximum = ball_displacement;
    ball_displacement_stats.minimum = ball_displacement;
    ball_velocity_stats.maximum = ball_velocity_diff;
    ball_velocity_stats.minimum = ball_velocity_diff;
    sum_ball_displacement = ball_displacement;
    sum_ball_velocity = ball_velocity_diff;
    sum_robots_displacement = robots_displacement;
    sum_robots_velocity = robots_velocity_diff;

    int num_robots  = robots_displacement.size()
    while (simulator->getTimestamp() < timeout_time && !validation_functions_done)
    {
        robots_displacement.clear();
        robots_velocity_diff.clear();
        if (!thunderbots_config->getAiControlConfig()->getRunAi()->value())
        {
            auto ms_to_sleep = std::chrono::milliseconds(
                static_cast<int>(ai_time_step.toMilliseconds()));
            std::this_thread::sleep_for(ms_to_sleep);
            continue;
        }

        validation_functions_done =
            tickTest(simulation_time_step, ai_time_step, world, simulator, ball_displacement, ball_velocity_diff, robots_displacement, robots_velocity_diff);
        
        sum_ball_displacement += ball_displacement;
        sum_ball_velocity += ball_velocity_diff;

        ball_displacement_stats.maximum = std::max(ball_displacement, ball_displacement_stats.maximum);
        ball_displacement_stats.minimum = std::min(ball_displacement, ball_displacement_stats.minimum);
        ball_velocity_stats.maximum = std::max(ball_velocity_diff, ball_velocity_stats.maximum);
        ball_velocity_stats.minimum = std::min(ball_velocity_diff, ball_velocity_stats.minimum);

        for(int i = 0; i < num_robots; i++){
            sum_robots_displacement[i] += robots_displacement[i];
            sum_robots_velocity[i] += robots_velocity_diff[i]

            robots_displacement_stats[i].maximum = std::max(robots_displacement[i], robots_displacement_stats[i].maximum);
            robots_displacement_stats[i].minimum = std::min(robots_displacement[i], robots_displacement_stats[i].minimum);
            robots_velocity_stats[i].maximum = std::max(robots_displacement[i], robots_displacement_stats[i].maximum);
            robots_velocity_stats[i].minimum = std::min(robots_velocity[i], robots_belocity_stats[i].minimum); 
        }
    }

    // compute the averages
    ball_displacement_stats.average = sum_ball_displacement / tick_count;
    ball_velocity_stats.average = sum_ball_velocity / tick_count;
    for(int i = 0; i < num_robots; i++){
        robots_displacement_stats[i].average = sum_robots_displacement[i] / tick_count;
        robots_velocity_stats[i].average = sum_robots_velocity[i] / tick_count;
    }

    // Output the tick duration results
    double avg_tick_duration = total_tick_duration / tick_count;
    LOG(INFO) << "max tick duration: " << max_tick_duration << "ms" << std::endl;
    LOG(INFO) << "min tick duration: " << min_tick_duration << "ms" << std::endl;
    LOG(INFO) << "avg tick duration: " << avg_tick_duration << "ms" << std::endl;
    
    // Output the statistics for ball and robots
    LOG(INFO) << "max ball displacement: " << ball_displacement_stats.maximum << std::endl;
    LOG(INFO) << "min ball displacement: " << ball_displacement_stats.minimum << std::endl;
    LOG(INFO) << "avg ball displacement: " << ball_displacement_stats.average << std::endl;
    LOG(INFO) << "max ball velocity difference: " << ball_velocity_stats.maximum << std::endl;
    LOG(INFO) << "min ball velocity difference: " << ball_velocity_stats.minimum << std::endl;
    LOG(INFO) << "avg ball velocity difference: " << ball_velocity_stats.average << std::endl;

    for(int i = 0; i < num_robots; i++){
        LOG(INFO) << "Robot " << i << std::endl;
        LOG(INFO) << "max robot displacement: " << ball_displacement_stats.maximum << std::endl;
        LOG(INFO) << "min robot displacement: " << ball_displacement_stats.minimum << std::endl;
        LOG(INFO) << "avg robot displacement: " << ball_displacement_stats.average << std::endl;
        LOG(INFO) << "max robot velocity difference: " << ball_velocity_stats.maximum << std::endl;
        LOG(INFO) << "min robot velocity difference: " << ball_velocity_stats.minimum << std::endl;
        LOG(INFO) << "avg robot velocity difference: " << ball_velocity_stats.average << std::endl;
    }

    if (!validation_functions_done && !terminating_validation_functions.empty())
    {
        std::string failure_message =
            "Not all validation functions passed within the timeout duration:\n";
        for (const auto &fun : terminating_function_validators)
        {
            if (fun.currentErrorMessage() != "")
            {
                failure_message += fun.currentErrorMessage() + std::string("\n");
            }
        }
        ADD_FAILURE() << failure_message;
    }
}

void SimulatedErForceSimTestFixture::registerTickTime(double tick_time_ms)
{
    total_tick_duration += tick_time_ms;
    max_tick_duration = std::max(max_tick_duration, tick_time_ms);
    min_tick_duration = std::min(min_tick_duration, tick_time_ms);
    tick_count++;
}

bool SimulatedErForceSimTestFixture::tickTest(Duration simulation_time_step,
                                              Duration ai_time_step,
                                              std::shared_ptr<World> world,
                                              std::shared_ptr<ErForceSimulator> simulator
                                              double &ball_displacement,
                                              double &ball_velocity_diff,
                                              std::vector<double> &robots_displacement,
                                              std::vector<double> &robots_velocity_diff)
{
    /* extract world ball and robot */
    Ball world_ball = world.ball();
    Team world_friendly_team = world.friendly_team();
    Team world_enemy_team = world.enemy_team();
    std::vector<Robot> world_friendly_robots = world_friendly_team.getAllRobots();
    std::vector<Robot>  world_enemy_robots = world_enemy_team.getAllRobots();
 
    /* extract simulator ball and robot */
    world::SimulatorState simulator_state = simulator.getSimulatorState();
    auto simulator_sim_ball = simulator_state.ball();
    auto simulator_friendly_sim_robots = simulator_state.blue_robots();
    auto simulator_enemy_sim_robots = simulator_state.yellow_robots();

    /* convert simball and simrobot to normal ball and robot in world for comparison */
    Ball simulator_ball = createBall(simulator_sim_ball, TimeStamp::fromSeconds(0));
    std::vector<Robot> simulator_friendly_robots;
    std::vector<Robot> simulator_enemy_robots;
    for(int i = 0; i < simulator_friendly_sim_robots.size(); i++){
        simulator_friendly_robots.push_back(createRobot(simulator_friendly_sim_robot[i], TimeStamp::fromSeconds(0)));
    }
    for(int i = 0; i < simulator_enemy_sim_robots.size(); i++){
        simulator_enemy_robots.push_back(createRobot(simulator_enemy__sim_robot[i], TimeStamp::fromSeconds(0)));
    }

    /* compare ball position */
    Point world_ball_pos = world_ball.position();
    Point simulator_ball_pos = simulator_ball.position();
    ball_displacement = (world_ball_pos - simulator_ball_pos).distanceFromOrigin();

    /* compare ball velocity */
    Vector world_ball_vel = world_ball.velocity();
    Vector simulator_ball_vel = simulator_ball.velocity();
    ball_velocity_diff = (world_ball_vel - simulator_ball_vel).length();

    /* compare world and simulator friendly robots */
    int robot_index = 0;
    for(Robot world_robot: world_friendly_robots){

        Robot simulator_robot;
        if(world_robot.id() == simulator_friendly_robots[robot_index].id())
            simulator_robot = simulator_friendly_robots[robot_index]

        /* find difference in robot[i] position */
        Point world_robot_pos = world_robot.position();
        Point simulator_robot_pos = simulator_robot.position();
        double robot_displacement = (world_robot_pos - simulator_robot_pos).distanceFromOrigin();

        /* find difference in robot[i] velocity */
        Vector world_robot_vel = world_robot.velocity();
        Vector simulator_robot_vel = simulator_robot.velocity();
        double robot_velocity_diff = (world_robot_vel - simulator_robot_vel).length();

        robots_displacement.push_back(robot_displacement);
        robots_velocity_diff.push_back(robot_velocity_diff);

    }
    
    /* compare world and simulator enemy robots */
    robot_index = 0;
    for(Robot world_robot: world_enemy_robots){

        Robot simulator_robot;
        if(world_robot.id() == simulator_enemy_robots[robot_index].id())
            simulator_robot = simulator_enemy_robots[robot_index]

        /* find difference in robot[i] position */
        Point world_robot_pos = world_robot.position();
        Point simulator_robot_pos = simulator_robot.position();
        double robot_displacement = (world_robot_pos - simulator_robot_pos).distanceFromOrigin();

        /* find difference in robot[i] velocity */
        Vector world_robot_vel = world_robot.velocity();
        Vector simulator_robot_vel = simulator_robot.velocity();
        double robot_velocity_diff = (world_robot_vel - simulator_robot_vel).length();

        robots_displacement.push_back(robot_displacement);
        robots_velocity_diff.push_back(robot_velocity_diff);
    }

    auto wall_start_time           = std::chrono::steady_clock::now();
    bool validation_functions_done = false;
    for (size_t i = 0; i < CAMERA_FRAMES_PER_AI_TICK; i++)
    {
        simulator->stepSimulation(simulation_time_step);
        updateSensorFusion(simulator);
    }

    if (auto world_opt = sensor_fusion.getWorld())
    {
        *world                    = world_opt.value();
        validation_functions_done = validateAndCheckCompletion(
            terminating_function_validators, non_terminating_function_validators);
        if (validation_functions_done)
        {
            return validation_functions_done;
        }

        updatePrimitives(*world_opt, simulator);

        if (run_simulation_in_realtime)
        {
            sleep(wall_start_time, ai_time_step);
        }

        if (full_system_gui)
        {
            full_system_gui->onValueReceived(*world);
            if (auto play_info_msg = getPlayInfo())
            {
                full_system_gui->onValueReceived(*play_info_msg);
            }
            full_system_gui->onValueReceived(getDrawFunctions());
        }
    }
    else
    {
        LOG(WARNING) << "SensorFusion did not output a valid World";
    }
    return validation_functions_done;
}
