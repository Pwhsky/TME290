/*
 * Copyright (C) 2019 Ola Benderius
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <iostream>
#include "cluon-complete.hpp"
#include "tme290-sim-grass-msg.hpp"

 //The robot can carry out the following actions (values 0-8): Stay in the same grid cell (0), travel up left (1), up (2), up right (3), right (4), down right (5), down (6), down left (7), and left (8).
 
 //Check distance to charging station
  //if distance is close to battery charge + some margin:
 	//go home
  //else
  	//cut 1 iteration of grass
 	
//Robot variables:
uint32_t savedi = 0;
uint32_t savedj = 0;
bool resumePath = false;
float batteryLimit = 0.35f;
bool reset = false;
float chargeGoal = 0.999f;
//Robot functions:

	bool isHome (const tme290::grass::Sensors& msg);
	void stay(tme290::grass::Control& control);
	void leaveHome(tme290::grass::Control& control);
 	
 	void goHome(const tme290::grass::Sensors& msg,tme290::grass::Control& control);
	void createPathing(const tme290::grass::Sensors& msg, tme290::grass::Control& control);
	void resumePathing(const tme290::grass::Sensors& msg, tme290::grass::Control& control);
	

 
int32_t main(int32_t argc, char **argv) {
  int32_t retCode{0};
  auto commandlineArguments = cluon::getCommandlineArguments(argc, argv);
  if (0 == commandlineArguments.count("cid")) {
    std::cerr << argv[0] 
      << " is a lawn mower control algorithm." << std::endl;
    std::cerr << "Usage:   " << argv[0] << " --cid=<OpenDLV session>" 
      << "[--verbose]" << std::endl;
    std::cerr << "Example: " << argv[0] << " --cid=111 --verbose" << std::endl;
    retCode = 1;
  } else {
    bool const verbose{commandlineArguments.count("verbose") != 0};
    uint16_t const cid = std::stoi(commandlineArguments["cid"]);
    
    cluon::OD4Session od4{cid};

   int32_t someVariable{0};

    auto onSensors{[&od4, &someVariable](cluon::data::Envelope &&envelope)
    {
        auto msg = cluon::extractMessage<tme290::grass::Sensors>(
            std::move(envelope));
            
        tme290::grass::Control control; 
        someVariable++;

	/////////////////////////////////////////////////////////////
	
	
	 if (msg.battery() < batteryLimit){ //Check battery level
         	goHome(msg,control);
   	
         }else if (msg.battery() < 1.0f && isHome(msg) == true) { //Stay to charge if not full
         	stay(control);
         	
         }else if (msg.battery() > chargeGoal && isHome(msg) == true){ //Leave charging station once full
       		leaveHome(control);
         
         }else if (isHome(msg)==false && msg.battery() >= batteryLimit && resumePath == false) { 
         	createPathing(msg,control); //Will return to the main-corridor path which is low battery drain

         }else if (resumePath == true) { 
		resumePathing(msg,control);
         }

        
     ///////////////////////////////////////////////////////////////
     


        std::cout << "Rain reading " << msg.rain() << ", direction (" <<
        msg.rainCloudDirX() << ", " << msg.rainCloudDirY() << ")" << std::endl; 
        
        od4.send(control);
   
     }};
      

    auto onStatus{[&verbose](cluon::data::Envelope &&envelope)
    {
        auto msg = cluon::extractMessage<tme290::grass::Status>(
            std::move(envelope));
        if (verbose) {
          std::cout << "Status at time " << msg.time() << ": " 
            << msg.grassMean() << "/" << msg.grassMax() << std::endl;
        }
    }};
    

    od4.dataTrigger(tme290::grass::Sensors::ID(), onSensors);
    od4.dataTrigger(tme290::grass::Status::ID(),  onStatus);

    if (verbose) {
      std::cout << "All systems ready, let's cut some grass!" << std::endl;
    }


   
    tme290::grass::Control control;
    control.command(0);
    od4.send(control);

    while (od4.isRunning()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    retCode = 0;
  }
  return retCode;
}


void stay(tme290::grass::Control& control){
        control.command(0);
}


void goHome(const tme290::grass::Sensors& msg,tme290::grass::Control& control ) {
    if (msg.j() >= 17){
            if (msg.i() < 22){
              control.command(4);
            }
            else { 
              control.command(2);
            }
    }else{
    
          if (msg.i() != 0) {
            control.command(8);
          }
          else if (msg.j() != 0) {
            control.command(2);
          }
    }
}
void leaveHome(tme290::grass::Control& control){
		control.command(4);
         	 if (savedi + savedj != 0){
          	 	resumePath = true;
         	 }
         	 if (reset == true){
          	 	resumePath = false;
         	 	reset = false;
          	 }
	}


bool isHome (const tme290::grass::Sensors& msg){

	if(msg.i()+msg.j() == 0) {
		return true;
	}else{
		return false;}
}

void createPathing(const tme290::grass::Sensors& msg, tme290::grass::Control& control) {
	
	 if (msg.j()%2 == 0) { 
         	if (msg.i() == 39){
             		control.command(6);
           	}
           	else{
               		control.command(4);
           	}
           	
         }else if (msg.j() == 17){
         
         	if (msg.i() == 22) {
                	control.command(6);
        	}
        	else {
                        control.command(8);
                }
                
         }else {
         
         	if (msg.i()== 0){
                	control.command(6);
            	}
                else {
             		control.command(8);
           	}
         }
         
          if (msg.battery() > batteryLimit) {
            savedi = msg.i();
            savedj = msg.j();
          }
          if ((msg.i() == 0) && (msg.j() == 39)) {
            reset = true;
          }
	  
}

void resumePathing(const tme290::grass::Sensors& msg, tme290::grass::Control& control) {
	
	 if (savedi > 22 || savedj < 18){
         	if (msg.i() < savedi){
                	control.command(4);
                }
                else if (msg.j() < savedj) {
                	control.command(6);
                }
          }
          else if (savedj > 18 && savedi < 22) {
          	if (msg.i() <= 22 && msg.j() <= 18) {
                	control.command(4);
                }
                else if (msg.j() < savedj && msg.i() > 22){
               		control.command(6);
                }
         	else if (msg.j() == savedj && msg.i() > savedi) {
            		control.command(8);
        	 }
         }
         
         if ((msg.j() == savedj)&&(msg.i() == savedi)){
            resumePath = false;
          }
}

