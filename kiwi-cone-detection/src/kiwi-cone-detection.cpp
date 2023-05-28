#include "cluon-complete.hpp"
#include "opendlv-standard-message-set.hpp"

#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

//#include <string.h>
//#include <cstdint>
#include <iostream>
//#include <memory>
#include <mutex>
#include <chrono>


float frameCounterLimit = 6;
float carSpeed      = 0.0f;
//Parameters for image processing:
//Color ranges
cv::Scalar  lowerBlue(100,50,0);
cv::Scalar  upperBlue(130,255,255);
cv::Scalar  lowerYellow(20, 55, 30);	
cv::Scalar  upperYellow(50,255,255);

const cv::Point imageCenter = cv::Point(640/2, 480/2);
const float maxDeviationX = 640/2;  // Assuming 640 is the width of the field of vision
const float maxSteeringAngle = 0.66f;
//Morphology kernel
cv::Mat const structuringElement = cv::getStructuringElement(cv::MORPH_RECT,cv::Size(3,3),cv::Point(-1,1));

//Trackbar functions (defined after main)

void updateBoundaries(int,void*);
cv::Mat getMask(cv::Mat image,cv::Scalar lowerBound,cv::Scalar upperBound);

void steerToMiddlePoint(cluon::OD4Session& od4, cv::Rect blueCones, cv::Rect yellowCones);
int getArea(std::vector<cv::Rect> rectangles);
cv::Mat drawBoundingBoxes(cv::Mat image,std::vector<cv::Rect> rectangles,cv::Scalar color);
//Canny threshhold
int const cannyLower = 200;
int const cannyUpper = cannyLower*3; 

//Function to update slider window:


//---------------------------------------



