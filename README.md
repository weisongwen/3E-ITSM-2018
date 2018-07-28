# Package for: Sensor fusion with the assistance of point clouds-based perception (for surroundings with high altitude)

- Point clouds based object detection
- Fault detection, exclusion (FDE)
- GNSS single point positioning with weighted least square (WLS) in python
- Skyplot visualization
- Positioning result visualization in python
- INS data for heading, transform the dynamic objects into skyplot

## Spec Recommendation

- Number of CPU cores: 4
- RAM size: 16GB
- Storage size: 30GB in SSD

## Requirements

- ROS jade (Ubuntu 14.04)
- Qt 5.2.1 or higher

### Install dependencies for Ubuntu 14.04 jade

install all the dependency when needed



## How to Build

```
$ cd $HOME
$ mkdir yourpackage/src
$ cd yourpackage/src
$ git clone https://github.com/weisongwen/3E-ITSM-2018.git
$ catkin_init_workspace
$ cd ..
$ catkin_make
```

## How to Start

```
$ cd $HOME/yourpackage/src
$ ./all.sh
```

## How to use this for your data

The data is saved in Dropbox. The data for public will be opened soon,


