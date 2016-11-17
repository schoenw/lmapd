# lmapd
[![Build Status](https://travis-ci.org/ungureanuvladvictor/lmapd.svg?branch=development)](https://travis-ci.org/ungureanuvladvictor/lmapd)


lmapd is the proof-of-concept daemon implemntation for the Large-Scale Measurement Platforms (LMAP) Protocol

### Version
0.3

### Dependencies

lmapd depends on a few open source libraries:

* [libevent] - an event notification library
* [libxml2] - an XML parser written in C

### Installation

The build system is based on cmake.

```sh
$ sudo apt-get install cmake pkg-config libevent-dev libxml2-dev check
$ git clone https://github.com/schoenw/lmapd.git
$ cd lmapd
$ mkdir build
$ cd build
$ cmake ..
$ make
$ make test   # optional
$ make doc    # optional
```
The binary for the daemon is located at src/lmapd. The binary at src/lmapctl provides a little command line tool that can be used to validate configuration files and to interact with the daemon.

```sh
$ ./src/lmapd -h
usage: lmapd [-f] [-n] [-s] [-v] [-h] [-q queue] [-c config] [-s status]
       -f fork (daemonize)
       -n parse config and dump config and exit
       -s parse config and dump state and exit
       -q path to queue directory
       -c path to config directory or file
       -r path to run directory (pid file and status file)
       -v show version information and exit
       -h show brief usage information and exit
$ ./src/lmapctl help
  clean       clean the workspace (be careful!)
  config      validate and render lmap configuration
  help        show brief list of commands
  reload      reload the lmap configuration
  running     test if the lmap daemon is running
  shutdown    shutdown the lmap daemon
  status      show status information
  validate    validate lmap configuration
  version     show version information
```

An example config file is located at docs/lmapd-config.xml.

### Development
Want to contribute? Great!

Check out the current issues and provide a fix for each of them. Fork your local lmapd repository and create a new branch. When the development is done create a pull request and we can get things upstream.

### Coverage

Enable coverage definitions in the top-level CMakeLists.txt and build
normally. In the build directory, you can now produce a nice report:

```sh
$ lcov --zerocounters --directory .
$ lcov --capture --initial --directory . --output-file app
$ make test
$ lcov --no-checksum --directory . --capture --output-file app.info
$ genhtml app.info
```

### Todo's

 - Fix lmapctl timezone bug
 - Implement schedule/duration
 - Implement calendar/timezone-offset
 - Implement suppression/suppress-by-default
 - Implement pipelined execution mode
 - Unify the reporting of measurement results


Acknowledgments
--
The following people have helped with suggestions and ideas:

- Vaibhav Bajpai
- Jürgen Schönwälder
- Roxana Nadrag
- Alexandru Barbarosie

Development of this code was kindly supported by the EU FP7 Leone and
Flamingo research projects.

License
----

GPLv3

[libevent]:http://libevent.org/
[libxml2]:http://www.xmlsoft.org/
