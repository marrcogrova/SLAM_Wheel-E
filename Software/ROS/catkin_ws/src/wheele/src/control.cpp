#include "ros/ros.h"
#include <std_msgs/Float64.h>
#include <std_msgs/Float64MultiArray.h>
#include <wheele/pwm6.h>
#include <geometry_msgs/Twist.h>
#include <chrono>
#include <ctime>
#include <unistd.h>
#include <cstdlib>

#define radius 0.035  // Radius of your wheels
#define enc_slits 22  // nº of slits in your encoders
#define pi 3.141592   // un arco de circunferencia entre su radio o algo asi

float kp=0.0,ki=0.0,kd=0.0; // pid gains
std_msgs::Float64 pidkp,pidki,pidkd;  // pid gains tunnable by topic

geometry_msgs::Twist ref_ms;
wheele::pwm6 pwm_msg;
wheele::pwm6 ticks;

void get_ref(const geometry_msgs::Twist::ConstPtr& ref){
  ref_ms.linear.x=ref->linear.x;
  ref_ms.angular.z=ref->angular.z;
}

void get_ticks(const wheele::pwm6::ConstPtr& ticks_read){
  ticks=ticks_read;
}

void get_kp(const std_msgs::Float64::ConstPtr& pidkp){
  kp=pidkp->data;
}

void get_ki(const std_msgs::Float64::ConstPtr& pidki){
  ki=pidki->data;
}

void get_kd(const std_msgs::Float64::ConstPtr& pidkd){
  kd=pidkd->data;
}

float ms2ticks(float ms){
  float tickss;
  tickss=ms/radius; // [m/s] to [rad/s] (wheel radius = 0.035)
  tickss=tickss*(enc_slits/2*pi); // [rad/s] to [ticks/s] (encoders have 22 slits)
  return tickss;
}

float claw(int m, std::chrono::duration<double> dt, float ref, float out, float ek1, float sat_err, float i){ // Control law with conditional integration antiwindup
  sat_err_integrated=sat_err*dt.count();  // Integration of how much we satured
  ek=ref-out;  // Error = Desired - Actual  (output)
  p = kp*ek;
  i = ki*(i+ek*dt.count());
  d = kd*((ek-ek1)/dt.count());
  ukns=p+i+d+(sat_err_integrated/0.01)+ueq;  // PID + antiwindup + offset'
  ROS_INFO_STREAM("ukns:  "<<ukns <<"\n");
  if(ukns<-255) ukreal=-255;  // Saturation
  else if(ukns>255) ukreal=255;
  else ukreal=ukns;
  sat_err=ukreal-ukns;  // Saturation error
  ek1=ek; // Error update
  switch(1) {
    case 1: sat_err_m1l=sat_err;
            i_m1l=i;
            ek1_m1l=ek1;
            break;
    case 2: sat_err_m1r=sat_err;
            i_m1r=i;
            ek1_m1l=ek1;
            break;
    case 3: sat_err_m2l=sat_err;
            i_m2l=i;
            ek1_m2l=ek1;
            break;
    case 4: sat_err_m2r=sat_err;
            i_m2r=i;
            ek1_m2r=ek1;
            break;
    case 5: sat_err_m3l=sat_err;
            i_m3l=i;
            ek1_m3l=ek1;
            break;
    case 6: sat_err_m3r=sat_err;
            i_m3r=i;
            ek1_m3r=ek1;
            break;
  }
  return ukreal;
}


int main(int argc, char **argv){
  std::chrono::time_point<std::chrono::system_clock> t_act,t_lastT;
  t_act=t_lastT;
  std::chrono::duration<double> dt;

  ros::init(argc, argv, "control");
  ros::NodeHandle n;
  ros::Subscriber ref_sub = n.subscribe("cmd_vel",10,get_ref);
  ros::Subscriber ticks_sub = n.subscribe("encoders_ticks",10,get_ticks);
  ros::Publisher pwm_pub = n.advertise<wheele::pwm6>("pwm", 10);

  ros::Subscriber kp_sub = n.subscribe("kp",10,get_kp);
  ros::Subscriber ki_sub = n.subscribe("ki",10,get_ki);
  ros::Subscriber kd_sub = n.subscribe("kd",10,get_kd);

  ros::Rate loop_rate(10); //10Hz

  float p,d,sat_err_integrated,ek,ukns,ukreal,ueq=0;
  //static float ek1_m1l, sat_err=0,i=0;
  static float ek1_m1l,sat_err_m1l=0,i_m1l=0;
  static float ek1_m1r,sat_err_m1r=0,i_m1r=0;
  static float ek1_m2l,sat_err_m2l=0,i_m2l=0;
  static float ek1_m2r,sat_err_m2r=0,i_m2r=0;
  static float ek1_m3l,sat_err_m3l=0,i_m3l=0;
  static float ek1_m3r,sat_err_m3r=0,i_m3r=0;

while(n.ok()) {
  float lin, ang, left_ref_ms, right_ref_ms, left_ref_ticks, right_ref_ticks;
  lin=ref_ms.linear.x;
  ang=ref_ms.angular.z;
  left_ref_ms =lin - (ang*radius)/2.0;
  right_ref_ms =lin + (ang*radius)/2.0;
  left_ref_ticks = ms2ticks(left_ref_ms);
  right_ref_ticks = ms2ticks(right_ref_ms);

  ROS_INFO_STREAM("dt:  "<<dt.count() <<"\n");
  ROS_INFO_STREAM("kp:  "<<kp <<"\n");
  ROS_INFO_STREAM("ki:  "<<ki <<"\n");
  ROS_INFO_STREAM("kd:  "<<kd <<"\n");

  t_act = std::chrono::system_clock::now();
  dt=t_act-t_lastT;
  t_lastT=t_act;

  tocks = ticks.m1l;
  pwm_msg.m1l=claw(1 ,dt, left_ref_ticks, tocks, ek1_m1l, sat_err_m1l, i_m1l);
  ROS_INFO_STREAM("pwm m1l:  "<<pwm_msg.m1l <<"\n");
  tocks = ticks.m1r;
  pwm_msg.m1r=claw(2 ,dt, right_ref_ticks, tocks, ek1_m1r, sat_err_m1r, i_m1r);
  ROS_INFO_STREAM("pwm m1r:  "<<pwm_msg.m1r <<"\n");
  tocks = ticks.m2l;
  pwm_msg.m2l=claw(3 ,dt, left_ref_ticks, tocks, ek1_m2l, sat_err_m2l, i_m2l);
  ROS_INFO_STREAM("pwm m2l:  "<<pwm_msg.m2l <<"\n");
  tocks = ticks.m2r;
  pwm_msg.m2r=claw(4 ,dt, right_ref_ticks, tocks, ek1_m2r, sat_err_m2r, i_m2r);
  ROS_INFO_STREAM("pwm m2r:  "<<pwm_msg.m2r <<"\n");
  tocks = ticks.m3l;
  pwm_msg.m3l=claw(5 ,dt, left_ref_ticks, tocks, ek1_m3l, sat_err_m3l, i_m3l);
  ROS_INFO_STREAM("pwm m3l:  "<<pwm_msg.m3l <<"\n");
  tocks = ticks.m3r;
  pwm_msg.m3r=claw(6 ,dt, right_ref_ticks, tocks, ek1_m3r, sat_err_m3r, i_m3r);
  ROS_INFO_STREAM("pwm m3r:  "<<pwm_msg.m3r <<"\n");

  pwm_pub.publish(pwm_msg);

  ros::spinOnce();
  loop_rate.sleep();
  }
  return 0;
}