int32_t main(int32_t argc, char **argv) 
{
    int32_t retCode{1};
    u_int32_t HEIGHT;
    u_int32_t WIDTH;
    auto commandlineArguments = cluon::getCommandlineArguments(argc, argv);
    if ( (0 == commandlineArguments.count("cid")) ||
         (0 == commandlineArguments.count("name")) ||
         (0 == commandlineArguments.count("type"))) 
         {
         
            std::cerr << argv[0] << " attaches to a shared memory area containing an ARGB image." << std::endl;
            std::cerr << "Usage:   " << argv[0] << " --cid=<OD4 session> --name=<name of shared memory area> [--verbose]" << std::endl;
            std::cerr << "         --cid:    CID of the OD4Session to send and receive messages" << std::endl;
            std::cerr << "         --name:   name of the shared memory area to attach" << std::endl;
            //std::cerr << "         --width:  width of the frame" << std::endl;
            //std::cerr << "         --height: height of the frame" << std::endl;
            std::cerr <<"           --type    Type of the video input stream tex rpi or rec" << std::endl;
            std::cerr << "Example: " << argv[0] << " --cid=112 --name=img.argb --type=rec --verbose" << std::endl;
        }
    else 
    {
        const std::string NAME{commandlineArguments["name"]};
        if (commandlineArguments["type"]=="rec")
        {
            WIDTH  = 1280;
            HEIGHT = 720;
        }
        else if (commandlineArguments["type"]=="rpi")
        {
            WIDTH  = 640;
            HEIGHT = 480;
        }
        const bool VERBOSE{commandlineArguments.count("verbose") != 0};

        // Attach to the shared memory.
        std::unique_ptr<cluon::SharedMemory> sharedMemory{new cluon::SharedMemory{NAME}};
        if (sharedMemory && sharedMemory->valid()) 
        {
            std::clog << argv[0] << ": Attached to shared memory '" << sharedMemory->name() << " (" << sharedMemory->size() << " bytes)." << std::endl;

            // Interface to a running OpenDaVINCI session; here, you can send and receive messages.
            cluon::OD4Session od4{static_cast<uint16_t>(std::stoi(commandlineArguments["cid"]))};

            // Handler to receive distance readings (realized as C++ lambda).
            std::mutex distancesMutex;
            float front{0};
            float rear{0};
            float left{0};
            float right{0};
            auto onDistance = [&distancesMutex, &front, &rear, &left, &right](cluon::data::Envelope &&env)
            {
                auto senderStamp = env.senderStamp();
                // Now, we unpack the cluon::data::Envelope to get the desired DistanceReading.
                opendlv::proxy::DistanceReading dr = 
                cluon::extractMessage<opendlv::proxy::DistanceReading>(std::move(env));

            // Store distance readings.
                std::lock_guard<std::mutex> lck(distancesMutex);
                switch (senderStamp) 
                {
                    case 0: front = dr.distance(); break;
                    case 2: rear = dr.distance(); break;
                    case 1: left = dr.distance(); break;
                    case 3: right = dr.distance(); break;
                }
            };
            od4.dataTrigger(opendlv::proxy::DistanceReading::ID(), onDistance);

    
          
    cv::namedWindow("Slider", cv::WINDOW_NORMAL); 
    cv::createTrackbar("Car speed", "Slider", NULL, 15, updateBoundaries);
    cv::createTrackbar("Blue Low H", "Slider", NULL, 179, updateBoundaries);
    cv::createTrackbar("Blue High H", "Slider", NULL, 179, updateBoundaries);
    cv::createTrackbar("Blue Low S", "Slider", NULL, 255, updateBoundaries);
    cv::createTrackbar("Blue High S", "Slider", NULL, 255, updateBoundaries);
    cv::createTrackbar("Blue Low V", "Slider", NULL, 255, updateBoundaries);
    cv::createTrackbar("Blue High V", "Slider", NULL, 255, updateBoundaries);
    
    cv::createTrackbar("Yellow Low H", "Slider",NULL, 179, updateBoundaries);
    cv::createTrackbar("Yellow High H", "Slider", NULL, 179, updateBoundaries);
    cv::createTrackbar("Yellow Low S", "Slider", NULL, 255, updateBoundaries);
    cv::createTrackbar("Yellow High S", "Slider",NULL, 255, updateBoundaries);
    cv::createTrackbar("Yellow Low V", "Slider", NULL, 255, updateBoundaries);
    cv::createTrackbar("Yellow High V", "Slider",NULL, 255, updateBoundaries);
         

            int counter{0};
            int steeringCooldown = 60;
            // Endless loop; end the program by pressing Ctrl-C.
            while (od4.isRunning()) 
            {
                cv::Mat img;

                // Wait for a notification of a new frame.
                sharedMemory->wait();
                // Lock the shared memory.
                sharedMemory->lock();
                {
                    if(commandlineArguments["type"] == "rpi")
                    {
                    cv::Mat wrapped(HEIGHT, WIDTH, CV_8UC3, sharedMemory->data());
                    img = wrapped.clone(); //matrix of dimensions HeightxWidthxColor
                    }
                    else if(commandlineArguments["type"] == "rec")
                    {
                    cv::Mat wrapped(HEIGHT, WIDTH, CV_8UC4, sharedMemory->data());
                    img = wrapped.clone(); //matrix of dimensions HeightxWidthxColor
                    }
                }
                sharedMemory->unlock();
               // cv::Rect visionArea(0,230,WIDTH,150);
	       // img = img(visionArea);
	       
                cv::Mat hsv_img;
                cv::cvtColor(img,hsv_img,cv::COLOR_BGR2HSV);
		cv::Mat blueCones = getMask(hsv_img,lowerBlue,upperBlue);
		cv::Mat yellowCones = getMask(hsv_img,lowerYellow,upperYellow);


	//find contours and make bounding boxes	////////////////////////////////////////////////////////////7
		std::vector<cv::Rect> rectBlueCones;
		std::vector<cv::Rect> rectYellowCones;
		
		std::vector<cv::Point> positionBlueCones;
		std::vector<cv::Point> positionYellowCones;
	{
		std::vector<std::vector<cv::Point>> blueContours;
		std::vector<std::vector<cv::Point>> yellowContours;
		
		std::vector<cv::Vec4i> order;
		cv::findContours(blueCones,blueContours,order,cv::RETR_TREE,cv::CHAIN_APPROX_SIMPLE); 
		cv::findContours(yellowCones,yellowContours,order,cv::RETR_TREE,cv::CHAIN_APPROX_SIMPLE);
		
		for (int i = 0; i<blueContours.size(); i++) {
			cv::RotatedRect rectRotated = cv::minAreaRect(blueContours[i]);
			cv::Rect rect = rectRotated.boundingRect();
			
			if (rect.width < 0.85*rect.height && (rect.area() > 600 && rect.area() < 
			3800) ) { 
				rectBlueCones.push_back(rect);
				cv::Point bottomCentre = cv::Point(static_cast<int>(rect.x + 
				rect.width/2),static_cast<int>(rect.y + rect.height));
				positionBlueCones.push_back(bottomCentre);
			}       
		}
		
		
		
		for (int i = 0; i<yellowContours.size(); i++) {
			cv::RotatedRect rectRotated = cv::minAreaRect(yellowContours[i]);
			cv::Rect rect = rectRotated.boundingRect();
			if (rect.width < 0.85*rect.height && (rect.area() > 600 && rect.area() < 
			3800) ) { 
				rectYellowCones.push_back(rect);
				cv::Point bottomCentre = cv::Point(static_cast<int>(rect.x + 	 
				rect.width/2),static_cast<int>(rect.y + rect.height));
				positionYellowCones.push_back(bottomCentre);
			}
		}
		
	}
	//////////////////////////////////////////////////////////////////////////////////////////////////7	
                if (VERBOSE)
                {
                  	cv::cvtColor(blueCones,blueCones,cv::COLOR_GRAY2BGR);
			cv::cvtColor(yellowCones,yellowCones,cv::COLOR_GRAY2BGR);
			blueCones.setTo(cv::Scalar(255,0,0),blueCones);
	 		yellowCones.setTo(cv::Scalar(0,255,255),yellowCones);
		
			cv::Mat colorVision = blueCones + yellowCones;
                   //cv::imshow(sharedMemory->name().c_str(), img); 
                   
     		    //Draw bounding boxes:
     		    cv::Scalar yellow(0,255,255);
     		    cv::Scalar blue(255,0,0);
     		    colorVision = drawBoundingBoxes(colorVision,rectYellowCones,yellow);
               	    colorVision = drawBoundingBoxes(colorVision,rectBlueCones,blue);
                    
                   //Display image
                   cv::imshow("Yellow Blue Vision",colorVision);   
                   //cv::imshow("Raw image",img);            
                   cv::waitKey(1); 
                }

                {
                    std::lock_guard<std::mutex> lck(distancesMutex);
                    std::cout << "front = " << front << ", "
                    << "rear = " << rear << ", "
                    << "left = " << left << ", "
                    << "right = " << right << "." << std::endl;
                }

        	opendlv::proxy::AngleReading ar;
      		ar.angle(123.45f);
      		od4.send(ar);
    
	
	//If the front sensor detects something, stop the car.
		if (front < 0.33f) {
		    opendlv::proxy::PedalPositionRequest ppr;
		    ppr.position(0);
		    od4.send(ppr);
		}
        	else {
			
		
		 //Steering logic /////////////////////////////////////////////////////
		 
	
		//If one color is missing and it can see the other color, do a hard turn regardless, and maybe slow down
		opendlv::proxy::GroundSteeringRequest gsr;
		opendlv::proxy::PedalPositionRequest ppr;
		
		steeringCooldown -= 1;
		
		
		
		//Steer between cones if they are detected
		if (rectBlueCones.size()  > 0 && rectYellowCones.size() > 0) {
			
                	steerToMiddlePoint(od4,rectBlueCones[0], rectYellowCones[0] );
                	ppr.position(carSpeed/100);
                 	od4.send(ppr);
		
		} 
		
		//Major steering correction if only either color is found (and slow down)
		else if (rectBlueCones.size() < 1){
		
                	gsr.groundSteering(-0.56f);
		        steeringCooldown = 500;
		        
			ppr.position(carSpeed/100);
                	od4.send(ppr);
                 }
              
                 else if (rectYellowCones.size() < 1) {	
                	gsr.groundSteering(0.56f);
			steeringCooldown =500;
			ppr.position(carSpeed/100);
                	od4.send(ppr);	
			
          	 }else{
          		 gsr.groundSteering(0.0f);
          	 	 ppr.position(carSpeed/100);
          	
          	 }
          	 
          	
		 od4.send(gsr);
		 
		}//if front sensor
		
      	    }//OD4 while loop
      
    	 } 
      retCode = 0; 
    }
    return retCode;
}//main
   
   
   //Color mask function
   cv::Mat getMask(cv::Mat image,cv::Scalar lowerBound, cv::Scalar upperBound)
   {
   	cv::Mat mask;
  	cv::inRange(image,lowerBound,upperBound,mask); 
   	cv::morphologyEx(mask, mask,cv::MORPH_DILATE,structuringElement,cv::Point(-1,-1), 1, 1, 1);
   	cv::morphologyEx(mask, mask,cv::MORPH_ERODE,structuringElement,cv::Point(-1,-1), 1, 1, 1);
   	

	   return mask;
   }
   
   int getArea(std::vector<cv::Rect> rectangles)
   {
   	 int sum = 0;
  	  for (int i = 0; i <rectangles.size();i++){
              	sum = sum + rectangles[i].area();    
       	    }
            return sum;
   }
   
   cv::Mat drawBoundingBoxes(cv::Mat image,std::vector<cv::Rect> rectangles, cv::Scalar color)
   {
   
    for (int i = 0; i <rectangles.size();i++){
              		     rectangle(image,rectangles[i],color,
              		     2);     
                    }
       return image;
   }
   


