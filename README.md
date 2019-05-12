# Restricted Zone Notifier

| Details            |              |
|-----------------------|---------------|
| Target OS:            |  Ubuntu\* 16.04 LTS   |
| Programming Language: |  C++ |
| Time to Complete:    |  45 min     |

![app image](./images/restricted-zone-notifier.png)

## Introduction

This restricted zone notifier application is one of a series of reference implementations for Computer Vision (CV) using the Intel® Distribution of OpenVINO™ toolkit. This application is designed for a machine mounted camera system that monitors if there are any humans present in a predefined selected assembly line area. It sends an alert if there is at least one person detected in the marked assembly area. The user can select the area coordinates either via command line parameters or once the application has been started they can select the region of interest (ROI) by pressing the `c` key; this will pause the application, and pop up a separate window on which the user can drag the mouse from the upper left ROI corner to whatever the size they require the area to cover. By default, the whole frame is selected.

This example is intended to demonstrate how to use CV to improve assembly line safety for human operators and factory workers.

## Requirements

### Hardware
* 6th to 8th Generation Intel® Core™ processors with Intel® Iris® Pro graphics or Intel® HD Graphics

### Software
* [Ubuntu\* 16.04 LTS](http://releases.ubuntu.com/16.04/)<br><br>
**Note**: We recommend using a 4.14+ kernel to use this software. Run the following command to determine your kernel version:
    ```
    uname -a
    ```
* OpenCL™ Runtime Package
* Intel® Distribution of OpenVINO™ toolkit 2019 R1 Release

## Setup

### Install the Intel® Distribution of OpenVINO™ toolkit
Refer to https://software.intel.com/en-us/articles/OpenVINO-Install-Linux for more information about how to install and setup the Intel® Distribution of OpenVINO™ toolkit.

You will need the OpenCL™ Runtime package if you plan to run inference on the GPU as shown by the
instructions below. It is not mandatory for CPU inference.

## How it Works

The application uses a video source, such as a camera, to grab frames, and then uses a Deep Neural Network (DNN) to process the data. The network detects persons in the frame, and then if successful it checks if the detected persons are in the indicated off-limits assembly line region.

The data can then optionally be sent to a MQTT machine to machine messaging server, as part of an industrial data analytics system.

The DNN models can be downloaded using `downloader.py` which is the part of the Intel® Distribution of OpenVINO™ toolkit.


![Code organization](./images/arch3.png)

The program creates three threads for concurrency:

- Main thread that performs the video i/o
- Worker thread that processes video frames using the deep neural networks
- Worker thread that publishes any MQTT messages

## Install the Dependencies

```
sudo apt-get install mosquitto mosquitto-clients
```
## Download the model
This application uses the **pedestrian-detection-adas-0002** Intel® model, that can be downloaded using the model downloader. The model downloader downloads the .xml and .bin files that will be used by the application.

Steps to download .xml and .bin files:
- Go to the **model_downloader** directory using the following command:
    ```
    cd /opt/intel/openvino/deployment_tools/tools/model_downloader
    ```

- Specify which model to download with __--name__: 

    ```
    sudo ./downloader.py --name pedestrian-detection-adas-0002
    ```
- To download the model for FP16, run the following command:
    ```
    sudo ./downloader.py --name pedestrian-detection-adas-0002-fp16
    ```
The files will be downloaded inside `/opt/intel/openvino/deployment_tools/tools/model_downloader/Transportation/object_detection/pedestrian/mobilenet-reduced-ssd/dldt` directory.

## Setting the Build Environment

You must configure the environment to use the Intel® Distribution of OpenVINO™ toolkit one time per session by running the following command:
```
source /opt/intel/openvino/bin/setupvars.sh
```

## Building the Code

To build the code, run the following commands in `restricted-zone-notifier-cpp` directory on the terminal:
```
mkdir -p build && cd build
cmake ..
make
```
## Running the Code

To see a list of the various options:
```
./monitor -help
```

To run the application with the needed model using the webcam:
```
./monitor -m=/opt/intel/openvino/deployment_tools/tools/model_downloader/Transportation/object_detection/pedestrian/mobilenet-reduced-ssd/dldt/pedestrian-detection-adas-0002.bin -c=/opt/intel/openvino/deployment_tools/tools/model_downloader/Transportation/object_detection/pedestrian/mobilenet-reduced-ssd/dldt/pedestrian-detection-adas-0002.xml
```

You can select an area to be used as the "off-limits" area by pressing the `c` key once the program is running. A new window will open showing a still image from the video capture device. Drag the mouse from left top corner to cover an area on the plane and once done (a blue rectangle is drawn) press `ENTER` or `SPACE` to proceed with monitoring.

Once you have selected the "off-limits" area the coordinates will be displayed in the terminal window like this:
```
Assembly Area Selection: -x=429 -y=101 -h=619 -w=690
```

You can run the application using those coordinates by using the `-x`, `-y`, `-h`, and `-w` flags to preselect that area.

For example:
```
./monitor -m=/opt/intel/openvino/deployment_tools/tools/model_downloader/Transportation/object_detection/pedestrian/mobilenet-reduced-ssd/dldt/pedestrian-detection-adas-0002.bin -c=/opt/intel/openvino/deployment_tools/tools/model_downloader/Transportation/object_detection/pedestrian/mobilenet-reduced-ssd/dldt/pedestrian-detection-adas-0002.xml -x=429 -y=101 -h=619 -w=690
```

If you do not select or specify an area, the default is to use the entire window as the off limits area.

### Hardware Acceleration

This application can take advantage of the hardware acceleration in the Intel® Distribution of OpenVINO™ toolkit by using the `-b` and `-t` parameters.

For example, to use the Intel® Distribution of OpenVINO™ toolkit backend with the GPU in 32-bit mode:
```
./monitor -m=/opt/intel/openvino/deployment_tools/tools/model_downloader/Transportation/object_detection/pedestrian/mobilenet-reduced-ssd/dldt/pedestrian-detection-adas-0002.bin -c=/opt/intel/openvino/deployment_tools/tools/model_downloader/Transportation/object_detection/pedestrian/mobilenet-reduced-ssd/dldt/pedestrian-detection-adas-0002.xml -b=2 -t=1
```

To run the code using 16-bit floats, you have to both set the `-t` flag to use the GPU in 16-bit mode, as well as use the FP16 version of the Intel® models:
```
./monitor -m=/opt/intel/openvino/deployment_tools/tools/model_downloader/Transportation/object_detection/pedestrian/mobilenet-reduced-ssd/dldt/pedestrian-detection-adas-0002-fp16.bin -c=/opt/intel/openvino/deployment_tools/tools/model_downloader/Transportation/object_detection/pedestrian/mobilenet-reduced-ssd/dldt/pedestrian-detection-adas-0002-fp16.xml -b=2 -t=2
```

To run the code using the VPU, you have to set the `-t` flag to `3` and also use the 16-bit FP16 version of the Intel® models:
```
./monitor -m=/opt/intel/openvino/deployment_tools/tools/model_downloader/Transportation/object_detection/pedestrian/mobilenet-reduced-ssd/dldt/pedestrian-detection-adas-0002-fp16.bin -c=/opt/intel/openvino/deployment_tools/tools/model_downloader/Transportation/object_detection/pedestrian/mobilenet-reduced-ssd/dldt/pedestrian-detection-adas-0002-fp16.xml -b=2 -t=3
```
#### Run the application on FPGA:

Before running the application on the FPGA, program the AOCX (bitstream) file.
Use the setup_env.sh script from [fpga_support_files.tgz](http://registrationcenter-download.intel.com/akdlm/irc_nas/12954/fpga_support_files.tgz) to set the environment variables.<br>

```
source /home/<user>/Downloads/fpga_support_files/setup_env.sh
```

The bitstreams for HDDL-F can be found under the `/opt/intel/openvino/bitstreams/a10_vision_design_bitstreams` folder. To program the bitstream use the below command:
```
aocl program acl0 /opt/intel/openvino/bitstreams/a10_vision_design_bitstreams/2019R1_PL1_FP11_MobileNet_Clamp.aocx
```

For more information on programming the bitstreams, please refer to https://software.intel.com/en-us/articles/OpenVINO-Install-Linux-FPGA#inpage-nav-11.

To run the code using the FPGA, you have to set the `-t` flag to `5`:
```
./monitor -m=/opt/intel/openvino/deployment_tools/tools/model_downloader/Transportation/object_detection/pedestrian/mobilenet-reduced-ssd/dldt/pedestrian-detection-adas-0002-fp16.bin -c=/opt/intel/openvino/deployment_tools/tools/model_downloader/Transportation/object_detection/pedestrian/mobilenet-reduced-ssd/dldt/pedestrian-detection-adas-0002-fp16.xml -b=2 -t=5
```

## Sample Videos

There are several videos available to use as sample videos to show the capabilities of this application. You can download them by running these commands from the `restricted-zone-notifier-cpp` directory:
```
mkdir resources
cd resources
wget https://github.com/intel-iot-devkit/sample-videos/raw/master/worker-zone-detection.mp4
cd ..
```

To then execute the code using one of these sample videos, run the following commands from the `restricted-zone-notifier-cpp` directory:
```
cd build
./monitor -m=/opt/intel/openvino/deployment_tools/tools/model_downloader/Transportation/object_detection/pedestrian/mobilenet-reduced-ssd/dldt/pedestrian-detection-adas-0002.bin -c=/opt/intel/openvino/deployment_tools/tools/model_downloader/Transportation/object_detection/pedestrian/mobilenet-reduced-ssd/dldt/pedestrian-detection-adas-0002.xml -i=../resources/worker-zone-detection.mp4
```

### Machine to Machine Messaging with MQTT

If you wish to use a MQTT server to publish data, you should set the following environment variables before running the program:
```
export MQTT_SERVER=localhost:1883
export MQTT_CLIENT_ID=cvservice
```

Change the `MQTT_SERVER` to a value that matches the MQTT server you are connecting to.

You should change the `MQTT_CLIENT_ID` to a unique value for each monitoring station, so you can track the data for individual locations. For example:
```
export MQTT_CLIENT_ID=zone1337
```

If you want to monitor the MQTT messages sent to your local server, and you have the `mosquitto` client utilities installed, you can run the following command on a new terminal while the application is running:
```
mosquitto_sub -t 'machine/zone'
```
