/*******************************************************************************
* Name: TelemetryViewer.h
*
* Purpose: Top level access to telemetry measurements
*
* Author: Will Merges
*
* RIT Launch Initiative
*******************************************************************************/
#ifndef TELVIEW_H
#define TELVIEW_H

#include <stdint.h>
#include "lib/telemetry/TelemetryShm.h"
#include "lib/vcm/vcm.h"
#include "common/types.h"

using namespace vcm;

class TelemetryViewer {
public:
    // construct a telemetry viewer
    TelemetryViewer();

    // destructor
    ~TelemetryViewer();

    // initialize the telemetry viewer for vehicle specified by VCM
    RetType init(VCM* vcm);

    // init using use the default VCM
    RetType init();

    // remove all measurements currently viewable
    void remove_all();

    // add all measurements for the vehicle to be viewable
    RetType add_all();

    // add a single measurement to be viewable
    // NOTE: this will also add other measurements in the same packet
    RetType add(std::string& measurement);

    // add all the measurements in a single telemetry packet to be viewable
    RetType add(uint32_t packet_id);

    // update mode
    typedef enum {
        STANDARD_UPDATE, // simply refresh with the newest telemetry regardless if it's identical to the last update
        BLOCKING_UPDATE, // sleep the process until new telemetry (e.g. not in the last update) comes in and then return
        NONBLOCKING_UPDATE, // return BLOCKED if there is no new telemetry since the last update
    } update_mode_t;

    // set which update mode to use
    // NOTE: if update mode is set to BLOCKING_UPDATE, signal handlers should NOT be changed after calling this function
    // the currently set signal handlers will still be called
    // if signal handlers are changed after this mode is set, receiving a signal could lock shared memory
    // NOTE: there could be some weird behavior in threads if this function is called with the BLOCKING_OPTION concurrently
    // this function will call 'init_signals' which modifies static memory without locking it
    void set_update_mode(update_mode_t mode);

    // update the telemetry viewer with the most recent telemetry data
    // return FAILURE after 'timeout' milliseconds if the telemetry has not updated
    // if 'timeout' is 0, never times out and waits forever
    RetType update(uint32_t timeout = 0);

    // wakes up the process if it's currently blocked
    void force_wake();

    // should be called by the processes signal handler, avoids locking shared memory on exit
    void sighandler();

    // set 'val' to the value of a telemetry measurement
    // converts raw telemetry data into usable types
    // returns FAILURE if type conversion is impossible
    RetType get_str(measurement_info_t* meas, std::string* val);
    RetType get_float(measurement_info_t* meas, float* val);
    RetType get_double(measurement_info_t* meas, double* val);
    RetType get_int(measurement_info_t* meas, int* val);
    RetType get_uint(measurement_info_t* meas, unsigned int* val);
    // place up to meas->size bytes into 'buffer'
    RetType get_raw(measurement_info_t* meas, uint8_t* buffer);

    // slightly slower functions that use strings as measurement parameters
    RetType get_str(std::string& meas, std::string* val);
    RetType get_float(std::string& meas, float* val);
    RetType get_double(std::string& meas, double* val);
    RetType get_int(std::string& meas, int* val);
    RetType get_uint(std::string& meas, unsigned int* val);
    RetType get_raw(std::string& meas, uint8_t* buffer);

private:
    TelemetryShm shm;
    VCM* vcm;

    update_mode_t update_mode;
    bool check_all; // if we're tracking all measurements

    unsigned int* packet_ids;
    size_t num_packets; // number of packets being tracked

    uint8_t** packet_buffers;
    size_t* packet_sizes;

    // set 'data' to the most recently updated memory containing 'measurement'
    RetType latest_data(measurement_info_t* meas, uint8_t** data);

    // command to execute to start the reaper process
    std::string reaper_cmd;
};

#endif
