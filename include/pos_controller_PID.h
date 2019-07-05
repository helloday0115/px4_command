/***************************************************************************************************************************
* pos_controller_PID.h
*
* Author: Qyp
*
* Update Time: 2019.6.29
*
* Introduction:  Position Controller using normal PID 
*                 output = a_ff + K_p * pos_error + K_d * vel_error + K_i * pos_error * dt;
***************************************************************************************************************************/
#ifndef POS_CONTROLLER_PID_H
#define POS_CONTROLLER_PID_H

#include <math.h>
#include <command_to_mavros.h>
#include <pos_controller_utils.h>
#include <px4_command/data_log.h>
#include <px4_command/DroneState.h>
#include <px4_command/TrajectoryPoint.h>
#include <px4_command/AttitudeReference.h>


using namespace std;

class pos_controller_PID
{
     //public表明该数据成员、成员函数是对全部用户开放的。全部用户都能够直接进行调用，在程序的不论什么其他地方訪问。
    public:

        //构造函数
        pos_controller_PID(void):
            pos_pid_nh("~")
        {
            pos_pid_nh.param<float>("Quad/mass", Quad_MASS, 1.0);
            pos_pid_nh.param<float>("Quad/throttle_a", throttle_a, 20.0);
            pos_pid_nh.param<float>("Quad/throttle_b", throttle_b, 0.0);

            pos_pid_nh.param<float>("Pos_pid/Kp_xy", Kp[0], 1.0);
            pos_pid_nh.param<float>("Pos_pid/Kp_xy", Kp[1], 1.0);
            pos_pid_nh.param<float>("Pos_pid/Kp_z" , Kp[2], 2.0);
            pos_pid_nh.param<float>("Pos_pid/Kd_xy", Kd[0], 0.5);
            pos_pid_nh.param<float>("Pos_pid/Kd_xy", Kd[1], 0.5);
            pos_pid_nh.param<float>("Pos_pid/Kd_z" , Kd[2], 0.5);
            pos_pid_nh.param<float>("Pos_pid/Ki_xy", Ki[0], 0.2);
            pos_pid_nh.param<float>("Pos_pid/Ki_xy", Ki[1], 0.2);
            pos_pid_nh.param<float>("Pos_pid/Ki_z" , Ki[2], 0.2);
            
            pos_pid_nh.param<float>("Limit/pxy_error_max", pos_error_max[0], 0.6);
            pos_pid_nh.param<float>("Limit/pxy_error_max", pos_error_max[1], 0.6);
            pos_pid_nh.param<float>("Limit/pz_error_max" , pos_error_max[2], 1.0);
            pos_pid_nh.param<float>("Limit/vxy_error_max", vel_error_max[0], 0.3);
            pos_pid_nh.param<float>("Limit/vxy_error_max", vel_error_max[1], 0.3);
            pos_pid_nh.param<float>("Limit/vz_error_max" , vel_error_max[2], 1.0);
            pos_pid_nh.param<float>("Limit/pxy_int_max"  , int_max[0], 0.5);
            pos_pid_nh.param<float>("Limit/pxy_int_max"  , int_max[1], 0.5);
            pos_pid_nh.param<float>("Limit/pz_int_max"   , int_max[2], 0.5);
            pos_pid_nh.param<float>("Limit/THR_MIN", THR_MIN, 0.1);
            pos_pid_nh.param<float>("Limit/THR_MAX", THR_MAX, 0.9);
            pos_pid_nh.param<float>("Limit/tilt_max", tilt_max, 20.0);
            pos_pid_nh.param<float>("Limit/int_start_error"  , int_start_error, 0.3);

            integral = Eigen::Vector3f(0.0,0.0,0.0);
            thrust_sp = Eigen::Vector3d(0.0,0.0,0.0);
        }

        //Quadrotor Parameter
        float Quad_MASS;
        float throttle_a;
        float throttle_b;

        //PID parameter for the control law
        Eigen::Vector3f Kp;
        Eigen::Vector3f Kd;
        Eigen::Vector3f Ki;

        //Limitation
        Eigen::Vector3f pos_error_max;
        Eigen::Vector3f vel_error_max;
        Eigen::Vector3f int_max;
        float THR_MIN;
        float THR_MAX;
        float tilt_max;
        float int_start_error;

        //积分项
        Eigen::Vector3f integral;

        //输出
        Eigen::Vector3d thrust_sp;

        //Printf the PID parameter
        void printf_param();

        void printf_result();

        // Position control main function 
        // [Input: Current state, Reference state, sub_mode, dt; Output: AttitudeReference;]
        Eigen::Vector3d pos_controller(px4_command::DroneState _DroneState, px4_command::TrajectoryPoint _Reference_State, float dt);

    private:
        ros::NodeHandle pos_pid_nh;

};

