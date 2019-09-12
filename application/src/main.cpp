/*
* Copyright (c) 2018 Intel Corporation.
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
* LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
* OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
* WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

// std includes
#include <iostream>
#include <stdio.h>
#include <thread>
#include <queue>
#include <map>
#include <atomic>
#include <csignal>
#include <ctime>
#include <mutex>
#include <syslog.h>
#include <fstream>

// OpenCV includes
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/dnn.hpp>
#include <nlohmann/json.hpp>

// MQTT
#include "mqtt.h"

using namespace std;
using namespace cv;
using namespace dnn;
using json = nlohmann::json;
json jsonobj;

// OpenCV-related variables
Mat frame, blob, im_info;
VideoCapture cap;
int delay = 5;
Net net;

// application parameters
String model;
String config;
int backendId;
int targetId;
int rate;
float confidenceFactor;

// application related flags
const string selector = "Assembly Selection";
// selected assembly line area
Rect area;

// flag to control background threads
atomic<bool> keepRunning(true);

// flag to handle UNIX signals
static volatile sig_atomic_t sig_caught = 0;

// mqtt parameters
const string topic = "machine/zone";

// AssemblyInfo contains information about assembly line
struct AssemblyInfo
{
    bool safe;
    bool alert;
};

// currentInfo contains the latest AssemblyInfo tracked by the application.
AssemblyInfo currentInfo = {
  true,
  false,
};

// nextImage provides queue for captured video frames
queue<Mat> nextImage;
String currentPerf;

mutex m, m1, m2;

const char* keys =
    "{ help     | | Print help message. }"
    "{ device d    | 0 | camera device number. }"
    "{ input i     | | Path to input image or video file. Skip this argument to capture frames from a camera. }"
    "{ model m     | | Path to .bin file of model containing face recognizer. }"
    "{ config c    | | Path to .xml file of model containing network configuration. }"
    "{ factor f    | 0.5 | Confidence factor required. }"
    "{ backend b   | 0 | Choose one of computation backends: "
                        "0: automatically (by default), "
                        "1: Halide language (http://halide-lang.org/), "
                        "2: Intel's Deep Learning Inference Engine (https://software.intel.com/openvino-toolkit), "
                        "3: OpenCV implementation }"
    "{ target t    | 0 | Choose one of target computation devices: "
                        "0: CPU target (by default), "
                        "1: OpenCL, "
                        "2: OpenCL fp16 (half-float precision), "
                        "3: VPU }"
    "{ rate r      | 1 | number of seconds between data updates to MQTT server. }"
    "{ pointx x    | 0 | X coordinate of the top left point of assembly area on camera feed. }"
    "{ pointy y    | 0 | Y coordinate of the top left point of assembly area on camera feed. }"
    "{ width w     | 0 | Width of the assembly area in pixels. }"
    "{ height h    | 0 | Height of the assembly area in pixels. }";


// nextImageAvailable returns the next image from the queue in a thread-safe way
Mat nextImageAvailable() {
    Mat rtn;
    m.lock();
    if (!nextImage.empty()) {
        rtn = nextImage.front();
        nextImage.pop();
    }
    m.unlock();

    return rtn;
}

// addImage adds an image to the queue in a thread-safe way
void addImage(Mat img) {
    m.lock();
    if (nextImage.empty()) {
        nextImage.push(img);
    }
    m.unlock();
}

// getCurrentInfo returns the most-recent AssemblyInfo for the application.
AssemblyInfo getCurrentInfo() {
    m2.lock();
    AssemblyInfo info;
    info = currentInfo;
    m2.unlock();

    return info;
}

// updateInfo uppdates the current AssemblyInfo for the application to the latest detected values
void updateInfo(AssemblyInfo info) {
    m2.lock();
    currentInfo.safe = info.safe;
    currentInfo.alert = info.alert;
    m2.unlock();
}

// resetInfo resets the current AssemblyInfo for the application.
void resetInfo() {
    m2.lock();
    currentInfo.safe = false;
    currentInfo.alert = false;
    m2.unlock();
}

// getCurrentPerf returns a display string with the most current performance stats for the Inference Engine.
string getCurrentPerf() {
    string perf;
    m1.lock();
    perf = currentPerf;
    m1.unlock();

    return perf;
}

// savePerformanceInfo sets the display string with the most current performance stats for the Inference Engine.
void savePerformanceInfo() {
    m1.lock();

    vector<double> times;
    double freq = getTickFrequency() / 1000;
    double t = net.getPerfProfile(times) / freq;

    string label = format("Person inference time: %.2f ms", t);

    currentPerf = label;

    m1.unlock();
}

// publish MQTT message with a JSON payload
void publishMQTTMessage(const string& topic, const AssemblyInfo& info)
{
    ostringstream s;
    s << "{\"Safe\": \"" << info.safe << "\"}";
    string payload = s.str();

    mqtt_publish(topic, payload);

    string msg = "MQTT message published to topic: " + topic;
    syslog(LOG_INFO, "%s", msg.c_str());
    syslog(LOG_INFO, "%s", payload.c_str());
}

// message handler for the MQTT subscription for the any desired control channel topic
int handleMQTTControlMessages(void *context, char *topicName, int topicLen, MQTTClient_message *message)
{
    string topic = topicName;
    string msg = "MQTT message received: " + topic;
    syslog(LOG_INFO, "%s", msg.c_str());

    return 1;
}

// Function called by worker thread to process the next available video frame.
void frameRunner() {
    while (keepRunning.load()) {
        Mat next = nextImageAvailable();
        if (!next.empty()) {
            // convert to 4d vector as required by face detection model, and detect faces
            blobFromImage(next, blob, 1.0, Size(672, 384));
            net.setInput(blob);
            Mat result = net.forward();

            // get detected persons
            vector<Rect> persons;
            // assembly line area flags
            bool safe = true;
            bool alert = false;

            float* data = (float*)result.data;
            for (size_t i = 0; i < result.total(); i += 7)
            {
                float confidence = data[i + 2];
                if (confidence > confidenceFactor)
                {
                    int left = (int)(data[i + 3] * frame.cols);
                    int top = (int)(data[i + 4] * frame.rows);
                    int right = (int)(data[i + 5] * frame.cols);
                    int bottom = (int)(data[i + 6] * frame.rows);
                    int width = right - left + 1;
                    int height = bottom - top + 1;

                    persons.push_back(Rect(left, top, width, height));
                }
            }

            // detect if there are any people in marked area
            for(auto const& r: persons) {
                // make sure the person rect is completely inside the main Mat
                if ((r & Rect(0, 0, next.cols, next.rows)) != r) {
                    continue;
                }

                // If the person is not within monitored assembly area theyre safe
                // Otherwise we need to trigger an alert
                if ((r & area) == r) {
                    alert = true;
                    safe = false;
                }
            }

            // operator data
            AssemblyInfo info;
            info.safe = safe;
            info.alert = alert;

            updateInfo(info);
            savePerformanceInfo();
        }
    }

    cout << "Video processing thread stopped" << endl;
}

// Function called by worker thread to handle MQTT updates. Pauses for rate second(s) between updates.
void messageRunner() {
    while (keepRunning.load()) {
        AssemblyInfo info = getCurrentInfo();
        publishMQTTMessage(topic, info);
        this_thread::sleep_for(chrono::seconds(rate));
    }

    cout << "MQTT sender thread stopped" << endl;
}

// signal handler for the main thread
void handle_sigterm(int signum)
{
    /* we only handle SIGTERM and SIGKILL here */
    if (signum == SIGTERM) {
        cout << "Interrupt signal (" << signum << ") received" << endl;
        sig_caught = 1;
    }
}