void updateBoundaries(int,void*) 
  {
    carSpeed       = cv::getTrackbarPos("Car speed","Slider");
    lowerBlue[0]   = cv::getTrackbarPos("Blue Low H","Slider");
    upperBlue[0]   = cv::getTrackbarPos("Blue High H","Slider");
    
    lowerBlue[1]   = cv::getTrackbarPos("Blue Low S","Slider");
    upperBlue[1]   = cv::getTrackbarPos("Blue High S","Slider");
    
    lowerBlue[2]   = cv::getTrackbarPos("Blue Low V","Slider");
    upperBlue[2]   = cv::getTrackbarPos("Blue High V","Slider");
    
    lowerYellow[0] = cv::getTrackbarPos("Yellow Low H","Slider");
    upperYellow[0] = cv::getTrackbarPos("Yellow High H","Slider");
    
    lowerYellow[1] = cv::getTrackbarPos("Yellow Low S","Slider");
    upperYellow[1] = cv::getTrackbarPos("Yellow High S","Slider");
    
    lowerYellow[2] = cv::getTrackbarPos("Yellow Low V","Slider");
    upperYellow[2] = cv::getTrackbarPos("Yellow High V","Slider");
  }
  
  
void steerToMiddlePoint(cluon::OD4Session& od4, cv::Rect blueCones, cv::Rect yellowCones){

 opendlv::proxy::GroundSteeringRequest gsr; 
 opendlv::proxy::PedalPositionRequest ppr;
 

 // Calculate the center point of the bounding box
 cv::Point blueCenter = blueCones.tl() + cv::Point(blueCones.width/2, 
   			blueCones.height / 2);	
   			
 cv::Point yellowCenter = yellowCones.tl() + cv::Point(yellowCones.width/2, 
   			yellowCones.height / 2);
   			
 cv::Point middlePoint = (blueCenter-yellowCenter)/2;
    				  			
 cv::Point deviation = middlePoint - imageCenter;
 // Calculate the steering angle based on the deviation
 float steeringAngle = -maxSteeringAngle * (deviation.x / maxDeviationX);
 gsr.groundSteering(steeringAngle);
 od4.send(gsr);
}

 	/*
			int yellowSum = getArea(rectYellowCones);
			int blueSum   = getArea(rectBlueCones);
                        
                        
       			accumulatedSumYellow += static_cast<float>(rectYellowCones.size())/
       			frameCounterLimit;
     			accumulatedSumBlue +=  static_cast<float>(rectBlueCones.size())/frameCounterLimit;
               		counter ++;
               		*/
               		