Eigen::Vector3d pos_controller_PID::pos_controller(
    px4_command::DroneState _DroneState, 
    px4_command::TrajectoryPoint _Reference_State, float dt)
{
    Eigen::Vector3d accel_sp;
    Eigen::Vector3d thrust_sp;

    // 计算误差项
    Eigen::Vector3f pos_error = pos_controller_utils::cal_pos_error(_DroneState, _Reference_State);
    Eigen::Vector3f vel_error = pos_controller_utils::cal_vel_error(_DroneState, _Reference_State);

    // 误差项限幅
    for (int i=0; i<3; i++)
    {
        pos_error[i] = constrain_function(pos_error[i], pos_error_max[i]);
        vel_error[i] = constrain_function(vel_error[i], vel_error_max[i]);
    }

    // 期望加速度 = 加速度前馈 + PID
    for (int i=0; i<3; i++)
    {
        accel_sp[i] = _Reference_State.acceleration_ref[i] + Kp[i] * pos_error[i] + Kd[i] * vel_error[i] + Ki[i] * integral[i];
    }
    
    accel_sp[2] = accel_sp[2] + 9.8;

    // 更新积分项
    for (int i=0; i<3; i++)
    {
        if(abs(pos_error[i]) < int_start_error)
        {
            integral[i] += pos_error[i] * dt;
            integral[i] = constrain_function(integral[i], int_max[i]);
        }else
        {
            integral[i] = 0;
        }

        // If not in OFFBOARD mode, set all intergral to zero.
        if(_DroneState.mode != "OFFBOARD")
        {
            integral[i] = 0;
        }
    }

    // cout << "ff [X Y Z] : " << _Reference_State.acceleration_ref[0] << " [m/s] "<< _Reference_State.acceleration_ref[1]<<" [m/s] "<<_Reference_State.acceleration_ref[2]<<" [m/s] "<<endl;
 

    // cout << "Vel_P_output [X Y Z] : " << Kp[0] * pos_error[0] << " [m/s] "<< Kp[1] * pos_error[1]<<" [m/s] "<<Kp[2] * pos_error[2]<<" [m/s] "<<endl;

    // cout << "Vel_I_output [X Y Z] : " << integral[0] << " [m/s] "<< integral[1]<<" [m/s] "<<integral[2]<<" [m/s] "<<endl;

    // cout << "Vel_D_output [X Y Z] : " << Kd[0] * vel_error[0] << " [m/s] "<< Kd[1] * vel_error[1]<<" [m/s] "<<Kd[2] * vel_error[2]<<" [m/s] "<<endl;


    // 期望推力 = 期望加速度 × 质量
    // 归一化推力 ： 根据电机模型，反解出归一化推力
    for (int i=0; i<3; i++)
    {
        thrust_sp[i] = (accel_sp[i] * Quad_MASS - throttle_b) / throttle_a;
    }

    // 推力限幅，根据最大倾斜角及最大油门
    // Get maximum allowed thrust in XY based on tilt angle and excess thrust.
    float thrust_max_XY_tilt = fabs(thrust_sp[2]) * tanf(tilt_max/180.0*M_PI);
    float thrust_max_XY = sqrtf(THR_MAX * THR_MAX - pow(thrust_sp[2],2));
    thrust_max_XY = min(thrust_max_XY_tilt, thrust_max_XY);

    if ((pow(thrust_sp[0],2) + pow(thrust_sp[1],2)) > thrust_max_XY * thrust_max_XY) {
        float mag = sqrtf((pow(thrust_sp[0],2) + pow(thrust_sp[1],2)));
        thrust_sp[0] = thrust_sp[0] / mag * thrust_max_XY;
        thrust_sp[1] = thrust_sp[1] / mag * thrust_max_XY;
    }

    return thrust_sp;


    // cout <<">>>>>>>>>>>>>>>>>>>>>>PID Position Controller<<<<<<<<<<<<<<<<<<<<<" <<endl;

    // //固定的浮点显示
    // cout.setf(ios::fixed);
    // //左对齐
    // cout.setf(ios::left);
    // // 强制显示小数点
    // cout.setf(ios::showpoint);
    // // 强制显示符号
    // cout.setf(ios::showpos);

    // cout<<setprecision(2);

    // cout << "Pos_ref [XYZ]: " << _Reference_State.position_ref[0] << " [ m ]" << _Reference_State.position_ref[1] << " [ m ]"<< _Reference_State.position_ref[2] << " [ m ]" << endl;
    // cout << "Vel_ref [XYZ]: " << _Reference_State.velocity_ref[0] << " [m/s]" << _Reference_State.velocity_ref[1] << " [m/s]" << _Reference_State.velocity_ref[2] << " [m/s]" <<endl;
    // cout << "Acc_ref [XYZ]: " << _Reference_State.acceleration_ref[0] << " [m/s^2]" << _Reference_State.acceleration_ref[1] << " [m/s^2]" << _Reference_State.acceleration_ref[2] << " [m/s^2]" <<endl;
    // cout << "Yaw_setpoint : " << _Reference_State.yaw_ref * 180/M_PI << " [deg] " <<endl;

    // cout << "e_p [X Y Z] : " << pos_error[0] << " [m] "<< pos_error[1]<<" [m] "<<pos_error[2]<<" [m] "<<endl;
    // cout << "e_v [X Y Z] : " << vel_error[0] << " [m/s] "<< vel_error[1]<<" [m/s] "<<vel_error[2]<<" [m/s] "<<endl;
    // cout << "acc_ref [X Y Z] : " << _Reference_State.acceleration_ref[0] << " [m/s^2] "<< _Reference_State.acceleration_ref[1]<<" [m/s^2] "<<_Reference_State.acceleration_ref[2]<<" [m/s^2] "<<endl;
    
    // cout << "desired_acceleration [X Y Z] : " << _AttitudeReference.desired_acceleration[0] << " [m/s^2] "<< _AttitudeReference.desired_acceleration[1]<<" [Nm/s^2] "<<_AttitudeReference.desired_acceleration[2]<<" [m/s^2] "<<endl;
    // cout << "desired_thrust [X Y Z] : " << _AttitudeReference.desired_thrust[0] << " [N] "<< _AttitudeReference.desired_thrust[1]<<" [N] "<<_AttitudeReference.desired_thrust[2]<<" [N] "<<endl;
    // cout << "desired_thrust_normalized [X Y Z] : " << _AttitudeReference.desired_thrust_normalized[0] << " [N] "<< _AttitudeReference.desired_thrust_normalized[1]<<" [N] "<<_AttitudeReference.desired_thrust_normalized[2]<<" [N] "<<endl;
    // cout << "desired_attitude [R P Y] : " << _AttitudeReference.desired_attitude[0] * 180/M_PI <<" [deg] "<<_AttitudeReference.desired_attitude[1] * 180/M_PI << " [deg] "<< _AttitudeReference.desired_attitude[2] * 180/M_PI<<" [deg] "<<endl;
    // cout << "desired_throttle [0-1] : " << _AttitudeReference.desired_throttle <<endl;
}