int main(int argc, char** argv)
{
    string conf_file = "../resources/config.json", input;
    std::ifstream confFile(conf_file);
    confFile>>jsonobj;
    auto obj = jsonobj["inputs"];
    input = obj[0]["video"];

    // parse command parameters
    CommandLineParser parser(argc, argv, keys);
    parser.about("Use this script to using OpenVINO.");
    if (argc == 1 || parser.has("help"))
    {
        parser.printMessage();

        return 0;
    }

    model = parser.get<String>("model");
    config = parser.get<String>("config");
    backendId = parser.get<int>("backend");
    targetId = parser.get<int>("target");
    rate = parser.get<int>("rate");
    confidenceFactor = parser.get<float>("factor");

    area.x = parser.get<int>("pointx");
    area.y = parser.get<int>("pointy");
    area.width = parser.get<int>("width");
    area.height = parser.get<int>("height");

    // connect MQTT messaging
    int result = mqtt_start(handleMQTTControlMessages);
    if (result == 0) {
        syslog(LOG_INFO, "MQTT started.");
    } else {
        syslog(LOG_INFO, "MQTT NOT started: have you set the ENV varables?");
    }

    mqtt_connect();

    // open face model
    net = readNet(model, config);
    net.setPreferableBackend(backendId);
    net.setPreferableTarget(targetId);

    // open video capture source
    if (input.size() == 1 && *(input.c_str()) >= '0' && *(input.c_str()) <= '9')
        cap.open(std::stoi(input));
    else
        cap.open(input);
    if (!cap.isOpened()) {
        cerr << "ERROR! Unable to open video source\n";
        return -1;
    }

    // register SIGTERM signal handler
    signal(SIGTERM, handle_sigterm);

    // start worker threads
    thread t1(frameRunner);
    thread t2(messageRunner);

    // read video input data
    for (;;) {
        cap.read(frame);

        if (frame.empty()) {
            keepRunning = false;
            cerr << "ERROR! blank frame grabbed\n";
            break;
        }

        // if negative number given, we will default to the start of the frame
        if (area.x < 0 || area.y < 0) {
            area.x = 0;
            area.y = 0;
        }

        // if either default values are given or negative we will default to the whole frame
        if (area.width <= 0) {
            area.width = frame.cols;
        }

        if (area.height <= 0) {
            area.height = frame.rows;
        }

        int keyPressed = waitKey(delay);
        // 'c' key pressed
        if (keyPressed == 99) {
            // Give operator chance to change the area
            // select rectangle from left upper corner, dont display crosshair
            namedWindow(selector);
            area = selectROI(selector, frame, true, false);
            cout << "Assembly Area Selection: -x=" << area.x << " -y=" << area.y << " -h=" << area.height << " -w=" << area.width << endl;
            destroyWindow(selector);
        } else if (keyPressed == 27) {
            cout << "Attempting to stop background threads" << endl;
            keepRunning = false;
            break;
        }

        // draw area rectangle
        rectangle(frame, area, CV_RGB(255,0,0));

        addImage(frame);

        string label = getCurrentPerf();
        putText(frame, label, Point(0, 15), FONT_HERSHEY_SIMPLEX, 0.5, CV_RGB(255, 255, 255));

        AssemblyInfo info = getCurrentInfo();
        label = format("Worker Safe: %s", info.safe ? "true" : "false");
        putText(frame, label, Point(0, 40), FONT_HERSHEY_SIMPLEX, 0.5, CV_RGB(255, 255, 255));

        if (info.alert) {
            string warning;
            warning = format("HUMAN IN ASSEMBLY AREA: PAUSE THE MACHINE!");
            putText(frame, warning, Point(0, 120), FONT_HERSHEY_SIMPLEX, 0.5, CV_RGB(255, 0, 0), 2);
        }

        imshow("Restricted Zone Notifier", frame);

        if (sig_caught) {
            cout << "Attempting to stop background threads" << endl;
            keepRunning = false;
            break;
        }
    }

    // wait for the threads to finish
    t1.join();
    t2.join();
    cap.release();

    // disconnect MQTT messaging
    mqtt_disconnect();
    mqtt_close();
    
    net.~Net();
    return 0;
}