/*
if (rectYellowCones.size() < 1 && steeringCooldown < 1){
			steeringAngle = 0.66f;	
                	gsr.groundSteering(steeringAngle);
                	steeringCooldown = 7;
                	ppr.position(carSpeed/130);
                 	od4.send(ppr);

		} 
		else if (rectBlueCones.size() < 1 && steeringCooldown < 1){
			steeringAngle = -0.66f;
                	gsr.groundSteering(steeringAngle);
			steeringCooldown =7;
			ppr.position(carSpeed/130);
                	od4.send(ppr);
                 }
               
                 else {	
				
			if (accumulatedSumYellow  > accumulatedSumBlue && 
			counter>frameCounterLimit) { 
				
		 		steeringAngle =  (accumulatedSumYellow - 
		 		accumulatedSumBlue)/
		 		(accumulatedSumYellow + accumulatedSumBlue)*(-0.32f);
				
                		gsr.groundSteering(steeringAngle);
                		ppr.position(carSpeed/110);
               		        od4.send(ppr);
                 		counter = 0;
           	 		accumulatedSumBlue = 0;
           	 		accumulatedSumYellow = 0;
			} 
			else if (accumulatedSumYellow  < accumulatedSumBlue && counter>frameCounterLimit) {
				steeringAngle =  (accumulatedSumBlue-
				accumulatedSumYellow)/
		 		(accumulatedSumYellow + accumulatedSumBlue)*(0.32f);
                 		gsr.groundSteering(steeringAngle);
                 		ppr.position(carSpeed/110);
               		        od4.send(ppr);
                 		counter = 0;
           	 		accumulatedSumBlue = 0;
           	 		accumulatedSumYellow = 0;
			}
          	 }*/