void pos_controller_PID::printf_result()
{


    cout <<">>>>>>>>>>>>>>>>>>>>PID Position Controller<<<<<<<<<<<<<<<<<<<<<" <<endl;

    //固定的浮点显示
    cout.setf(ios::fixed);
    //左对齐
    cout.setf(ios::left);
    // 强制显示小数点
    cout.setf(ios::showpoint);
    // 强制显示符号
    cout.setf(ios::showpos);

    cout<<setprecision(2);

        cout << "thrust_sp    [X Y Z] : " << thrust_sp[0] << " [m/s^2] "<< thrust_sp[1]<<" [m/s^2] "<<thrust_sp[2]<<" [m/s^2] "<<endl;

}

// 【打印参数函数】
void pos_controller_PID::printf_param()
{
    cout <<">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>PID Parameter <<<<<<<<<<<<<<<<<<<<<<<<<<<" <<endl;

    cout <<"Quad_MASS : "<< Quad_MASS << endl;
    cout <<"throttle_a : "<< throttle_a << endl;
    cout <<"throttle_b : "<< throttle_b << endl;

    cout <<"Kp_x : "<< Kp[0] << endl;
    cout <<"Kp_y : "<< Kp[1] << endl;
    cout <<"Kp_z : "<< Kp[2] << endl;

    cout <<"Kd_x : "<< Kd[0] << endl;
    cout <<"Kd_y : "<< Kd[1] << endl;
    cout <<"Kd_z : "<< Kd[2] << endl;

    cout <<"Ki_x : "<< Ki[0] << endl;
    cout <<"Ki_y : "<< Ki[1] << endl;
    cout <<"Ki_z : "<< Ki[2] << endl;

    cout <<"Limit:  " <<endl;
    cout <<"pxy_error_max : "<< pos_error_max[0] << endl;
    cout <<"pz_error_max :  "<< pos_error_max[2] << endl;
    cout <<"vxy_error_max : "<< vel_error_max[0] << endl;
    cout <<"vz_error_max :  "<< vel_error_max[2] << endl;
    cout <<"pxy_int_max : "<< int_max[0] << endl;
    cout <<"pz_int_max : "<< int_max[2] << endl;
    cout <<"THR_MIN : "<< THR_MIN << endl;
    cout <<"THR_MAX : "<< THR_MAX << endl;
    cout <<"tilt_max : "<< tilt_max << endl;
    cout <<"int_start_error : "<< int_start_error << endl;

}

#endif
